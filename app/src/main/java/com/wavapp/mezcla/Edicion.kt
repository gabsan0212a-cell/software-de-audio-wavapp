package com.wavapp.mezcla

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToLong

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
 * Edición: mover, recortar, fundidos, cortar y borrar pedazos, con deshacer/rehacer.
 * Las operaciones son funciones puras sobre [Pedazo]; aquí se aplican y se recuerdan.
 */
object Edicion {
    const val LARGO_MIN = 2400L  // 50 ms: un pedazo no puede quedar más corto
    const val MAX_PEDAZOS = 256
    private const val MAX_HISTORIAL = 100

    data class Seleccion(val pistaId: Int, val indice: Int)

    var abierta by mutableStateOf(false)
    var seleccion by mutableStateOf<Seleccion?>(null)
    var iman by mutableStateOf(true)
    var mensaje by mutableStateOf("")

    // Vista de la línea de tiempo: segundos a la izquierda y segundos que caben a lo ancho.
    var vistaInicio by mutableDoubleStateOf(0.0)
    var vistaAncho by mutableDoubleStateOf(0.0)  // 0 = ajustar a la canción

    private val deshacerPila = ArrayDeque<Map<Int, List<Pedazo>>>()
    private val rehacerPila = ArrayDeque<Map<Int, List<Pedazo>>>()
    var puedeDeshacer by mutableStateOf(false)
        private set
    var puedeRehacer by mutableStateOf(false)
        private set
    /** Cambia con cada edición (para redibujar). */
    var version by mutableIntStateOf(0)
        private set

    private var antesDelGesto: Map<Int, List<Pedazo>>? = null
    /** Hay un dedo arrastrando en la línea de tiempo (la vista no sigue al cursor). */
    var tocando = false
    private var ultimaClave = ""
    private var ultimaVez = 0L

    // ------------------------------------------------------------------ operaciones puras

    fun mover(pz: Pedazo, inicio: Long): Pedazo = pz.copy(inicio = max(0L, inicio))

    /** Mueve el borde izquierdo a [nuevo] (el final queda donde estaba). */
    fun recortarInicio(pz: Pedazo, nuevo: Long): Pedazo {
        val minimo = max(pz.inicio - pz.origen, 0L)  // no antes del principio del archivo
        val n = nuevo.coerceIn(minimo, max(minimo, pz.fin - min(LARGO_MIN, pz.largo)))
        val d = n - pz.inicio
        val largo = pz.largo - d
        return ajustarFades(pz.copy(inicio = n, origen = pz.origen + d, largo = largo))
    }

    /** Mueve el borde derecho a [nuevo] (sin pasar del final del archivo de [cuadros]). */
    fun recortarFin(pz: Pedazo, nuevo: Long, cuadros: Long): Pedazo {
        val maximo = pz.inicio + (cuadros - pz.origen)
        val n = nuevo.coerceIn(pz.inicio + min(LARGO_MIN, pz.largo), maximo)
        return ajustarFades(pz.copy(largo = n - pz.inicio))
    }

    fun ponerFades(pz: Pedazo, fadeIn: Long, fadeOut: Long): Pedazo {
        val fi = fadeIn.coerceIn(0L, pz.largo)
        val fo = fadeOut.coerceIn(0L, pz.largo - fi)
        return pz.copy(fadeIn = fi, fadeOut = fo)
    }

    private fun ajustarFades(pz: Pedazo): Pedazo {
        val fi = min(pz.fadeIn, pz.largo)
        return pz.copy(fadeIn = fi, fadeOut = min(pz.fadeOut, pz.largo - fi))
    }

    /** Corta en el cuadro [en] de la línea de tiempo. null si queda un lado demasiado corto. */
    fun cortar(pz: Pedazo, en: Long): Pair<Pedazo, Pedazo>? {
        if (en < pz.inicio + LARGO_MIN || en > pz.fin - LARGO_MIN) return null
        val izq = en - pz.inicio
        val a = pz.copy(largo = izq, fadeIn = min(pz.fadeIn, izq), fadeOut = 0)
        val b = Pedazo(en, pz.origen + izq, pz.largo - izq, 0, min(pz.fadeOut, pz.largo - izq))
        return a to b
    }

    // ------------------------------------------------------------------ imán

