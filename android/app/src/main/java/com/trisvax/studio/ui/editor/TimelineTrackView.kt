package com.trisvax.studio.ui.editor

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.LockOpen
import androidx.compose.material.icons.filled.SelectAll
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.material.icons.filled.VolumeOff
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil.compose.AsyncImage
import com.trisvax.studio.models.TimelineDto
import com.trisvax.studio.models.TimelineSelectionDto
import com.trisvax.studio.models.TrackStateDto
import com.trisvax.studio.theme.TrisvaxColors
import java.io.File
import kotlin.math.abs
import kotlin.math.roundToInt
import kotlin.math.sin

private const val DP_PER_SECOND = 44f
private val HEADER_WIDTH = 72.dp

@Composable
fun TimelineTrackView(
    timeline: TimelineDto,
    playheadMs: Long,
    selectedClipId: String?,
    selection: TimelineSelectionDto,
    multiSelectMode: Boolean,
    onToggleMultiSelectMode: () -> Unit,
    onSelectAll: () -> Unit,
    onClearSelection: () -> Unit,
    onBatchShift: (Long) -> Unit,
    onBatchMute: (Boolean) -> Unit,
    onBatchDelete: () -> Unit,
    onToggleTrackState: (Int, Int, String) -> Unit,
    onSeek: (Long) -> Unit,
    onSelectClip: (String) -> Unit,
    onToggleClipMute: (String) -> Unit,
    onSelectMusic: (String) -> Unit,
    onSelectVoiceover: (String) -> Unit,
    onOpenMusicTool: () -> Unit,
    onOpenTextTool: () -> Unit,
    onSelectText: (String) -> Unit,
    onOpenVoiceTool: () -> Unit
) {
    val totalMs = timeline.totalDurationMs.coerceAtLeast(3000L)
    val totalSeconds = (totalMs / 1000f).coerceAtLeast(3f)
    val trackWidthDp = (totalSeconds * DP_PER_SECOND).dp.coerceAtLeast(280.dp)
    val density = LocalDensity.current
    val pxPerMs = with(density) { (DP_PER_SECOND.dp.toPx()) / 1000f }
    val scrollState = rememberScrollState()

    val maxVideoTrack = (timeline.clips.maxOfOrNull { it.trackIndex } ?: 0).coerceAtLeast(1)
    val videoTrackIndices = (maxVideoTrack downTo 0).toList()

    fun stateFor(kind: Int, trackIndex: Int): TrackStateDto =
        timeline.trackStates.firstOrNull { it.kind == kind && it.trackIndex == trackIndex }
            ?: TrackStateDto(kind = kind, trackIndex = trackIndex)

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(TrisvaxColors.MainBackground)
            .padding(horizontal = 8.dp, vertical = 6.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        // Multi-Select & Batch Action Bar
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Surface(
                color = if (multiSelectMode) TrisvaxColors.PrimaryOrange else TrisvaxColors.PrimaryCard,
                shape = RoundedCornerShape(8.dp),
                border = BorderStroke(1.dp, if (multiSelectMode) TrisvaxColors.SecondaryOrange else TrisvaxColors.Border),
                modifier = Modifier.clickable { onToggleMultiSelectMode() }
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Icon(Icons.Filled.SelectAll, contentDescription = "Multi-select", tint = TrisvaxColors.PrimaryText, modifier = Modifier.size(14.dp))
                    Text(
                        text = if (selection.totalCount > 1) "${selection.totalCount} selected" else "Multi-Select",
                        color = TrisvaxColors.PrimaryText,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.SemiBold
                    )
                }
            }

            if (selection.totalCount > 0) {
                BatchChip("-0.5s") { onBatchShift(-500L) }
                BatchChip("+0.5s") { onBatchShift(500L) }
                BatchChip("Mute") { onBatchMute(true) }
                BatchChip("Unmute") { onBatchMute(false) }
                Surface(
                    color = TrisvaxColors.PrimaryCard,
                    shape = RoundedCornerShape(8.dp),
                    border = BorderStroke(1.dp, TrisvaxColors.ErrorDanger),
                    modifier = Modifier.clickable { onBatchDelete() }
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 7.dp, vertical = 4.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(3.dp)
                    ) {
                        Icon(Icons.Filled.Delete, contentDescription = "Delete selected", tint = TrisvaxColors.ErrorDanger, modifier = Modifier.size(12.dp))
                        Text("Del", color = TrisvaxColors.ErrorDanger, fontSize = 10.sp, fontWeight = FontWeight.SemiBold)
                    }
                }
                BatchChip("All") { onSelectAll() }
                BatchChip("Clear") { onClearSelection() }
            }
        }

        Row(modifier = Modifier.fillMaxWidth()) {
            // Left Track Header Column (V2, V1, T1 above divider; A1, A2 below divider)
            Column(
                modifier = Modifier.width(HEADER_WIDTH),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Box(modifier = Modifier.height(18.dp), contentAlignment = Alignment.CenterStart) {
                    Text("TRACKS", color = TrisvaxColors.MutedText, fontSize = 9.sp, fontWeight = FontWeight.Bold)
                }
                videoTrackIndices.forEach { vIdx ->
                    val st = stateFor(0, vIdx)
                    TrackHeaderCell(
                        badge = "V${vIdx + 1}",
                        badgeColor = TrisvaxColors.BlueHighlight,
                        locked = st.locked,
                        secondaryActive = !st.hidden,
                        isAudio = false,
                        muted = false,
                        solo = false,
                        onToggleLock = { onToggleTrackState(0, vIdx, "locked") },
                        onToggleSecondary = { onToggleTrackState(0, vIdx, "hidden") },
                        onToggleSolo = {}
                    )
                }
                val txtSt = stateFor(2, 0)
                TrackHeaderCell(
                    badge = "T1",
                    badgeColor = TrisvaxColors.SecondaryOrange,
                    locked = txtSt.locked,
                    secondaryActive = !txtSt.hidden,
                    isAudio = false,
                    muted = false,
                    solo = false,
                    onToggleLock = { onToggleTrackState(2, 0, "locked") },
                    onToggleSecondary = { onToggleTrackState(2, 0, "hidden") },
                    onToggleSolo = {}
                )
                HorizontalDivider(color = TrisvaxColors.Border, thickness = 1.dp)
                val a1St = stateFor(1, 0)
                TrackHeaderCell(
                    badge = "A1",
                    badgeColor = TrisvaxColors.PrimaryOrange,
                    locked = a1St.locked,
                    secondaryActive = true,
                    isAudio = true,
                    muted = a1St.muted,
                    solo = a1St.solo,
                    onToggleLock = { onToggleTrackState(1, 0, "locked") },
                    onToggleSecondary = { onToggleTrackState(1, 0, "muted") },
                    onToggleSolo = { onToggleTrackState(1, 0, "solo") }
                )
                val a2St = stateFor(1, 1)
                TrackHeaderCell(
                    badge = "A2",
                    badgeColor = TrisvaxColors.BlueHighlight,
                    locked = a2St.locked,
                    secondaryActive = true,
                    isAudio = true,
                    muted = a2St.muted,
                    solo = a2St.solo,
                    onToggleLock = { onToggleTrackState(1, 1, "locked") },
                    onToggleSecondary = { onToggleTrackState(1, 1, "muted") },
                    onToggleSolo = { onToggleTrackState(1, 1, "solo") }
                )
            }

            Spacer(modifier = Modifier.width(6.dp))

            // Scrollable Multi-Track Lanes
            Box(
                modifier = Modifier
                    .weight(1f)
                    .horizontalScroll(scrollState)
                    .pointerInput(totalMs, pxPerMs) {
                        detectTapGestures { offset ->
                            val ms = (offset.x / pxPerMs).toLong().coerceIn(0L, totalMs)
                            onSeek(ms)
                        }
                    }
            ) {
                Column(
                    modifier = Modifier.width(trackWidthDp),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    // Time Ruler
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(18.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        val tickCount = totalSeconds.toInt().coerceAtLeast(1)
                        for (sec in 0..tickCount) {
                            Box(modifier = Modifier.width(DP_PER_SECOND.dp)) {
                                Text(
                                    text = "%d:%02d".format(sec / 60, sec % 60),
                                    color = TrisvaxColors.MutedText,
                                    fontSize = 10.sp
                                )
                            }
                        }
                    }

                    // Video Lanes (V2, V1)
                    videoTrackIndices.forEach { vIdx ->
                        val st = stateFor(0, vIdx)
                        val laneClips = timeline.clips.filter { it.trackIndex == vIdx }
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(42.dp)
                                .clip(RoundedCornerShape(8.dp))
                                .background(TrisvaxColors.PrimaryCard.copy(alpha = if (st.hidden) 0.35f else 1f))
                                .border(1.dp, TrisvaxColors.Border, RoundedCornerShape(8.dp))
                        ) {
                            laneClips.forEach { clip ->
                                val startDp = ((clip.timelinePositionUs / 1_000_000f) * DP_PER_SECOND).dp
                                val clipWidthDp = ((clip.effectiveDurationMs / 1000f) * DP_PER_SECOND)
                                    .dp
                                    .coerceAtLeast(48.dp)
                                val isSelected = clip.id == selectedClipId || clip.id in selection.clipIds
                                Box(
                                    modifier = Modifier
                                        .offset(x = startDp)
                                        .width(clipWidthDp)
                                        .fillMaxHeight()
                                        .padding(1.dp)
                                        .clip(RoundedCornerShape(7.dp))
                                        .background(TrisvaxColors.SecondaryCard)
                                        .border(
                                            width = if (isSelected) 2.dp else 1.dp,
                                            color = if (isSelected) TrisvaxColors.PrimaryOrange else TrisvaxColors.Border,
                                            shape = RoundedCornerShape(7.dp)
                                        )
                                        .clickable { onSelectClip(clip.id) }
                                ) {
                                    val thumbFile = clip.thumbnailPath.takeIf { it.isNotBlank() }?.let(::File)?.takeIf { it.exists() }
                                    if (thumbFile != null) {
                                        AsyncImage(
                                            model = thumbFile,
                                            contentDescription = clip.displayName,
                                            contentScale = ContentScale.Crop,
                                            modifier = Modifier.fillMaxWidth().fillMaxHeight()
                                        )
                                    }
                                    Box(
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .fillMaxHeight()
                                            .background(Color.Black.copy(alpha = 0.32f))
                                    )
                                    Text(
                                        text = clip.displayName,
                                        color = TrisvaxColors.PrimaryText,
                                        fontSize = 10.sp,
                                        fontWeight = FontWeight.Medium,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis,
                                        modifier = Modifier
                                            .align(Alignment.CenterStart)
                                            .padding(start = 6.dp, end = 24.dp)
                                    )
                                    Box(
                                        modifier = Modifier
                                            .align(Alignment.CenterEnd)
                                            .padding(end = 3.dp)
                                            .size(18.dp)
                                            .clip(CircleShape)
                                            .background(Color.Black.copy(alpha = 0.65f))
                                            .clickable { onToggleClipMute(clip.id) },
                                        contentAlignment = Alignment.Center
                                    ) {
                                        Icon(
                                            imageVector = if (clip.isMuted) Icons.Filled.VolumeOff else Icons.Filled.VolumeUp,
                                            contentDescription = "Mute clip",
                                            tint = if (clip.isMuted) TrisvaxColors.ErrorDanger else TrisvaxColors.PrimaryText,
                                            modifier = Modifier.size(11.dp)
                                        )
                                    }
                                }
                            }
                        }
                    }

                    // Text Track (T1)
                    val txtSt = stateFor(2, 0)
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(34.dp)
                            .clip(RoundedCornerShape(8.dp))
                            .background(TrisvaxColors.PrimaryCard.copy(alpha = if (txtSt.hidden) 0.35f else 1f))
                            .border(1.dp, TrisvaxColors.Border, RoundedCornerShape(8.dp))
                            .clickable { onOpenTextTool() }
                    ) {
                        if (timeline.textOverlays.isEmpty()) {
                            Row(
                                modifier = Modifier.fillMaxHeight().padding(horizontal = 10.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(Icons.Filled.Add, contentDescription = null, tint = TrisvaxColors.SecondaryOrange, modifier = Modifier.size(14.dp))
                                Spacer(modifier = Modifier.width(6.dp))
                                Text("Add text", color = TrisvaxColors.SecondaryText, fontSize = 11.sp)
                            }
                        } else {
                            timeline.textOverlays.forEach { overlay ->
                                val startDp = ((overlay.startUs / 1_000_000f) * DP_PER_SECOND).dp
                                val durDp = (((overlay.endUs - overlay.startUs).coerceAtLeast(400_000L) / 1_000_000f) * DP_PER_SECOND)
                                    .dp
                                    .coerceAtLeast(58.dp)
                                val isSelected = overlay.id in selection.textIds
                                Surface(
                                    color = TrisvaxColors.SecondaryCard,
                                    shape = RoundedCornerShape(6.dp),
                                    border = BorderStroke(
                                        if (isSelected) 2.dp else 1.dp,
                                        if (isSelected) TrisvaxColors.PrimaryOrange else TrisvaxColors.SecondaryOrange
                                    ),
                                    modifier = Modifier
                                        .offset(x = startDp)
                                        .width(durDp)
                                        .fillMaxHeight()
                                        .padding(2.dp)
                                        .clickable { onSelectText(overlay.id) }
                                ) {
                                    Box(modifier = Modifier.padding(horizontal = 6.dp), contentAlignment = Alignment.CenterStart) {
                                        Text(
                                            text = overlay.text,
                                            color = TrisvaxColors.PrimaryText,
                                            fontSize = 10.sp,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                    }
                                }
                            }
                        }
                    }

                    HorizontalDivider(color = TrisvaxColors.Border, thickness = 1.dp)

                    // Audio Track A1 (Music with waveform)
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(38.dp)
                            .clip(RoundedCornerShape(8.dp))
                            .background(TrisvaxColors.PrimaryCard)
                            .border(1.dp, TrisvaxColors.Border, RoundedCornerShape(8.dp))
                            .clickable { onOpenMusicTool() }
                    ) {
                        val music = timeline.musicTrack
                        if (music == null) {
                            Row(
                                modifier = Modifier.fillMaxHeight().padding(horizontal = 10.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(Icons.Filled.Add, contentDescription = null, tint = TrisvaxColors.PrimaryOrange, modifier = Modifier.size(14.dp))
                                Spacer(modifier = Modifier.width(6.dp))
                                Text("Add audio", color = TrisvaxColors.SecondaryText, fontSize = 11.sp)
                            }
                        } else {
                            val startDp = ((music.timelineStartUs / 1_000_000f) * DP_PER_SECOND).dp
                            val spanUs = if (music.loopToFit) {
                                maxOf(music.trimEndUs - music.trimStartUs, totalMs * 1000L)
                            } else {
                                (music.trimEndUs - music.trimStartUs).coerceAtLeast(500_000L)
                            }
                            val widthDp = ((spanUs / 1_000_000f) * DP_PER_SECOND).dp.coerceAtLeast(64.dp)
                            val isSelected = music.id in selection.audioIds
                            Box(
                                modifier = Modifier
                                    .offset(x = startDp)
                                    .width(widthDp)
                                    .fillMaxHeight()
                                    .padding(2.dp)
                                    .clip(RoundedCornerShape(6.dp))
                                    .background(Color(0xFF132D24))
                                    .border(
                                        if (isSelected) 2.dp else 1.dp,
                                        if (isSelected) TrisvaxColors.PrimaryOrange else Color(0xFF2FA37A),
                                        RoundedCornerShape(6.dp)
                                    )
                                    .clickable { onSelectMusic(music.id) }
                            ) {
                                WaveformBars(color = Color(0xFF43E3A7), seed = music.title.hashCode())
                                Text(
                                    text = "${music.title} (${(music.volume * 100).roundToInt()}%)",
                                    color = TrisvaxColors.PrimaryText,
                                    fontSize = 10.sp,
                                    fontWeight = FontWeight.Medium,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                    modifier = Modifier.align(Alignment.TopStart).padding(horizontal = 6.dp, vertical = 2.dp)
                                )
                            }
                        }
                    }

                    // Audio Track A2 (Voiceovers with waveform)
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(38.dp)
                            .clip(RoundedCornerShape(8.dp))
                            .background(TrisvaxColors.PrimaryCard)
                            .border(1.dp, TrisvaxColors.Border, RoundedCornerShape(8.dp))
                            .clickable { onOpenVoiceTool() }
                    ) {
                        if (timeline.voiceovers.isEmpty()) {
                            Row(
                                modifier = Modifier.fillMaxHeight().padding(horizontal = 10.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(Icons.Filled.Add, contentDescription = null, tint = TrisvaxColors.BlueHighlight, modifier = Modifier.size(14.dp))
                                Spacer(modifier = Modifier.width(6.dp))
                                Text("Add voiceover", color = TrisvaxColors.SecondaryText, fontSize = 11.sp)
                            }
                        } else {
                            timeline.voiceovers.forEach { vo ->
                                val startDp = ((vo.timelineStartUs / 1_000_000f) * DP_PER_SECOND).dp
                                val spanUs = (vo.trimEndUs - vo.trimStartUs).coerceAtLeast(400_000L)
                                val widthDp = ((spanUs / 1_000_000f) * DP_PER_SECOND).dp.coerceAtLeast(60.dp)
                                val isSelected = vo.id in selection.voiceoverIds
                                Box(
                                    modifier = Modifier
                                        .offset(x = startDp)
                                        .width(widthDp)
                                        .fillMaxHeight()
                                        .padding(2.dp)
                                        .clip(RoundedCornerShape(6.dp))
                                        .background(Color(0xFF15243A))
                                        .border(
                                            if (isSelected) 2.dp else 1.dp,
                                            if (isSelected) TrisvaxColors.PrimaryOrange else TrisvaxColors.BlueHighlight,
                                            RoundedCornerShape(6.dp)
                                        )
                                        .clickable { onSelectVoiceover(vo.id) }
                                ) {
                                    WaveformBars(color = TrisvaxColors.BlueHighlight, seed = vo.title.hashCode())
                                    Text(
                                        text = vo.title,
                                        color = TrisvaxColors.PrimaryText,
                                        fontSize = 10.sp,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis,
                                        modifier = Modifier.align(Alignment.TopStart).padding(horizontal = 6.dp, vertical = 2.dp)
                                    )
                                }
                            }
                        }
                    }
                }

                // Draggable Playhead Line
                val playheadX = (playheadMs * pxPerMs).roundToInt()
                Box(
                    modifier = Modifier
                        .offset { IntOffset(playheadX - 8, 0) }
                        .width(16.dp)
                        .height((18 + videoTrackIndices.size * 46 + 128).dp)
                        .pointerInput(totalMs, pxPerMs) {
                            detectDragGestures { change, dragAmount ->
                                change.consume()
                                val deltaMs = (dragAmount.x / pxPerMs).toLong()
                                onSeek((playheadMs + deltaMs).coerceIn(0L, totalMs))
                            }
                        },
                    contentAlignment = Alignment.Center
                ) {
                    Box(modifier = Modifier.width(2.dp).fillMaxHeight().background(TrisvaxColors.PrimaryText))
                    Box(
                        modifier = Modifier
                            .size(12.dp)
                            .clip(CircleShape)
                            .background(TrisvaxColors.PrimaryText)
                            .border(2.dp, TrisvaxColors.MainBackground, CircleShape)
                    )
                }
            }
        }
    }
}

