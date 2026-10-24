package com.wavapp.mezcla.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.Voz
import java.util.Locale
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.roundToInt

/** La escala en uso, para leerla: "La menor · de la canción", "Todas las notas"... */
internal fun textoEscalaVoz(v: Voz): String = when {
    v.deCancion && v.tonica < 0 -> "Todas las notas (sin canción preparada)"
    v.tonica < 0 || v.escala == 2 -> "Todas las notas"
    v.deCancion -> nombreTonalidad(v.tonica, v.escala == 1) + " · de la canción"
    else -> nombreTonalidad(v.tonica, v.escala == 1)
}

/** Lo que está prendido, en una línea. */
internal fun resumenVoz(v: Voz): String {
    val on = listOfNotNull(
        "Auto-tune".takeIf { v.on(Nucleo.VOZ_AUTOTUNE) },
        "Compresor".takeIf { v.on(Nucleo.VOZ_COMP) },
        "Doblaje".takeIf { v.on(Nucleo.VOZ_DOBLAJE) },
        "Delay".takeIf { v.on(Nucleo.VOZ_DELAY) },
        "Reverb".takeIf { v.on(Nucleo.VOZ_REVERB) },
    )
    return if (on.isEmpty()) "Sin efectos" else on.joinToString(" · ")
}

/** En la pestaña Grabar: prender la voz en vivo, el afinador y los presets. */
@Composable
internal fun TarjetaVozEnVivo(salida: Salida) {
    val pal = LocalPaleta.current
    val v = Mesa.voz
    Tarjeta(Modifier.padding(horizontal = 16.dp), PaddingValues(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 6.dp)) {
        FilaInterruptor(
            "Voz en vivo", v.encendida, { Mesa.prenderVoz(it, salida != Salida.BOCINA) }, Modifier.testTag("vozEnVivo"),
        ) {
            Texto(
                if (v.encendida) resumenVoz(v) else "Auto-tune y efectos para cantar sobre la pista",
                Tipo.nota, pal.texto2, lineas = 1,
            )
        }
        if (v.encendida) {
            Separador()
            Afinador(Modifier.fillMaxWidth().height(84.dp).padding(vertical = 10.dp))
            PresetsVoz()
            Separador(Modifier.padding(top = 10.dp))
            FilaBoton("Ajustes de la voz…", { Vista.voz = true }, pal.acento)
            Separador()
            FilaBoton("Cantar en vivo…", { Vista.escenario = true }, pal.acento)
        }
    }
}

@Composable
internal fun PresetsVoz() {
    val pal = LocalPaleta.current
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).testTag("presetsVoz"),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Mesa.presetsVoz.forEachIndexed { i, nombre ->
            Chip(nombre, false, { Mesa.aplicarPresetVoz(i) }, colorInactivo = pal.acento, descripcion = "Preset de voz $nombre")
        }
    }
}

/**
 * Afinador: la nota de la escala a la que va la voz (grande) y una regla de ±50 cents con lo que
 * se canta (círculo vacío) y como sale después del auto-tune (punto; verde si queda afinado).
 * Todo se lee al dibujar.
 */
@Composable
internal fun Afinador(modifier: Modifier, grande: Boolean = false) {
    val pal = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    val v = Mesa.voz
    val estiloNota = TextStyle(color = pal.texto, fontSize = if (grande) 40.sp else 30.sp, fontWeight = FontWeight.Bold)
    val estiloOctava = TextStyle(color = pal.texto2, fontSize = if (grande) 15.sp else 13.sp)
    val estiloRegla = TextStyle(color = pal.texto3, fontSize = 11.sp)
    Canvas(modifier.semantics { contentDescription = "Afinador" }) {
        val objetivo = v.objetivo
        val hay = objetivo >= 0 && v.nota > 0f
        // La nota a la izquierda.
        val izq = (if (grande) 92.dp else 76.dp).toPx()
        val nombre = if (hay) NOTAS[objetivo % 12] else "—"
        val tn = medidor.measure(nombre, estiloNota)
        drawText(tn, topLeft = Offset(0f, size.height / 2 - tn.size.height / 2 - 4.dp.toPx()))
        if (hay) {
            val to = medidor.measure("${objetivo / 12 - 1}", estiloOctava)
            drawText(to, topLeft = Offset(tn.size.width + 3.dp.toPx(), size.height / 2 + tn.size.height / 2 - to.size.height - 8.dp.toPx()))
        }
        // La regla: −50 a +50 cents.
        val x0 = izq + 8.dp.toPx()
        val x1 = size.width - 10.dp.toPx()
        val cy = size.height / 2 - 6.dp.toPx()
        fun x(cents: Float) = x0 + (x1 - x0) * ((cents.coerceIn(-60f, 60f) + 60f) / 120f)
        val alto = 4.dp.toPx()
        drawRoundRect(pal.relleno, Offset(x0, cy - alto / 2), Size(x1 - x0, alto), CornerRadius(alto / 2))
        // Zona afinada (±5 cents).
        drawRoundRect(pal.verde.copy(alpha = 0.35f), Offset(x(-5f), cy - alto), Size(x(5f) - x(-5f), 2 * alto), CornerRadius(alto))
        for (c in listOf(-50f, -25f, 0f, 25f, 50f)) {
            val h = if (c == 0f) 18.dp.toPx() else 10.dp.toPx()
            drawLine(pal.texto3, Offset(x(c), cy - h / 2), Offset(x(c), cy + h / 2), 1.dp.toPx())
        }
        for ((c, t) in listOf(-50f to "−50", 0f to "0", 50f to "+50")) {
            val tr = medidor.measure(t, estiloRegla)
            drawText(tr, topLeft = Offset(x(c) - tr.size.width / 2, cy + 12.dp.toPx()))
        }
        if (!hay) return@Canvas
        val entra = (v.nota - objetivo) * 100f
        val sale = (v.sale - objetivo) * 100f
        drawCircle(pal.texto2, 7.dp.toPx(), Offset(x(entra), cy), style = Stroke(2.dp.toPx()))
        drawCircle(if (abs(sale) < 5f) pal.verde else pal.acento, 6.dp.toPx(), Offset(x(sale), cy))
    }
}

