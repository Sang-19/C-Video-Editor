package com.trisvax.studio.nativebridge

import android.view.Surface
import java.io.Closeable

interface NativeExportCallback {
    fun onProgress(percent: Int, statusMessage: String)
}

/**
 * Thin JNI bridge delegating all project state, timeline editing, undo/redo,
 * OpenGL ES 3.0 surface preview, AAudio playback, and MP4 export to C++20 trisvax_jni.
 */
class VideoEditorBridge(workspaceDir: String) : Closeable {
    private var nativeHandle: Long = nativeCreate(workspaceDir)

    val isReady: Boolean
        get() = nativeHandle != 0L

    fun listProjectsJson(): String =
        if (nativeHandle != 0L) nativeListProjectsJson(nativeHandle) else "[]"

    fun createProject(name: String): String =
        if (nativeHandle != 0L) nativeCreateProject(nativeHandle, name) else ""

    fun openProject(projectId: String): Boolean =
        nativeHandle != 0L && nativeOpenProject(nativeHandle, projectId)

    fun saveProject(): Boolean =
        nativeHandle != 0L && nativeSaveProject(nativeHandle)

    fun renameProject(projectId: String, newName: String): Boolean =
        nativeHandle != 0L && nativeRenameProject(nativeHandle, projectId, newName)

    fun deleteProject(projectId: String): Boolean =
        nativeHandle != 0L && nativeDeleteProject(nativeHandle, projectId)

    fun importMedia(
        uri: String,
        displayName: String,
        isImage: Boolean,
        durationUs: Long,
        width: Int,
        height: Int,
        fileSizeBytes: Long,
        thumbnailPath: String
    ): String = if (nativeHandle != 0L) {
        nativeImportMedia(
            nativeHandle, uri, displayName, isImage, durationUs, width, height, fileSizeBytes, thumbnailPath
        )
    } else ""

    fun getActiveProjectJson(): String =
        if (nativeHandle != 0L) nativeGetActiveProjectJson(nativeHandle) else "{}"

    fun trimClip(clipId: String, startUs: Long, endUs: Long): Boolean =
        nativeHandle != 0L && nativeTrimClip(nativeHandle, clipId, startUs, endUs)

    fun splitAtPlayhead(playheadUs: Long): Boolean =
        nativeHandle != 0L && nativeSplitAtPlayhead(nativeHandle, playheadUs)

    fun setSpeed(clipId: String, speed: Double, audioSpeedMode: Int): Boolean =
        nativeHandle != 0L && nativeSetSpeed(nativeHandle, clipId, speed, audioSpeedMode)

    fun setVolume(clipId: String, volume: Float, muted: Boolean): Boolean =
        nativeHandle != 0L && nativeSetVolume(nativeHandle, clipId, volume, muted)

    fun setCrop(
        clipId: String,
        preset: String,
        aspectW: Int,
        aspectH: Int,
        offsetX: Float,
        offsetY: Float,
        zoom: Float,
        rotationDeg: Int
    ): Boolean = nativeHandle != 0L && nativeSetCrop(
        nativeHandle, clipId, preset, aspectW, aspectH, offsetX, offsetY, zoom, rotationDeg
    )

    fun setFilter(
        clipId: String,
        brightness: Float,
        contrast: Float,
        saturation: Float,
        temperature: Float,
        exposure: Float,
        sharpness: Float
    ): Boolean = nativeHandle != 0L && nativeSetFilter(
        nativeHandle, clipId, brightness, contrast, saturation, temperature, exposure, sharpness
    )

    fun setTransition(clipId: String, transitionType: Int, durationUs: Long): Boolean =
        nativeHandle != 0L && nativeSetTransition(nativeHandle, clipId, transitionType, durationUs)

    fun reorderClip(fromIdx: Int, toIdx: Int): Boolean =
        nativeHandle != 0L && nativeReorderClip(nativeHandle, fromIdx, toIdx)

    fun deleteClip(clipId: String): Boolean =
        nativeHandle != 0L && nativeDeleteClip(nativeHandle, clipId)

