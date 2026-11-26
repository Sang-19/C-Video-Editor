package com.trisvax.studio.ui.editor

import android.app.Application
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.view.Surface
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.trisvax.studio.TrisvaxApplication
import com.trisvax.studio.models.AspectRatioPreset
import com.trisvax.studio.models.BundledMusicAsset
import com.trisvax.studio.models.ClipDto
import com.trisvax.studio.models.ExportUiState
import com.trisvax.studio.models.ProjectDocumentDto
import com.trisvax.studio.models.TextOverlayDto
import com.trisvax.studio.models.TimelineDto
import com.trisvax.studio.models.TimelineSelectionDto
import com.trisvax.studio.models.TrackStateDto
import com.trisvax.studio.nativebridge.NativeExportCallback
import com.trisvax.studio.platform.MediaStorePublisher
import com.trisvax.studio.platform.VideoExportService
import com.trisvax.studio.platform.VoiceoverRecorder
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import java.io.File

enum class EditorTool {
    NONE,
    MUSIC,
    TEXT,
    CROP,
    TRIM,
    SPLIT,
    ADJUST,
    SPEED,
    VOLUME,
    ROTATE,
    VOICE,
    TRANSITIONS,
    CLIPS
}

data class EditorUiState(
    val projectId: String = "",
    val projectName: String = "New project",
    val timeline: TimelineDto = TimelineDto(),
    val selectedClipId: String? = null,
    val selectedTextId: String? = null,
    val selection: TimelineSelectionDto = TimelineSelectionDto(),
    val multiSelectMode: Boolean = false,
    val playheadMs: Long = 0L,
    val isPlaying: Boolean = false,
    val activeTool: EditorTool = EditorTool.NONE,
    val canUndo: Boolean = false,
    val canRedo: Boolean = false,
    val saveBanner: String? = null,
    val bundledMusic: List<BundledMusicAsset> = emptyList(),
    val isRecordingVoice: Boolean = false,
    val recordingElapsedSec: Int = 0,
    val exportState: ExportUiState = ExportUiState.Idle
) {
    val totalDurationMs: Long
        get() = timeline.totalDurationMs

    val selectedClip: ClipDto?
        get() = timeline.clips.firstOrNull { it.id == selectedClipId } ?: activeClipAtPlayhead

    val activeClipAtPlayhead: ClipDto?
        get() {
            val posUs = playheadMs * 1_000L
            for (clip in timeline.clips) {
                val durUs = ((clip.outPointUs - clip.inPointUs).coerceAtLeast(250_000L) /
                    clip.playbackSpeed.coerceIn(0.25, 4.0)).toLong()
                if (posUs >= clip.timelinePositionUs && posUs <= clip.timelinePositionUs + durUs) {
                    return clip
                }
            }
            return timeline.clips.firstOrNull()
        }

    val selectedText: TextOverlayDto?
        get() = timeline.textOverlays.firstOrNull { it.id == selectedTextId }
            ?: timeline.textOverlays.firstOrNull()
}

class EditorViewModel(application: Application) : AndroidViewModel(application) {
    private val app = application as TrisvaxApplication
    private val bridge = app.nativeBridge
    private val json = Json { ignoreUnknownKeys = true }
    private val voiceRecorder = VoiceoverRecorder(application)

    private val _uiState = MutableStateFlow(EditorUiState())
    val uiState: StateFlow<EditorUiState> = _uiState.asStateFlow()

    private var playbackJob: Job? = null
    private var recordingTimerJob: Job? = null
    private var exportJob: Job? = null

    init {
        viewModelScope.launch {
            val tracks = app.musicCatalog.listBundledTracks()
            _uiState.update { it.copy(bundledMusic = tracks) }
        }
    }

    fun loadProject(projectId: String) {
        viewModelScope.launch(Dispatchers.IO) {
            if (bridge.openProject(projectId)) {
                syncFromNative(resetPlayhead = true)
            }
        }
    }

