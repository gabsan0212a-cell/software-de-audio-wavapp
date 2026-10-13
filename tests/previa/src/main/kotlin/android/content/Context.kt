package android.content

import android.database.Cursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import java.io.File

/** Solo lo que usa Mesa.kt (vista previa de escritorio). */
open class Context(private val dir: File) {
    open val applicationContext: Context get() = this
    open val filesDir: File get() = File(dir, "files").apply { mkdirs() }
    open val cacheDir: File get() = File(dir, "cache").apply { mkdirs() }
    open val contentResolver: ContentResolver = ContentResolver()
}

class ContentResolver {
    fun openFileDescriptor(uri: Uri, mode: String): ParcelFileDescriptor? = null
    fun query(uri: Uri, projection: Array<String>?, selection: String?, selectionArgs: Array<String>?, sortOrder: String?): Cursor? = null
}
