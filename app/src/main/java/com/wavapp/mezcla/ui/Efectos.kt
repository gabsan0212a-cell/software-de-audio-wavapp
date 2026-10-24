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
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.Pista
import java.util.Locale
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.roundToInt

/** Hoja de efectos de una pista: presets, los de la voz (auto-tune y limpieza), compresor, delay y reverb. */
@Composable
fun HojaEfectos(pista: Pista?, alCerrar: () -> Unit) {
    val recordada = remember { arrayOfNulls<Pista>(1) }
    if (pista != null) recordada[0] = pista
    val p = pista ?: recordada[0]
    Hoja(
        visible = pista != null,
        alCerrar = alCerrar,
        titulo = "Efectos",
        alta = true,
        izquierda = { BotonReproducir(LocalPaleta.current.acento, 24.dp, 44.dp) },
        derecha = { BotonTexto("Listo", alCerrar, estilo = Tipo.encabezado) },
    ) {
        if (p != null) ContenidoEfectos(p)
    }
}

/** Lo que trae un efecto prendido, para mostrarlo en una línea. */
internal fun resumenEfectos(p: Pista): String {
    val on = listOfNotNull(
        "Auto-tune".takeIf { p.fxOn(Nucleo.FX_AUTOTUNE) },
        "Compresor".takeIf { p.fxOn(Nucleo.FX_COMP) },
        "Doblaje".takeIf { p.fxOn(Nucleo.FX_DOBLAJE) },
        "Delay".takeIf { p.fxOn(Nucleo.FX_DELAY) },
        "Reverb".takeIf { p.fxOn(Nucleo.FX_REVERB) },
    ).ifEmpty {
        listOfNotNull(
            "Graves".takeIf { p.fxOn(Nucleo.FX_GRAVES) },
            "Compuerta".takeIf { p.fxOn(Nucleo.FX_COMPUERTA) },
            "De-esser".takeIf { p.fxOn(Nucleo.FX_DEESSER) },
        )
    }
    return if (on.isEmpty()) "Sin efectos" else on.joinToString(" · ")
}

@Composable
private fun ContenidoEfectos(p: Pista) {
    val pal = LocalPaleta.current
    Row(Modifier.padding(start = 4.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Portada(p.id, pal.pistas[p.color], 40.dp)
        Column(Modifier.padding(start = 12.dp)) {
            Texto(p.nombre, Tipo.encabezado, pal.texto, lineas = 1)
            Texto(resumenEfectos(p), Tipo.nota, pal.texto2, lineas = 1)
        }
    }
    EncabezadoGrupo("Presets")
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).testTag("presetsFx"),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Mesa.presetsFx.forEachIndexed { i, nombre ->
            Chip(nombre, false, { Mesa.aplicarPresetFx(p, i) }, colorInactivo = pal.acento, descripcion = "Preset $nombre")
        }
    }
    PieGrupo("Un punto de partida: después ajusta lo que quieras.")

    VozDeLaPista(p)

    EncabezadoGrupo("Compresor")
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 16.dp, top = 2.dp, bottom = 2.dp)) {
        FilaInterruptor("Compresor", p.fxOn(Nucleo.FX_COMP), { Mesa.alternarFx(p, Nucleo.FX_COMP) }) {
            MedidorCompresion(p, Modifier.padding(top = 4.dp).width(140.dp).height(14.dp))
        }
        if (p.fxOn(Nucleo.FX_COMP)) {
            Separador()
            Ajuste(p, Nucleo.FX_COMP_UMBRAL, "Umbral", ::textoDbFx)
            Ajuste(p, Nucleo.FX_COMP_PROPORCION, "Proporción") { String.format(Locale.US, "%.1f:1", it) }
            Ajuste(p, Nucleo.FX_COMP_ATAQUE, "Ataque", ::textoMs)
            Ajuste(p, Nucleo.FX_COMP_LIBERACION, "Liberación", ::textoMs)
            Ajuste(p, Nucleo.FX_COMP_GANANCIA, "Ganancia", ::textoDbFx)
        }
    }
    PieGrupo(
        "Empareja el volumen: lo que pasa del umbral se baja según la proporción (3:1 = suave, 10:1 = fuerte). " +
            "Ganancia sube todo después.",
    )

    EncabezadoGrupo("Delay (eco)")
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 16.dp, top = 2.dp, bottom = 2.dp)) {
        FilaInterruptor("Delay", p.fxOn(Nucleo.FX_DELAY), { Mesa.alternarFx(p, Nucleo.FX_DELAY) })
        if (p.fxOn(Nucleo.FX_DELAY)) {
            Separador()
            Ajuste(p, Nucleo.FX_DELAY_CANTIDAD, "Cantidad", ::textoPorciento)
            Ajuste(p, Nucleo.FX_DELAY_TIEMPO, "Tiempo", ::textoMs)
            Ajuste(p, Nucleo.FX_DELAY_REPETICIONES, "Repeticiones", ::textoPorciento)
            Ajuste(p, Nucleo.FX_DELAY_BRILLO, "Brillo", ::textoHzFx)
            Separador(Modifier.padding(top = 6.dp))
            FilaInterruptor(
                "Ping-pong", p.fxOn(Nucleo.FX_DELAY_PINGPONG), { Mesa.alternarFx(p, Nucleo.FX_DELAY_PINGPONG) },
            ) { Texto("Los ecos van de un lado al otro", Tipo.nota, pal.texto2, lineas = 1) }
        }
    }

    EncabezadoGrupo("Reverb")
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 16.dp, top = 2.dp, bottom = 2.dp)) {
        FilaInterruptor("Reverb", p.fxOn(Nucleo.FX_REVERB), { Mesa.alternarFx(p, Nucleo.FX_REVERB) })
        if (p.fxOn(Nucleo.FX_REVERB)) {
            Separador()
            Ajuste(p, Nucleo.FX_REVERB_CANTIDAD, "Cantidad", ::textoPorciento)
            Ajuste(p, Nucleo.FX_REVERB_DURACION, "Duración") { String.format(Locale.US, "%.2f s", it) }
            Ajuste(p, Nucleo.FX_REVERB_BRILLO, "Brillo", ::textoHzFx)
            Ajuste(p, Nucleo.FX_REVERB_PREDELAY, "Pre-delay", ::textoMs)
            Spacer(Modifier.height(8.dp))
        }
    }
    PieGrupo(
        "El delay y la reverb se suman aparte (el sonido original no cambia) y siguen el volumen y el paneo " +
            "de la pista. También suenan al exportar.",
    )
}