    fun setMusicTrack(
        title: String,
        sourceUri: String,
        isBundled: Boolean,
        durationUs: Long,
        trimStartUs: Long,
        trimEndUs: Long,
        volume: Float,
        muted: Boolean,
        loop: Boolean,
        fadeInUs: Long,
        fadeOutUs: Long
    ): Boolean = nativeHandle != 0L && nativeSetMusicTrack(
        nativeHandle, title, sourceUri, isBundled, durationUs, trimStartUs, trimEndUs,
        volume, muted, loop, fadeInUs, fadeOutUs
    )

    fun removeMusicTrack(): Boolean =
        nativeHandle != 0L && nativeRemoveMusicTrack(nativeHandle)

    fun addVoiceover(
        title: String,
        filePath: String,
        durationUs: Long,
        timelineStartUs: Long,
        volume: Float
    ): String = if (nativeHandle != 0L) {
        nativeAddVoiceover(nativeHandle, title, filePath, durationUs, timelineStartUs, volume)
    } else ""

    fun removeVoiceover(voId: String): Boolean =
        nativeHandle != 0L && nativeRemoveVoiceover(nativeHandle, voId)

    fun upsertTextOverlay(
        id: String,
        text: String,
        fontFamily: String,
        fontSizeSp: Float,
        colorArgb: Long,
        align: Int,
        normX: Float,
        normY: Float,
        startUs: Long,
        endUs: Long,
        animation: Int
    ): String = if (nativeHandle != 0L) {
        nativeUpsertTextOverlay(
            nativeHandle, id, text, fontFamily, fontSizeSp, colorArgb,
            align, normX, normY, startUs, endUs, animation
        )
    } else ""

    fun removeTextOverlay(id: String): Boolean =
        nativeHandle != 0L && nativeRemoveTextOverlay(nativeHandle, id)

    fun seekTo(playheadUs: Long) {
        if (nativeHandle != 0L) nativeSeekTo(nativeHandle, playheadUs)
    }

    fun undo(): Boolean = nativeHandle != 0L && nativeUndo(nativeHandle)
    fun redo(): Boolean = nativeHandle != 0L && nativeRedo(nativeHandle)
    fun canUndo(): Boolean = nativeHandle != 0L && nativeCanUndo(nativeHandle)
    fun canRedo(): Boolean = nativeHandle != 0L && nativeCanRedo(nativeHandle)

    fun exportProject(outputPath: String, fps: Int, callback: NativeExportCallback): Boolean =
        nativeHandle != 0L && nativeExportProject(nativeHandle, outputPath, fps, callback)

    fun cancelExport() {
        if (nativeHandle != 0L) nativeCancelExport(nativeHandle)
    }

    fun attachPreviewSurface(surface: Surface, width: Int, height: Int): Boolean =
        nativeHandle != 0L && nativeAttachPreviewSurface(nativeHandle, surface, width, height)

    fun detachPreviewSurface() {
        if (nativeHandle != 0L) nativeDetachPreviewSurface(nativeHandle)
    }

    fun setClipPlacement(clipId: String, timelinePositionUs: Long, trackIndex: Int): Boolean =
        nativeHandle != 0L && nativeSetClipPlacement(nativeHandle, clipId, timelinePositionUs, trackIndex)

    fun setTrackState(
        kind: Int,
        trackIndex: Int,
        locked: Boolean,
        muted: Boolean,
        hidden: Boolean,
        solo: Boolean
    ): Boolean = nativeHandle != 0L && nativeSetTrackState(
        nativeHandle, kind, trackIndex, locked, muted, hidden, solo
    )

    fun batchShiftSelection(selectionJson: String, deltaUs: Long, trackDelta: Int): Boolean =
        nativeHandle != 0L && nativeBatchShiftSelection(nativeHandle, selectionJson, deltaUs, trackDelta)

    fun batchMuteSelection(selectionJson: String, muted: Boolean): Boolean =
        nativeHandle != 0L && nativeBatchMuteSelection(nativeHandle, selectionJson, muted)

    fun batchDeleteSelection(selectionJson: String): Boolean =
        nativeHandle != 0L && nativeBatchDeleteSelection(nativeHandle, selectionJson)