    private fun syncFromNative(resetPlayhead: Boolean = false) {
        val raw = bridge.getActiveProjectJson()
        if (raw.isBlank() || raw == "{}") return
        val doc = runCatching { json.decodeFromString<ProjectDocumentDto>(raw) }.getOrNull() ?: return
        val canUndo = bridge.canUndo()
        val canRedo = bridge.canRedo()
        _uiState.update { prev ->
            val totalMs = doc.timeline.totalDurationMs
            val nextPlayhead = if (resetPlayhead) 0L else prev.playheadMs.coerceIn(0L, totalMs)
            val nextClipId = prev.selectedClipId?.takeIf { id -> doc.timeline.clips.any { it.id == id } }
                ?: doc.timeline.clips.firstOrNull()?.id
            val nextTextId = prev.selectedTextId?.takeIf { id -> doc.timeline.textOverlays.any { it.id == id } }
                ?: doc.timeline.textOverlays.firstOrNull()?.id
            prev.copy(
                projectId = doc.metadata.id,
                projectName = doc.metadata.name,
                timeline = doc.timeline,
                selectedClipId = nextClipId,
                selectedTextId = nextTextId,
                playheadMs = nextPlayhead,
                canUndo = canUndo,
                canRedo = canRedo
            )
        }
    }

    fun attachPreviewSurface(surface: Surface, width: Int, height: Int) {
        bridge.attachPreviewSurface(surface, width, height)
        bridge.seekTo(_uiState.value.playheadMs * 1_000L)
    }

    fun detachPreviewSurface() {
        bridge.detachPreviewSurface()
    }

    fun selectTool(tool: EditorTool) {
        if (tool == EditorTool.SPLIT) {
            splitAtPlayhead()
            return
        }
        _uiState.update {
            it.copy(activeTool = if (it.activeTool == tool) EditorTool.NONE else tool)
        }
    }

    fun selectClip(clipId: String) {
        _uiState.update { state ->
            if (state.multiSelectMode) {
                val next = if (clipId in state.selection.clipIds) {
                    state.selection.clipIds - clipId
                } else {
                    state.selection.clipIds + clipId
                }
                state.copy(
                    selectedClipId = clipId,
                    selection = state.selection.copy(clipIds = next)
                )
            } else {
                state.copy(
                    selectedClipId = clipId,
                    selection = TimelineSelectionDto(clipIds = setOf(clipId))
                )
            }
        }
    }

    fun selectText(textId: String) {
        _uiState.update { state ->
            if (state.multiSelectMode) {
                val next = if (textId in state.selection.textIds) {
                    state.selection.textIds - textId
                } else {
                    state.selection.textIds + textId
                }
                state.copy(
                    selectedTextId = textId,
                    selection = state.selection.copy(textIds = next)
                )
            } else {
                state.copy(
                    selectedTextId = textId,
                    activeTool = EditorTool.TEXT,
                    selection = TimelineSelectionDto(textIds = setOf(textId))
                )
            }
        }
    }

    fun toggleMultiSelectMode() {
        _uiState.update { it.copy(multiSelectMode = !it.multiSelectMode) }
    }

    fun toggleSelectMusic(musicId: String) {
        _uiState.update { state ->
            if (state.multiSelectMode) {
                val next = if (musicId in state.selection.audioIds) {
                    state.selection.audioIds - musicId
                } else {
                    state.selection.audioIds + musicId
                }
                state.copy(selection = state.selection.copy(audioIds = next))
            } else {
                state.copy(
                    activeTool = EditorTool.MUSIC,
                    selection = TimelineSelectionDto(audioIds = setOf(musicId))
                )
            }
        }
    }

    fun toggleSelectVoiceover(voId: String) {
        _uiState.update { state ->
            if (state.multiSelectMode) {
                val next = if (voId in state.selection.voiceoverIds) {
                    state.selection.voiceoverIds - voId
                } else {
                    state.selection.voiceoverIds + voId
                }
                state.copy(selection = state.selection.copy(voiceoverIds = next))
            } else {
                state.copy(
                    activeTool = EditorTool.VOICE,
                    selection = TimelineSelectionDto(voiceoverIds = setOf(voId))
                )
            }
        }
    }

    fun selectAllTimelineItems() {
        _uiState.update { state ->
            val tl = state.timeline
            state.copy(
                multiSelectMode = true,
                selection = TimelineSelectionDto(
                    clipIds = tl.clips.map { it.id }.toSet(),
                    audioIds = tl.musicTrack?.let { setOf(it.id) } ?: emptySet(),
                    voiceoverIds = tl.voiceovers.map { it.id }.toSet(),
                    textIds = tl.textOverlays.map { it.id }.toSet()
                )
            )
        }
    }

    fun clearSelection() {
        _uiState.update { it.copy(selection = TimelineSelectionDto(), multiSelectMode = false) }
    }

