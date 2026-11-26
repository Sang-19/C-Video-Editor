package com.trisvax.studio.models

import kotlinx.serialization.Serializable

@Serializable
enum class AspectRatioPreset(
    val label: String,
    val ratioWidth: Int,
    val ratioHeight: Int,
    val exportWidth: Int,
    val exportHeight: Int
) {
    PORTRAIT_9_16("9:16", 9, 16, 1080, 1920),
    LANDSCAPE_16_9("16:9", 16, 9, 1920, 1080),
    SQUARE_1_1("1:1", 1, 1, 1080, 1080),
    FREEFORM("Free", 9, 16, 1080, 1920)
}

@Serializable
data class ProjectSummaryDto(
    val id: String = "",
    val name: String = "New project",
    val category: String = "DRAFTS",
    val thumbnailPath: String = "",
    val durationMs: Long = 0L,
    val storageSizeBytes: Long = 0L,
    val createdAtMs: Long = 0L,
    val lastModifiedAtMs: Long = 0L,
    val exportedPath: String = "",
    val missingMediaCount: Int = 0
)

@Serializable
data class CropDto(
    val preset: String = "PORTRAIT_9_16",
    val aspectWidth: Int = 9,
    val aspectHeight: Int = 16,
    val offsetX: Float = 0f,
    val offsetY: Float = 0f,
    val zoom: Float = 1f,
    val rotationDegrees: Int = 0
)

@Serializable
data class FilterDto(
    val brightness: Float = 0f,
    val contrast: Float = 1f,
    val saturation: Float = 1f,
    val temperature: Float = 0f,
    val exposure: Float = 0f,
    val sharpness: Float = 0f
)

@Serializable
data class TransitionDto(
    val type: Int = 0, // 0=Cut, 1=Fade, 2=CrossDissolve
    val durationUs: Long = 0L
)

@Serializable
data class ClipDto(
    val id: String = "",
    val sourceUri: String = "",
    val displayName: String = "",
    val isImage: Boolean = false,
    val sourceDurationUs: Long = 3_000_000L,
    val inPointUs: Long = 0L,
    val outPointUs: Long = 3_000_000L,
    val timelinePositionUs: Long = 0L,
    val trackIndex: Int = 0,
    val playbackSpeed: Double = 1.0,
    val audioSpeedMode: Int = 0,
    val volume: Float = 1f,
    val isMuted: Boolean = false,
    val crop: CropDto = CropDto(),
    val filter: FilterDto = FilterDto(),
    val transitionIn: TransitionDto = TransitionDto(),
    val thumbnailPath: String = "",
    val mediaAvailable: Boolean = true
) {
    val effectiveDurationMs: Long
        get() = (((outPointUs - inPointUs).coerceAtLeast(250_000L)) / playbackSpeed.coerceIn(0.25, 4.0) / 1000.0).toLong()
}

@Serializable
data class MusicTrackDto(
    val id: String = "",
    val title: String = "",
    val sourceUri: String = "",
    val isBundledAsset: Boolean = false,
    val sourceDurationUs: Long = 15_000_000L,
    val trimStartUs: Long = 0L,
    val trimEndUs: Long = 15_000_000L,
    val timelineStartUs: Long = 0L,
    val trackIndex: Int = 0,
    val volume: Float = 0.8f,
    val isMuted: Boolean = false,
    val loopToFit: Boolean = true,
    val fadeInUs: Long = 0L,
    val fadeOutUs: Long = 0L
)

@Serializable
data class VoiceoverDto(
    val id: String = "",
    val title: String = "Voiceover",
    val filePath: String = "",
    val sourceDurationUs: Long = 0L,
    val trimStartUs: Long = 0L,
    val trimEndUs: Long = 0L,
    val timelineStartUs: Long = 0L,
    val trackIndex: Int = 1,
    val volume: Float = 1f,
    val isMuted: Boolean = false
)

@Serializable
data class TextOverlayDto(
    val id: String = "",
    val text: String = "",
    val fontFamily: String = "Sans",
    val fontSizeSp: Float = 28f,
    val colorArgb: Long = 0xFFFFFFFFL,
    val alignment: Int = 1,
    val normalizedX: Float = 0.5f,
    val normalizedY: Float = 0.5f,
    val startUs: Long = 0L,
    val endUs: Long = 3_500_000L,
    val trackIndex: Int = 0,
    val animation: Int = 0
)

@Serializable
data class TrackStateDto(
    val kind: Int = 0, // 0=Video, 1=Audio, 2=Text
    val trackIndex: Int = 0,
    val locked: Boolean = false,
    val muted: Boolean = false,
    val hidden: Boolean = false,
    val solo: Boolean = false
)

@Serializable
data class TimelineSelectionDto(
    val clipIds: Set<String> = emptySet(),
    val audioIds: Set<String> = emptySet(),
    val voiceoverIds: Set<String> = emptySet(),
    val textIds: Set<String> = emptySet()
) {
    val totalCount: Int
        get() = clipIds.size + audioIds.size + voiceoverIds.size + textIds.size
    val isEmpty: Boolean
        get() = totalCount == 0
}

@Serializable
data class TimelineDto(
    val aspectRatio: String = "PORTRAIT_9_16",
    val customAspectWidth: Int = 9,
    val customAspectHeight: Int = 16,
    val clips: List<ClipDto> = emptyList(),
    val musicTrack: MusicTrackDto? = null,
    val voiceovers: List<VoiceoverDto> = emptyList(),
    val textOverlays: List<TextOverlayDto> = emptyList(),
    val trackStates: List<TrackStateDto> = emptyList()
) {
    val totalDurationMs: Long
        get() {
            val clipEnd = clips.maxOfOrNull { (it.timelinePositionUs / 1000L) + it.effectiveDurationMs } ?: 0L
            val voEnd = voiceovers.maxOfOrNull { (it.timelineStartUs + (it.trimEndUs - it.trimStartUs).coerceAtLeast(0L)) / 1000L } ?: 0L
            val txtEnd = textOverlays.maxOfOrNull { it.endUs / 1000L } ?: 0L
            return maxOf(clipEnd, voEnd, txtEnd)
        }
}

@Serializable
data class ProjectDocumentDto(
    val schemaVersion: Int = 1,
    val metadata: ProjectSummaryDto = ProjectSummaryDto(),
    val timeline: TimelineDto = TimelineDto()
)

data class BundledMusicAsset(
    val fileName: String,
    val title: String,
    val assetPath: String,
    val durationUs: Long
)

sealed interface ExportUiState {
    data object Idle : ExportUiState
    data class Exporting(val progressPercent: Int, val statusMessage: String) : ExportUiState
    data class Success(val outputUri: String, val outputFileName: String) : ExportUiState
    data class Error(val message: String) : ExportUiState
}