    override fun close() {
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0L
        }
    }

    private external fun nativeCreate(workspaceDir: String): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeListProjectsJson(handle: Long): String
    private external fun nativeCreateProject(handle: Long, name: String): String
    private external fun nativeOpenProject(handle: Long, projectId: String): Boolean
    private external fun nativeSaveProject(handle: Long): Boolean
    private external fun nativeRenameProject(handle: Long, projectId: String, newName: String): Boolean
    private external fun nativeDeleteProject(handle: Long, projectId: String): Boolean
    private external fun nativeImportMedia(
        handle: Long, uri: String, displayName: String, isImage: Boolean,
        durationUs: Long, width: Int, height: Int, fileSizeBytes: Long, thumbnailPath: String
    ): String
    private external fun nativeGetActiveProjectJson(handle: Long): String
    private external fun nativeTrimClip(handle: Long, clipId: String, startUs: Long, endUs: Long): Boolean
    private external fun nativeSplitAtPlayhead(handle: Long, playheadUs: Long): Boolean
    private external fun nativeSetSpeed(handle: Long, clipId: String, speed: Double, audioSpeedMode: Int): Boolean
    private external fun nativeSetVolume(handle: Long, clipId: String, volume: Float, muted: Boolean): Boolean
    private external fun nativeSetCrop(
        handle: Long, clipId: String, preset: String, aspectW: Int, aspectH: Int,
        offsetX: Float, offsetY: Float, zoom: Float, rotationDeg: Int
    ): Boolean
    private external fun nativeSetFilter(
        handle: Long, clipId: String, brightness: Float, contrast: Float, saturation: Float,
        temperature: Float, exposure: Float, sharpness: Float
    ): Boolean
    private external fun nativeSetTransition(handle: Long, clipId: String, transitionType: Int, durationUs: Long): Boolean
    private external fun nativeReorderClip(handle: Long, fromIdx: Int, toIdx: Int): Boolean
    private external fun nativeDeleteClip(handle: Long, clipId: String): Boolean
    private external fun nativeSetMusicTrack(
        handle: Long, title: String, sourceUri: String, isBundled: Boolean, durationUs: Long,
        trimStartUs: Long, trimEndUs: Long, volume: Float, muted: Boolean, loop: Boolean,
        fadeInUs: Long, fadeOutUs: Long
    ): Boolean
    private external fun nativeRemoveMusicTrack(handle: Long): Boolean
    private external fun nativeAddVoiceover(
        handle: Long, title: String, filePath: String, durationUs: Long, timelineStartUs: Long, volume: Float
    ): String
    private external fun nativeRemoveVoiceover(handle: Long, voId: String): Boolean
    private external fun nativeUpsertTextOverlay(
        handle: Long, id: String, text: String, fontFamily: String, fontSizeSp: Float,
        colorArgb: Long, align: Int, normX: Float, normY: Float, startUs: Long, endUs: Long, animation: Int
    ): String
    private external fun nativeRemoveTextOverlay(handle: Long, id: String): Boolean
    private external fun nativeSeekTo(handle: Long, playheadUs: Long)
    private external fun nativeUndo(handle: Long): Boolean
    private external fun nativeRedo(handle: Long): Boolean
    private external fun nativeCanUndo(handle: Long): Boolean
    private external fun nativeCanRedo(handle: Long): Boolean
    private external fun nativeExportProject(
        handle: Long, outputPath: String, fps: Int, callback: NativeExportCallback
    ): Boolean
    private external fun nativeCancelExport(handle: Long)
    private external fun nativeAttachPreviewSurface(handle: Long, surface: Surface, width: Int, height: Int): Boolean
    private external fun nativeDetachPreviewSurface(handle: Long)
    private external fun nativeSetClipPlacement(handle: Long, clipId: String, timelinePositionUs: Long, trackIndex: Int): Boolean
    private external fun nativeSetTrackState(
        handle: Long, kind: Int, trackIndex: Int, locked: Boolean, muted: Boolean, hidden: Boolean, solo: Boolean
    ): Boolean
    private external fun nativeBatchShiftSelection(handle: Long, selectionJson: String, deltaUs: Long, trackDelta: Int): Boolean
    private external fun nativeBatchMuteSelection(handle: Long, selectionJson: String, muted: Boolean): Boolean
    private external fun nativeBatchDeleteSelection(handle: Long, selectionJson: String): Boolean

    companion object {
        init {
            System.loadLibrary("trisvax_jni")
        }
    }
}
