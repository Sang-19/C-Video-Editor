package com.trisvax.studio.platform

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.FileOutputStream

data class ResolvedMediaInfo(
    val uriString: String,
    val displayName: String,
    val isImage: Boolean,
    val durationUs: Long,
    val width: Int,
    val height: Int,
    val fileSizeBytes: Long,
    val thumbnailPath: String
)

/**
 * Resolves Android content:// or file:// media metadata and small JPEG thumbnails
 * without copying large original video files into app storage.
 */
class MediaImportResolver(private val context: Context) {

    fun resolve(projectId: String, uri: Uri, index: Int): ResolvedMediaInfo? {
        return try {
            if (uri.scheme == "content") {
                try {
                    context.contentResolver.takePersistableUriPermission(
                        uri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION
                    )
                } catch (_: SecurityException) {
                    // Photo Picker URIs remain readable during session; fallback handled gracefully
                }
            }

            val mime = context.contentResolver.getType(uri).orEmpty()
            val isImage = mime.startsWith("image/")
            val displayName = queryDisplayName(uri) ?: "Clip ${index + 1}"
            val sizeBytes = queryFileSize(uri)
            val thumbDir = File(context.filesDir, "projects/$projectId/thumbs").apply { mkdirs() }
            val thumbFile = File(thumbDir, "thumb_${System.currentTimeMillis()}_$index.jpg")

            var durationUs = 3_000_000L
            var width = 1080
            var height = 1920

            if (!isImage) {
                val retriever = MediaMetadataRetriever()
                try {
                    retriever.setDataSource(context, uri)
                    val durMs = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)
                        ?.toLongOrNull()?.coerceAtLeast(500L) ?: 3_000L
                    durationUs = durMs * 1_000L
                    width = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)
                        ?.toIntOrNull() ?: 1080
                    height = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)
                        ?.toIntOrNull() ?: 1920
                    val frame = retriever.getFrameAtTime(500_000L, MediaMetadataRetriever.OPTION_CLOSEST_SYNC)
                        ?: retriever.frameAtTime
                    if (frame != null) {
                        FileOutputStream(thumbFile).use { out ->
                            frame.compress(Bitmap.CompressFormat.JPEG, 82, out)
                        }
                        frame.recycle()
                    }
                } finally {
                    retriever.release()
                }
            } else {
                context.contentResolver.openInputStream(uri)?.use { input ->
                    FileOutputStream(thumbFile).use { out -> input.copyTo(out) }
                }
            }

            ResolvedMediaInfo(
                uriString = uri.toString(),
                displayName = displayName,
                isImage = isImage,
                durationUs = durationUs,
                width = width,
                height = height,
                fileSizeBytes = sizeBytes,
                thumbnailPath = if (thumbFile.exists()) thumbFile.absolutePath else ""
            )
        } catch (_: Exception) {
            null
        }
    }

    private fun queryDisplayName(uri: Uri): String? {
        if (uri.scheme == "file") return File(uri.path.orEmpty()).name
        return context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { c -> if (c.moveToFirst()) c.getString(0) else null }
    }

    private fun queryFileSize(uri: Uri): Long {
        if (uri.scheme == "file") return File(uri.path.orEmpty()).length()
        return context.contentResolver.query(uri, arrayOf(OpenableColumns.SIZE), null, null, null)
            ?.use { c -> if (c.moveToFirst()) c.getLong(0) else 0L } ?: 0L
    }
}
