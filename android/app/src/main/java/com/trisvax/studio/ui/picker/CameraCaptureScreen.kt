package com.trisvax.studio.ui.picker

import android.Manifest
import android.content.pm.PackageManager
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.video.FileOutputOptions
import androidx.camera.video.Quality
import androidx.camera.video.QualitySelector
import androidx.camera.video.Recorder
import androidx.camera.video.Recording
import androidx.camera.video.VideoCapture
import androidx.camera.video.VideoRecordEvent
import androidx.camera.view.PreviewView
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Cameraswitch
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import com.trisvax.studio.TrisvaxApplication
import com.trisvax.studio.theme.TrisvaxColors
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

@Composable
fun CameraCaptureScreen(
    onBack: () -> Unit,
    onVideoRecorded: (String) -> Unit
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val app = context.applicationContext as TrisvaxApplication
    val scope = rememberCoroutineScope()

    var hasPermissions by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED &&
                ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
        )
    }
    var useFrontCamera by remember { mutableStateOf(false) }
    var isRecording by remember { mutableStateOf(false) }
    var elapsedSec by remember { mutableIntStateOf(0) }
    var activeRecording by remember { mutableStateOf<Recording?>(null) }
    var videoCapture by remember { mutableStateOf<VideoCapture<Recorder>?>(null) }
    val previewView = remember { PreviewView(context) }

    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { grants ->
        hasPermissions = grants[Manifest.permission.CAMERA] == true &&
            grants[Manifest.permission.RECORD_AUDIO] == true
    }

    LaunchedEffect(Unit) {
        if (!hasPermissions) {
            permissionLauncher.launch(arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO))
        }
    }

    LaunchedEffect(isRecording) {
        if (!isRecording) {
            elapsedSec = 0
            return@LaunchedEffect
        }
        while (isRecording) {
            delay(1000L)
            elapsedSec += 1
        }
    }

    DisposableEffect(hasPermissions, useFrontCamera) {
        if (!hasPermissions) return@DisposableEffect onDispose {}
        val providerFuture = ProcessCameraProvider.getInstance(context)
        val executor = ContextCompat.getMainExecutor(context)
        var boundProvider: ProcessCameraProvider? = null
        providerFuture.addListener({
            try {
                val provider = providerFuture.get()
                boundProvider = provider
                val preview = Preview.Builder().build().also {
                    it.surfaceProvider = previewView.surfaceProvider
                }
                val recorder = Recorder.Builder()
                    .setQualitySelector(QualitySelector.from(Quality.FHD))
                    .build()
                val capture = VideoCapture.withOutput(recorder)
                val selector = if (useFrontCamera) {
                    CameraSelector.DEFAULT_FRONT_CAMERA
                } else {
                    CameraSelector.DEFAULT_BACK_CAMERA
                }
                provider.unbindAll()
                provider.bindToLifecycle(lifecycleOwner, selector, preview, capture)
                videoCapture = capture
            } catch (_: Exception) {
                Toast.makeText(context, "Camera initialization failed", Toast.LENGTH_SHORT).show()
            }
        }, executor)

        onDispose {
            activeRecording?.stop()
            boundProvider?.unbindAll()
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(TrisvaxColors.MainBackground)
            .safeDrawingPadding()
    ) {
        if (!hasPermissions) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(24.dp),
                contentAlignment = Alignment.Center
            ) {
                Surface(
                    color = TrisvaxColors.PrimaryCard,
                    shape = RoundedCornerShape(16.dp),
                    border = BorderStroke(1.dp, TrisvaxColors.Border)
                ) {
                    Column(
                        modifier = Modifier.padding(20.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Text("Camera & Microphone Permission", color = TrisvaxColors.PrimaryText, fontWeight = FontWeight.Bold)
                        Text(
                            text = "Allow Camera and Audio permissions to record Full HD video directly into a new project.",
                            color = TrisvaxColors.SecondaryText,
                            fontSize = 13.sp
                        )
                        Button(
                            onClick = {
                                permissionLauncher.launch(arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO))
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = TrisvaxColors.PrimaryOrange),
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("Grant Permissions", color = TrisvaxColors.PrimaryText)
                        }
                        Button(
                            onClick = onBack,
                            colors = ButtonDefaults.buttonColors(containerColor = TrisvaxColors.SecondaryCard),
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("Back", color = TrisvaxColors.SecondaryText)
                        }
                    }
                }
            }
        } else {
            AndroidView(
                factory = { previewView },
                modifier = Modifier.fillMaxSize()
            )

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .align(Alignment.TopCenter)
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(
                    onClick = {
                        activeRecording?.stop()
                        onBack()
                    },
                    modifier = Modifier
                        .size(44.dp)
                        .clip(CircleShape)
                        .background(Color.Black.copy(alpha = 0.6f))
                ) {
                    Icon(Icons.Filled.Close, contentDescription = "Close camera", tint = TrisvaxColors.PrimaryText)
                }

                if (isRecording) {
                    Surface(
                        color = TrisvaxColors.ErrorDanger,
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Text(
                            text = "REC %d:%02d".format(elapsedSec / 60, elapsedSec % 60),
                            color = TrisvaxColors.PrimaryText,
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Bold,
                            modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp)
                        )
                    }
                }

                IconButton(
                    enabled = !isRecording,
                    onClick = { useFrontCamera = !useFrontCamera },
                    modifier = Modifier
                        .size(44.dp)
                        .clip(CircleShape)
                        .background(Color.Black.copy(alpha = 0.6f))
                ) {
                    Icon(Icons.Filled.Cameraswitch, contentDescription = "Switch camera", tint = TrisvaxColors.PrimaryText)
                }
            }

            Box(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .padding(bottom = 36.dp),
                contentAlignment = Alignment.Center
            ) {
                Box(
                    modifier = Modifier
                        .size(76.dp)
                        .clip(CircleShape)
                        .border(4.dp, TrisvaxColors.PrimaryText, CircleShape)
                        .padding(6.dp)
                        .clickable {
                            val capture = videoCapture ?: return@clickable
                            if (isRecording) {
                                activeRecording?.stop()
                                activeRecording = null
                                isRecording = false
                            } else {
                                val recDir = File(context.filesDir, "camera_captures").apply { mkdirs() }
                                val outFile = File(recDir, "TRISVAX_CAM_${System.currentTimeMillis()}.mp4")
                                val outputOptions = FileOutputOptions.Builder(outFile).build()
                                val pending = capture.output
                                    .prepareRecording(context, outputOptions)
                                    .apply {
                                        if (ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
                                            withAudioEnabled()
                                        }
                                    }
                                    .start(ContextCompat.getMainExecutor(context)) { event ->
                                        if (event is VideoRecordEvent.Finalize) {
                                            isRecording = false
                                            if (!event.hasError() && outFile.exists() && outFile.length() > 0L) {
                                                scope.launch {
                                                    val pid = withContext(Dispatchers.IO) {
                                                        val createdId = app.nativeBridge.createProject("Camera Clip")
                                                        if (createdId.isBlank()) return@withContext ""
                                                        val resolved = app.mediaResolver.resolve(createdId, Uri.fromFile(outFile), 0)
                                                        if (resolved != null) {
                                                            app.nativeBridge.importMedia(
                                                                resolved.uriString,
                                                                resolved.displayName,
                                                                resolved.isImage,
                                                                resolved.durationUs,
                                                                resolved.width,
                                                                resolved.height,
                                                                resolved.fileSizeBytes,
                                                                resolved.thumbnailPath
                                                            )
                                                            app.nativeBridge.saveProject()
                                                            createdId
                                                        } else {
                                                            app.nativeBridge.deleteProject(createdId)
                                                            ""
                                                        }
                                                    }
                                                    if (pid.isNotBlank()) {
                                                        onVideoRecorded(pid)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                activeRecording = pending
                                isRecording = true
                            }
                        },
                    contentAlignment = Alignment.Center
                ) {
                    Box(
                        modifier = Modifier
                            .size(if (isRecording) 30.dp else 56.dp)
                            .clip(RoundedCornerShape(if (isRecording) 8.dp else 28.dp))
                            .background(TrisvaxColors.PrimaryOrange)
                    )
                }
            }
        }
    }
}
