package android.os

import java.io.Closeable

class ParcelFileDescriptor : Closeable {
    val fd: Int = -1
    fun detachFd(): Int = -1
    override fun close() {}
}