    private fun selectionToJson(sel: TimelineSelectionDto): String =
        json.encodeToString(TimelineSelectionDto.serializer(), sel)

    fun toggleTrackState(kind: Int, trackIndex: Int, field: String) {
        val current = _uiState.value.timeline.trackStates.firstOrNull {
            it.kind == kind && it.trackIndex == trackIndex
        } ?: TrackStateDto(kind = kind, trackIndex = trackIndex)
        val updated = when (field) {
            "locked" -> current.copy(locked = !current.locked)
            "muted" -> current.copy(muted = !current.muted)
            "hidden" -> current.copy(hidden = !current.hidden)
            "solo" -> current.copy(solo = !current.solo)
            else -> current
        }
        if (bridge.setTrackState(updated.kind, updated.trackIndex, updated.locked, updated.muted, updated.hidden, updated.solo)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun batchShiftSelection(deltaMs: Long, trackDelta: Int = 0) {
        val sel = _uiState.value.selection
        if (sel.isEmpty) return
        if (bridge.batchShiftSelection(selectionToJson(sel), deltaMs * 1_000L, trackDelta)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun batchMuteSelection(muted: Boolean) {
        val sel = _uiState.value.selection
        if (sel.isEmpty) return
        if (bridge.batchMuteSelection(selectionToJson(sel), muted)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun batchDeleteSelection() {
        val sel = _uiState.value.selection
        if (sel.isEmpty) return
        if (bridge.batchDeleteSelection(selectionToJson(sel))) {
            clearSelection()
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun togglePlayback() {
        if (_uiState.value.isPlaying) {
            pausePlayback()
        } else {
            startPlayback()
        }
    }

    fun pausePlayback() {
        playbackJob?.cancel()
        playbackJob = null
        _uiState.update { it.copy(isPlaying = false) }
    }

    private fun startPlayback() {
        val total = _uiState.value.totalDurationMs
        if (total <= 0L) return
        playbackJob?.cancel()
        _uiState.update { it.copy(isPlaying = true) }
        playbackJob = viewModelScope.launch {
            var current = _uiState.value.playheadMs
            while (_uiState.value.isPlaying) {
                delay(33L)
                current += 33L
                if (current >= total) {
                    bridge.seekTo(0L)
                    _uiState.update { it.copy(playheadMs = 0L, isPlaying = false) }
                    break
                }
                bridge.seekTo(current * 1_000L)
                _uiState.update { it.copy(playheadMs = current) }
            }
        }
    }

    fun seekTo(ms: Long) {
        val total = _uiState.value.totalDurationMs
        val clamped = ms.coerceIn(0L, total)
        bridge.seekTo(clamped * 1_000L)
        _uiState.update { it.copy(playheadMs = clamped) }
    }

    fun undo() {
        if (bridge.undo()) syncFromNative()
    }

    fun redo() {
        if (bridge.redo()) syncFromNative()
    }

    fun saveProjectExplicit() {
        viewModelScope.launch(Dispatchers.IO) {
            val saved = bridge.saveProject()
            _uiState.update {
                it.copy(saveBanner = if (saved) "Project saved locally" else "Unable to save project")
            }
            delay(2200L)
            _uiState.update { it.copy(saveBanner = null) }
        }
    }

    fun trimSelectedClip(startMs: Long, endMs: Long) {
        val clip = _uiState.value.selectedClip ?: return
        if (bridge.trimClip(clip.id, startMs * 1_000L, endMs * 1_000L)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun splitAtPlayhead() {
        val posUs = _uiState.value.playheadMs * 1_000L
        if (bridge.splitAtPlayhead(posUs)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun setClipSpeed(speed: Double, pitchPreserved: Boolean) {
        val clip = _uiState.value.selectedClip ?: return
        val mode = if (pitchPreserved) 1 else 0
        if (bridge.setSpeed(clip.id, speed, mode)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun setClipVolume(volume: Float, muted: Boolean) {
        val clip = _uiState.value.selectedClip ?: return
        if (bridge.setVolume(clip.id, volume, muted)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun toggleClipMute(clipId: String) {
        val clip = _uiState.value.timeline.clips.firstOrNull { it.id == clipId } ?: return
        if (bridge.setVolume(clip.id, clip.volume, !clip.isMuted)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun setCropAndAspect(
        preset: AspectRatioPreset,
        offsetX: Float,
        offsetY: Float,
        zoom: Float,
        rotationDegrees: Int
    ) {
        val clip = _uiState.value.selectedClip ?: return
        if (bridge.setCrop(
                clip.id,
                preset.name,
                preset.ratioWidth,
                preset.ratioHeight,
                offsetX,
                offsetY,
                zoom,
                rotationDegrees
            )
        ) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun rotateSelectedClip90() {
        val clip = _uiState.value.selectedClip ?: return
        val nextRot = (clip.crop.rotationDegrees + 90) % 360
        val preset = AspectRatioPreset.entries.firstOrNull { it.name == clip.crop.preset }
            ?: AspectRatioPreset.PORTRAIT_9_16
        setCropAndAspect(preset, clip.crop.offsetX, clip.crop.offsetY, clip.crop.zoom, nextRot)
    }

    fun setClipFilters(
        brightness: Float,
        contrast: Float,
        saturation: Float,
        temperature: Float,
        exposure: Float,
        sharpness: Float
    ) {
        val clip = _uiState.value.selectedClip ?: return
        if (bridge.setFilter(clip.id, brightness, contrast, saturation, temperature, exposure, sharpness)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun setClipTransition(transitionType: Int, durationMs: Long) {
        val clip = _uiState.value.selectedClip ?: return
        if (bridge.setTransition(clip.id, transitionType, durationMs * 1_000L)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun moveClip(fromIdx: Int, toIdx: Int) {
        if (bridge.reorderClip(fromIdx, toIdx)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun deleteClip(clipId: String) {
        if (bridge.deleteClip(clipId)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun appendMediaUris(uris: List<Uri>) {
        val pid = _uiState.value.projectId
        if (pid.isBlank() || uris.isEmpty()) return
        viewModelScope.launch(Dispatchers.IO) {
            uris.forEachIndexed { idx, uri ->
                val resolved = app.mediaResolver.resolve(pid, uri, idx)
                if (resolved != null) {
                    bridge.importMedia(
                        resolved.uriString,
                        resolved.displayName,
                        resolved.isImage,
                        resolved.durationUs,
                        resolved.width,
                        resolved.height,
                        resolved.fileSizeBytes,
                        resolved.thumbnailPath
                    )
                }
            }
            bridge.saveProject()
            syncFromNative()
        }
    }

    fun applyBundledMusic(asset: BundledMusicAsset) {
        if (bridge.setMusicTrack(
                title = asset.title,
                sourceUri = asset.assetPath,
                isBundled = true,
                durationUs = asset.durationUs,
                trimStartUs = 0L,
                trimEndUs = asset.durationUs,
                volume = 0.75f,
                muted = false,
                loop = true,
                fadeInUs = 300_000L,
                fadeOutUs = 500_000L
            )
        ) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun applyCustomMusicUri(uri: Uri) {
        val title = uri.lastPathSegment?.substringAfterLast('/') ?: "Custom Track"
        if (bridge.setMusicTrack(
                title = title,
                sourceUri = uri.toString(),
                isBundled = false,
                durationUs = 30_000_000L,
                trimStartUs = 0L,
                trimEndUs = 30_000_000L,
                volume = 0.8f,
                muted = false,
                loop = true,
                fadeInUs = 0L,
                fadeOutUs = 0L
            )
        ) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun updateMusicSettings(
        trimStartMs: Long,
        trimEndMs: Long,
        volume: Float,
        muted: Boolean,
        loop: Boolean,
        fadeInMs: Long,
        fadeOutMs: Long
    ) {
        val m = _uiState.value.timeline.musicTrack ?: return
        if (bridge.setMusicTrack(
                title = m.title,
                sourceUri = m.sourceUri,
                isBundled = m.isBundledAsset,
                durationUs = m.sourceDurationUs,
                trimStartUs = trimStartMs * 1_000L,
                trimEndUs = trimEndMs * 1_000L,
                volume = volume,
                muted = muted,
                loop = loop,
                fadeInUs = fadeInMs * 1_000L,
                fadeOutUs = fadeOutMs * 1_000L
            )
        ) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun removeMusicTrack() {
        if (bridge.removeMusicTrack()) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun startVoiceoverRecording() {
        val pid = _uiState.value.projectId.ifBlank { "default" }
        val voFile = File(app.filesDir, "projects/$pid/voiceovers/vo_${System.currentTimeMillis()}.m4a")
        if (voiceRecorder.startRecording(voFile)) {
            _uiState.update { it.copy(isRecordingVoice = true, recordingElapsedSec = 0) }
            recordingTimerJob?.cancel()
            recordingTimerJob = viewModelScope.launch {
                while (_uiState.value.isRecordingVoice) {
                    delay(1000L)
                    _uiState.update { it.copy(recordingElapsedSec = it.recordingElapsedSec + 1) }
                }
            }
        }
    }

    fun stopVoiceoverRecording() {
        recordingTimerJob?.cancel()
        val result = voiceRecorder.stopRecording()
        _uiState.update { it.copy(isRecordingVoice = false) }
        if (result != null) {
            val (file, durMs) = result
            val startUs = _uiState.value.playheadMs * 1_000L
            val count = _uiState.value.timeline.voiceovers.size + 1
            bridge.addVoiceover("Voiceover $count", file.absolutePath, durMs * 1_000L, startUs, 1.0f)
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun removeVoiceover(voId: String) {
        if (bridge.removeVoiceover(voId)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun upsertTextOverlay(
        id: String,
        text: String,
        fontFamily: String,
        fontSizeSp: Float,
        colorArgb: Long,
        align: Int,
        normX: Float,
        normY: Float,
        startMs: Long,
        endMs: Long,
        animation: Int
    ) {
        val createdId = bridge.upsertTextOverlay(
            id = id,
            text = text,
            fontFamily = fontFamily,
            fontSizeSp = fontSizeSp,
            colorArgb = colorArgb,
            align = align,
            normX = normX,
            normY = normY,
            startUs = startMs * 1_000L,
            endUs = endMs * 1_000L,
            animation = animation
        )
        if (createdId.isNotBlank()) {
            _uiState.update { it.copy(selectedTextId = createdId) }
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun removeTextOverlay(id: String) {
        if (bridge.removeTextOverlay(id)) {
            syncFromNative()
            bridge.saveProject()
        }
    }

    fun startExport(onFinished: () -> Unit) {
        if (_uiState.value.timeline.clips.isEmpty()) return
        pausePlayback()
        val pid = _uiState.value.projectId.ifBlank { "export" }
        val exportDir = File(app.filesDir, "exports").apply { mkdirs() }
        val fileName = "TRISVAX_${System.currentTimeMillis()}.mp4"
        val outFile = File(exportDir, fileName)

        val serviceIntent = Intent(app, VideoExportService::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            app.startForegroundService(serviceIntent)
        } else {
            app.startService(serviceIntent)
        }

        _uiState.update {
            it.copy(exportState = ExportUiState.Exporting(2, "Initializing C++ export pipeline..."))
        }

        exportJob = viewModelScope.launch(Dispatchers.IO) {
            bridge.saveProject()
            val ok = bridge.exportProject(
                outFile.absolutePath,
                30,
                object : NativeExportCallback {
                    override fun onProgress(percent: Int, statusMessage: String) {
                        _uiState.update {
                            it.copy(exportState = ExportUiState.Exporting(percent, statusMessage))
                        }
                    }
                }
            )
            app.stopService(serviceIntent)

            if (ok && outFile.exists() && outFile.length() > 0L) {
                val published = MediaStorePublisher.publishMp4(app, outFile, fileName)
                withContext(Dispatchers.Main) {
                    _uiState.update {
                        it.copy(
                            exportState = ExportUiState.Success(
                                outputUri = published?.toString() ?: outFile.absolutePath,
                                outputFileName = fileName
                            )
                        )
                    }
                    onFinished()
                }
            } else {
                _uiState.update {
                    it.copy(
                        exportState = ExportUiState.Error(
                            "Native export did not complete for project $pid. Ensure source clips remain accessible."
                        )
                    )
                }
            }
        }
    }

    fun cancelExport() {
        bridge.cancelExport()
        exportJob?.cancel()
        app.stopService(Intent(app, VideoExportService::class.java))
        _uiState.update { it.copy(exportState = ExportUiState.Idle) }
    }

    fun dismissExportError() {
        _uiState.update { it.copy(exportState = ExportUiState.Idle) }
    }

    override fun onCleared() {
        super.onCleared()
        playbackJob?.cancel()
        recordingTimerJob?.cancel()
        voiceRecorder.stopAndRelease()
        bridge.detachPreviewSurface()
    }
}
