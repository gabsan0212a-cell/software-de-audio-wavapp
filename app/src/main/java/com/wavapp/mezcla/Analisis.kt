package com.wavapp.mezcla

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * Lo que muestra el panel de análisis. Todo se calcula en el núcleo (espectro, fase, paneo,
 * veredicto, zumbido, goniómetro); aquí solo se guarda lo último que llegó y los textos.
 */
object Analisis {
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
    val notch: Int get() = Mesa.notch

    var correlacion by mutableFloatStateOf(1f)
        private set
    var paneo by mutableFloatStateOf(0f)
        private set
    var fuerte by mutableStateOf(false)
        private set
    var veredicto by mutableIntStateOf(0)  // 0 nada, 1 fuera de fase, 2 muy ancho, 3 estéreo, 4 mono
        private set
    var desbalance by mutableIntStateOf(0)  // -1 L, 0, 1 R
        private set
    var malo by mutableStateOf(false)
        private set
    var zumbidoHz by mutableIntStateOf(0)
        private set
    var zumbidoArmonicos by mutableIntStateOf(0)
        private set
    var zumbidoDb by mutableIntStateOf(0)
        private set
    var gananciaGonio by mutableIntStateOf(0)
        private set

    private val stats = FloatArray(Nucleo.DATOS_ANALISIS)

    /** Espectrogramas que ya llegaron del núcleo, por id de pista. */
    val espectrogramas = mutableStateMapOf<Int, ByteArray>()
    private val pedidos = mutableSetOf<Int>()

    fun ponerNotch(hz: Int) = Mesa.ponerNotch(hz)

    /** Se llama en cada cuadro de pantalla. */
    fun tick(ahoraMs: Long) {
        val eq = Mesa.eqAbierta
        // Qué escucha el analizador: la pista del EQ abierto, o la fuente elegida.
        Nucleo.setAnalysisSource(eq?.id ?: (Mesa.pistas.firstOrNull { it.id == fuenteId }?.id ?: -1))
        if (!abierto && eq == null) return
        if (!Nucleo.analysis(ahoraMs, modoClasico, stats)) return
        activo = stats[0] != 0f
        fuerte = stats[1] != 0f
        correlacion = stats[2]
        paneo = stats[3]
        veredicto = stats[4].toInt()
        desbalance = stats[5].toInt()
        malo = stats[6] != 0f
        zumbidoHz = stats[7].toInt()
        zumbidoArmonicos = stats[8].toInt()
        zumbidoDb = stats[9].toInt()
        gananciaGonio = Math.round(stats[10])
        version++
    }

    /** Veredicto de fase y paneo, con los mismos textos que el prototipo. */
    fun veredicto(): Pair<String, Boolean> {
        if (veredicto == 0) return (if (Mesa.reproduciendo) "Silencio." else "Reproduce para medir.") to false
        var t = when (veredicto) {
            1 -> "Fuera de fase: se cancela en mono. Revisa polaridad o fase entre canales."
            2 -> "Muy ancho: revisa que no pierda cuerpo al sumar a mono."
            3 -> "Estéreo en fase: se mantiene bien en mono."
            else -> "Mono o casi mono: fase correcta."
        }
        if (desbalance != 0) t += " Desbalance hacia " + (if (desbalance < 0) "L" else "R") + "."
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

    /** Pide el espectrograma de la pista (el núcleo lo calcula una vez, en segundo plano). */
    fun pedirEspectrograma(p: Pista) {
        if (espectrogramas.containsKey(p.id)) return
        pedidos += p.id
        Nucleo.spectrogram(p.id)?.let {
            espectrogramas[p.id] = it
            pedidos -= p.id
        }
    }

    /** Terminó algo en segundo plano: los espectrogramas pedidos que ya estén listos. */
    internal fun alCambiarFondo() {
        for (id in pedidos.toList()) {
            Nucleo.spectrogram(id)?.let {
                espectrogramas[id] = it
                pedidos -= id
            }
        }
    }

    internal fun alCambiarPistas() {
        val ids = Mesa.pistas.map { it.id }.toSet()
        espectrogramas.keys.filter { it !in ids }.forEach { espectrogramas.remove(it) }
        pedidos.retainAll(ids)
        if (fuenteId != -1 && fuenteId !in ids) fuenteId = -1
    }
}
