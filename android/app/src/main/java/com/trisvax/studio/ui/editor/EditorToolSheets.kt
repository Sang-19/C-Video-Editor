package com.trisvax.studio.ui.editor

import android.Manifest
import android.content.pm.PackageManager
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDownward
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CheckboxDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.trisvax.studio.models.AspectRatioPreset
import com.trisvax.studio.theme.TrisvaxColors
import kotlin.math.roundToInt

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun EditorToolSheet(
    state: EditorUiState,
    viewModel: EditorViewModel,
    onClose: () -> Unit
) {
    if (state.activeTool == EditorTool.NONE) return
    val clip = state.selectedClip

    Surface(
        color = TrisvaxColors.PrimaryCard,
        shape = RoundedCornerShape(topStart = 18.dp, topEnd = 18.dp),
        border = BorderStroke(1.dp, TrisvaxColors.Border),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = state.activeTool.name.lowercase().replaceFirstChar { it.uppercase() },
                    color = TrisvaxColors.PrimaryText,
                    fontSize = 15.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "Done",
                    color = TrisvaxColors.PrimaryOrange,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.clickable(onClick = onClose)
                )
            }

            when (state.activeTool) {
                EditorTool.TRIM -> if (clip != null) {
                    val maxMs = (clip.sourceDurationUs / 1000L).coerceAtLeast(1000L)
                    var inMs by remember(clip.id, clip.inPointUs) {
                        mutableFloatStateOf((clip.inPointUs / 1000L).toFloat())
                    }
                    var outMs by remember(clip.id, clip.outPointUs) {
                        mutableFloatStateOf((clip.outPointUs / 1000L).toFloat())
                    }
                    Text("Start: ${(inMs / 1000f).format1()}s", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                    Slider(
                        value = inMs,
                        onValueChange = { inMs = it.coerceAtMost(outMs - 250f) },
                        onValueChangeFinished = { viewModel.trimSelectedClip(inMs.toLong(), outMs.toLong()) },
                        valueRange = 0f..maxMs.toFloat(),
                        colors = trisvaxSliderColors()
                    )
                    Text("End: ${(outMs / 1000f).format1()}s", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                    Slider(
                        value = outMs,
                        onValueChange = { outMs = it.coerceAtLeast(inMs + 250f) },
                        onValueChangeFinished = { viewModel.trimSelectedClip(inMs.toLong(), outMs.toLong()) },
                        valueRange = 0f..maxMs.toFloat(),
                        colors = trisvaxSliderColors()
                    )
                }

                EditorTool.CROP -> if (clip != null) {
                    Text("Aspect Ratio", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        AspectRatioPreset.entries.forEach { preset ->
                            val selected = clip.crop.preset == preset.name
                            ChoiceChip(
                                label = preset.label,
                                selected = selected,
                                onClick = {
                                    viewModel.setCropAndAspect(
                                        preset,
                                        clip.crop.offsetX,
                                        clip.crop.offsetY,
                                        clip.crop.zoom,
                                        clip.crop.rotationDegrees
                                    )
                                }
                            )
                        }
                    }
                    var zoom by remember(clip.id, clip.crop.zoom) { mutableFloatStateOf(clip.crop.zoom) }
                    var offX by remember(clip.id, clip.crop.offsetX) { mutableFloatStateOf(clip.crop.offsetX) }
                    var offY by remember(clip.id, clip.crop.offsetY) { mutableFloatStateOf(clip.crop.offsetY) }
                    val curPreset = AspectRatioPreset.entries.firstOrNull { it.name == clip.crop.preset }
                        ?: AspectRatioPreset.PORTRAIT_9_16
                    Text("Zoom: ${zoom.format2()}x", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                    Slider(
                        value = zoom,
                        onValueChange = { zoom = it },
                        onValueChangeFinished = {
                            viewModel.setCropAndAspect(curPreset, offX, offY, zoom, clip.crop.rotationDegrees)
                        },
                        valueRange = 1f..3f,
                        colors = trisvaxSliderColors()
                    )
                    Text("Pan X / Y", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Slider(
                            value = offX,
                            onValueChange = { offX = it },
                            onValueChangeFinished = {
                                viewModel.setCropAndAspect(curPreset, offX, offY, zoom, clip.crop.rotationDegrees)
                            },
                            valueRange = -1f..1f,
                            colors = trisvaxSliderColors(),
                            modifier = Modifier.weight(1f)
                        )
                        Slider(
                            value = offY,
                            onValueChange = { offY = it },
                            onValueChangeFinished = {
                                viewModel.setCropAndAspect(curPreset, offX, offY, zoom, clip.crop.rotationDegrees)
                            },
                            valueRange = -1f..1f,
                            colors = trisvaxSliderColors(),
                            modifier = Modifier.weight(1f)
                        )
                    }
                }

                EditorTool.ROTATE -> if (clip != null) {
                    Text(
                        text = "Current orientation: ${clip.crop.rotationDegrees}\u00B0",
                        color = TrisvaxColors.SecondaryText,
                        fontSize = 13.sp
                    )
                    Button(
                        onClick = { viewModel.rotateSelectedClip90() },
                        colors = ButtonDefaults.buttonColors(containerColor = TrisvaxColors.PrimaryOrange),
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Text("Rotate +90\u00B0", color = TrisvaxColors.PrimaryText)
                    }
                }

                EditorTool.ADJUST -> if (clip != null) {
                    var bri by remember(clip.id, clip.filter.brightness) { mutableFloatStateOf(clip.filter.brightness) }
                    var con by remember(clip.id, clip.filter.contrast) { mutableFloatStateOf(clip.filter.contrast) }
                    var sat by remember(clip.id, clip.filter.saturation) { mutableFloatStateOf(clip.filter.saturation) }
                    var tmp by remember(clip.id, clip.filter.temperature) { mutableFloatStateOf(clip.filter.temperature) }
                    var exp by remember(clip.id, clip.filter.exposure) { mutableFloatStateOf(clip.filter.exposure) }
                    var shp by remember(clip.id, clip.filter.sharpness) { mutableFloatStateOf(clip.filter.sharpness) }

                    val commitFilters = {
                        viewModel.setClipFilters(bri, con, sat, tmp, exp, shp)
                    }

                    FilterSlider("Brightness", bri, -1f..1f, { bri = it }, commitFilters)
                    FilterSlider("Contrast", con, 0f..2f, { con = it }, commitFilters)
                    FilterSlider("Saturation", sat, 0f..2f, { sat = it }, commitFilters)
                    FilterSlider("Temperature", tmp, -1f..1f, { tmp = it }, commitFilters)
                    FilterSlider("Exposure", exp, -1f..1f, { exp = it }, commitFilters)
                    FilterSlider("Sharpness", shp, 0f..1f, { shp = it }, commitFilters)

                    OutlinedButton(
                        onClick = {
                            bri = 0f; con = 1f; sat = 1f; tmp = 0f; exp = 0f; shp = 0f
                            viewModel.setClipFilters(0f, 1f, 1f, 0f, 0f, 0f)
                        },
                        border = BorderStroke(1.dp, TrisvaxColors.Border)
                    ) {
                        Text("Reset Filters", color = TrisvaxColors.PrimaryText)
                    }
                }

                EditorTool.SPEED -> if (clip != null) {
                    var pitchPreserved by remember(clip.id, clip.audioSpeedMode) {
                        mutableStateOf(clip.audioSpeedMode == 1)
                    }
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf(0.5, 1.0, 1.5, 2.0, 3.0).forEach { spd ->
                            ChoiceChip(
                                label = "${spd}x",
                                selected = kotlin.math.abs(clip.playbackSpeed - spd) < 0.05,
                                onClick = { viewModel.setClipSpeed(spd, pitchPreserved) }
                            )
                        }
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(
                            checked = pitchPreserved,
                            onCheckedChange = {
                                pitchPreserved = it
                                viewModel.setClipSpeed(clip.playbackSpeed, it)
                            },
                            colors = CheckboxDefaults.colors(checkedColor = TrisvaxColors.PrimaryOrange)
                        )
                        Text("Preserve audio pitch", color = TrisvaxColors.PrimaryText, fontSize = 13.sp)
                    }
                }

                EditorTool.VOLUME -> if (clip != null) {
                    var vol by remember(clip.id, clip.volume) { mutableFloatStateOf(clip.volume) }
                    Text("Clip Volume: ${(vol * 100).roundToInt()}%", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                    Slider(
                        value = vol,
                        onValueChange = { vol = it },
                        onValueChangeFinished = { viewModel.setClipVolume(vol, clip.isMuted) },
                        valueRange = 0f..2f,
                        colors = trisvaxSliderColors()
                    )
                    OutlinedButton(
                        onClick = { viewModel.setClipVolume(vol, !clip.isMuted) },
                        border = BorderStroke(1.dp, TrisvaxColors.Border)
                    ) {
                        Text(if (clip.isMuted) "Unmute Clip" else "Mute Clip", color = TrisvaxColors.PrimaryText)
                    }
                }

                EditorTool.TRANSITIONS -> if (clip != null) {
                    val types = listOf(0 to "Cut", 1 to "Fade", 2 to "Cross Dissolve")
                    var durMs by remember(clip.id, clip.transitionIn.durationUs) {
                        mutableFloatStateOf((clip.transitionIn.durationUs / 1000L).toFloat().coerceAtLeast(400f))
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        types.forEach { (typeId, label) ->
                            ChoiceChip(
                                label = label,
                                selected = clip.transitionIn.type == typeId,
                                onClick = {
                                    viewModel.setClipTransition(typeId, if (typeId == 0) 0L else durMs.toLong())
                                }
                            )
                        }
                    }
                    if (clip.transitionIn.type != 0) {
                        Text("Duration: ${(durMs / 1000f).format1()}s", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                        Slider(
                            value = durMs,
                            onValueChange = { durMs = it },
                            onValueChangeFinished = {
                                viewModel.setClipTransition(clip.transitionIn.type, durMs.toLong())
                            },
                            valueRange = 200f..1500f,
                            colors = trisvaxSliderColors()
                        )
                    }
                }

                EditorTool.MUSIC -> {
                    val audioPicker = rememberLauncherForActivityResult(
                        ActivityResultContracts.OpenDocument()
                    ) { uri: Uri? ->
                        if (uri != null) viewModel.applyCustomMusicUri(uri)
                    }
                    Text("Bundled Studio Tracks", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        state.bundledMusic.forEach { track ->
                            ChoiceChip(
                                label = track.title,
                                selected = state.timeline.musicTrack?.title == track.title,
                                onClick = { viewModel.applyBundledMusic(track) }
                            )
                        }
                    }
                    OutlinedButton(
                        onClick = { audioPicker.launch(arrayOf("audio/*")) },
                        border = BorderStroke(1.dp, TrisvaxColors.Border)
                    ) {
                        Text("Pick Audio File from Device", color = TrisvaxColors.PrimaryOrange)
                    }

                    state.timeline.musicTrack?.let { music ->
                        val maxMs = (music.sourceDurationUs / 1000L).coerceAtLeast(1000L)
                        var mVol by remember(music.id, music.volume) { mutableFloatStateOf(music.volume) }
                        var trimStart by remember(music.id, music.trimStartUs) { mutableFloatStateOf((music.trimStartUs / 1000L).toFloat()) }
                        var trimEnd by remember(music.id, music.trimEndUs) { mutableFloatStateOf((music.trimEndUs / 1000L).toFloat()) }
                        var fadeIn by remember(music.id, music.fadeInUs) { mutableFloatStateOf((music.fadeInUs / 1000L).toFloat()) }
                        var fadeOut by remember(music.id, music.fadeOutUs) { mutableFloatStateOf((music.fadeOutUs / 1000L).toFloat()) }

                        val applyMusic = {
                            viewModel.updateMusicSettings(
                                trimStart.toLong(),
                                trimEnd.toLong(),
                                mVol,
                                music.isMuted,
                                music.loopToFit,
                                fadeIn.toLong(),
                                fadeOut.toLong()
                            )
                        }

                        Text("Music Volume: ${(mVol * 100).roundToInt()}%", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                        Slider(value = mVol, onValueChange = { mVol = it }, onValueChangeFinished = applyMusic, valueRange = 0f..1.5f, colors = trisvaxSliderColors())

                        Text("Trim Audio: ${(trimStart / 1000f).format1()}s - ${(trimEnd / 1000f).format1()}s", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                        Slider(value = trimStart, onValueChange = { trimStart = it.coerceAtMost(trimEnd - 300f) }, onValueChangeFinished = applyMusic, valueRange = 0f..maxMs.toFloat(), colors = trisvaxSliderColors())
                        Slider(value = trimEnd, onValueChange = { trimEnd = it.coerceAtLeast(trimStart + 300f) }, onValueChangeFinished = applyMusic, valueRange = 0f..maxMs.toFloat(), colors = trisvaxSliderColors())

                        Text("Fade In / Out: ${(fadeIn / 1000f).format1()}s / ${(fadeOut / 1000f).format1()}s", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Slider(value = fadeIn, onValueChange = { fadeIn = it }, onValueChangeFinished = applyMusic, valueRange = 0f..3000f, colors = trisvaxSliderColors(), modifier = Modifier.weight(1f))
                            Slider(value = fadeOut, onValueChange = { fadeOut = it }, onValueChangeFinished = applyMusic, valueRange = 0f..3000f, colors = trisvaxSliderColors(), modifier = Modifier.weight(1f))
                        }

                        Button(
                            onClick = { viewModel.removeMusicTrack() },
                            colors = ButtonDefaults.buttonColors(containerColor = TrisvaxColors.ErrorDanger)
                        ) {
                            Text("Remove Music", color = TrisvaxColors.PrimaryText)
                        }
                    }
                }

                EditorTool.TEXT -> {
                    val editing = state.selectedText
                    var textVal by remember(editing?.id) { mutableStateOf(editing?.text ?: "TRISVAX") }
                    var fontSize by remember(editing?.id) { mutableFloatStateOf(editing?.fontSizeSp ?: 28f) }
                    var colorArgb by remember(editing?.id) { mutableStateOf(editing?.colorArgb ?: 0xFFFFFFFFL) }
                    var align by remember(editing?.id) { mutableIntStateOf(editing?.alignment ?: 1) }
                    var anim by remember(editing?.id) { mutableIntStateOf(editing?.animation ?: 0) }
                    var posY by remember(editing?.id) { mutableFloatStateOf(editing?.normalizedY ?: 0.5f) }
                    var startMs by remember(editing?.id) { mutableFloatStateOf(((editing?.startUs ?: (state.playheadMs * 1000L)) / 1000L).toFloat()) }
                    var endMs by remember(editing?.id) {
                        val fallbackEnd = ((state.playheadMs + 3500L).coerceAtMost(state.totalDurationMs.coerceAtLeast(3500L))).toFloat()
                        mutableFloatStateOf(((editing?.endUs?.div(1000L))?.toFloat()) ?: fallbackEnd)
                    }

                    OutlinedTextField(
                        value = textVal,
                        onValueChange = { textVal = it },
                        label = { Text("Overlay Text", color = TrisvaxColors.SecondaryText) },
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedBorderColor = TrisvaxColors.PrimaryOrange,
                            unfocusedBorderColor = TrisvaxColors.Border,
                            focusedTextColor = TrisvaxColors.PrimaryText,
                            unfocusedTextColor = TrisvaxColors.PrimaryText
                        ),
                        modifier = Modifier.fillMaxWidth()
                    )

                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf(0xFFFFFFFFL, 0xFFFF5500L, 0xFFFF8232L, 0xFF82B8FFL, 0xFFFFD166L).forEach { c ->
                            Box(
                                modifier = Modifier
                                    .size(28.dp)
                                    .clip(CircleShape)
                                    .background(Color(c.toInt()))
                                    .border(
                                        width = if (colorArgb == c) 2.dp else 1.dp,
                                        color = if (colorArgb == c) TrisvaxColors.PrimaryOrange else TrisvaxColors.Border,
                                        shape = CircleShape
                                    )
                                    .clickable { colorArgb = c }
                            )
                        }
                    }

                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf(0 to "Left", 1 to "Center", 2 to "Right").forEach { (id, lbl) ->
                            ChoiceChip(label = lbl, selected = align == id, onClick = { align = id })
                        }
                    }

                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf(0 to "None", 1 to "Fade", 2 to "Slide Up").forEach { (id, lbl) ->
                            ChoiceChip(label = lbl, selected = anim == id, onClick = { anim = id })
                        }
                    }

                    Text("Size: ${fontSize.roundToInt()}sp | Vertical Y: ${(posY * 100).roundToInt()}%", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                    Slider(value = fontSize, onValueChange = { fontSize = it }, valueRange = 16f..64f, colors = trisvaxSliderColors())
                    Slider(value = posY, onValueChange = { posY = it }, valueRange = 0.1f..0.9f, colors = trisvaxSliderColors())

                    val maxTimeline = state.totalDurationMs.coerceAtLeast(1000L).toFloat()
                    Text("Time: ${(startMs / 1000f).format1()}s - ${(endMs / 1000f).format1()}s", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                    Slider(value = startMs, onValueChange = { startMs = it.coerceAtMost(endMs - 300f) }, valueRange = 0f..maxTimeline, colors = trisvaxSliderColors())
                    Slider(value = endMs, onValueChange = { endMs = it.coerceAtLeast(startMs + 300f) }, valueRange = 0f..maxTimeline, colors = trisvaxSliderColors())

                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(
                            onClick = {
                                viewModel.upsertTextOverlay(
                                    id = editing?.id.orEmpty(),
                                    text = textVal.ifBlank { "TRISVAX" },
                                    fontFamily = "Sans",
                                    fontSizeSp = fontSize,
                                    colorArgb = colorArgb,
                                    align = align,
                                    normX = when (align) {
                                        0 -> 0.15f
                                        2 -> 0.85f
                                        else -> 0.5f
                                    },
                                    normY = posY,
                                    startMs = startMs.toLong(),
                                    endMs = endMs.toLong(),
                                    animation = anim
                                )
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = TrisvaxColors.PrimaryOrange),
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(if (editing != null) "Update Text" else "Add Text", color = TrisvaxColors.PrimaryText)
                        }
                        if (editing != null) {
                            Button(
                                onClick = { viewModel.removeTextOverlay(editing.id) },
                                colors = ButtonDefaults.buttonColors(containerColor = TrisvaxColors.ErrorDanger)
                            ) {
                                Text("Delete", color = TrisvaxColors.PrimaryText)
                            }
                        }
                    }
                }

                EditorTool.VOICE -> {
                    val context = LocalContext.current
                    var hasMic by remember {
                        mutableStateOf(
                            ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
                        )
                    }
                    val micPermLauncher = rememberLauncherForActivityResult(
                        ActivityResultContracts.RequestPermission()
                    ) { granted ->
                        hasMic = granted
                        if (granted) viewModel.startVoiceoverRecording()
                    }
                    Button(
                        onClick = {
                            if (state.isRecordingVoice) {
                                viewModel.stopVoiceoverRecording()
                            } else if (hasMic) {
                                viewModel.startVoiceoverRecording()
                            } else {
                                micPermLauncher.launch(Manifest.permission.RECORD_AUDIO)
                            }
                        },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (state.isRecordingVoice) TrisvaxColors.ErrorDanger else TrisvaxColors.PrimaryOrange
                        ),
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(
                            imageVector = if (state.isRecordingVoice) Icons.Filled.Stop else Icons.Filled.Mic,
                            contentDescription = null,
                            tint = TrisvaxColors.PrimaryText
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = if (state.isRecordingVoice) {
                                "Stop Recording (${state.recordingElapsedSec}s)"
                            } else {
                                "Record Voiceover at Playhead"
                            },
                            color = TrisvaxColors.PrimaryText
                        )
                    }
                    state.timeline.voiceovers.forEach { vo ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(10.dp))
                                .background(TrisvaxColors.SecondaryCard)
                                .padding(horizontal = 12.dp, vertical = 8.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = "${vo.title} (${vo.sourceDurationUs / 1_000_000L}s @ ${vo.timelineStartUs / 1_000_000L}s)",
                                color = TrisvaxColors.PrimaryText,
                                fontSize = 12.sp
                            )
                            IconButton(onClick = { viewModel.removeVoiceover(vo.id) }, modifier = Modifier.size(28.dp)) {
                                Icon(Icons.Filled.Delete, contentDescription = "Delete voiceover", tint = TrisvaxColors.ErrorDanger, modifier = Modifier.size(16.dp))
                            }
                        }
                    }
                }

                EditorTool.CLIPS -> {
                    state.timeline.clips.forEachIndexed { idx, item ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(10.dp))
                                .background(TrisvaxColors.SecondaryCard)
                                .padding(horizontal = 12.dp, vertical = 8.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = "${idx + 1}. ${item.displayName} (${(item.effectiveDurationMs / 1000f).format1()}s)",
                                color = TrisvaxColors.PrimaryText,
                                fontSize = 12.sp,
                                modifier = Modifier.weight(1f)
                            )
                            Row {
                                IconButton(
                                    enabled = idx > 0,
                                    onClick = { viewModel.moveClip(idx, idx - 1) },
                                    modifier = Modifier.size(30.dp)
                                ) {
                                    Icon(Icons.Filled.ArrowUpward, contentDescription = "Move up", tint = TrisvaxColors.PrimaryText, modifier = Modifier.size(16.dp))
                                }
                                IconButton(
                                    enabled = idx < state.timeline.clips.lastIndex,
                                    onClick = { viewModel.moveClip(idx, idx + 1) },
                                    modifier = Modifier.size(30.dp)
                                ) {
                                    Icon(Icons.Filled.ArrowDownward, contentDescription = "Move down", tint = TrisvaxColors.PrimaryText, modifier = Modifier.size(16.dp))
                                }
                                IconButton(
                                    enabled = state.timeline.clips.size > 1,
                                    onClick = { viewModel.deleteClip(item.id) },
                                    modifier = Modifier.size(30.dp)
                                ) {
                                    Icon(Icons.Filled.Delete, contentDescription = "Delete clip", tint = TrisvaxColors.ErrorDanger, modifier = Modifier.size(16.dp))
                                }
                            }
                        }
                    }
                }

                else -> Unit
            }
        }
    }
}

