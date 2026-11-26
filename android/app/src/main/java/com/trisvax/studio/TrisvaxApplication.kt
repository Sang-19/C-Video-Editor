package com.trisvax.studio

import android.app.Application
import com.trisvax.studio.nativebridge.VideoEditorBridge
import com.trisvax.studio.platform.MediaImportResolver
import com.trisvax.studio.platform.MusicAssetCatalog
import java.io.File

class TrisvaxApplication : Application() {
    val workspaceDir: File by lazy {
        File(filesDir, "projects").apply { mkdirs() }
    }

    val nativeBridge: VideoEditorBridge by lazy {
        VideoEditorBridge(workspaceDir.absolutePath)
    }

    val mediaResolver: MediaImportResolver by lazy {
        MediaImportResolver(this)
    }

    val musicAssetCatalog: MusicAssetCatalog by lazy {
        MusicAssetCatalog(this)
    }
}
