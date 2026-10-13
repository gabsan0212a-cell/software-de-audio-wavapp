package android.os

import java.io.Closeable

class ParcelFileDescriptor : Closeable {
    fun detachFd(): Int = -1
    override fun close() {}
}
