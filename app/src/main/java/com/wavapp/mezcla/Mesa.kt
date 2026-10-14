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

/**
 * Un carril de automatización listo para dibujar (lo calcula el núcleo): las posiciones van de 0
 * (abajo) a 1 (arriba) y entre dos puntos el valor va en línea recta.
 */
class CurvaAuto(
    val cuadros: LongArray,
    val pos: FloatArray,
    val valores: FloatArray,
    val posFija: Float,   // el valor fijo del deslizador (lo que suena sin puntos)
    val valorFijo: Float,
    val guia: Float,      // 0 dB o el centro
) {
    val n: Int get() = cuadros.size

    companion object {
        val VACIA = CurvaAuto(LongArray(0), FloatArray(0), FloatArray(0), 0.5f, 0f, 0.5f)

        fun de(d: DoubleArray): CurvaAuto {
            if (d.size < 4) return VACIA
            val n = d[0].toInt().coerceIn(0, (d.size - 4) / 3)
            return CurvaAuto(
                LongArray(n) { d[4 + 3 * it].toLong() }, FloatArray(n) { d[5 + 3 * it].toFloat() },
                FloatArray(n) { d[6 + 3 * it].toFloat() }, d[1].toFloat(), d[2].toFloat(), d[3].toFloat(),
            )
        }
    }
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
    // Automatización: modo, los dos carriles y lo que marcan ahora (cambia en cada cuadro al sonar).
    var autoModo by mutableIntStateOf(Nucleo.AUTO_LEER)
    var autoVol by mutableStateOf(CurvaAuto.VACIA)
    var autoPan by mutableStateOf(CurvaAuto.VACIA)
    var autoEstado by mutableIntStateOf(0)
    var volAhora by mutableFloatStateOf(0f)
    var panAhora by mutableFloatStateOf(0f)
    fun curva(param: Int): CurvaAuto = if (param == Nucleo.AUTO_PANEO) autoPan else autoVol
    /** El deslizador sigue la automatización (si no, muestra el valor fijo). */
    fun sigue(param: Int): Boolean = (autoEstado and (1 shl param)) != 0
    fun grabando(param: Int): Boolean = (autoEstado and (4 shl param)) != 0
    val volMostrado: Float get() = if (sigue(Nucleo.AUTO_VOLUMEN)) volAhora else volDb
    val panMostrado: Int get() = if (sigue(Nucleo.AUTO_PANEO)) Math.round(panAhora) else pan
    val tieneAuto: Boolean get() = autoVol.n > 0 || autoPan.n > 0
    /** Efectos: los ajustes (ver Nucleo.FX_...) y cuánto está bajando el compresor ahora (dB). */
    var fx by mutableStateOf(FloatArray(0))
    var compresion by mutableFloatStateOf(0f)
    fun fxOn(param: Int): Boolean = fx.getOrElse(param) { 0f } > 0.5f
    val fxActivo: Boolean get() = fxOn(Nucleo.FX_COMP) || fxOn(Nucleo.FX_DELAY) || fxOn(Nucleo.FX_REVERB)
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
    internal var vAuto = -1
    internal var vFx = -1
}

/** Volumen percibido (LUFS) de lo que sale, como lo calcula el núcleo. */
class Volumen {
    var momentaneo by mutableFloatStateOf(Nucleo.SIN_DATO)
    var corto by mutableFloatStateOf(Nucleo.SIN_DATO)
    var integrado by mutableFloatStateOf(Nucleo.SIN_DATO)
    var rango by mutableFloatStateOf(Nucleo.SIN_DATO)
    var picoReal by mutableFloatStateOf(Nucleo.SIN_DATO)
    var maxCorto by mutableFloatStateOf(Nucleo.SIN_DATO)
    var segundos by mutableFloatStateOf(0f)
    var veredicto by mutableIntStateOf(0)  // 0 sin dato, 1 bajo, 2 en el objetivo, 3 fuerte
    var diferencia by mutableFloatStateOf(0f)