@Composable
private fun FilterSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    onChange: (Float) -> Unit,
    onFinish: () -> Unit
) {
    Column {
        Text("$label: ${value.format2()}", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
        Slider(
            value = value,
            onValueChange = onChange,
            onValueChangeFinished = onFinish,
            valueRange = range,
            colors = trisvaxSliderColors()
        )
    }
}

@Composable
private fun ChoiceChip(label: String, selected: Boolean, onClick: () -> Unit) {
    Surface(
        color = if (selected) TrisvaxColors.PrimaryOrange else TrisvaxColors.SecondaryCard,
        shape = RoundedCornerShape(10.dp),
        border = BorderStroke(1.dp, if (selected) TrisvaxColors.SecondaryOrange else TrisvaxColors.Border),
        modifier = Modifier.clickable(onClick = onClick)
    ) {
        Text(
            text = label,
            color = TrisvaxColors.PrimaryText,
            fontSize = 12.sp,
            fontWeight = FontWeight.Medium,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 7.dp)
        )
    }
}

@Composable
private fun trisvaxSliderColors() = SliderDefaults.colors(
    thumbColor = TrisvaxColors.PrimaryOrange,
    activeTrackColor = TrisvaxColors.PrimaryOrange,
    inactiveTrackColor = TrisvaxColors.Border
)

private fun Float.format1(): String = String.format("%.1f", this)
private fun Float.format2(): String = String.format("%.2f", this)
