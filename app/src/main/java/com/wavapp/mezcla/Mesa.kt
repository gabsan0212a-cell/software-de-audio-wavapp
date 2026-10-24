package com.wavapp.mezcla

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
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
import kotlin.math.ceil

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

/**
 * Lo que toca un instrumento y su sonido, como lo tiene el núcleo. Las notas van de a 4 números
 * (pulso, largo, nota o pieza, fuerza); los pulsos de un paso de la cuadrícula los dice el núcleo.
 */
class Patron(
    val tipo: Int,
    val preset: Int,
    val compases: Int,
    val veces: Int,
    val swing: Float,
    val ajustes: FloatArray,
    val notas: IntArray,
    val preparando: Boolean,
) {
    val n: Int get() = notas.size / 4
    fun pulso(i: Int) = notas[4 * i]
    fun largo(i: Int) = notas[4 * i + 1]
    fun nota(i: Int) = notas[4 * i + 2]
    fun fuerza(i: Int) = notas[4 * i + 3]
}

/** Una fila de la cuadrícula de un instrumento (de arriba a abajo). */
class Fila(val nota: Int, val nombre: String, val tonica: Boolean, val enEscala: Boolean)

/** Un tipo de instrumento: su nombre, sus presets y qué ajustes usa (vienen del núcleo). */
class TipoInstrumento(val nombre: String, val presets: List<String>, val ajustes: List<Int>, val inicio: Int)

/** Un estilo: varios instrumentos que suenan juntos, con su tempo y tonalidad (vienen del núcleo). */
class Estilo(val nombre: String, val detalle: String, val bpm: Int, val tonica: Int, val menor: Boolean, val partes: List<Int>)

/** Lo que el núcleo sabe de los instrumentos (no cambia). */
class InfoInstrumentos(
    val tipos: List<TipoInstrumento>,
    val piezas: List<String>,
    val pasoPulsos: Int,
    val pasosCompas: Int,
    val maxCompases: Int,
    val maxVeces: Int,
    val estilos: List<Estilo>,
)

/** La música que se crea: tempo, tonalidad y metrónomo, como los tiene el núcleo. */
class Musica {
    var bpm by mutableFloatStateOf(90f)
    var tonica by mutableIntStateOf(9)
    var menor by mutableStateOf(true)
    var metronomo by mutableStateOf(false)
    var bpmDeCancion by mutableStateOf(false)
    var tonicaDeCancion by mutableStateOf(false)
    var preparando by mutableIntStateOf(0)  // instrumentos que se están preparando
    // La escala "de la canción" del auto-tune de las pistas (-1 = todas las notas).
    var tonicaAutotune by mutableIntStateOf(-1)
    var menorAutotune by mutableStateOf(false)
}