/** La escala que usa el auto-tune de la pista, para leerla. */
private fun textoEscalaPista(p: Pista): String {
    val m = Mesa.musica
    return when (p.fx.getOrElse(Nucleo.FX_ESCALA) { 0f }.roundToInt()) {
        Nucleo.ESCALA_CANCION ->
            if (m.tonicaAutotune < 0) "Todas las notas (sin canción ni instrumentos)"
            else nombreTonalidad(m.tonicaAutotune, m.menorAutotune) + " · de la canción"
        Nucleo.ESCALA_MAYOR -> nombreTonalidad(p.fx[Nucleo.FX_TONICA].roundToInt(), false)
        Nucleo.ESCALA_MENOR -> nombreTonalidad(p.fx[Nucleo.FX_TONICA].roundToInt(), true)
        else -> "Todas las notas"
    }
}

/** Los efectos de la voz en vivo, para una voz grabada. */
@Composable
private fun VozDeLaPista(p: Pista) {
    val pal = LocalPaleta.current
    EncabezadoGrupo("Voz")
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 16.dp, top = 2.dp, bottom = 8.dp)) {
        FilaInterruptor("Auto-tune", p.fxOn(Nucleo.FX_AUTOTUNE), { Mesa.alternarFx(p, Nucleo.FX_AUTOTUNE) }, Modifier.testTag("autotunePista")) {
            Texto(textoEscalaPista(p), Tipo.nota, pal.texto2, lineas = 1)
        }
        if (p.fxOn(Nucleo.FX_AUTOTUNE)) {
            Separador()
            Texto("Escala", Tipo.subtitulo, pal.texto, Modifier.padding(top = 10.dp, bottom = 6.dp))
            val escala = p.fx[Nucleo.FX_ESCALA].roundToInt()
            Segmentado(
                listOf("Canción", "Mayor", "Menor", "Todas"), escala,
                { Mesa.ponerFx(p, Nucleo.FX_ESCALA, it.toFloat()) }, Modifier.fillMaxWidth(),
            )
            if (escala == Nucleo.ESCALA_MAYOR || escala == Nucleo.ESCALA_MENOR) {
                val tonica = p.fx[Nucleo.FX_TONICA].roundToInt()
                Row(
                    Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(top = 10.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    NOTAS.forEachIndexed { i, n -> Chip(n, i == tonica, { Mesa.ponerFx(p, Nucleo.FX_TONICA, i.toFloat()) }) }
                }
            }
            Ajuste(p, Nucleo.FX_VELOCIDAD, "Velocidad") { if (it < 0.5f) "Al instante (robot)" else "${it.roundToInt()} ms" }
            Ajuste(p, Nucleo.FX_AFINAR, "Cuánto corrige", ::textoPorciento)
            Texto("Voz", Tipo.subtitulo, pal.texto, Modifier.padding(top = 12.dp, bottom = 6.dp))
            Segmentado(
                listOf("Grave", "Media", "Aguda"), p.fx[Nucleo.FX_REGISTRO].roundToInt(),
                { Mesa.ponerFx(p, Nucleo.FX_REGISTRO, it.toFloat()) }, Modifier.fillMaxWidth(),
            )
            Spacer(Modifier.height(6.dp))
        }
        Separador()
        FilaInterruptor("Quitar graves", p.fxOn(Nucleo.FX_GRAVES), { Mesa.alternarFx(p, Nucleo.FX_GRAVES) }) {
            Texto("El retumbe del micrófono", Tipo.nota, pal.texto2, lineas = 1)
        }
        if (p.fxOn(Nucleo.FX_GRAVES)) Ajuste(p, Nucleo.FX_GRAVES_HZ, "Debajo de") { "${it.roundToInt()} Hz" }
        Separador(Modifier.padding(top = 6.dp))
        FilaInterruptor("Compuerta", p.fxOn(Nucleo.FX_COMPUERTA), { Mesa.alternarFx(p, Nucleo.FX_COMPUERTA) }) {
            Texto("Baja el ruido entre frases", Tipo.nota, pal.texto2, lineas = 1)
        }
        if (p.fxOn(Nucleo.FX_COMPUERTA)) Ajuste(p, Nucleo.FX_COMPUERTA_UMBRAL, "Abre arriba de", ::textoDbFx)
        Separador(Modifier.padding(top = 6.dp))
        FilaInterruptor("De-esser", p.fxOn(Nucleo.FX_DEESSER), { Mesa.alternarFx(p, Nucleo.FX_DEESSER) }) {
            Texto("Suaviza las \"eses\"", Tipo.nota, pal.texto2, lineas = 1)
        }
        if (p.fxOn(Nucleo.FX_DEESSER)) Ajuste(p, Nucleo.FX_DEESSER_UMBRAL, "Baja las \"eses\" arriba de", ::textoDbFx)
        Separador(Modifier.padding(top = 6.dp))
        FilaInterruptor("Doblaje", p.fxOn(Nucleo.FX_DOBLAJE), { Mesa.alternarFx(p, Nucleo.FX_DOBLAJE) }) {
            Texto("Como grabar la voz dos veces: más ancha", Tipo.nota, pal.texto2, lineas = 1)
        }
        if (p.fxOn(Nucleo.FX_DOBLAJE)) Ajuste(p, Nucleo.FX_DOBLAJE_CANTIDAD, "Cantidad", ::textoPorciento)
    }
    PieGrupo(
        "Los mismos de la voz en vivo, para una voz grabada. El auto-tune deja la pista en mono y la voz queda en su " +
            "lugar (el núcleo compensa lo que tarda).",
    )
}