    // La canción completa (medida aparte, fuera de tiempo real).
    var cancion by mutableIntStateOf(0)  // 0 nada, 1 midiendo, 2 lista, 3 la mezcla cambió
    var cancionIntegrado by mutableFloatStateOf(Nucleo.SIN_DATO)
    var cancionRango by mutableFloatStateOf(Nucleo.SIN_DATO)
    var cancionPico by mutableFloatStateOf(Nucleo.SIN_DATO)
    var cancionVeredicto by mutableIntStateOf(0)
    var cancionDiferencia by mutableFloatStateOf(0f)
    /** Corto plazo a lo largo de la canción (para dibujar). */
    var curva by mutableStateOf(FloatArray(0))
}

/** La canción preparada para cantar en vivo (al quitarle la voz), como la tiene el núcleo. */
class Cancion {
    var lista by mutableStateOf(false)
    var fuente by mutableIntStateOf(0)        // la pista de la que salió
    var instrumental by mutableIntStateOf(0)
    var voz by mutableIntStateOf(0)           // la voz original (guía); 0 = se quitó
    var tonica by mutableIntStateOf(-1)       // 0 = Do ... 11 = Si; -1 = no se sabe
    var menor by mutableStateOf(false)
    var claridad by mutableFloatStateOf(0f)   // qué tan clara es la tonalidad (0..1)
    var bpm by mutableFloatStateOf(0f)
    var pulsos by mutableIntStateOf(0)
    var escala by mutableStateOf(IntArray(0))
    var notas by mutableStateOf(FloatArray(0))  // cuánto usa la voz cada nota (0..1)
    /** Dónde canta la voz original: (inicio, fin) en segundos. */
    var partes by mutableStateOf(listOf<Pair<Float, Float>>())
}

/** Cómo va el trabajo de quitar la voz (lo lleva el núcleo). */
class Separacion {
    var fase by mutableIntStateOf(0)  // 0 nada, 1 preparando, 2 quitando la voz, 3 armando las pistas, 4 analizando
    var progreso by mutableFloatStateOf(0f)
    var hechos by mutableIntStateOf(0)
    var total by mutableIntStateOf(0)
    var faltan by mutableFloatStateOf(-1f)  // segundos; -1 = todavía no se sabe
    var pista by mutableIntStateOf(0)
    val activa: Boolean get() = fase != 0
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

/**
 * La voz en vivo (auto-tune y efectos de la entrada), como la tiene el núcleo. Lo que cambia en
 * cada cuadro (la nota, la compuerta, cuánto bajan el de-esser y el compresor) se lee al dibujar.
 */
class Voz {
    var valores by mutableStateOf(FloatArray(0))
    fun valor(p: Int): Float = valores.getOrElse(p) { 0f }
    fun on(p: Int): Boolean = valor(p) > 0.5f
    val encendida: Boolean get() = on(Nucleo.VOZ_ENCENDIDA)
    var nota by mutableFloatStateOf(0f)       // lo que se canta (MIDI con decimales; 0 = nada)
    var sale by mutableFloatStateOf(0f)       // como sale después del auto-tune
    var objetivo by mutableIntStateOf(-1)     // la nota de la escala a la que va
    var compuerta by mutableStateOf(false)    // abierta
    var deEsser by mutableFloatStateOf(0f)    // dB que baja
    var compresor by mutableFloatStateOf(0f)  // dB que baja
    var retrasoMs by mutableFloatStateOf(0f)  // del auto-tune
    var tonica by mutableIntStateOf(-1)       // la escala en uso (-1 = cromática)
    var escala by mutableIntStateOf(2)        // 0 mayor, 1 menor, 2 cromática
    var deCancion by mutableStateOf(true)     // la escala sale de la canción preparada
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
    val volumen = Volumen()
    /** LUFS de referencia: −14 (Spotify, YouTube), −16 (Apple Music) o −23 (radio y TV). */
    var objetivoLufs by mutableIntStateOf(-14)
        private set
    var ajustarAlExportar by mutableStateOf(false)
        private set
    /** Nombres de los presets de efectos y rangos de cada ajuste (vienen del núcleo). */
    val presetsFx: List<String> by lazy {
        val a = JSONArray(Nucleo.fxPresetsJson())
        List(a.length()) { a.getString(it) }
    }
    val infoFx: FloatArray by lazy { Nucleo.fxInfo() }
    private var moviendo = false

