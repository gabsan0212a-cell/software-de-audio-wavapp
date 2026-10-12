package com.wavapp.mezcla

/** Puente hacia el motor de audio en C++ (Oboe). Todo el audio vive del lado nativo. */
object NativeAudio {
    init {
        System.loadLibrary("wavapp")
    }

    // Motor
    external fun start(): Boolean
    external fun stop()
    external fun sampleRate(): Int
    external fun deviceRate(): Int
    external fun maxTracks(): Int
    /** Último error en el hilo que llama. */
    external fun lastError(): String

    // Pistas. Los que agregan devuelven [espacio, canales, cuadros, ...] o null (ver lastError()).
    /**
     * Convierte el WAV (el motor cierra el descriptor) al formato interno en [dstPath].
     * Devuelve [espacio, canales, cuadros, frecuencia original, canales originales, bits,
     * esFloat, eraEstéreoIgual].
     */
    external fun importWav(fd: Int, dstPath: String): LongArray?
    external fun addTrackFile(path: String): LongArray?
    external fun addDemoTrack(index: Int, path: String): LongArray?
    external fun removeTrack(slot: Int)
    external fun importProgress(): Float
    /** Forma de onda: [columna][canal][min, max]. */
    external fun overview(slot: Int, columns: Int): FloatArray?

    external fun setTrackGain(slot: Int, gain: Float)
    external fun setTrackPan(slot: Int, pan: Float)
    external fun setTrackMute(slot: Int, on: Boolean)
    external fun setTrackSolo(slot: Int, on: Boolean)
    external fun setTrackInvert(slot: Int, on: Boolean)

    // Master
    external fun setMasterGain(gain: Float)
    external fun setLimiter(on: Boolean)

    // Transporte
    external fun play()
    external fun pause()
    external fun stopTransport()
    external fun seek(seconds: Double)
    external fun isPlaying(): Boolean
    external fun positionSeconds(): Double
    external fun durationSeconds(): Double

    /** [pico L, pico R, reducción dB, luego L/R por espacio]; los picos se reinician al leer. */
    external fun meterCount(): Int
    external fun readMeters(out: FloatArray)
}
