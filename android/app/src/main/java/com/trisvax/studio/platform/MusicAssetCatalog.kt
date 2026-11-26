package com.trisvax.studio.platform

import android.content.Context
import android.media.MediaMetadataRetriever
import android.net.Uri
import com.trisvax.studio.models.BundledMusicAsset
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

class MusicAssetCatalog(private val context: Context) {
    suspend fun listBundledTracks(): List<BundledMusicAsset> = withContext(Dispatchers.IO) {
        val files = context.assets.list("music")?.filter {
            it.endsWith(".mp3", ignoreCase = true) ||
                it.endsWith(".m4a", ignoreCase = true) ||
                it.endsWith(".wav", ignoreCase = true) ||
                it.endsWith(".ogg", ignoreCase = true)
        }.orEmpty()

        files.map { fileName ->
            val copied = materializeAsset(fileName)
            val durationUs = extractDurationMs(copied) * 1_000L
            BundledMusicAsset(
                fileName = fileName,
                title = fileName.substringBeforeLast(".")
                    .replace("_", " ")
                    .split(" ")
                    .joinToString(" ") { part -> part.replaceFirstChar { it.uppercase() } },
                assetPath = Uri.fromFile(copied).toString(),
                durationUs = durationUs
            )
        }
    }

    private fun materializeAsset(fileName: String): File {
        val dir = File(context.filesDir, "bundled_music").apply { mkdirs() }
        val target = File(dir, fileName)
        if (!target.exists() || target.length() == 0L) {
            context.assets.open("music/$fileName").use { input ->
                target.outputStream().use { out -> input.copyTo(out) }
            }
        }
        return target
    }

    private fun extractDurationMs(file: File): Long {
        val retriever = MediaMetadataRetriever()
        return try {
            retriever.setDataSource(file.absolutePath)
            retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)
                ?.toLongOrNull() ?: 15_000L
        } catch (_: Exception) {
            15_000L
        } finally {
            retriever.release()
        }
    }
}