    /** Puntos a los que se pega lo que se arrastra: inicio, cursor y bordes de los demás pedazos. */
    fun puntosIman(excluir: Seleccion?): List<Long> {
        val pts = mutableListOf(0L, (Mesa.posicion * Mesa.RATE).roundToLong())
        for (p in Mesa.pistas) p.pedazos.forEachIndexed { i, pz ->
            if (excluir == null || excluir.pistaId != p.id || excluir.indice != i) {
                pts += pz.inicio
                pts += pz.fin
            }
        }
        return pts
    }

    /** El punto más cercano a alguno de [bordes] dentro de [umbral]: devuelve el corrimiento (0 si nada). */
    fun corrimientoIman(bordes: List<Long>, puntos: List<Long>, umbral: Long): Long {
        if (!iman) return 0L
        var mejor = 0L
        var dist = umbral + 1
        for (b in bordes) for (p in puntos) {
            val d = abs(p - b)
            if (d < dist) {
                dist = d
                mejor = p - b
            }
        }
        return if (dist <= umbral) mejor else 0L
    }

    // ------------------------------------------------------------------ aplicar y recordar

    private fun foto(): Map<Int, List<Pedazo>> = Mesa.pistas.associate { it.id to it.pedazos }

    private fun recordar(antes: Map<Int, List<Pedazo>>) {
        deshacerPila.addLast(antes)
        while (deshacerPila.size > MAX_HISTORIAL) deshacerPila.removeFirst()
        rehacerPila.clear()
        estadoPilas()
    }

    private fun estadoPilas() {
        puedeDeshacer = deshacerPila.isNotEmpty()
        puedeRehacer = rehacerPila.isNotEmpty()
    }

    /**
     * Cambia los pedazos de una pista y lo deja en el historial. Con [clave], los cambios
     * seguidos de lo mismo (un deslizador, los toques de mover) cuentan como uno solo.
     */
    fun cambiar(p: Pista, nuevos: List<Pedazo>, clave: String = "") {
        if (nuevos == p.pedazos) return
        val antes = foto()
        val ahora = System.currentTimeMillis()
        val junto = clave.isNotEmpty() && clave == ultimaClave && ahora - ultimaVez < 1500
        ultimaClave = clave
        ultimaVez = ahora
        if (antesDelGesto == null && !junto) recordar(antes)
        Mesa.ponerPedazos(p, nuevos)
        version++
    }

    /** Un arrastre: todo lo que cambie hasta [terminarGesto] se deshace de una vez. */
    fun empezarGesto() {
        antesDelGesto = foto()
        ultimaClave = ""
    }

    fun terminarGesto() {
        val antes = antesDelGesto ?: return
        antesDelGesto = null
        if (antes != foto()) recordar(antes)
    }

    fun deshacer() {
        val f = deshacerPila.removeLastOrNull() ?: return
        rehacerPila.addLast(foto())
        aplicarFoto(f)
    }

    fun rehacer() {
        val f = rehacerPila.removeLastOrNull() ?: return
        deshacerPila.addLast(foto())
        aplicarFoto(f)
    }

    private fun aplicarFoto(f: Map<Int, List<Pedazo>>) {
        for (p in Mesa.pistas) f[p.id]?.let { if (it != p.pedazos) Mesa.ponerPedazos(p, it) }
        ultimaClave = ""
        estadoPilas()
        validarSeleccion()
        version++
    }

    private fun validarSeleccion() {
        val s = seleccion ?: return
        val p = Mesa.pistas.firstOrNull { it.id == s.pistaId }
        if (p == null || s.indice !in p.pedazos.indices) seleccion = null
    }

    fun olvidar(p: Pista) {
        if (seleccion?.pistaId == p.id) seleccion = null
    }

    /** Al cargar otra mezcla el historial ya no aplica. */
    fun reiniciarHistorial() {
        deshacerPila.clear()
        rehacerPila.clear()
        estadoPilas()
        seleccion = null
    }

    fun seleccionado(): Pair<Pista, Int>? {
        val s = seleccion ?: return null
        val p = Mesa.pistas.firstOrNull { it.id == s.pistaId } ?: return null
        return if (s.indice in p.pedazos.indices) p to s.indice else null
    }

    // ------------------------------------------------------------------ acciones de los botones

    private fun cursor(): Long = (Mesa.posicion * Mesa.RATE).roundToLong()

