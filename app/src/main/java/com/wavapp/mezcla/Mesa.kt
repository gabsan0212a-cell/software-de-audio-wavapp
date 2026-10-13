package com.wavapp.mezcla

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Nivel de un medidor en dB (ya con su caída, calculada en el núcleo). */
class Medidor {
    var l by mutableFloatStateOf(-120f)
    var r by mutableFloatStateOf(-120f)
}

/** Nombres de los tipos de filtro del EQ y qué controles tiene cada uno. */
object Filtros {
    val nombres = listOf("Campana", "Shelf grave", "Shelf agudo", "Pasa-altos", "Pasa-bajos", "Notch")
    fun conGanancia(tipo: Int) = tipo <= 2
    fun conQ(tipo: Int) = tipo == 0 || tipo >= 3
}

/** Una banda del EQ, como la tiene el núcleo (se nombra por su posición en la pista). */
class Banda(tipo: Int, f: Float, g: Float, q: Float, on: Boolean) {
    var tipo by mutableIntStateOf(tipo)
    var f by mutableFloatStateOf(f)
    var g by mutableFloatStateOf(g)
    var q by mutableFloatStateOf(q)
    var on by mutableStateOf(on)
}

/** Un pedazo de una pista: qué parte del archivo suena y dónde. Todo en cuadros a 48 kHz. */
data class Pedazo(
    val inicio: Long,  // en la línea de tiempo
    val origen: Long,  // en el archivo
    val largo: Long,
    val fadeIn: Long = 0,
    val fadeOut: Long = 0,
) {
    val fin: Long get() = inicio + largo
}

/** Lo que la pantalla muestra de una pista. El dueño de los datos es el núcleo. */
class Pista(
    val slot: Int,
    val id: Int,
    val nombre: String,
    val canales: Int,
    val cuadros: Long,
    val info: String,
) {
    var volDb by mutableFloatStateOf(0f)
    var pan by mutableIntStateOf(0)
    var mute by mutableStateOf(false)
    var solo by mutableStateOf(false)
    var fase by mutableStateOf(false)
    val medidor = Medidor()
    val bandas = mutableStateListOf<Banda>()
    var eqBypass by mutableStateOf(false)
    var autoGanancia by mutableStateOf(true)
    var eqVersion by mutableIntStateOf(0)  // cambia con cada ajuste: la curva se vuelve a dibujar
    var pedazos by mutableStateOf(listOf(Pedazo(0, 0, cuadros)))
    val eqActivo: Boolean get() = !eqBypass && bandas.any { it.on }
    val editada: Boolean get() = pedazos != listOf(Pedazo(0, 0, cuadros))
    val segundos: Double get() = cuadros / Mesa.RATE
    val color: Int get() = if (id <= 0) 0 else (id - 1) % 12
    /** No es una pista: es la entrada en vivo (solo se usa su EQ). */
    val esEntrada: Boolean get() = id == Nucleo.ID_ENTRADA

    // Versiones ya leídas del núcleo (-1 = nunca).
    internal var vParams = -1
    internal var vEq = -1
    internal var vPedazos = -1
}

/** La entrada en vivo (micrófono o interfaz), como la tiene el núcleo. */
class Entrada {
    var abierta by mutableStateOf(false)
    var canales by mutableIntStateOf(1)
    var gananciaDb by mutableFloatStateOf(0f)
    var escucha by mutableStateOf(false)
    var escuchaDb by mutableFloatStateOf(0f)
    var retrasoMs by mutableIntStateOf(-1)
    var grabando by mutableStateOf(false)
    var segundos by mutableDoubleStateOf(0.0)
    var satura by mutableStateOf(false)
    val medidor = Medidor()
    /** Su EQ se maneja como el de una pista (id especial). */
    val pista by lazy { Pista(-1, Nucleo.ID_ENTRADA, "Micrófono", 1, 0, "entrada en vivo") }
}

