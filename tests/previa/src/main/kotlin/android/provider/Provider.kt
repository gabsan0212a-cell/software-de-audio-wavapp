package android.provider

import android.content.ContentResolver
import android.net.Uri

object DocumentsContract {
    fun deleteDocument(resolver: ContentResolver, uri: Uri): Boolean = false
}

object OpenableColumns {
    const val DISPLAY_NAME = "_display_name"
}