    /**
     * Corta el pedazo elegido en el cursor. Sin pedazo elegido, corta todos los que estén bajo el
     * cursor (en todas las pistas), como una sola acción para deshacer.
     */
    fun cortarEnCursor() {
        val t = cursor()
        val sel = seleccionado()
        val antes = foto()
        var cortes = 0
        val objetivos = if (sel != null) listOf(sel) else
            Mesa.pistas.flatMap { p -> p.pedazos.indices.filter { i -> p.pedazos[i].inicio < t && p.pedazos[i].fin > t }.map { p to it } }
        var nuevaSel: Seleccion? = null
        for ((p, i) in objetivos.groupBy({ it.first }, { it.second })) {
            val lista = p.pedazos.toMutableList()
            for (idx in i) {
                if (lista.size >= MAX_PEDAZOS) break
                val (a, b) = cortar(lista[idx], t) ?: continue
                lista[idx] = a
                lista.add(b)
                cortes++
                if (sel != null) nuevaSel = Seleccion(p.id, lista.size - 1)
            }
            if (lista != p.pedazos) Mesa.ponerPedazos(p, lista)
        }
        if (cortes == 0) {
            mensaje = if (sel != null) "Pon el cursor dentro del pedazo elegido (a más de 50 ms de sus bordes) para cortarlo."
            else "No hay ningún pedazo bajo el cursor para cortar."
            return
        }
        recordar(antes)
        ultimaClave = ""
        if (nuevaSel != null) seleccion = nuevaSel
        mensaje = if (cortes == 1) "Cortado en el cursor." else "$cortes pedazos cortados en el cursor."
        version++
    }

    fun borrarSeleccionado() {
        val (p, i) = seleccionado() ?: return
        ultimaClave = ""
        cambiar(p, p.pedazos.filterIndexed { k, _ -> k != i })
        seleccion = null
        mensaje = "Pedazo borrado (Deshacer lo regresa)."
    }

    /** Mueve el pedazo elegido unos milisegundos (ajuste fino). */
    fun empujar(ms: Int) {
        val (p, i) = seleccionado() ?: return
        val lista = p.pedazos.toMutableList()
        lista[i] = mover(lista[i], lista[i].inicio + ms * 48L)
        cambiar(p, lista, clave = "empujar-${p.id}-$i")
    }

    /** Cambia el fundido de entrada ([entrada]) o de salida del pedazo elegido. */
    fun ponerFadeSeleccionado(entrada: Boolean, cuadros: Long) {
        val (p, i) = seleccionado() ?: return
        val lista = p.pedazos.toMutableList()
        val pz = lista[i]
        // Uno no empuja al otro: cada fundido llega hasta donde empieza el otro.
        lista[i] = if (entrada) ponerFades(pz, min(cuadros, pz.largo - pz.fadeOut), pz.fadeOut)
        else ponerFades(pz, pz.fadeIn, cuadros)
        cambiar(p, lista, clave = (if (entrada) "fi" else "fo") + "-${p.id}-$i")
    }

    /** Regresa la pista a su archivo completo desde 0, sin cortes. */
    fun restaurarPista(p: Pista) {
        ultimaClave = ""
        cambiar(p, listOf(Pedazo(0, 0, p.cuadros)))
        if (seleccion?.pistaId == p.id) seleccion = null
        mensaje = "Pista ${p.id} como el archivo original."
    }

    // ------------------------------------------------------------------ vista

    fun anchoVista(): Double = if (vistaAncho > 0) vistaAncho else max(1.0, Mesa.duracion * 1.05)

    fun ajustarVista() {
        vistaAncho = 0.0
        vistaInicio = 0.0
    }

    fun acercar(factor: Double, centro: Double) {
        val ancho = anchoVista()
        val nuevo = (ancho / factor).coerceIn(0.25, max(60.0, Mesa.duracion * 2))
        vistaAncho = nuevo
        ponerInicio(centro - (centro - vistaInicio) * nuevo / ancho)
    }

    fun ponerInicio(s: Double) {
        vistaInicio = s.coerceIn(0.0, max(0.0, Mesa.duracion + anchoVista() * 0.5 - anchoVista()))
    }

    /** Mientras suena, la vista sigue al cursor. */
    fun seguirCursor() {
        val pos = Mesa.posicion
        val ancho = anchoVista()
        if (pos < vistaInicio || pos > vistaInicio + ancho) ponerInicio(pos - ancho * 0.1)
    }
}