class MedidorMaster {
    var l by mutableFloatStateOf(-120f)
    var r by mutableFloatStateOf(-120f)
    var tickL by mutableFloatStateOf(-120f)
    var tickR by mutableFloatStateOf(-120f)
    var picoL by mutableFloatStateOf(-120f)
    var picoR by mutableFloatStateOf(-120f)
    var clipL by mutableStateOf(false)
    var clipR by mutableStateOf(false)
    var reduccion by mutableFloatStateOf(0f)
}

/**
 * Estado de la pantalla: un espejo de lo que tiene el núcleo (C++), que es el dueño de todo.
 * Las acciones van directo al núcleo; después se vuelve a leer lo que cambió (el núcleo lleva
 * una versión de cada cosa, así solo se lee lo necesario).
 */
object Mesa {
    const val RATE = 48000.0
    const val MAX_PISTAS = 12
    const val VOL_MIN = -60f
    const val VOL_MAX = 6f
    const val MASTER_MIN = -70f
    const val MAX_BANDAS = 12

    private var iniciada = false
    private var appContext: Context? = null
    private val scope = MainScope()

    val pistas = mutableStateListOf<Pista>()

    var masterDb by mutableFloatStateOf(0f)
        private set
    var masterMute by mutableStateOf(false)
        private set
    var limitador by mutableStateOf(true)
        private set
    var notch by mutableIntStateOf(0)
        private set
    var exportBits by mutableIntStateOf(24)
        private set
    var exportRate by mutableIntStateOf(48000)
        private set

    var reproduciendo by mutableStateOf(false)
        private set
    var posicion by mutableDoubleStateOf(0.0)
        private set
    var duracion by mutableDoubleStateOf(0.0)
        private set

    var estado by mutableStateOf("")
        private set
    var ocupada by mutableStateOf(false)
        private set
    var ocupadaTexto by mutableStateOf("")
        private set
    var progreso by mutableFloatStateOf(-1f)  // -1 = sin barra
        private set
    var exportando by mutableStateOf(false)
        private set
    var puedeDeshacer by mutableStateOf(false)
        private set
    var puedeRehacer by mutableStateOf(false)
        private set

    /** Pista cuyo EQ está abierto en pantalla (null = ninguno). */
    var eqAbierta by mutableStateOf<Pista?>(null)

    val master = MedidorMaster()
    val entrada = Entrada()
    private var moviendo = false

    /** Diagnóstico de la salida de audio (frecuencia, búfer, cortes), una vez por segundo. */
    var salida by mutableStateOf("")
        private set
    private var ultimaSalida = 0L

    /** La app está a la vista (si no, no hace falta preguntarle nada al núcleo). */
    var enPantalla = true

    private val versiones = IntArray(Nucleo.VERSIONES)
    private val vistas = IntArray(Nucleo.VER_FONDO + 1) { -1 }
    private val bufParams = FloatArray(5)
    private val bufMaster = FloatArray(6)
    private val bufEntrada = FloatArray(Nucleo.DATOS_ENTRADA)
    private val bufMedidores = FloatArray(Nucleo.MEDIDORES)

    // Exportación en curso: si sale mal, se borra el archivo que se creó.
    private var exportPendiente: Uri? = null
    private var exportacionesVistas = 0

    // ------------------------------------------------------------------ inicio

    fun iniciar(context: Context) {
        appContext = context.applicationContext
        // El núcleo abre la sesión (o las pistas de prueba la primera vez); si ya estaba abierta, nada.
        Nucleo.init(context.filesDir.path, context.cacheDir.path)
        if (iniciada) return
        iniciada = true
        sincronizar()
        // Lo que termina en segundo plano (importar, exportar, abrir la sesión) se ve aunque la
        // pantalla esté quieta.
        scope.launch {
            while (true) {
                if (enPantalla) sincronizar()
                delay(if (ocupada) 100 else 250)
            }
        }
    }

