package com.wavapp.mezcla

/**
 * Puente con el núcleo en C++ (el corazón de la app). Todo lo técnico vive allá: el proyecto,
 * la mezcla, importar y decodificar, exportar, la sesión, la edición con deshacer, los
 * medidores y el análisis. Kotlin solo dibuja la pantalla y manda lo que la persona hace.
 *
 * Las pistas se nombran por su id. Cada cosa que puede cambiar tiene una versión
 * ([versions]): la pantalla las compara para saber qué volver a leer.
 */
object Nucleo {
    init {
        System.loadLibrary("wavapp")
    }

    private val c = constants()
    val MAX_PISTAS = c[0]
    val MAX_BANDAS = c[1]
    val MEDIDORES = c[2]
    val DATOS_ANALISIS = c[3]
    val VERSIONES = c[4]
    val SG_COLUMNAS = c[5]
    val SG_FILAS = c[6]
    val FRECUENCIA = c[7]
    val MAX_PEDAZOS = c[8]

    // Lugares en el arreglo de versions().
    const val VER_PISTAS = 0
    const val VER_MASTER = 1
    const val VER_HISTORIAL = 2
    const val VER_ESTADO = 3
    const val VER_FONDO = 4
    fun verParams(espacio: Int) = 5 + 3 * espacio
    fun verEq(espacio: Int) = 6 + 3 * espacio
    fun verPedazos(espacio: Int) = 7 + 3 * espacio

    // Lugares en el arreglo de meters(): [2*espacio] L, [2*espacio+1] R de cada pista, y luego:
    fun medMasterL() = 2 * MAX_PISTAS
    // master R +1, marca L +2, marca R +3, pico máx. L +4, pico máx. R +5, clip L +6, clip R +7,
    // reducción del limitador +8, algo se mueve +9

    // Modos de edición (iguales a Studio::EditMode).
    const val MOVER = 1
    const val RECORTAR_INICIO = 2
    const val RECORTAR_FIN = 3
    const val FADE_IN = 4
    const val FADE_OUT = 5

    // ---------- Ciclo de vida
    /** Abre el proyecto de esa carpeta (la primera vez carga las pistas de prueba). */
    external fun init(filesDir: String, cacheDir: String)
    external fun start(): Boolean
    external fun stop()
    external fun saveNow()
    external fun clearProject()
    /** Espera a que terminen los trabajos en segundo plano (pruebas). */
    external fun waitIdle(timeoutMs: Int): Boolean
    /** Salida real: [frecuencia, ráfaga, búfer en cuadros, cortes, exclusivo 1/0]. */
    external fun outputInfo(): IntArray
    external fun constants(): IntArray

    // ---------- Estado
    external fun versions(out: IntArray)
    /** [{"id","slot","nombre","canales","cuadros","info"}, ...] en el orden de la pantalla. */
    external fun tracksJson(): String
    /** volumen (dB), paneo, mute, solo, fase. false si la pista ya no existe. */
    external fun trackParams(id: Int, out: FloatArray): Boolean
    /** [bypass, auto-ganancia, n, y por banda: tipo, frecuencia, ganancia, Q, activa]. */
    external fun eqState(id: Int): FloatArray
    /** 5 números por pedazo: inicio, origen en el archivo, largo, fade in, fade out (cuadros). */
    external fun regions(id: Int): LongArray
    /** volumen master (dB), mute, limitador, notch, bits y frecuencia de exportación. */
    external fun masterState(out: FloatArray)
    /** {"mensaje","ocupada","texto","exportando","exportaciones","ultimaOk"} */
    external fun statusJson(): String
    external fun progress(): Float
    /** 1 = se puede deshacer, 2 = se puede rehacer. */
    external fun history(): Int

