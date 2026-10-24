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
    /** Versión de la canción preparada para cantar (y de cómo va la separación). */
    val VER_CANCION = c[21]
    val DATOS_SEPARAR = c[22]
    /** Cuántos ajustes tiene la voz en vivo (ver los VOZ_ de abajo) y cuántos datos da [voiceMeters]. */
    val VOZ_PARAMS = c[23]
    val DATOS_VOZ = c[24]
    /** Valores de [showState] y de [songCue]. */
    val DATOS_PRESENTACION = c[25]
    val DATOS_CUE = c[26]
    /** Versión de la música que se crea (tempo, tonalidad, instrumentos preparándose) y sus datos. */
    val VER_MUSICA = c[27]
    val DATOS_MUSICA = c[28]
    /** Cuántos ajustes tiene el sonido de un instrumento (ver los INST_ de abajo). */
    val INST_AJUSTES = c[29]
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
    fun verInst(espacio: Int) = VER_PRIMERA + VER_POR_ESPACIO * espacio + 5

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

    // Calidad al quitar la voz (igual a Studio::SepQuality): alta, o máxima (el doble de tiempo).
    const val CALIDAD_ALTA = 0
    const val CALIDAD_MAXIMA = 1

    // Para cantar en vivo, dónde va la canción (igual a Studio::Cue): sin canción, cantando,
    // esperando la entrada, ya no hay más partes.
    const val CUE_NADA = 0
    const val CUE_CANTANDO = 1
    const val CUE_ESPERANDO = 2
    const val CUE_FIN = 3

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
    // Los de la voz en vivo, para las grabaciones.
    const val FX_GRAVES = 17
    const val FX_GRAVES_HZ = 18
    const val FX_COMPUERTA = 19
    const val FX_COMPUERTA_UMBRAL = 20
    const val FX_AUTOTUNE = 21
    const val FX_AFINAR = 22
    const val FX_VELOCIDAD = 23
    const val FX_TONICA = 24
    const val FX_ESCALA = 25              // ESCALA_
    const val FX_REGISTRO = 26
    const val FX_DEESSER = 27
    const val FX_DEESSER_UMBRAL = 28
    const val FX_DOBLAJE = 29
    const val FX_DOBLAJE_CANTIDAD = 30

    // Instrumentos (iguales a inst::Kind) y ajustes de su sonido (iguales a inst::Param).
    const val INST_BATERIA = 0
    const val INST_808 = 1
    const val INST_BAJO = 2
    const val INST_GUITARRA = 3
    const val INST_SINTE = 4
    const val INST_BRILLO = 0
    const val INST_ATAQUE = 1
    const val INST_DURACION = 2
    const val INST_DISTORSION = 3
    const val INST_DESLIZAR = 4
    const val INST_AFINACION = 5

    // Ajustes de la voz en vivo (iguales a vfx::Param del núcleo).
    const val VOZ_ENCENDIDA = 0
    const val VOZ_AUTOTUNE = 1
    const val VOZ_AFINAR = 2              // cuánto corrige (0..1)
    const val VOZ_VELOCIDAD = 3           // ms para llegar a la nota (0 = robot)
    const val VOZ_TONICA = 4              // 0 = Do (si la escala es a mano)
    const val VOZ_ESCALA = 5              // ESCALA_
    const val VOZ_REGISTRO = 6            // 0 grave, 1 media, 2 aguda
    const val VOZ_GRAVES = 7              // Hz del filtro de graves
    const val VOZ_COMPUERTA = 8
    const val VOZ_COMPUERTA_UMBRAL = 9
    const val VOZ_DEESSER = 10
    const val VOZ_DEESSER_UMBRAL = 11
    const val VOZ_COMP = 12
    const val VOZ_COMP_UMBRAL = 13
    const val VOZ_COMP_PROPORCION = 14
    const val VOZ_COMP_GANANCIA = 15
    const val VOZ_DOBLAJE = 16
    const val VOZ_DOBLAJE_CANTIDAD = 17
    const val VOZ_DELAY = 18
    const val VOZ_DELAY_PULSOS = 19       // 0 = 1/4, 1 = 1/8 con punto, 2 = 1/8, 3 = 1/16
    const val VOZ_DELAY_REPETICIONES = 20
    const val VOZ_DELAY_CANTIDAD = 21
    const val VOZ_REVERB = 22
    const val VOZ_REVERB_CANTIDAD = 23
    const val VOZ_REVERB_DURACION = 24
    const val VOZ_VOLUMEN = 25            // dB

    // Escala del auto-tune (VOZ_ESCALA).
    const val ESCALA_CANCION = 0
    const val ESCALA_MAYOR = 1
    const val ESCALA_MENOR = 2
    const val ESCALA_CROMATICA = 3

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
    /**
     * Cómo se está leyendo la entrada: [modo (0 cerrada, 1 a la par de la salida, 2 con hilo
     * propio), segundos leídos, veces que faltaron muestras, cuadros tirados, reserva en ráfagas,
     * ráfaga, capacidad].
     */
    external fun inputInfo(): IntArray
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

    // ---------- Voz en vivo: auto-tune y efectos de la entrada
    external fun setVoiceFx(param: Int, value: Float)
    external fun voiceFxState(): FloatArray
    /** [mín, máx, inicial, log 1/0] por cada ajuste. */
    external fun voiceFxInfo(): FloatArray
    /** ["Paraíso", "Natural", ...] */
    external fun voicePresetsJson(): String
    /** Pone un preset (y prende la voz); no cambia la escala, la tónica, el registro ni el volumen. */
    external fun applyVoicePreset(index: Int): Boolean
    /**
     * [nota que se canta (MIDI con decimales; 0 = nada), como sale, nota de la escala (-1 = nada),
     * compuerta abierta, de-esser dB, compresor dB, retraso del auto-tune ms, tónica en uso
     * (-1 = cromática), escala en uso (0 mayor, 1 menor, 2 cromática), viene de la canción 1/0].
     */
    external fun voiceMeters(out: FloatArray)

    // ---------- Grabar la presentación: lo que sale hacia la consola (pista + voz en vivo)
    external fun showStart(): Boolean
    external fun showStop(): Boolean
    /** [grabando, segundos grabados, hay una para guardar, sus segundos]. */
    external fun showState(out: FloatArray)
    /** La guarda en [fd] (WAV con el formato de exportar), en segundo plano, como exportar. */
    external fun saveShow(fd: Int, name: String): Boolean

    // ---------- Preparar una canción para cantar en vivo (quitarle la voz con IA)
    /** El modelo de voz en un archivo. false si no es válido. */
    external fun setVoiceModel(path: String): Boolean
    /** El modelo dentro del APK (asset sin comprimir): [length] bytes desde [offset]. */
    external fun setVoiceModelFd(fd: Int, offset: Long, length: Long): Boolean
    external fun hasVoiceModel(): Boolean
    /**
     * Quita la voz de la pista en segundo plano (puede tardar bastante) y deja el instrumental y la
     * voz original como pistas nuevas; después analiza la canción. false si no se puede empezar.
     */
    external fun separate(id: Int, quality: Int): Boolean
    /** [trozos ya guardados de una separación interrumpida, trozos en total]. */
    external fun separationSaved(id: Int, quality: Int, out: IntArray)
    /** Cancela el trabajo en curso (separar o exportar). */
    external fun cancelJob()
    /** [fase (0 nada, 1 preparando, 2 quitando la voz, 3 armando, 4 analizando), progreso, trozos hechos, trozos, segundos que faltan, pista]. */
    external fun separationState(out: FloatArray)
    /** {"lista","fuente","instrumental","voz","tonica","menor","claridad","bpm","pulsos","primerPulso","escala","notas","partes"} */
    external fun songJson(): String
    /** La voz original a lo largo de la mezcla, en [cols] columnas de 0 a 1. */
    external fun songEnvelope(cols: Int): FloatArray
    /**
     * Para cantar en vivo: [estado (CUE_), segundos (los que faltan para entrar o de la parte),
     * parte (desde 0), partes, avance 0..1 (de la espera o de la parte)].
     */
    external fun songCue(out: FloatArray)

    // ---------- Crear música: instrumentos (cada uno es una pista cuyo audio arma el núcleo)
    /** [tempo, tónica (0 = Do), menor, metrónomo, el tempo es de la canción, la tonalidad es de la canción, preparándose]. */
    external fun musicState(out: FloatArray)
    external fun setTempo(bpm: Float)
    external fun setKey(key: Int, minor: Boolean)
    external fun setMetronome(on: Boolean)
    /** {"tipos":[{"nombre","presets","ajustes","inicio"}],"piezas","pasoPulsos","pasosCompas","maxCompases","maxVeces"} */
    external fun instrumentsJson(): String
    /** [mín, máx, inicial, log 1/0] por cada ajuste del sonido. */
    external fun instParamInfo(): FloatArray
    /** Agrega un instrumento (con una idea para empezar); la pista aparece cuando está lista. Devuelve su id o -1. */
    external fun addInstrument(kind: Int, preset: Int): Int
    /** {"tipo","preset","compases","veces","swing","ajustes","notas":[pulso, largo, nota, fuerza, ...],"preparando"} o "". */
    external fun instrumentJson(id: Int): String
    /** Filas de la cuadrícula de arriba a abajo: [[nota, "nombre", tónica, en la escala], ...]. */
    external fun instRowsJson(id: Int, scaleOnly: Boolean): String
    /** 1 = se agregó, 0 = se quitó, -1 = no se pudo. */
    external fun instToggle(id: Int, tick: Int, pitch: Int, len: Int, chord: Boolean): Int
    /** Redoble de la batería en ese paso: devuelve cuántos golpes quedaron (1 a 4). */
    external fun instRoll(id: Int, step: Int, pitch: Int): Int
    external fun instSetNote(id: Int, index: Int, tick: Int, len: Int, pitch: Int, vel: Int): Boolean
    external fun instRemoveNote(id: Int, index: Int): Boolean
    external fun instClear(id: Int)
    external fun instSetPattern(id: Int, bars: Int, repeats: Int, swing: Float)
    external fun instSetPreset(id: Int, preset: Int)
    external fun instSetParam(id: Int, param: Int, value: Float)
    /** Otra idea para empezar (se puede deshacer). */
    external fun instIdea(id: Int): Boolean
    /** Tocar en vivo (apretar o soltar). Si se está grabando y la canción suena, la nota entra al patrón. */
    external fun instPlay(id: Int, pitch: Int, vel: Int, down: Boolean)
    external fun instRecord(id: Int, on: Boolean)
    external fun instRecording(): Boolean
    /** Dónde va la canción dentro del patrón, en pasos (-1 = no suena). */
    external fun instPosition(id: Int): Double
    /** La pestaña Crear está a la vista (la salida no se duerme); con [loopId] se repite ese patrón. */
    external fun setCreating(on: Boolean, loopId: Int)
}