    /** Vuelve a leer del núcleo lo que cambió. Barato si no cambió nada. */
    fun sincronizar() {
        if (!iniciada) return
        Nucleo.versions(versiones)
        if (versiones[Nucleo.VER_PISTAS] != vistas[Nucleo.VER_PISTAS]) leerPistas()
        if (versiones[Nucleo.VER_MASTER] != vistas[Nucleo.VER_MASTER]) leerMaster()
        if (versiones[Nucleo.VER_HISTORIAL] != vistas[Nucleo.VER_HISTORIAL]) leerHistorial()
        if (versiones[Nucleo.VER_ESTADO] != vistas[Nucleo.VER_ESTADO]) leerEstado()
        if (versiones[Nucleo.VER_FONDO] != vistas[Nucleo.VER_FONDO]) Analisis.alCambiarFondo()
        for (i in vistas.indices) vistas[i] = versiones[i]
        val ve = versiones[Nucleo.VER_ENTRADA]
        if (ve != entrada.pista.vEq) {
            entrada.pista.vEq = ve
            leerEntrada()
            leerEq(entrada.pista)
        }
        for (p in pistas) {
            val vp = versiones[Nucleo.verParams(p.slot)]
            if (vp != p.vParams) { p.vParams = vp; leerParams(p) }
            val ve = versiones[Nucleo.verEq(p.slot)]
            if (ve != p.vEq) { p.vEq = ve; leerEq(p) }
            val vr = versiones[Nucleo.verPedazos(p.slot)]
            if (vr != p.vPedazos) { p.vPedazos = vr; leerPedazos(p) }
        }
        reproduciendo = Nucleo.isPlaying()
        posicion = Nucleo.positionSeconds()
        duracion = Nucleo.durationSeconds()
        if (ocupada) progreso = Nucleo.progress()
    }

    private fun leerPistas() {
        val arr = JSONArray(Nucleo.tracksJson())
        val nuevas = ArrayList<Pista>(arr.length())
        for (i in 0 until arr.length()) {
            val o = arr.getJSONObject(i)
            val id = o.getInt("id")
            val slot = o.getInt("slot")
            nuevas += pistas.firstOrNull { it.id == id && it.slot == slot }
                ?: Pista(slot, id, o.optString("nombre"), o.optInt("canales"), o.optLong("cuadros"), o.optString("info"))
        }
        if (nuevas != pistas.toList()) {
            pistas.clear()
            pistas.addAll(nuevas)
        }
        if (eqAbierta != null && eqAbierta !in nuevas) eqAbierta = null
        Analisis.alCambiarPistas()
        Edicion.alCambiarPistas()
    }

    private fun leerParams(p: Pista) {
        if (!Nucleo.trackParams(p.id, bufParams)) return
        p.volDb = bufParams[0]
        p.pan = bufParams[1].toInt()
        p.mute = bufParams[2] != 0f
        p.solo = bufParams[3] != 0f
        p.fase = bufParams[4] != 0f
    }

    private fun leerEq(p: Pista) {
        val e = Nucleo.eqState(p.id)
        if (e.size < 3) return
        p.eqBypass = e[0] != 0f
        p.autoGanancia = e[1] != 0f
        val n = e[2].toInt()
        if (p.bandas.size != n) {
            p.bandas.clear()
            for (k in 0 until n) p.bandas.add(Banda(0, 1000f, 0f, 1.4f, true))
        }
        for (k in 0 until n) {
            val b = p.bandas[k]
            val o = 3 + 5 * k
            b.tipo = e[o].toInt()
            b.f = e[o + 1]
            b.g = e[o + 2]
            b.q = e[o + 3]
            b.on = e[o + 4] != 0f
        }
        p.eqVersion++
    }

    private fun leerPedazos(p: Pista) {
        val r = Nucleo.regions(p.id)
        val lista = (0 until r.size / 5).map { Pedazo(r[5 * it], r[5 * it + 1], r[5 * it + 2], r[5 * it + 3], r[5 * it + 4]) }
        if (lista != p.pedazos) p.pedazos = lista
    }