/** Hoja con todos los ajustes de la voz en vivo. */
@Composable
fun HojaVoz(visible: Boolean, alCerrar: () -> Unit) {
    Hoja(
        visible = visible,
        alCerrar = alCerrar,
        titulo = "Voz en vivo",
        alta = true,
        derecha = { BotonTexto("Listo", alCerrar, estilo = Tipo.encabezado) },
    ) {
        ContenidoVoz()
    }
}

@Composable
private fun ContenidoVoz() {
    val pal = LocalPaleta.current
    val v = Mesa.voz
    val e = Mesa.entrada
    Tarjeta(relleno = PaddingValues(horizontal = 16.dp, vertical = 6.dp)) {
        FilaInterruptor("Voz en vivo", v.encendida, { Mesa.prenderVoz(it, true) }) {
            Texto(
                if (!e.abierta) "La entrada está cerrada" else "Sale por la consola con la pista",
                Tipo.nota, pal.texto2, lineas = 1,
            )
        }
        Afinador(Modifier.fillMaxWidth().height(96.dp).padding(vertical = 10.dp), grande = true)
    }
    EncabezadoGrupo("Presets")
    PresetsVoz()
    PieGrupo("\"Paraíso\" suena como la canción: afinación rápida, doblaje y eco a tiempo. Después ajusta lo que quieras.")

    // ---------- Auto-tune
    EncabezadoGrupo("Auto-tune")
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 16.dp, top = 2.dp, bottom = 10.dp)) {
        FilaInterruptor("Auto-tune", v.on(Nucleo.VOZ_AUTOTUNE), { Mesa.alternarVoz(Nucleo.VOZ_AUTOTUNE) }) {
            Texto(textoEscalaVoz(v), Tipo.nota, pal.texto2, lineas = 1)
        }
        if (v.on(Nucleo.VOZ_AUTOTUNE)) {
            Separador()
            Texto("Escala", Tipo.subtitulo, pal.texto, Modifier.padding(top = 10.dp, bottom = 6.dp))
            Segmentado(
                listOf("Canción", "Mayor", "Menor", "Todas"), v.valor(Nucleo.VOZ_ESCALA).roundToInt(),
                { Mesa.ponerVoz(Nucleo.VOZ_ESCALA, it.toFloat()) }, Modifier.fillMaxWidth().testTag("escalaVoz"),
            )
            val escala = v.valor(Nucleo.VOZ_ESCALA).roundToInt()
            if (escala == Nucleo.ESCALA_MAYOR || escala == Nucleo.ESCALA_MENOR) {
                val tonica = v.valor(Nucleo.VOZ_TONICA).roundToInt()
                Row(
                    Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(top = 10.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    NOTAS.forEachIndexed { i, n -> Chip(n, i == tonica, { Mesa.ponerVoz(Nucleo.VOZ_TONICA, i.toFloat()) }) }
                }
            }
            AjusteVoz(Nucleo.VOZ_VELOCIDAD, "Velocidad") { if (it < 0.5f) "Al instante (robot)" else "${it.roundToInt()} ms" }
            AjusteVoz(Nucleo.VOZ_AFINAR, "Cuánto corrige", ::textoPorcientoVoz)
            Texto("Tu voz", Tipo.subtitulo, pal.texto, Modifier.padding(top = 12.dp, bottom = 6.dp))
            Segmentado(
                listOf("Grave", "Media", "Aguda"), v.valor(Nucleo.VOZ_REGISTRO).roundToInt(),
                { Mesa.ponerVoz(Nucleo.VOZ_REGISTRO, it.toFloat()) }, Modifier.fillMaxWidth(),
            )
        }
    }
    PieGrupo(
        "Lleva cada nota a la más cercana de la escala. Rápido suena moderno (al instante, efecto robot); " +
            "lento deja pasar tu vibrato y suena natural.",
    )

    // ---------- Limpieza
    EncabezadoGrupo("Limpieza")
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 16.dp, top = 6.dp, bottom = 2.dp)) {
        AjusteVoz(Nucleo.VOZ_GRAVES, "Quitar graves debajo de") { "${it.roundToInt()} Hz" }
        Separador(Modifier.padding(top = 10.dp))
        FilaInterruptor("Compuerta", v.on(Nucleo.VOZ_COMPUERTA), { Mesa.alternarVoz(Nucleo.VOZ_COMPUERTA) }) {
            Texto("Baja el ruido cuando no cantas", Tipo.nota, pal.texto2, lineas = 1)
        }
        if (v.on(Nucleo.VOZ_COMPUERTA)) AjusteVoz(Nucleo.VOZ_COMPUERTA_UMBRAL, "Abre arriba de", ::textoDbVoz)
        Separador(Modifier.padding(top = 10.dp))
        FilaInterruptor("De-esser", v.on(Nucleo.VOZ_DEESSER), { Mesa.alternarVoz(Nucleo.VOZ_DEESSER) }) {
            MedidorReduccion({ v.deEsser }, Modifier.padding(top = 4.dp).width(140.dp).height(14.dp))
        }
        if (v.on(Nucleo.VOZ_DEESSER)) {
            AjusteVoz(Nucleo.VOZ_DEESSER_UMBRAL, "Baja las \"eses\" arriba de", ::textoDbVoz)
            Spacer(Modifier.height(8.dp))
        }
    }

    // ---------- Compresor
    EncabezadoGrupo("Compresor")
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 16.dp, top = 2.dp, bottom = 2.dp)) {
        FilaInterruptor("Compresor", v.on(Nucleo.VOZ_COMP), { Mesa.alternarVoz(Nucleo.VOZ_COMP) }) {
            MedidorReduccion({ v.compresor }, Modifier.padding(top = 4.dp).width(140.dp).height(14.dp))
        }
        if (v.on(Nucleo.VOZ_COMP)) {
            Separador()
            AjusteVoz(Nucleo.VOZ_COMP_UMBRAL, "Umbral", ::textoDbVoz)
            AjusteVoz(Nucleo.VOZ_COMP_PROPORCION, "Proporción") { String.format(Locale.US, "%.1f:1", it) }
            AjusteVoz(Nucleo.VOZ_COMP_GANANCIA, "Ganancia", ::textoDbVoz)
            Spacer(Modifier.height(8.dp))
        }
    }

    // ---------- Espacio
    EncabezadoGrupo("Espacio")
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 16.dp, top = 2.dp, bottom = 2.dp)) {
        FilaInterruptor("Doblaje", v.on(Nucleo.VOZ_DOBLAJE), { Mesa.alternarVoz(Nucleo.VOZ_DOBLAJE) }) {
            Texto("Como grabar la voz dos veces: más ancha", Tipo.nota, pal.texto2, lineas = 1)
        }
        if (v.on(Nucleo.VOZ_DOBLAJE)) AjusteVoz(Nucleo.VOZ_DOBLAJE_CANTIDAD, "Cantidad", ::textoPorcientoVoz)
        Separador(Modifier.padding(top = 10.dp))
        FilaInterruptor("Delay (eco)", v.on(Nucleo.VOZ_DELAY), { Mesa.alternarVoz(Nucleo.VOZ_DELAY) }) {
            Texto(textoTiempoDelay(v), Tipo.nota.numeros, pal.texto2, lineas = 1)
        }
        if (v.on(Nucleo.VOZ_DELAY)) {
            Segmentado(
                listOf("1/4", "1/8 ·", "1/8", "1/16"), v.valor(Nucleo.VOZ_DELAY_PULSOS).roundToInt(),
                { Mesa.ponerVoz(Nucleo.VOZ_DELAY_PULSOS, it.toFloat()) }, Modifier.fillMaxWidth().padding(top = 4.dp),
            )
            AjusteVoz(Nucleo.VOZ_DELAY_REPETICIONES, "Repeticiones", ::textoPorcientoVoz)
            AjusteVoz(Nucleo.VOZ_DELAY_CANTIDAD, "Cantidad", ::textoPorcientoVoz)
        }
        Separador(Modifier.padding(top = 10.dp))
        FilaInterruptor("Reverb", v.on(Nucleo.VOZ_REVERB), { Mesa.alternarVoz(Nucleo.VOZ_REVERB) })
        if (v.on(Nucleo.VOZ_REVERB)) {
            AjusteVoz(Nucleo.VOZ_REVERB_CANTIDAD, "Cantidad", ::textoPorcientoVoz)
            AjusteVoz(Nucleo.VOZ_REVERB_DURACION, "Duración") { String.format(Locale.US, "%.1f s", it) }
            Spacer(Modifier.height(8.dp))
        }
    }
    PieGrupo("El eco va a tiempo con la canción preparada (con su tempo). Sin canción, a 120 BPM.")

    // ---------- Nivel
    EncabezadoGrupo("Nivel")
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 16.dp, top = 2.dp, bottom = 10.dp)) {
        FilaAjuste("Ganancia del micrófono", textoDbVoz(e.gananciaDb)) {
            Deslizador(
                e.gananciaDb, -24f, 24f, { Mesa.ponerGananciaEntrada(it) }, it, alReiniciar = { Mesa.ponerGananciaEntrada(0f) },
                desde = 0f, descripcion = "Ganancia del micrófono",
            )
        }
        MedidorEntrada(Modifier.fillMaxWidth().padding(top = 6.dp, bottom = 4.dp))
        AjusteVoz(Nucleo.VOZ_VOLUMEN, "Volumen de la voz", ::textoDbVoz)
    }
    PieGrupo(
        if (e.retrasoMs > 0) {
            "Retraso de la entrada y la salida ≈ ${e.retrasoMs} ms, más 1 a 6 ms del auto-tune. Con una interfaz USB " +
                "o la consola por cable es lo mínimo que permite el celular."
        } else {
            "La voz pasa por el limitador del master, igual que la pista: no satura la consola."
        },
    )
}