/** Lo que la pantalla muestra de una pista. El dueño de los datos es el núcleo. */
class Pista(
    val slot: Int,
    val id: Int,
    val nombre: String,
    val canales: Int,
    cuadros: Long,
    info: String,
    /** -1 = audio; si no, el instrumento (Nucleo.INST_...). */
    val instrumento: Int = -1,
) {
    /** Cambia cuando el núcleo vuelve a preparar un instrumento. */
    var cuadros by mutableLongStateOf(cuadros)
    var info by mutableStateOf(info)
    /** Lo que toca (solo en un instrumento). */
    var patron by mutableStateOf<Patron?>(null)
    val esInstrumento: Boolean get() = instrumento >= 0
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
    val fxActivo: Boolean get() = listOf(
        Nucleo.FX_COMP, Nucleo.FX_DELAY, Nucleo.FX_REVERB, Nucleo.FX_GRAVES, Nucleo.FX_COMPUERTA, Nucleo.FX_AUTOTUNE,
        Nucleo.FX_DEESSER, Nucleo.FX_DOBLAJE,
    ).any { fxOn(it) }
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
    internal var vInst = -1
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

/** La presentación que se graba en vivo (lo que sale hacia la consola), como la tiene el núcleo. */
class Presentacion {
    var grabando by mutableStateOf(false)
    var segundos by mutableIntStateOf(0)  // lo grabado (cambia una vez por segundo)
    var lista by mutableStateOf(false)    // hay una para guardar en el teléfono
    var duracion by mutableIntStateOf(0)  // sus segundos
    var guardada by mutableStateOf(false) // la última ya se guardó en el teléfono
    var error by mutableStateOf("")       // por qué no se pudo grabar o guardar (vacío = nada)
}

/**
 * Para cantar en vivo: dónde va la canción respecto a las partes en que canta la voz original
 * (la da el núcleo). Mientras el escenario está a la vista se lee en cada cuadro.
 */
class Cuenta {
    var estado by mutableIntStateOf(Nucleo.CUE_NADA)
    var segundos by mutableIntStateOf(0)    // para entrar o lo que queda de la parte (hacia arriba)
    var parte by mutableIntStateOf(0)       // desde 0
    var partes by mutableIntStateOf(0)
    var avance by mutableFloatStateOf(0f)   // 0..1 de la espera o de la parte (se lee al dibujar)
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

    /** Crear música: tempo y tonalidad, los instrumentos que hay y el instrumento abierto. */
    val musica = Musica()
    val infoInstrumentos: InfoInstrumentos by lazy {
        val o = JSONObject(Nucleo.instrumentsJson())
        val t = o.getJSONArray("tipos")
        fun lista(a: JSONArray) = List(a.length()) { a.getString(it) }
        InfoInstrumentos(
            List(t.length()) {
                val x = t.getJSONObject(it)
                val aj = x.getJSONArray("ajustes")
                TipoInstrumento(x.getString("nombre"), lista(x.getJSONArray("presets")), List(aj.length()) { k -> aj.getInt(k) }, x.optInt("inicio"))
            },
            lista(o.getJSONArray("piezas")), o.optInt("pasoPulsos", 24), o.optInt("pasosCompas", 16),
            o.optInt("maxCompases", 16), o.optInt("maxVeces", 64),
            o.optJSONArray("estilos")?.let { e ->
                List(e.length()) {
                    val x = e.getJSONObject(it)
                    val p = x.getJSONArray("partes")
                    Estilo(
                        x.getString("nombre"), x.optString("detalle"), x.optInt("bpm"), x.optInt("tonica"),
                        x.optBoolean("menor"), List(p.length()) { k -> p.getInt(k) },
                    )
                }
            } ?: emptyList(),
        )
    }
    val infoAjustesInst: FloatArray by lazy { Nucleo.instParamInfo() }
    /** El instrumento que se está editando (su id; 0 = ninguno). */
    var instrumentoAbierto by mutableIntStateOf(0)
    /** Dónde va la canción en el patrón del instrumento abierto (pasos; -1 = no suena). Cambia en cada cuadro. */
    var pasoAhora by mutableFloatStateOf(-1f)
        private set
    var grabandoNotas by mutableStateOf(false)
        private set
    private var vMusica = -1
    private val bufMusica = FloatArray(Nucleo.DATOS_MUSICA)
    private var creandoVisto: Pair<Boolean, Int>? = null

    /** La canción preparada para cantar y cómo va la separación de la voz. */
    val cancion = Cancion()
    val separacion = Separacion()
    /** Esta compilación trae el modelo para quitar la voz. */
    var modeloVoz by mutableStateOf(false)
        private set
    private var vCancion = -1
    private val bufSeparar = FloatArray(Nucleo.DATOS_SEPARAR)

    /** El escenario (cantar en vivo): la presentación que se graba y la cuenta para entrar. */
    val presentacion = Presentacion()
    val cuenta = Cuenta()
    /** El escenario está a la vista (la cuenta se lee en cada cuadro). */
    var escenario = false
    private val bufShow = FloatArray(Nucleo.DATOS_PRESENTACION)
    private val bufCue = FloatArray(Nucleo.DATOS_CUE)

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
    private var guardandoPresentacion = false

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
            leerPresentacion()
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
            if (p.esInstrumento) {
                val vi = versiones[Nucleo.verInst(p.slot)]
                if (vi != p.vInst) { p.vInst = vi; leerPatron(p) }
            }
        }
        val vm = versiones[Nucleo.VER_MUSICA]
        if (vm != vMusica) { vMusica = vm; leerMusica() }
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
            val vieja = pistas.firstOrNull { it.id == id && it.slot == slot }
            vieja?.cuadros = o.optLong("cuadros")
            vieja?.info = o.optString("info")
            nuevas += vieja ?: Pista(
                slot, id, o.optString("nombre"), o.optInt("canales"), o.optLong("cuadros"), o.optString("info"),
                o.optInt("instrumento", -1),
            )
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

    private fun leerPatron(p: Pista) {
        val t = Nucleo.instrumentJson(p.id)
        if (t.isEmpty()) return
        val o = JSONObject(t)
        val aj = o.getJSONArray("ajustes")
        val no = o.getJSONArray("notas")
        p.patron = Patron(
            o.optInt("tipo"), o.optInt("preset"), o.optInt("compases", 4), o.optInt("veces", 4),
            o.optDouble("swing", 0.0).toFloat(), FloatArray(aj.length()) { aj.getDouble(it).toFloat() },
            IntArray(no.length()) { no.getInt(it) }, o.optBoolean("preparando"),
        )
    }

    private fun leerMusica() {
        Nucleo.musicState(bufMusica)
        val m = musica
        m.bpm = bufMusica[0]
        m.tonica = bufMusica[1].toInt()
        m.menor = bufMusica[2] != 0f
        m.metronomo = bufMusica[3] != 0f
        m.bpmDeCancion = bufMusica[4] != 0f
        m.tonicaDeCancion = bufMusica[5] != 0f
        m.preparando = bufMusica[6].toInt()
        m.tonicaAutotune = bufMusica[7].toInt()
        m.menorAutotune = bufMusica[8] != 0f
        grabandoNotas = Nucleo.instRecording()
        // La escala cambió: las filas de la cuadrícula también.
        for (p in pistas) if (p.esInstrumento) p.vInst = -1
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

    private fun leerPresentacion() {
        Nucleo.showState(bufShow)
        val p = presentacion
        p.grabando = bufShow[0] != 0f
        p.segundos = bufShow[1].toInt()
        p.lista = bufShow[2] != 0f
        p.duracion = bufShow[3].toInt()
    }

    private fun leerCuenta() {
        Nucleo.songCue(bufCue)
        val c = cuenta
        c.estado = bufCue[0].toInt()
        c.segundos = ceil(bufCue[1].toDouble()).toInt()
        c.parte = bufCue[2].toInt()
        c.partes = bufCue[3].toInt()
        c.avance = bufCue[4]
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
            if (guardandoPresentacion) {
                guardandoPresentacion = false
                presentacion.guardada = o.optBoolean("ultimaOk")
                presentacion.error = if (presentacion.guardada) "" else estado
            }
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

    // ------------------------------------------------------------------ crear música

    /** Agrega un instrumento (el núcleo lo prepara con una idea para empezar). Devuelve su id o -1. */
    fun agregarInstrumento(tipo: Int, preset: Int = 0): Int {
        val id = Nucleo.addInstrument(tipo, preset)
        sincronizar()
        return id
    }
    /** Agrega los instrumentos de un estilo. Devuelve el id del primero o -1. */
    fun agregarEstilo(indice: Int): Int {
        val id = Nucleo.addStyle(indice)
        sincronizar()
        return id
    }
    fun ponerTempo(bpm: Float) { Nucleo.setTempo(bpm); sincronizar() }
    fun ponerTonalidad(tonica: Int, menor: Boolean) { Nucleo.setKey(tonica, menor); sincronizar() }
    fun ponerMetronomo(on: Boolean) { Nucleo.setMetronome(on); sincronizar() }
    /** Filas de la cuadrícula (de arriba a abajo). */
    fun filas(p: Pista, soloEscala: Boolean): List<Fila> {
        val a = JSONArray(Nucleo.instRowsJson(p.id, soloEscala))
        return List(a.length()) {
            val f = a.getJSONArray(it)
            Fila(f.getInt(0), f.getString(1), f.getInt(2) != 0, f.getInt(3) != 0)
        }
    }
    /** Pone o quita una nota (o un acorde). true = se agregó. */
    fun alternarNota(p: Pista, pulso: Int, nota: Int, largo: Int, acorde: Boolean): Boolean {
        val r = Nucleo.instToggle(p.id, pulso, nota, largo, acorde)
        sincronizar()
        return r == 1
    }
    fun redoble(p: Pista, paso: Int, pieza: Int): Int = Nucleo.instRoll(p.id, paso, pieza).also { sincronizar() }
    fun cambiarNota(p: Pista, indice: Int, pulso: Int, largo: Int, nota: Int, fuerza: Int) {
        Nucleo.instSetNote(p.id, indice, pulso, largo, nota, fuerza)
        sincronizar()
    }
    fun borrarNotas(p: Pista) { Nucleo.instClear(p.id); sincronizar() }
    fun ponerPatron(p: Pista, compases: Int, veces: Int, swing: Float) {
        Nucleo.instSetPattern(p.id, compases, veces, swing)
        sincronizar()
    }
    fun ponerPresetInst(p: Pista, preset: Int) { Nucleo.instSetPreset(p.id, preset); sincronizar() }
    fun ponerAjusteInst(p: Pista, ajuste: Int, valor: Float) { Nucleo.instSetParam(p.id, ajuste, valor); sincronizar() }
    fun otraIdea(p: Pista) { Nucleo.instIdea(p.id); sincronizar() }
    /** Tocar en vivo: apretar (true) o soltar (false). */
    fun tocar(p: Pista, nota: Int, abajo: Boolean, fuerza: Int = 110) = Nucleo.instPlay(p.id, nota, fuerza, abajo)
    fun grabarNotas(p: Pista, on: Boolean) {
        Nucleo.instRecord(p.id, on)
        if (on && !reproduciendo) Nucleo.playPause()
        sincronizar()
    }
    /** La pestaña Crear (o un instrumento abierto) está a la vista; con [repetir] se repite ese patrón. */
    fun creando(on: Boolean, repetirId: Int) {
        val v = on to repetirId
        if (v == creandoVisto) return
        creandoVisto = v
        Nucleo.setCreating(on, repetirId)
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
        if (reproduciendo && grabandoNotas) instrumentoAbierto.takeIf { it > 0 }?.let { Nucleo.instRecord(it, false) }
        Nucleo.playPause()
        reproduciendo = Nucleo.isPlaying()
        sincronizar()
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
    fun activa(): Boolean =
        reproduciendo || (ocupada && !separacion.activa) || moviendo || entrada.abierta || presentacion.grabando

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
        if (presentacion.grabando) leerPresentacion()
        if (escenario) leerCuenta()
        val abierto = instrumentoAbierto
        pasoAhora = if (abierto > 0 && reproduciendo) Nucleo.instPosition(abierto).toFloat() else -1f
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

    // ------------------------------------------------------------------ escenario (cantar en vivo)

    /** Empieza a grabar la presentación (lo que sale hacia la consola) y, si estaba parada, la canción. */
    fun grabarPresentacion() {
        if (Nucleo.showStart()) {
            presentacion.guardada = false
            presentacion.error = ""
            if (!Nucleo.isPlaying()) Nucleo.playPause()
            sincronizar()
        } else {
            sincronizar()
            presentacion.error = estado  // el núcleo dice por qué
        }
    }

    /** Termina la presentación (y la canción, si sigue). Queda lista para guardarla en el teléfono. */
    fun pararPresentacion() {
        Nucleo.showStop()
        if (Nucleo.isPlaying()) Nucleo.playPause()
        sincronizar()
    }

    fun nombrePresentacion(): String =
        "Presentación " + SimpleDateFormat("yyyy-MM-dd HHmm", Locale.US).format(Date()) + ".wav"

    /** El núcleo guarda la última presentación en [uri] (elegido por la persona), con el formato de exportar. */
    fun guardarPresentacion(context: Context, uri: Uri) {
        if (ocupada || !presentacion.lista) return
        presentacion.error = ""
        guardarEn(context, uri, "No se guardó la presentación", { presentacion.error = it }) { fd, nombre ->
            Nucleo.saveShow(fd, nombre).also { guardandoPresentacion = it }
        }
    }

    /** La voz original (guía) de la canción preparada, si sigue en la mesa. */
    fun pistaGuia(): Pista? = if (cancion.voz == 0) null else pistas.firstOrNull { it.id == cancion.voz }

    /** El nombre de la canción preparada (el del instrumental, sin lo que le agregó la app). */
    fun nombreCancion(): String? =
        pistas.firstOrNull { it.id == cancion.instrumental }?.nombre?.removeSuffix(" · instrumental")

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
        guardarEn(context, uri, "No se exportó") { fd, nombre -> Nucleo.exportMix(fd, nombre) }
    }

    /**
     * Abre [uri] para escribir y se lo pasa al núcleo ([escribir] trabaja en segundo plano). Si no
     * se puede o sale mal, se borra el archivo que se creó.
     */
    private fun guardarEn(
        context: Context, uri: Uri, fallo: String, alFallar: ((String) -> Unit)? = null, escribir: (Int, String) -> Boolean,
    ) {
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
                avisar("$fallo: no se pudo crear el archivo")
                alFallar?.invoke(estado)
                return@launch
            }
            exportPendiente = uri
            val empezo = escribir(fd, nombre)
            if (!empezo) {
                exportPendiente = null
                borrarDocumento(uri)
            }
            sincronizar()
            if (!empezo) alFallar?.invoke(estado)
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
