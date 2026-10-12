package com.wavapp.mezcla

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlin.math.max

/**
 * Lo que la pantalla de edición necesita recordar: qué pedazo está elegido, el zoom y el
 * desplazamiento, si el imán está prendido y el último mensaje. Las operaciones (mover,
 * recortar, fundidos, cortar, borrar, deshacer/rehacer) las hace el núcleo.
 */
object Edicion {
    data class Seleccion(val pistaId: Int, val indice: Int)

    var abierta by mutableStateOf(false)
    var seleccion by mutableStateOf<Seleccion?>(null)
    var iman by mutableStateOf(true)
    var mensaje by mutableStateOf("")

    // Vista de la línea de tiempo: segundos a la izquierda y segundos que caben a lo ancho.
    var vistaInicio by mutableDoubleStateOf(0.0)
    var vistaAncho by mutableDoubleStateOf(0.0)  // 0 = ajustar a la canción

    /** Hay un dedo arrastrando en la línea de tiempo (la vista no sigue al cursor). */
    var tocando = false

    val puedeDeshacer: Boolean get() = Mesa.puedeDeshacer
    val puedeRehacer: Boolean get() = Mesa.puedeRehacer

    fun seleccionado(): Pair<Pista, Int>? {
        val s = seleccion ?: return null
        val p = Mesa.pistas.firstOrNull { it.id == s.pistaId } ?: return null
        return if (s.indice in p.pedazos.indices) p to s.indice else null
    }

    // ------------------------------------------------------------------ arrastres

    fun empezar(p: Pista, indice: Int, modo: Int): Boolean = Nucleo.editBegin(p.id, indice, modo)

    /** [delta] cuadros desde que empezó el arrastre; el imán pega a [umbral] cuadros o menos. */
    fun arrastrar(delta: Long, umbral: Long) {
        Nucleo.editDrag(delta, if (iman) umbral else 0L)
        Mesa.sincronizar()
    }

    fun terminar() {
        Nucleo.editEnd()
        Mesa.sincronizar()
    }

    // ------------------------------------------------------------------ botones

    fun deshacer() {
        Nucleo.undo()
        Mesa.sincronizar()
        validarSeleccion()
    }

    fun rehacer() {
        Nucleo.redo()
        Mesa.sincronizar()
        validarSeleccion()
    }

    /**
     * Corta el pedazo elegido en el cursor. Sin pedazo elegido, corta todos los que estén bajo el
     * cursor (en todas las pistas), como una sola acción para deshacer.
     */
    fun cortarEnCursor() {
        val sel = seleccionado()
        val r = Nucleo.cut(sel?.first?.id ?: -1, sel?.second ?: -1)
        Mesa.sincronizar()
        mensaje = when {
            sel != null && r < 0 -> "Pon el cursor dentro del pedazo elegido (a más de 50 ms de sus bordes) para cortarlo."
            sel != null -> { seleccion = Seleccion(sel.first.id, r); "Cortado en el cursor." }
            r == 0 -> "No hay ningún pedazo bajo el cursor para cortar."
            r == 1 -> "Cortado en el cursor."
            else -> "$r pedazos cortados en el cursor."
        }
    }

    fun borrarSeleccionado() {
        val (p, i) = seleccionado() ?: return
        Nucleo.deleteRegion(p.id, i)
        Mesa.sincronizar()
        seleccion = null
        mensaje = "Pedazo borrado (Deshacer lo regresa)."
    }

    /** Mueve el pedazo elegido unos milisegundos (ajuste fino). */
    fun empujar(ms: Int) {
        val (p, i) = seleccionado() ?: return
        Nucleo.nudgeRegion(p.id, i, ms)
        Mesa.sincronizar()
    }

    /** Cambia el fundido de entrada ([entrada]) o de salida del pedazo elegido. */
    fun ponerFadeSeleccionado(entrada: Boolean, cuadros: Long) {
        val (p, i) = seleccionado() ?: return
        Nucleo.setRegionFade(p.id, i, entrada, cuadros)
        Mesa.sincronizar()
    }

    /** Regresa la pista a su archivo completo desde 0, sin cortes. */
    fun restaurarPista(p: Pista) {
        Nucleo.restoreRegions(p.id)
        Mesa.sincronizar()
        if (seleccion?.pistaId == p.id) seleccion = null
        mensaje = "Pista ${p.id} como el archivo original."
    }

    private fun validarSeleccion() {
        if (seleccionado() == null) seleccion = null
    }

    internal fun alCambiarPistas() = validarSeleccion()

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