private fun textoTiempoDelay(v: Voz): String {
    val bpm = Mesa.cancion.bpm.takeIf { it > 0f } ?: 120f
    val pulsos = floatArrayOf(1f, 0.75f, 0.5f, 0.25f)[v.valor(Nucleo.VOZ_DELAY_PULSOS).roundToInt().coerceIn(0, 3)]
    return "${(60000f / bpm * pulsos).roundToInt()} ms · ${bpm.roundToInt()} BPM"
}

/** Un ajuste con su deslizador (rango y escala del núcleo, como en los efectos de las pistas). */
@Composable
private fun AjusteVoz(param: Int, etiqueta: String, texto: (Float) -> String) {
    val info = Mesa.infoVoz
    val v = Mesa.voz
    if (info.size < 4 * (param + 1) || v.valores.size <= param) return
    val min = info[4 * param]
    val max = info[4 * param + 1]
    val inicial = info[4 * param + 2]
    val log = info[4 * param + 3] != 0f
    val valor = v.valor(param)
    fun pos(x: Float) = if (log) ln(x / min) / ln(max / min) else (x - min) / (max - min)
    fun desde(p: Float) = if (log) min * exp(p * ln(max / min)) else min + p * (max - min)
    FilaAjuste(etiqueta, texto(valor)) {
        Deslizador(
            pos(valor), 0f, 1f, { Mesa.ponerVoz(param, desde(it)) }, it,
            alReiniciar = { Mesa.ponerVoz(param, inicial) }, descripcion = "$etiqueta (voz)",
        )
    }
}