    // ---------- Pistas
    /** Importa en segundo plano y cierra los descriptores. false si hay otro trabajo en curso. */
    external fun importFiles(fds: IntArray, names: Array<String>): Boolean
    external fun loadDemo(): Boolean
    external fun removeTrack(id: Int): Boolean
    external fun setVolume(id: Int, db: Float)
    external fun setPan(id: Int, pan: Float)
    external fun setMute(id: Int, on: Boolean)
    external fun setSolo(id: Int, on: Boolean)
    external fun setPhase(id: Int, on: Boolean)

    // ---------- EQ (bandas por su posición en la pista). Tipos: 0 campana, 1 shelf grave,
    // 2 shelf agudo, 3 pasa-altos, 4 pasa-bajos, 5 notch.
    external fun addEqBand(id: Int): Int
    external fun removeEqBand(id: Int, pos: Int)
    external fun setEqBand(id: Int, pos: Int, type: Int, freq: Float, gain: Float, q: Float, on: Boolean)
    external fun setEqBypass(id: Int, on: Boolean)
    external fun setEqAutoGain(id: Int, on: Boolean)
    external fun resetEq(id: Int)
    external fun eqTrimDb(id: Int): Float
    /** Respuesta en dB para cada frecuencia. pos = -1: todo el EQ. */
    external fun eqResponse(id: Int, pos: Int, freqs: FloatArray, out: FloatArray)

    // ---------- Master y exportar
    external fun setMasterDb(db: Float)
    external fun setMasterMute(on: Boolean)
    external fun setLimiter(on: Boolean)
    external fun setNotch(hz: Int)
    external fun setExportFormat(bits: Int, rate: Int)
    external fun exportBytes(): Long
    /** Exporta en segundo plano y cierra el descriptor. */
    external fun exportMix(fd: Int, name: String): Boolean
    external fun cancelExport()

    // ---------- Transporte
    external fun play()
    external fun pause()
    external fun playPause()
    external fun stopTransport()
    external fun seek(seconds: Double)
    external fun isPlaying(): Boolean
    external fun positionSeconds(): Double
    external fun durationSeconds(): Double

    // ---------- Medidores y análisis
    external fun meters(nowMs: Long, out: FloatArray)
    external fun clearClip()
    /** -1 = la mezcla. */
    external fun setAnalysisSource(id: Int)
    /**
     * Llamar en cada cuadro mientras se muestra el análisis. stats: activo, fuerte, correlación,
     * paneo, veredicto, desbalance, malo, zumbido Hz, armónicos, dB sobre el piso, ganancia del
     * goniómetro. Devuelve true si cambió lo que hay que dibujar.
     */
    external fun analysis(nowMs: Long, classic: Boolean, stats: FloatArray): Boolean
    /** Clásico: puntos (x, y) en -1..1. Por fuentes: rayos (ángulo, largo, opacidad). Devuelve cuántos. */
    external fun goniometer(out: FloatArray): Int
    /** Espectro de 20 Hz a 20 kHz en columnas logarítmicas (dB). */
    external fun spectrumColumns(out: FloatArray)
    /** 0..255 [col * filas + fila], fila 0 = 20 Hz. null mientras se calcula. */
    external fun spectrogram(id: Int): ByteArray?

    // ---------- Forma de onda: [columna][canal][min, max] de la parte [from, to) del pedazo.
    external fun regionWave(id: Int, index: Int, from: Long, to: Long, cols: Int): FloatArray?

    // ---------- Edición
    external fun editBegin(id: Int, index: Int, mode: Int): Boolean
    external fun editDrag(delta: Long, snap: Long)
    external fun editEnd()
    /** Con id: índice del pedazo nuevo o -1. Con id = -1: cuántos pedazos se cortaron. */
    external fun cut(id: Int, index: Int): Int
    external fun deleteRegion(id: Int, index: Int): Boolean
    external fun nudgeRegion(id: Int, index: Int, ms: Int)
    external fun setRegionFade(id: Int, index: Int, fadeIn: Boolean, frames: Long)
    external fun restoreRegions(id: Int)
    external fun undo(): Boolean
    external fun redo(): Boolean
}
