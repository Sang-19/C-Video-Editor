package com.trisvax.studio.ui.editor

import android.net.Uri
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Redo
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.Crop
import androidx.compose.material.icons.filled.FastForward
import androidx.compose.material.icons.filled.Layers
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.RotateRight
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.SwapHoriz
import androidx.compose.material.icons.filled.TextFields
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.trisvax.studio.components.EditorToolButton
import com.trisvax.studio.components.formatShortTimecode
import com.trisvax.studio.theme.TrisvaxColors
import com.trisvax.studio.ui.export.ExportProgressDialog

@Composable
fun EditorScreen(
    projectId: String,
    viewModel: EditorViewModel,
    onClose: () -> Unit,
    onExportFinished: () -> Unit
) {
    val state by viewModel.uiState.collectAsState()

    LaunchedEffect(projectId) {
        viewModel.loadProject(projectId)
    }

    val appendPicker = rememberLauncherForActivityResult(
        ActivityResultContracts.PickMultipleVisualMedia()
    ) { uris: List<Uri> ->
        if (uris.isNotEmpty()) {
            viewModel.appendMediaUris(uris)
        }
    }

    DisposableEffect(Unit) {
        onDispose { viewModel.detachPreviewSurface() }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(TrisvaxColors.MainBackground)
            .safeDrawingPadding()
    ) {
        // Video Preview Viewport with X Close & Next Export
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .padding(horizontal = 12.dp, vertical = 8.dp)
                .clip(RoundedCornerShape(16.dp))
                .background(Color.Black)
                .border(1.dp, TrisvaxColors.Border, RoundedCornerShape(16.dp))
        ) {
            AndroidView(
                factory = { ctx ->
                    SurfaceView(ctx).apply {
                        holder.addCallback(object : SurfaceHolder.Callback {
                            override fun surfaceCreated(holder: SurfaceHolder) {
                                viewModel.attachPreviewSurface(holder.surface, width, height)
                            }

                            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                                viewModel.attachPreviewSurface(holder.surface, width, height)
                            }

                            override fun surfaceDestroyed(holder: SurfaceHolder) {
                                viewModel.detachPreviewSurface()
                            }
                        })
                    }
                },
                modifier = Modifier.fillMaxSize()
            )

            // Active text overlay preview labels rendered over surface
            val curUs = state.playheadMs * 1_000L
            state.timeline.textOverlays.filter { curUs in it.startUs..it.endUs }.forEach { overlay ->
                Box(
                    modifier = Modifier
                        .align(
                            when {
                                overlay.normalizedY < 0.35f -> Alignment.TopCenter
                                overlay.normalizedY > 0.65f -> Alignment.BottomCenter
                                else -> Alignment.Center
                            }
                        )
                        .padding(horizontal = 18.dp, vertical = 28.dp)
                        .clickable { viewModel.selectText(overlay.id) }
                ) {
                    Text(
                        text = overlay.text,
                        color = Color(overlay.colorArgb.toInt()),
                        fontSize = overlay.fontSizeSp.sp,
                        fontWeight = FontWeight.Bold
                    )
                }
            }

            IconButton(
                onClick = onClose,
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(10.dp)
                    .size(40.dp)
                    .clip(CircleShape)
                    .background(Color.Black.copy(alpha = 0.55f))
            ) {
                Icon(Icons.Filled.Close, contentDescription = "Close editor", tint = TrisvaxColors.PrimaryText)
            }

            Row(
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(10.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(
                    enabled = state.canUndo,
                    onClick = { viewModel.undo() },
                    modifier = Modifier
                        .size(38.dp)
                        .clip(CircleShape)
                        .background(Color.Black.copy(alpha = 0.55f))
                ) {
                    Icon(
                        Icons.AutoMirrored.Filled.Undo,
                        contentDescription = "Undo",
                        tint = if (state.canUndo) TrisvaxColors.PrimaryText else TrisvaxColors.MutedText,
                        modifier = Modifier.size(18.dp)
                    )
                }
                IconButton(
                    enabled = state.canRedo,
                    onClick = { viewModel.redo() },
                    modifier = Modifier
                        .size(38.dp)
                        .clip(CircleShape)
                        .background(Color.Black.copy(alpha = 0.55f))
                ) {
                    Icon(
                        Icons.AutoMirrored.Filled.Redo,
                        contentDescription = "Redo",
                        tint = if (state.canRedo) TrisvaxColors.PrimaryText else TrisvaxColors.MutedText,
                        modifier = Modifier.size(18.dp)
                    )
                }
                Button(
                    onClick = { viewModel.startExport(onExportFinished) },
                    shape = RoundedCornerShape(10.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = TrisvaxColors.PrimaryOrange),
                    modifier = Modifier.height(38.dp)
                ) {
                    Text("Next", color = TrisvaxColors.PrimaryText, fontWeight = FontWeight.Bold, fontSize = 13.sp)
                }
            }

            state.saveBanner?.let { banner ->
                Surface(
                    color = TrisvaxColors.PrimaryCard,
                    shape = RoundedCornerShape(8.dp),
                    border = BorderStroke(1.dp, TrisvaxColors.PrimaryOrange),
                    modifier = Modifier
                        .align(Alignment.BottomCenter)
                        .padding(bottom = 10.dp)
                ) {
                    Text(
                        text = banner,
                        color = TrisvaxColors.PrimaryText,
                        fontSize = 12.sp,
                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp)
                    )
                }
            }
        }

        // Playback Transport Bar
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton(
                    onClick = { viewModel.togglePlayback() },
                    modifier = Modifier
                        .size(40.dp)
                        .clip(CircleShape)
                        .background(TrisvaxColors.PrimaryCard)
                        .border(1.dp, TrisvaxColors.Border, CircleShape)
                ) {
                    Icon(
                        imageVector = if (state.isPlaying) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                        contentDescription = "Play or pause",
                        tint = TrisvaxColors.PrimaryText
                    )
                }
                Spacer(modifier = Modifier.width(10.dp))
                Text(
                    text = "${formatShortTimecode(state.playheadMs)} / ${formatShortTimecode(state.totalDurationMs)}",
                    color = TrisvaxColors.PrimaryText,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Medium
                )
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                IconButton(
                    onClick = {
                        appendPicker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
                    },
                    modifier = Modifier
                        .size(36.dp)
                        .clip(CircleShape)
                        .background(TrisvaxColors.PrimaryCard)
                ) {
                    Icon(Icons.Filled.Add, contentDescription = "Append clips", tint = TrisvaxColors.PrimaryOrange)
                }
                IconButton(
                    onClick = { viewModel.saveProjectExplicit() },
                    modifier = Modifier
                        .size(36.dp)
                        .clip(CircleShape)
                        .background(TrisvaxColors.PrimaryCard)
                ) {
                    Icon(Icons.Filled.Save, contentDescription = "Save draft", tint = TrisvaxColors.PrimaryText, modifier = Modifier.size(18.dp))
                }
            }
        }

        // Multi-Track Timeline
        TimelineTrackView(
            timeline = state.timeline,
            playheadMs = state.playheadMs,
            selectedClipId = state.selectedClipId,
            selection = state.selection,
            multiSelectMode = state.multiSelectMode,
            onToggleMultiSelectMode = viewModel::toggleMultiSelectMode,
            onSelectAll = viewModel::selectAllTimelineItems,
            onClearSelection = viewModel::clearSelection,
            onBatchShift = { deltaMs -> viewModel.batchShiftSelection(deltaMs, 0) },
            onBatchMute = viewModel::batchMuteSelection,
            onBatchDelete = viewModel::batchDeleteSelection,
            onToggleTrackState = viewModel::toggleTrackState,
            onSeek = viewModel::seekTo,
            onSelectClip = viewModel::selectClip,
            onToggleClipMute = viewModel::toggleClipMute,
            onSelectMusic = viewModel::toggleSelectMusic,
            onSelectVoiceover = viewModel::toggleSelectVoiceover,
            onOpenMusicTool = { viewModel.selectTool(EditorTool.MUSIC) },
            onOpenTextTool = { viewModel.selectTool(EditorTool.TEXT) },
            onSelectText = viewModel::selectText,
            onOpenVoiceTool = { viewModel.selectTool(EditorTool.VOICE) }
        )

        // Expandable Tool Parameter Sheet
        EditorToolSheet(
            state = state,
            viewModel = viewModel,
            onClose = { viewModel.selectTool(EditorTool.NONE) }
        )

        // Bottom Circular Tool Strip
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(TrisvaxColors.MainBackground)
                .horizontalScroll(rememberScrollState())
                .padding(horizontal = 8.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            EditorToolButton("Music", Icons.Filled.MusicNote, state.activeTool == EditorTool.MUSIC) {
                viewModel.selectTool(EditorTool.MUSIC)
            }
            EditorToolButton("Text", Icons.Filled.TextFields, state.activeTool == EditorTool.TEXT) {
                viewModel.selectTool(EditorTool.TEXT)
            }
            EditorToolButton("Crop", Icons.Filled.Crop, state.activeTool == EditorTool.CROP) {
                viewModel.selectTool(EditorTool.CROP)
            }
            EditorToolButton("Trim", Icons.Filled.Tune, state.activeTool == EditorTool.TRIM) {
                viewModel.selectTool(EditorTool.TRIM)
            }
            EditorToolButton("Split", Icons.Filled.ContentCut, false) {
                viewModel.selectTool(EditorTool.SPLIT)
            }
            EditorToolButton("Adjust", Icons.Filled.AutoFixHigh, state.activeTool == EditorTool.ADJUST) {
                viewModel.selectTool(EditorTool.ADJUST)
            }
            EditorToolButton("Speed", Icons.Filled.FastForward, state.activeTool == EditorTool.SPEED) {
                viewModel.selectTool(EditorTool.SPEED)
            }
            EditorToolButton("Volume", Icons.Filled.VolumeUp, state.activeTool == EditorTool.VOLUME) {
                viewModel.selectTool(EditorTool.VOLUME)
            }
            EditorToolButton("Rotate", Icons.Filled.RotateRight, state.activeTool == EditorTool.ROTATE) {
                viewModel.selectTool(EditorTool.ROTATE)
            }
            EditorToolButton("Voice", Icons.Filled.Mic, state.activeTool == EditorTool.VOICE) {
                viewModel.selectTool(EditorTool.VOICE)
            }
            EditorToolButton("Transitions", Icons.Filled.Layers, state.activeTool == EditorTool.TRANSITIONS) {
                viewModel.selectTool(EditorTool.TRANSITIONS)
            }
            EditorToolButton("Clips", Icons.Filled.SwapHoriz, state.activeTool == EditorTool.CLIPS) {
                viewModel.selectTool(EditorTool.CLIPS)
            }
            EditorToolButton("Save", Icons.Filled.Save, false) {
                viewModel.saveProjectExplicit()
            }
        }
    }

    ExportProgressDialog(
        exportState = state.exportState,
        onCancel = viewModel::cancelExport,
        onDismiss = viewModel::dismissExportError
    )
}