/** Cuánto está bajando (dB); se lee al dibujar. */
@Composable
private fun MedidorReduccion(db: () -> Float, modifier: Modifier) {
    val pal = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    val estilo = TextStyle(color = pal.texto2, fontSize = 11.sp)
    Canvas(modifier.semantics { contentDescription = "Cuánto baja" }) {
        val d = db().coerceIn(0f, 24f)
        val alto = 4.dp.toPx()
        val ancho = size.width * 0.55f
        val y = size.height / 2 - alto / 2
        drawRoundRect(pal.relleno, Offset(0f, y), Size(ancho, alto), CornerRadius(alto / 2))
        val w = ancho * d / 24f
        if (w > 0f) drawRoundRect(pal.naranja, Offset(ancho - w, y), Size(w, alto), CornerRadius(alto / 2))
        val t = if (d < 0.1f) "0 dB" else "−" + String.format(Locale.US, "%.1f dB", d)
        textoSeguro(medidor, t, Offset(ancho + 6.dp.toPx(), size.height / 2 - 7.dp.toPx()), estilo)
    }
}

private fun textoDbVoz(v: Float): String =
    (if (v > 0.05f) "+" else "") + String.format(Locale.US, "%.1f dB", v).replace('-', '−')

private fun textoPorcientoVoz(v: Float): String = String.format(Locale.US, "%.0f %%", v * 100f)
