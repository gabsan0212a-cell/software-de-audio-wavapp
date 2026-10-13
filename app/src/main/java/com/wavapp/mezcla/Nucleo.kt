package com.wavapp.mezcla

/**
 * Puente con el núcleo en C++ (el corazón de la app). Todo lo técnico vive allá: el proyecto,
 * la mezcla, importar y decodificar, exportar, la sesión, la edición con deshacer, la
 * automatización, los medidores y el análisis. Kotlin solo dibuja la pantalla y manda lo que la persona hace.
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
    val VER_ENTRADA = c[9]
    val DATOS_ENTRADA = c[10]
    /** El EQ de la entrada en vivo usa las funciones del EQ con este id. */
    val ID_ENTRADA = c[11]
    private val VER_PRIMERA = c[12]
    private val VER_POR_ESPACIO = c[13]
    /** Valores de [automationNow] por espacio: estado, volumen (dB), paneo. */
    val DATOS_AUTO = c[14]
    val DATOS_PISTA = c[15]
    val DATOS_MASTER = c[16]
    val DATOS_LUFS = c[17]
    val DATOS_LUFS_CANCION = c[18]
    /** Cuántos ajustes tienen los efectos de una pista (ver los FX_ de abajo). */
    val FX_PARAMS = c[19]
    private val MED_COMPRESOR = c[20]
    /** "Sin dato" en los valores de LUFS. */
    const val SIN_DATO = -200f

    // Lugares en el arreglo de versions().
    const val VER_PISTAS = 0
    const val VER_MASTER = 1
    const val VER_HISTORIAL = 2
    const val VER_ESTADO = 3
    const val VER_FONDO = 4
    fun verParams(espacio: Int) = VER_PRIMERA + VER_POR_ESPACIO * espacio
    fun verEq(espacio: Int) = VER_PRIMERA + VER_POR_ESPACIO * espacio + 1
    fun verPedazos(espacio: Int) = VER_PRIMERA + VER_POR_ESPACIO * espacio + 2
    fun verAuto(espacio: Int) = VER_PRIMERA + VER_POR_ESPACIO * espacio + 3
    fun verFx(espacio: Int) = VER_PRIMERA + VER_POR_ESPACIO * espacio + 4

    // Lugares en el arreglo de meters(): [2*espacio] L, [2*espacio+1] R de cada pista, y luego:
    fun medMasterL() = 2 * MAX_PISTAS
    // master R +1, marca L +2, marca R +3, pico máx. L +4, pico máx. R +5, clip L +6, clip R +7,
    // reducción del limitador +8, algo se mueve +9, entrada L +10, entrada R +11, la entrada saturó +12
    fun medEntradaL() = 2 * MAX_PISTAS + 10
    /** Reducción del compresor de la pista (dB, ≥ 0). */
    fun medCompresor(espacio: Int) = MED_COMPRESOR + espacio

    // Modos de edición (iguales a Studio::EditMode).
    const val MOVER = 1
    const val RECORTAR_INICIO = 2
    const val RECORTAR_FIN = 3
    const val FADE_IN = 4
    const val FADE_OUT = 5

    // Automatización: parámetros y modos (iguales a autom::Param y Studio::AutoMode).
    const val AUTO_VOLUMEN = 0
    const val AUTO_PANEO = 1
    const val AUTO_APAGADA = 0
    const val AUTO_LEER = 1
    const val AUTO_GRABAR = 2

    // Ajustes de los efectos de una pista (iguales a fx::Param del núcleo).
    const val FX_COMP = 0
    const val FX_COMP_UMBRAL = 1
    const val FX_COMP_PROPORCION = 2
    const val FX_COMP_ATAQUE = 3
    const val FX_COMP_LIBERACION = 4
    const val FX_COMP_GANANCIA = 5
    const val FX_DELAY = 6
    const val FX_DELAY_TIEMPO = 7
    const val FX_DELAY_REPETICIONES = 8
    const val FX_DELAY_CANTIDAD = 9
    const val FX_DELAY_BRILLO = 10
    const val FX_DELAY_PINGPONG = 11
    const val FX_REVERB = 12
    const val FX_REVERB_CANTIDAD = 13
    const val FX_REVERB_DURACION = 14
    const val FX_REVERB_BRILLO = 15
    const val FX_REVERB_PREDELAY = 16

    // ---------- Ciclo de vida
    /** Abre el proyecto de esa carpeta (la primera vez carga las pistas de prueba). */
    external fun init(filesDir: String, cacheDir: String)
    external fun start(): Boolean
    external fun stop()
    external fun saveNow()
    external fun clearProject()
    /** Espera a que terminen los trabajos en segundo plano (pruebas). */
    external fun waitIdle(timeoutMs: Int): Boolean
    /** Salida real: [frecuencia, ráfaga, búfer en cuadros, cortes, exclusivo 1/0, dormida 1/0]. */
    external fun outputInfo(): IntArray
    external fun constants(): IntArray

    // ---------- Estado
    external fun versions(out: IntArray)
    /** [{"id","slot","nombre","canales","cuadros","info"}, ...] en el orden de la pantalla. */
    external fun tracksJson(): String
    /** volumen (dB), paneo, mute, solo, fase, modo de automatización. false si la pista ya no existe. */
    external fun trackParams(id: Int, out: FloatArray): Boolean
    /** [bypass, auto-ganancia, n, y por banda: tipo, frecuencia, ganancia, Q, activa]. */
    external fun eqState(id: Int): FloatArray
    /** 5 números por pedazo: inicio, origen en el archivo, largo, fade in, fade out (cuadros). */
    external fun regions(id: Int): LongArray
    /** volumen master (dB), mute, limitador, notch, bits y frecuencia de exportación, objetivo de LUFS, ajustar al exportar. */
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

    // ---------- Entrada en vivo (micrófono o interfaz USB) y grabación
    /** Abre la entrada (deviceId 0 = la del sistema; 1 o 2 canales). Devuelve los canales abiertos o 0. */
    external fun inputStart(deviceId: Int, channels: Int): Int
    external fun inputStop()
    external fun setInputGain(db: Float)
    external fun setMonitor(on: Boolean)
    external fun setMonitorDb(db: Float)
    /** [abierta, canales, ganancia dB, escucha, volumen de escucha dB, retraso ms, grabando, segundos grabados] */
    external fun inputState(out: FloatArray)
    /** Graba desde el cursor con la mezcla sonando. false si no se pudo (el motivo va al mensaje). */
    external fun recordStart(): Boolean
    /** La toma queda como pista nueva; devuelve su id o -1. */
    external fun recordStop(): Int
    external fun recordCancel()
    /** Lo último de la toma en curso: [cols] valores de 0 a 1 que cubren [seconds]. */
    external fun recordingWave(cols: Int, seconds: Float): FloatArray

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

    // ---------- Automatización de volumen y paneo (param: AUTO_VOLUMEN o AUTO_PANEO)
    /** AUTO_APAGADA (suenan los valores fijos), AUTO_LEER o AUTO_GRABAR (mover los deslizadores graba). */
    external fun setAutoMode(id: Int, mode: Int)
    /**
     * Para dibujar el carril: [n, posición del valor fijo, valor fijo, posición guía (0 dB o el
     * centro), y por punto: cuadro, posición (0 abajo .. 1 arriba), valor].
     */
    external fun autoLane(id: Int, param: Int): DoubleArray
    /**
     * En cada cuadro: avanza lo que se esté grabando y da por espacio [estado, volumen dB, paneo].
     * Estado: 1 = el volumen sigue la automatización, 2 = el paneo, 4 = se graba el volumen, 8 = el paneo.
     */
    external fun automationNow(out: FloatArray)
    /** Un punto desde el carril (pos 0..1). Devuelve su índice o -1. */
    external fun autoAdd(id: Int, param: Int, frame: Long, pos: Float): Int
    external fun autoRemove(id: Int, param: Int, index: Int): Boolean
    external fun autoClear(id: Int, param: Int)
    /** Arrastre de un punto: cuadros y posición desde que empezó (un paso de deshacer). */
    external fun autoBegin(id: Int, param: Int, index: Int): Boolean
    external fun autoDrag(deltaFrames: Long, deltaPos: Float)
    external fun autoEnd()
    /** El dedo está (o ya no) sobre el deslizador de volumen o paneo. */
    external fun autoTouch(id: Int, param: Int, down: Boolean)

    // ---------- Volumen percibido (LUFS) de lo que sale
    /**
     * momentáneo, corto plazo, integrado (LUFS), rango (LU), pico real (dBTP), máx. momentáneo, máx.
     * corto plazo, segundos medidos, objetivo, veredicto (0 sin dato, 1 bajo, 2 bien, 3 fuerte),
     * diferencia con el objetivo (dB). Sin dato: [SIN_DATO].
     */
    external fun loudness(out: FloatArray)
    /** Los últimos valores (10 por segundo) del momentáneo y del corto plazo. */
    external fun loudnessHistory(momentary: FloatArray, shortTerm: FloatArray)
    external fun resetLoudness()
    external fun setLoudnessTarget(lufs: Int)
    external fun setExportNormalize(on: Boolean)
    /** Mide la canción completa en segundo plano. false si hay otro trabajo. */
    external fun measureSong(): Boolean
    /**
     * estado (0 nada, 1 midiendo, 2 lista, 3 la mezcla cambió después), integrado, rango, pico real,
     * máx. momentáneo, máx. corto plazo, segundos, veredicto, diferencia.
     */
    external fun songLoudness(out: FloatArray)
    /** Corto plazo a lo largo de la canción en [cols] columnas (vacío si no se ha medido). */
    external fun songLoudnessCurve(cols: Int): FloatArray

    // ---------- Efectos de la pista: compresor, delay y reverb
    external fun setFx(id: Int, param: Int, value: Float)
    external fun fxState(id: Int): FloatArray
    /** Por ajuste: mínimo, máximo, valor inicial, escala logarítmica (0/1). */
    external fun fxInfo(): FloatArray
    /** ["Voz", "Voz con eco", ...] */
    external fun fxPresetsJson(): String
    external fun applyFxPreset(id: Int, index: Int): Boolean
}