    private fun leerMaster() {
        Nucleo.masterState(bufMaster)
        masterDb = bufMaster[0]
        masterMute = bufMaster[1] != 0f
        limitador = bufMaster[2] != 0f
        notch = bufMaster[3].toInt()
        exportBits = bufMaster[4].toInt()
        exportRate = bufMaster[5].toInt()
    }

    private fun leerEntrada() {
        Nucleo.inputState(bufEntrada)
        val e = entrada
        e.abierta = bufEntrada[0] != 0f
        e.canales = bufEntrada[1].toInt()
        e.gananciaDb = bufEntrada[2]
        e.escucha = bufEntrada[3] != 0f
        e.escuchaDb = bufEntrada[4]
        e.retrasoMs = Math.round(bufEntrada[5])
        e.grabando = bufEntrada[6] != 0f
        e.segundos = bufEntrada[7].toDouble()
    }

    private fun leerHistorial() {
        val h = Nucleo.history()
        puedeDeshacer = (h and 1) != 0
        puedeRehacer = (h and 2) != 0
    }

    private fun leerEstado() {
        val o = JSONObject(Nucleo.statusJson())
        estado = o.optString("mensaje")
        ocupada = o.optBoolean("ocupada")
        ocupadaTexto = o.optString("texto")
        exportando = o.optBoolean("exportando")
        progreso = if (ocupada) Nucleo.progress() else -1f
        val hechas = o.optInt("exportaciones")
        if (hechas != exportacionesVistas) {
            exportacionesVistas = hechas
            val uri = exportPendiente
            exportPendiente = null
            if (uri != null && !o.optBoolean("ultimaOk")) borrarDocumento(uri)
        }
    }

    // ------------------------------------------------------------------ pistas

    fun agregarArchivos(context: Context, uris: List<Uri>) {
        if (ocupada || uris.isEmpty()) return
        scope.launch {
            val abiertos = withContext(Dispatchers.IO) {
                uris.map { uri ->
                    val fd = try {
                        context.contentResolver.openFileDescriptor(uri, "r")?.use { it.detachFd() } ?: -1
                    } catch (_: Exception) {
                        -1
                    }
                    fd to nombreDe(context, uri)
                }
            }
            Nucleo.importFiles(abiertos.map { it.first }.toIntArray(), abiertos.map { it.second }.toTypedArray())
            sincronizar()
        }
    }

    fun cargarPrueba() {
        if (ocupada) return
        Nucleo.loadDemo()
        sincronizar()
    }

    fun quitar(p: Pista) {
        if (eqAbierta == p) eqAbierta = null
        Nucleo.removeTrack(p.id)
        sincronizar()
    }

    fun ponerVolumen(p: Pista, db: Float) { Nucleo.setVolume(p.id, db); sincronizar() }
    fun ponerPan(p: Pista, pan: Float) { Nucleo.setPan(p.id, pan); sincronizar() }
    fun alternarMute(p: Pista) { Nucleo.setMute(p.id, !p.mute); sincronizar() }
    fun alternarSolo(p: Pista) { Nucleo.setSolo(p.id, !p.solo); sincronizar() }
    fun alternarFase(p: Pista) { Nucleo.setPhase(p.id, !p.fase); sincronizar() }

    /** ¿Suena esta pista con el mute/solo actual? (para atenuar su carril, como el prototipo) */
    fun suena(p: Pista): Boolean = !p.mute && (pistas.none { it.solo } || p.solo)

    // ------------------------------------------------------------------ EQ

    /** Agrega una banda (el núcleo la pone en el hueco de frecuencias más grande). */
    fun agregarBanda(p: Pista): Banda? {
        val pos = Nucleo.addEqBand(p.id)
        sincronizar()
        return p.bandas.getOrNull(pos)
    }

    fun quitarBanda(p: Pista, b: Banda) {
        val pos = p.bandas.indexOf(b)
        if (pos < 0) return
        Nucleo.removeEqBand(p.id, pos)
        sincronizar()
    }