/**
 * Un ajuste con su deslizador. El núcleo dice el rango y si va en escala logarítmica (tiempos,
 * frecuencias); aquí solo se pasa de valor a posición del dedo y al revés.
 */
@Composable
private fun Ajuste(p: Pista, param: Int, etiqueta: String, texto: (Float) -> String) {
    val info = Mesa.infoFx
    if (info.size < 4 * (param + 1) || p.fx.size <= param) return
    val min = info[4 * param]
    val max = info[4 * param + 1]
    val inicial = info[4 * param + 2]
    val log = info[4 * param + 3] != 0f
    val valor = p.fx[param]
    fun pos(v: Float) = if (log) ln(v / min) / ln(max / min) else (v - min) / (max - min)
    fun desde(pos: Float) = if (log) min * exp(pos * ln(max / min)) else min + pos * (max - min)
    FilaAjuste(etiqueta, texto(valor)) {
        Deslizador(
            pos(valor), 0f, 1f, { Mesa.ponerFx(p, param, desde(it)) }, it,
            alReiniciar = { Mesa.ponerFx(p, param, inicial) }, descripcion = "$etiqueta pista ${p.id}",
        )
    }
}

/** Cuánto está bajando el compresor ahora (se lee al dibujar). */
@Composable
private fun MedidorCompresion(p: Pista, modifier: Modifier) {
    val pal = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    val estilo = TextStyle(color = pal.texto2, fontSize = 11.sp)
    Canvas(modifier.semantics { contentDescription = "Reducción del compresor" }) {
        val db = p.compresion.coerceIn(0f, 24f)
        val alto = 4.dp.toPx()
        val ancho = size.width * 0.55f
        val y = size.height / 2 - alto / 2
        drawRoundRect(pal.relleno, Offset(0f, y), Size(ancho, alto), CornerRadius(alto / 2))
        // La barra crece de derecha a izquierda, como los medidores de reducción.
        val w = ancho * db / 24f
        if (w > 0f) drawRoundRect(pal.naranja, Offset(ancho - w, y), Size(w, alto), CornerRadius(alto / 2))
        val t = if (db < 0.1f) "0 dB" else "−" + String.format(Locale.US, "%.1f dB", db)
        drawText(medidor, t, Offset(ancho + 6.dp.toPx(), size.height / 2 - 7.dp.toPx()), estilo)
    }
}

private fun textoDbFx(v: Float): String =
    (if (v > 0.05f) "+" else "") + String.format(Locale.US, "%.1f dB", v).replace('-', '−')

private fun textoMs(v: Float): String =
    if (v < 10f) String.format(Locale.US, "%.1f ms", v) else String.format(Locale.US, "%.0f ms", v)

private fun textoPorciento(v: Float): String = String.format(Locale.US, "%.0f %%", v * 100f)

private fun textoHzFx(v: Float): String =
    if (v >= 1000f) String.format(Locale.US, "%.1f kHz", v / 1000f) else String.format(Locale.US, "%.0f Hz", v)
