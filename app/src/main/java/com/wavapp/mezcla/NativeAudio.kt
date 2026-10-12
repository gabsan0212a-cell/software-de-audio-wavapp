package com.wavapp.mezcla

/** Puente hacia el motor de audio en C++ (Oboe). */
object NativeAudio {
    init {
        System.loadLibrary("wavapp")
    }

    external fun start(): Boolean
    external fun stop()
    external fun deviceRate(): Int

    /** Lee el WAV desde el descriptor (el motor lo cierra). Devuelve "" o un error. */
    external fun loadWav(fd: Int): String
    external fun play()
    external fun pause()
    external fun stopPlayback()
    external fun isPlaying(): Boolean
    external fun positionSeconds(): Double

    /** [frecuencia, canales, bits, esFloat, frames] o null. */
    external fun clipInfo(): LongArray?
}
