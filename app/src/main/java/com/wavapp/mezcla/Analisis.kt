package com.wavapp.mezcla

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.abs

/**
 * Análisis en vivo (como el panel "Diagnóstico" del prototipo): espectro, goniómetro, fase,
 * paneo y zumbido. El cálculo pesado es nativo; aquí van el suavizado y los textos.
 */
object Analisis {
    const val BINS = 8192           // espectro de 16384 puntos
    const val BINS_FUENTES = 1024   // espectros L/R de 2048 puntos
    const val TIEMPO = 2048
    const val SG_COLS = 1200
    const val SG_FILAS = 256
    const val SG_TOPE = 20000f

    val espectro = FloatArray(BINS) { -200f }
    val fanE = FloatArray(BINS_FUENTES) { -200f }
    val fanP = FloatArray(BINS_FUENTES)
    val tiempo = FloatArray(2 * TIEMPO)  // L y luego R
    private val stats = FloatArray(8)

    /** Cambia con cada análisis nuevo: los dibujos lo leen para rehacerse. */
    var version by mutableIntStateOf(0)
        private set
    /** Hay audio analizado (la mezcla está sonando). */
    var activo by mutableStateOf(false)
        private set

    /** Panel de análisis abierto. */
    var abierto by mutableStateOf(false)
    /** -1 = mezcla completa; si no, id de la pista. */
    var fuenteId by mutableIntStateOf(-1)
    var modoClasico by mutableStateOf(false)
    var notch by mutableIntStateOf(0)
        private set

    // Valores derivados, como diag() y setMeters() del prototipo.
    var correlacion by mutableStateOf(1f)
        private set
    var paneo by mutableStateOf(0f)
        private set
    var fuerte by mutableStateOf(false)
        private set
    var zumbidoHz by mutableIntStateOf(0)
        private set
    var zumbidoArmonicos by mutableIntStateOf(0)
        private set
    var zumbidoDb by mutableIntStateOf(0)
        private set

    /** Espectrogramas ya calculados, por id de pista (no cambian: el archivo no cambia). */
    val espectrogramas = mutableStateMapOf<Int, ByteArray>()
    private val calculando = mutableSetOf<Int>()
    private val scope = MainScope()
    private var cuadro = 0

    fun ponerNotch(hz: Int, guardar: Boolean = true) {
        notch = if (hz == 50 || hz == 60) hz else 0
        NativeAudio.setNotch(notch)
        if (guardar) Mesa.pedirGuardarDesdeFuera()
    }

    /** Se llama en cada cuadro de pantalla. */
    fun tick(sonando: Boolean) {
        val eq = Mesa.eqAbierta
        val necesita = abierto || eq != null
        // Qué escucha el analizador: la pista del EQ abierto, o la fuente elegida.
        val fuente = eq?.slot ?: Mesa.pistas.firstOrNull { it.id == fuenteId }?.slot ?: -1
        NativeAudio.setAnalysisSource(fuente)
        if (!necesita) return
        if (!sonando) {
            if (activo) reposo()
            return
        }
        if (++cuadro % 2 != 0) return  // ~30 veces por segundo, como el prototipo
        if (!NativeAudio.analyze(espectro, fanE, fanP, tiempo, stats)) return
        val rl = stats[2]
        val rr = stats[3]
        fuerte = rl + rr > 1e-4f
        correlacion = correlacion * 0.85f + stats[4] * 0.15f
        paneo = if (fuerte) (rr - rl) / (rr + rl) else 0f
        if (cuadro % 8 == 0) {  // el zumbido se revisa ~4 veces por segundo
            zumbidoHz = stats[5].toInt()
            zumbidoArmonicos = stats[6].toInt()
            zumbidoDb = stats[7].toInt()
        }
        activo = true
        version++
    }

    private fun reposo() {
        activo = false
        correlacion = 1f
        paneo = 0f
        fuerte = false
        zumbidoHz = 0
        espectro.fill(-200f)
        fanE.fill(-200f)
        tiempo.fill(0f)
        NativeAudio.analysisReset()
        version++
    }

    /** Veredicto de fase y paneo, con los mismos textos que el prototipo. */
    fun veredicto(): Pair<String, Boolean> {
        if (!activo || !fuerte) return (if (Mesa.reproduciendo) "Silencio." else "Reproduce para medir.") to false
        val c = correlacion
        var malo = false
        var t = when {
            c < 0 -> { malo = true; "Fuera de fase: se cancela en mono. Revisa polaridad o fase entre canales." }
            c < 0.3f -> "Muy ancho: revisa que no pierda cuerpo al sumar a mono."
            c < 0.9f -> "Estéreo en fase: se mantiene bien en mono."
            else -> "Mono o casi mono: fase correcta."
        }
        if (abs(paneo) > 0.25f) {
            t += " Desbalance hacia " + (if (paneo < 0) "L" else "R") + "."
            malo = malo || abs(paneo) > 0.5f
        }
        return t to malo
    }

    fun textoZumbido(): Pair<String, Boolean> {
        if (!activo) return "Zumbido: reproduce para analizar." to false
        if (zumbidoHz > 0) {
            val extra = if (notch == zumbidoHz) "" else " Prueba el notch de $zumbidoHz Hz."
            return "Posible zumbido de $zumbidoHz Hz: $zumbidoArmonicos de 4 armónicos marcados, unos +$zumbidoDb dB " +
                "sobre el piso.$extra" to true
        }
        return (if (notch > 0) "Sin zumbido a la salida con el notch de $notch Hz."
        else "Sin zumbido evidente en 50/60 Hz ni armónicos.") to false
    }

    /** Pide el espectrograma de la pista (se calcula una vez, en segundo plano). */
    fun pedirEspectrograma(p: Pista) {
        if (espectrogramas.containsKey(p.id) || !calculando.add(p.id)) return
        scope.launch {
            val datos = withContext(Dispatchers.Default) { NativeAudio.spectrogram(p.slot, SG_COLS, SG_FILAS) }
            calculando.remove(p.id)
            if (datos != null && Mesa.pistas.any { it.id == p.id }) espectrogramas[p.id] = datos
        }
    }

    fun olvidar(p: Pista) {
        espectrogramas.remove(p.id)
        if (fuenteId == p.id) fuenteId = -1
    }
}