@Composable
private fun BatchChip(label: String, onClick: () -> Unit) {
    Surface(
        color = TrisvaxColors.PrimaryCard,
        shape = RoundedCornerShape(8.dp),
        border = BorderStroke(1.dp, TrisvaxColors.Border),
        modifier = Modifier.clickable { onClick() }
    ) {
        Text(
            text = label,
            color = TrisvaxColors.PrimaryText,
            fontSize = 10.sp,
            fontWeight = FontWeight.Medium,
            modifier = Modifier.padding(horizontal = 7.dp, vertical = 4.dp)
        )
    }
}

@Composable
private fun TrackHeaderCell(
    badge: String,
    badgeColor: Color,
    locked: Boolean,
    secondaryActive: Boolean,
    isAudio: Boolean,
    muted: Boolean,
    solo: Boolean,
    onToggleLock: () -> Unit,
    onToggleSecondary: () -> Unit,
    onToggleSolo: () -> Unit
) {
    val rowHeight = if (badge.startsWith("V")) 42.dp else if (badge.startsWith("T")) 34.dp else 38.dp
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(rowHeight)
            .clip(RoundedCornerShape(6.dp))
            .background(TrisvaxColors.PrimaryCard)
            .border(1.dp, TrisvaxColors.Border, RoundedCornerShape(6.dp))
            .padding(horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(text = badge, color = badgeColor, fontSize = 10.sp, fontWeight = FontWeight.Bold)
        Row(horizontalArrangement = Arrangement.spacedBy(3.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(
                imageVector = if (locked) Icons.Filled.Lock else Icons.Filled.LockOpen,
                contentDescription = "Lock $badge",
                tint = if (locked) TrisvaxColors.PrimaryOrange else TrisvaxColors.MutedText,
                modifier = Modifier.size(12.dp).clickable { onToggleLock() }
            )
            if (isAudio) {
                Text(
                    text = "M",
                    color = if (muted) TrisvaxColors.ErrorDanger else TrisvaxColors.SecondaryText,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.clickable { onToggleSecondary() }
                )
                Text(
                    text = "S",
                    color = if (solo) TrisvaxColors.SecondaryOrange else TrisvaxColors.SecondaryText,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.clickable { onToggleSolo() }
                )
            } else {
                Icon(
                    imageVector = if (secondaryActive) Icons.Filled.Visibility else Icons.Filled.VisibilityOff,
                    contentDescription = "Hide $badge",
                    tint = if (secondaryActive) TrisvaxColors.PrimaryText else TrisvaxColors.MutedText,
                    modifier = Modifier.size(12.dp).clickable { onToggleSecondary() }
                )
            }
        }
    }
}

@Composable
private fun WaveformBars(color: Color, seed: Int) {
    Canvas(modifier = Modifier.fillMaxWidth().fillMaxHeight()) {
        val barWidth = 3f
        val gap = 3f
        var x = 2f
        var i = 0
        while (x < size.width - 2f) {
            val amp = 0.25f + 0.65f * abs(sin((i * 0.7f) + seed * 0.1f))
            val h = size.height * amp
            drawLine(
                color = color.copy(alpha = 0.45f),
                start = Offset(x, size.height - 2f),
                end = Offset(x, size.height - h),
                strokeWidth = barWidth
            )
            x += barWidth + gap
            i++
        }
    }
}
