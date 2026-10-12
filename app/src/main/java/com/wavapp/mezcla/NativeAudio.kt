package com.wavapp.mezcla

/** Puente hacia el motor de audio en C++ (Oboe). */
object NativeAudio {
    init {
        System.loadLibrary("wavapp")
    }

    external fun start(): Boolean
    external fun stop()
    external fun setToneOn(on: Boolean)
    external fun sampleRate(): Int
}