    fun cambiarBanda(
        p: Pista, b: Banda, tipo: Int = b.tipo, f: Float = b.f, g: Float = b.g, q: Float = b.q, on: Boolean = b.on,
    ) {
        val pos = p.bandas.indexOf(b)
        if (pos < 0) return
        Nucleo.setEqBand(p.id, pos, tipo, f, g, q, on)
        sincronizar()
    }

    fun alternarEqBypass(p: Pista) { Nucleo.setEqBypass(p.id, !p.eqBypass); sincronizar() }
    fun alternarAutoGanancia(p: Pista) { Nucleo.setEqAutoGain(p.id, !p.autoGanancia); sincronizar() }

    /** Quita todas las bandas: el EQ queda plano y sin procesar. */
    fun restablecerEq(p: Pista) { Nucleo.resetEq(p.id); sincronizar() }

    // ------------------------------------------------------------------ master

    fun ponerMaster(db: Float) { Nucleo.setMasterDb(db); sincronizar() }
    fun alternarMasterMute() { Nucleo.setMasterMute(!masterMute); sincronizar() }
    fun alternarLimitador() { Nucleo.setLimiter(!limitador); sincronizar() }
    fun ponerNotch(hz: Int) { Nucleo.setNotch(hz); sincronizar() }

    fun borrarClip() {
        Nucleo.clearClip()
        master.clipL = false
        master.clipR = false
        master.picoL = -120f
        master.picoR = -120f
    }

    // ------------------------------------------------------------------ transporte

    fun reproducirPausar() {
        if (pistas.isEmpty()) return
        Nucleo.playPause()
        reproduciendo = Nucleo.isPlaying()
    }

    fun detener() {
        Nucleo.stopTransport()
        reproduciendo = false
    }

    fun saltar(segundos: Double) {
        Nucleo.seek(segundos)
        posicion = Nucleo.positionSeconds()
    }

    /** ¿Hay algo moviéndose en pantalla (audio, entrada, un trabajo o medidores bajando)? */
    fun activa(): Boolean = reproduciendo || ocupada || moviendo || entrada.abierta

    /** Se llama en cada cuadro de pantalla: posición, medidores y análisis. */
    fun tick(ahoraMs: Long) {
        sincronizar()
        if (ahoraMs - ultimaSalida >= 1000) {
            ultimaSalida = ahoraMs
            salida = textoSalida()
        }
        Analisis.tick(ahoraMs)
        Nucleo.meters(ahoraMs, bufMedidores)
        for (p in pistas) {
            p.medidor.l = bufMedidores[2 * p.slot]
            p.medidor.r = bufMedidores[2 * p.slot + 1]
        }
        val b = Nucleo.medMasterL()
        val m = master
        m.l = bufMedidores[b]
        m.r = bufMedidores[b + 1]
        m.tickL = bufMedidores[b + 2]
        m.tickR = bufMedidores[b + 3]
        m.picoL = bufMedidores[b + 4]
        m.picoR = bufMedidores[b + 5]
        m.clipL = bufMedidores[b + 6] != 0f
        m.clipR = bufMedidores[b + 7] != 0f
        m.reduccion = bufMedidores[b + 8]
        moviendo = bufMedidores[b + 9] != 0f
        val e = entrada
        val k = Nucleo.medEntradaL()
        e.medidor.l = bufMedidores[k]
        e.medidor.r = bufMedidores[k + 1]
        e.satura = bufMedidores[k + 2] != 0f
        if (e.grabando) leerEntrada()  // los segundos grabados
    }

    // ------------------------------------------------------------------ entrada en vivo y grabación

    /** Abre la entrada ([dispositivo] 0 = la del sistema). */
    fun abrirEntrada(dispositivo: Int, canales: Int) {
        Nucleo.inputStart(dispositivo, canales)
        sincronizar()
    }

    fun cerrarEntrada() {
        Nucleo.inputStop()
        sincronizar()
    }