    /** La voz en vivo: sus ajustes, presets y rangos (vienen del núcleo). */
    val voz = Voz()
    val presetsVoz: List<String> by lazy {
        val a = JSONArray(Nucleo.voicePresetsJson())
        List(a.length()) { a.getString(it) }
    }
    val infoVoz: FloatArray by lazy { Nucleo.voiceFxInfo() }
    private val bufVoz = FloatArray(Nucleo.DATOS_VOZ)

    /** La canción preparada para cantar y cómo va la separación de la voz. */
    val cancion = Cancion()
    val separacion = Separacion()
    /** Esta compilación trae el modelo para quitar la voz. */
    var modeloVoz by mutableStateOf(false)
        private set
    private var vCancion = -1
    private val bufSeparar = FloatArray(Nucleo.DATOS_SEPARAR)

    /** Diagnóstico de la salida de audio (frecuencia, búfer, cortes), una vez por segundo. */
    var salida by mutableStateOf("")
        private set
    private var ultimaSalida = 0L

    /** La app está a la vista (si no, no hace falta preguntarle nada al núcleo). */
    var enPantalla = true

    private val versiones = IntArray(Nucleo.VERSIONES)
    private val vistas = IntArray(Nucleo.VER_FONDO + 1) { -1 }
    private val bufParams = FloatArray(Nucleo.DATOS_PISTA)
    private val bufAuto = FloatArray(Nucleo.DATOS_AUTO * Nucleo.MAX_PISTAS)
    private val bufMaster = FloatArray(Nucleo.DATOS_MASTER)
    private val bufLufs = FloatArray(Nucleo.DATOS_LUFS)
    private val bufCancion = FloatArray(Nucleo.DATOS_LUFS_CANCION)
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
        cargarModeloVoz(context)
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
            voz.valores = Nucleo.voiceFxState()
            leerMedidoresVoz()
        }
        for (p in pistas) {
            val vp = versiones[Nucleo.verParams(p.slot)]
            val va = versiones[Nucleo.verAuto(p.slot)]
            // Los carriles traen el valor fijo: también se leen si cambió el volumen o el paneo.
            val releerAuto = va != p.vAuto || vp != p.vParams
            val vf = versiones[Nucleo.verFx(p.slot)]
            if (vf != p.vFx) { p.vFx = vf; p.fx = Nucleo.fxState(p.id) }
            if (vp != p.vParams) { p.vParams = vp; leerParams(p) }
            val ve = versiones[Nucleo.verEq(p.slot)]
            if (ve != p.vEq) { p.vEq = ve; leerEq(p) }
            val vr = versiones[Nucleo.verPedazos(p.slot)]
            if (vr != p.vPedazos) { p.vPedazos = vr; leerPedazos(p) }
            if (releerAuto) { p.vAuto = va; leerAuto(p) }
        }
        if (pistas.isNotEmpty()) leerAutoAhora()
        val vc = versiones[Nucleo.VER_CANCION]
        if (vc != vCancion) { vCancion = vc; leerCancion() }
        leerVolumen()
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
        p.autoModo = bufParams[5].toInt()
    }

    private fun leerAuto(p: Pista) {
        p.autoVol = CurvaAuto.de(Nucleo.autoLane(p.id, Nucleo.AUTO_VOLUMEN))
        p.autoPan = CurvaAuto.de(Nucleo.autoLane(p.id, Nucleo.AUTO_PANEO))
    }

    /** Lo que marcan ahora los deslizadores con automatización (y lo que se está grabando). */
    private fun leerAutoAhora() {
        Nucleo.automationNow(bufAuto)
        val k = Nucleo.DATOS_AUTO
        for (p in pistas) {
            val o = k * p.slot
            p.autoEstado = bufAuto[o].toInt()
            p.volAhora = bufAuto[o + 1]
            p.panAhora = bufAuto[o + 2]
        }
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
        objetivoLufs = bufMaster[6].toInt()
        ajustarAlExportar = bufMaster[7] != 0f
    }

    /** LUFS en vivo y de la canción completa (baratos: el núcleo ya tiene las cuentas). */
    private fun leerVolumen() {
        Nucleo.loudness(bufLufs)
        val v = volumen
        v.momentaneo = bufLufs[0]
        v.corto = bufLufs[1]
        v.integrado = bufLufs[2]
        v.rango = bufLufs[3]
        v.picoReal = bufLufs[4]
        v.maxCorto = bufLufs[6]
        v.segundos = bufLufs[7]
        v.veredicto = bufLufs[9].toInt()
        v.diferencia = bufLufs[10]
        Nucleo.songLoudness(bufCancion)
        val estado = bufCancion[0].toInt()
        // La curva se lee una vez, cuando termina de medir.
        if (estado == 2 && (v.cancion < 2 || v.curva.isEmpty())) v.curva = Nucleo.songLoudnessCurve(240)
        if (estado == 0) v.curva = FloatArray(0)
        v.cancion = estado
        v.cancionIntegrado = bufCancion[1]
        v.cancionRango = bufCancion[2]
        v.cancionPico = bufCancion[3]
        v.cancionVeredicto = bufCancion[7].toInt()
        v.cancionDiferencia = bufCancion[8]
    }

    /** La canción preparada y cómo va la separación de la voz (cambian juntas de versión). */
    private fun leerCancion() {
        Nucleo.separationState(bufSeparar)
        val s = separacion
        s.fase = bufSeparar[0].toInt()
        s.progreso = bufSeparar[1]
        s.hechos = bufSeparar[2].toInt()
        s.total = bufSeparar[3].toInt()
        s.faltan = bufSeparar[4]
        s.pista = bufSeparar[5].toInt()
        modeloVoz = Nucleo.hasVoiceModel()
        val o = JSONObject(Nucleo.songJson())
        val c = cancion
        c.lista = o.optBoolean("lista")
        c.fuente = o.optInt("fuente")
        c.instrumental = o.optInt("instrumental")
        c.voz = o.optInt("voz")
        c.tonica = o.optInt("tonica", -1)
        c.menor = o.optBoolean("menor")
        c.claridad = o.optDouble("claridad", 0.0).toFloat()
        c.bpm = o.optDouble("bpm", 0.0).toFloat()
        c.pulsos = o.optInt("pulsos")
        val e = o.optJSONArray("escala")
        c.escala = IntArray(e?.length() ?: 0) { e!!.getInt(it) }
        val n = o.optJSONArray("notas")
        c.notas = FloatArray(n?.length() ?: 0) { n!!.getDouble(it).toFloat() }
        val pa = o.optJSONArray("partes")
        c.partes = List(pa?.length() ?: 0) {
            val x = pa!!.getJSONArray(it)
            x.getDouble(0).toFloat() to x.getDouble(1).toFloat()
        }
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

    /** El dedo está (o ya no) sobre el deslizador de volumen o paneo: así se graban los movimientos. */
    fun tocarDeslizador(p: Pista, param: Int, abajo: Boolean) { Nucleo.autoTouch(p.id, param, abajo); sincronizar() }

    // ------------------------------------------------------------------ efectos

    fun ponerFx(p: Pista, param: Int, valor: Float) { Nucleo.setFx(p.id, param, valor); sincronizar() }
    fun alternarFx(p: Pista, param: Int) = ponerFx(p, param, if (p.fxOn(param)) 0f else 1f)
    fun aplicarPresetFx(p: Pista, indice: Int) { Nucleo.applyFxPreset(p.id, indice); sincronizar() }

    // ------------------------------------------------------------------ volumen percibido (LUFS)

    fun ponerObjetivoLufs(lufs: Int) { Nucleo.setLoudnessTarget(lufs); sincronizar() }
    fun ponerAjusteAlExportar(on: Boolean) { Nucleo.setExportNormalize(on); sincronizar() }
    fun reiniciarLufs() { Nucleo.resetLoudness(); sincronizar() }
    fun medirCancion() {
        if (ocupada || pistas.isEmpty()) return
        Nucleo.measureSong()
        sincronizar()
    }

    // ------------------------------------------------------------------ preparar para cantar

    /**
     * Quita la voz de la pista con el modelo (en el núcleo). Es largo: sigue en segundo plano con
     * un aviso del sistema, aunque se apague la pantalla.
     */
    fun prepararCancion(p: Pista, calidad: Int) {
        if (ocupada) return
        if (Nucleo.separate(p.id, calidad)) appContext?.let { TrabajoLargo.empezar(it) }
        sincronizar()
    }

    fun cancelarTrabajo() { Nucleo.cancelJob(); sincronizar() }

    /** [trozos ya guardados, trozos en total] de una separación que quedó a medias. */
    fun guardadosSeparacion(p: Pista, calidad: Int): IntArray = IntArray(2).also { Nucleo.separationSaved(p.id, calidad, it) }

    /** La voz original a lo largo de la canción (para dibujar dónde canta). */
    fun envolventeVoz(columnas: Int): FloatArray = Nucleo.songEnvelope(columnas)

    /** El modelo viene dentro del APK sin comprimir: el núcleo lo lee directo del archivo. */
    private fun cargarModeloVoz(context: Context) {
        modeloVoz = try {
            context.assets.openFd("modelos/voz-kim.wvm").use { a ->
                Nucleo.setVoiceModelFd(a.parcelFileDescriptor.fd, a.startOffset, a.length)
            }
        } catch (e: Exception) {
            Nucleo.hasVoiceModel()  // esta compilación no lo trae (o ya estaba puesto)
        }
    }

    // ------------------------------------------------------------------ automatización

    fun ponerModoAuto(p: Pista, modo: Int) { Nucleo.setAutoMode(p.id, modo); sincronizar() }

    /** Un punto en [cuadro] a la altura [pos] del carril (0 abajo, 1 arriba). Devuelve su índice o -1. */
    fun agregarPunto(p: Pista, param: Int, cuadro: Long, pos: Float): Int {
        val i = Nucleo.autoAdd(p.id, param, cuadro, pos)
        sincronizar()
        return i
    }

    fun quitarPunto(p: Pista, param: Int, indice: Int) { Nucleo.autoRemove(p.id, param, indice); sincronizar() }
    fun borrarAuto(p: Pista, param: Int) { Nucleo.autoClear(p.id, param); sincronizar() }
    fun empezarPunto(p: Pista, param: Int, indice: Int): Boolean = Nucleo.autoBegin(p.id, param, indice)
    fun arrastrarPunto(cuadros: Long, pos: Float) { Nucleo.autoDrag(cuadros, pos); sincronizar() }
    fun terminarPunto() { Nucleo.autoEnd(); sincronizar() }

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
    // Quitar la voz tarda mucho: su avance basta leerlo unas veces por segundo (ahorra batería).
    fun activa(): Boolean = reproduciendo || (ocupada && !separacion.activa) || moviendo || entrada.abierta

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
            p.compresion = bufMedidores[Nucleo.medCompresor(p.slot)]
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
        if (e.abierta && voz.encendida) leerMedidoresVoz()
    }

    private fun leerMedidoresVoz() {
        Nucleo.voiceMeters(bufVoz)
        val v = voz
        v.nota = bufVoz[0]
        v.sale = bufVoz[1]
        v.objetivo = bufVoz[2].toInt()
        v.compuerta = bufVoz[3] != 0f
        v.deEsser = bufVoz[4]
        v.compresor = bufVoz[5]
        v.retrasoMs = bufVoz[6]
        v.tonica = bufVoz[7].toInt()
        v.escala = bufVoz[8].toInt()
        v.deCancion = bufVoz[9] != 0f
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

    // ------------------------------------------------------------------ voz en vivo

    fun ponerVoz(param: Int, valor: Float) { Nucleo.setVoiceFx(param, valor); sincronizar() }
    fun alternarVoz(param: Int) = ponerVoz(param, if (voz.on(param)) 0f else 1f)
    fun aplicarPresetVoz(indice: Int) { Nucleo.applyVoicePreset(indice); sincronizar() }

    /** Prende o apaga la voz en vivo. Al prenderla también se escucha (sale por la consola). */
    fun prenderVoz(on: Boolean, conEscucha: Boolean) {
        Nucleo.setVoiceFx(Nucleo.VOZ_ENCENDIDA, if (on) 1f else 0f)
        if (on && conEscucha && !entrada.escucha) Nucleo.setMonitor(true)
        sincronizar()
    }

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
