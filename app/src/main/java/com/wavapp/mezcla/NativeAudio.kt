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
    /** Salida real: [frecuencia, ráfaga, búfer en cuadros, cortes, exclusivo 1/0]. */
    external fun outputInfo(): IntArray
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

    // EQ por pista (hasta 12 bandas). Tipos: 0 campana, 1 shelf grave, 2 shelf agudo,
    // 3 pasa-altos, 4 pasa-bajos, 5 notch.
    external fun setEqBand(slot: Int, band: Int, present: Boolean, on: Boolean, type: Int, freq: Float, gain: Float, q: Float)
    external fun setEqBypass(slot: Int, on: Boolean)
    external fun setEqAutoGain(slot: Int, on: Boolean)
    /** Compensación de la auto-ganancia en dB (≤ 0). */
    external fun eqTrimDb(slot: Int): Float
    /** Respuesta en dB para cada frecuencia. band = -1: todo el EQ; band ≥ 0: solo esa banda. */
    external fun eqResponse(slot: Int, band: Int, freqs: FloatArray, out: FloatArray)

    // Exportar la mezcla completa a WAV. Cierra el descriptor. Devuelve [cuadros, bytes] o null.
    external fun exportMix(fd: Int, bits: Int, isFloat: Boolean, rate: Int): LongArray?
    external fun exportProgress(): Float
    external fun cancelExport()
    external fun exportBytes(bits: Int, isFloat: Boolean, rate: Int): Long

    // Análisis. Fuente: -1 = mezcla, 0..11 = pista.
    external fun setNotch(hz: Int)
    external fun setAnalysisSource(slot: Int)
    external fun analysisReset()
    /**
     * Llena espectro[8192] (dB), fanE[1024] (dB), fanP[1024] (-1..1), tiempo[4096] (L y luego R)
     * y stats[8] = picoL, picoR, rmsL, rmsR, correlación, zumbido Hz, armónicos, dB sobre el piso.
     * Devuelve true si llegó audio nuevo.
     */
    external fun analyze(spec: FloatArray, fanE: FloatArray, fanP: FloatArray, time: FloatArray, stats: FloatArray): Boolean
    /** Espectrograma 0..255, [col * rows + fila], fila 0 = 20 Hz, arriba 20 kHz. */
    external fun spectrogram(slot: Int, cols: Int, rows: Int): ByteArray?

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
