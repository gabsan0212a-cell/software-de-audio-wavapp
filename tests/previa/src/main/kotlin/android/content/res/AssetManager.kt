package android.content.res

import android.os.ParcelFileDescriptor
import java.io.Closeable
import java.io.FileNotFoundException

/** Solo lo que usa Mesa.kt: en la vista previa no hay modelo dentro de "assets". */
class AssetManager {
    fun openFd(name: String): AssetFileDescriptor = throw FileNotFoundException(name)
}

class AssetFileDescriptor : Closeable {
    val parcelFileDescriptor = ParcelFileDescriptor()
    val startOffset = 0L
    val length = 0L
    override fun close() {}
}