    fun ponerGananciaEntrada(db: Float) { Nucleo.setInputGain(db); sincronizar() }
    fun ponerEscucha(on: Boolean) { Nucleo.setMonitor(on); sincronizar() }
    fun ponerVolumenEscucha(db: Float) { Nucleo.setMonitorDb(db); sincronizar() }

    /** Empieza a grabar o termina (la toma queda como pista nueva). Devuelve el id nuevo o -1. */
    fun grabarOParar(): Int {
        val id = if (entrada.grabando) Nucleo.recordStop() else { Nucleo.recordStart(); -1 }
        sincronizar()
        reproduciendo = Nucleo.isPlaying()
        return id
    }

    fun descartarGrabacion() {
        Nucleo.recordCancel()
        sincronizar()
        reproduciendo = Nucleo.isPlaying()
    }

    fun ondaGrabacion(columnas: Int, segundos: Float): FloatArray = Nucleo.recordingWave(columnas, segundos)

    // ------------------------------------------------------------------ exportar

    fun ponerExportacion(bits: Int, rate: Int) { Nucleo.setExportFormat(bits, rate); sincronizar() }

    fun tamanoExportacion(): Long = Nucleo.exportBytes()

    fun nombreExportacion(): String =
        "Mezcla " + SimpleDateFormat("yyyy-MM-dd HHmm", Locale.US).format(Date()) + ".wav"

    fun textoFormato(bits: Int = exportBits, rate: Int = exportRate): String =
        (if (bits == 32) "32 bits float" else "$bits bits") + " · $rate Hz"

    /** El núcleo renderiza la mezcla completa y la guarda en [uri] (elegido por la persona). */
    fun exportar(context: Context, uri: Uri) {
        if (ocupada || pistas.isEmpty()) return
        scope.launch {
            val (fd, nombre) = withContext(Dispatchers.IO) {
                val fd = try {
                    context.contentResolver.openFileDescriptor(uri, "w")?.use { it.detachFd() } ?: -1
                } catch (_: Exception) {
                    -1
                }
                fd to nombreDe(context, uri)
            }
            if (fd < 0) {
                borrarDocumento(uri)
                avisar("No se exportó: no se pudo crear el archivo")
                return@launch
            }
            exportPendiente = uri
            if (!Nucleo.exportMix(fd, nombre)) {
                exportPendiente = null
                borrarDocumento(uri)
            }
            sincronizar()
        }
    }

    fun cancelarExportacion() = Nucleo.cancelExport()

    private fun borrarDocumento(uri: Uri) {
        val ctx = appContext ?: return
        scope.launch(Dispatchers.IO) {
            try { DocumentsContract.deleteDocument(ctx.contentResolver, uri) } catch (_: Exception) {}
        }
    }

    // ------------------------------------------------------------------ sesión

    /** Al salir de la app: que el núcleo guarde ya (normalmente guarda solo, poco después de cada cambio). */
    fun guardarYa() = Nucleo.saveNow()

    // ------------------------------------------------------------------ utilidades

    /** Mensaje de la pantalla misma (lo siguiente que informe el núcleo lo reemplaza). */
    fun avisar(msg: String) {
        estado = msg
    }

    private fun textoSalida(): String {
        val i = Nucleo.outputInfo()
        if (i[0] == 0) return "Salida de audio cerrada"
        val ms = String.format(Locale.US, "%.1f", i[2] * 1000.0 / i[0])
        val modo = if (i[4] == 1) "exclusivo" else "compartido"
        val cortes = if (i[3] < 0) "?" else i[3].toString()
        val reposo = if (i.size > 5 && i[5] == 1) " · en reposo" else ""
        return "Salida: ${i[0]} Hz · búfer ${i[2]} ($ms ms) · $modo · cortes $cortes$reposo"
    }

    private fun nombreDe(context: Context, uri: Uri): String {
        try {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) return it.getString(0)
            }
        } catch (_: Exception) {
        }
        return uri.lastPathSegment ?: "archivo"
    }
}
