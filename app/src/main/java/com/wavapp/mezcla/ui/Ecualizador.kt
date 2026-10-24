package com.wavapp.mezcla.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.Analisis
import com.wavapp.mezcla.Banda
import com.wavapp.mezcla.Filtros
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.Pista
import java.util.Locale
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.roundToInt

/** Etiqueta corta de frecuencia, como el prototipo: 63, 1.25k, 12.5k. */
fun etiquetaHz(f: Float): String {
    if (f < 1000f) return f.roundToInt().toString()
    val k = f / 1000f
    val t = String.format(Locale.US, if (f >= 10000f) "%.1f" else "%.2f", k).trimEnd('0').trimEnd('.')
    return t + "k"
}

/** Frecuencia con unidad: 120 Hz, 3 kHz, 12.5 kHz. */
fun textoHz(f: Float): String = if (f < 1000f) "${f.roundToInt()} Hz" else etiquetaHz(f).dropLast(1) + " kHz"

// Escalas de los deslizadores (0..1000), iguales a las del prototipo.
private fun fDesdePos(v: Float) = 20f * 1000f.pow(v / 1000f)
private fun posDesdeF(f: Float) = (ln(f / 20f) / ln(1000f)) * 1000f
private fun qDesdePos(v: Float) = 0.1f * 180f.pow(v / 1000f)
private fun posDesdeQ(q: Float) = (ln(q / 0.1f) / ln(180f)) * 1000f

/** El EQ de una pista, en una hoja alta. Recuerda la última pista abierta mientras se cierra. */
@Composable
fun HojaEq(pista: Pista?, alCerrar: () -> Unit) {
    val recordada = remember { arrayOfNulls<Pista>(1) }
    if (pista != null) recordada[0] = pista
    val p = pista ?: recordada[0]
    Hoja(
        visible = pista != null,
        alCerrar = alCerrar,
        titulo = if (p?.esEntrada == true) "Ecualizador en vivo" else "Ecualizador",
        alta = true,
        izquierda = { BotonReproducir(LocalPaleta.current.acento, 24.dp, 44.dp) },
        derecha = { BotonTexto("Listo", alCerrar, estilo = Tipo.encabezado) },
    ) {
        if (p != null) ContenidoEq(p)
    }
}

@Composable
private fun ContenidoEq(p: Pista) {
    val pal = LocalPaleta.current
    var elegida by remember(p.id) { mutableIntStateOf(0) }
    val sel = elegida.coerceAtMost(max(0, p.bandas.size - 1))

    Row(Modifier.padding(start = 4.dp, bottom = 12.dp), verticalAlignment = Alignment.CenterVertically) {
        if (p.esEntrada) AzulejoIcono(Iconos.microfono, pal.acento, 40.dp) else Portada(p.id, pal.pistas[p.color], 40.dp)
        Column(Modifier.padding(start = 12.dp)) {
            Texto(if (p.esEntrada) "Entrada en vivo" else p.nombre, Tipo.encabezado, pal.texto, lineas = 1)
            TextoCompensacion(p)
        }
    }
    CurvaEq(
        p, sel, alElegir = { elegida = it },
        Modifier.fillMaxWidth().height(240.dp).clip(RoundedCornerShape(14.dp)).background(pal.tarjeta)
            .alpha(if (p.eqBypass) 0.5f else 1f),
    )
    Row(
        Modifier.fillMaxWidth().padding(top = 12.dp).horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        p.bandas.forEachIndexed { i, b ->
            Chip(
                "${i + 1} · ${etiquetaHz(b.f)}", i == sel, { elegida = i },
                Modifier.alpha(if (b.on) 1f else 0.45f), descripcion = "Banda ${i + 1}",
            )
        }
        Chip(
            "Banda", false, { if (Mesa.agregarBanda(p) != null) elegida = p.bandas.size - 1 },
            colorInactivo = pal.acento, icono = Iconos.mas, descripcion = "Agregar banda",
            habilitado = p.bandas.size < Mesa.MAX_BANDAS,
        )
    }
    val banda = p.bandas.getOrNull(sel)
    if (banda == null) {
        PieGrupo(
            "Sin bandas: el EQ no hace nada ni gasta batería. Toca \"+ Banda\" para agregar una " +
                "(hasta ${Mesa.MAX_BANDAS}).",
            Modifier.padding(top = 4.dp),
        )
    } else {
        EncabezadoGrupo("Banda ${sel + 1}")
        ControlesBanda(p, banda)
    }
    EncabezadoGrupo("Ajustes")
    Tarjeta(relleno = PaddingValues(horizontal = 16.dp, vertical = 2.dp)) {
        FilaInterruptor("Auto-ganancia", p.autoGanancia, { Mesa.alternarAutoGanancia(p) })
        Separador()
        FilaInterruptor("Bypass", p.eqBypass, { Mesa.alternarEqBypass(p) })
        Separador()
        FilaBoton("Restablecer EQ", { Mesa.restablecerEq(p) }, pal.rojo, habilitado = p.bandas.isNotEmpty())
    }
    PieGrupo(
        "Arrastra los puntos de la curva: a los lados cambia la frecuencia y arriba o abajo la ganancia. " +
            "Doble toque en un punto lo regresa a 0 dB. Con la música sonando, el espectro de la pista aparece al fondo.",
    )
}

@Composable
private fun TextoCompensacion(p: Pista) {
    @Suppress("UNUSED_VARIABLE") val v = p.eqVersion  // se recalcula con cada cambio
    val comp = -Nucleo.eqTrimDb(p.id)
    val texto = when {
        p.bandas.isEmpty() -> "Sin bandas"
        p.eqBypass -> "En bypass"
        !p.autoGanancia -> "Sin compensar"
        comp > 0.05f -> "Compensando −" + String.format(Locale.US, "%.1f", comp) + " dB"
        else -> "Sin subidas que compensar"
    }
    Texto(texto, Tipo.nota, LocalPaleta.current.texto2, lineas = 1)
}

@Composable
private fun ControlesBanda(p: Pista, b: Banda) {
    val pal = LocalPaleta.current
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 16.dp, top = 14.dp, bottom = 2.dp)) {
        Segmentado(
            Filtros.nombres.subList(0, 3), if (b.tipo < 3) b.tipo else -1, { Mesa.cambiarBanda(p, b, tipo = it) },
            Modifier.fillMaxWidth(),
        )
        Spacer(Modifier.height(8.dp))
        Segmentado(
            Filtros.nombres.subList(3, 6), if (b.tipo >= 3) b.tipo - 3 else -1, { Mesa.cambiarBanda(p, b, tipo = it + 3) },
            Modifier.fillMaxWidth(),
        )
        FilaAjuste("Frecuencia", textoHz(b.f)) {
            Deslizador(
                posDesdeF(b.f), 0f, 1000f, { Mesa.cambiarBanda(p, b, f = fDesdePos(it)) }, it,
                alReiniciar = { Mesa.cambiarBanda(p, b, f = 1000f) }, descripcion = "Frecuencia banda",
            )
        }
        val conG = Filtros.conGanancia(b.tipo)
        FilaAjuste("Ganancia", if (conG) (if (b.g > 0) "+" else "") + String.format(Locale.US, "%.1f dB", b.g) else "—") {
            Deslizador(
                if (conG) b.g else 0f, -15f, 15f, { Mesa.cambiarBanda(p, b, g = it) }, it,
                alReiniciar = { Mesa.cambiarBanda(p, b, g = 0f) }, desde = 0f, descripcion = "Ganancia banda",
                habilitado = conG,
            )
        }
        val conQ = Filtros.conQ(b.tipo)
        FilaAjuste("Q (ancho)", if (conQ) String.format(Locale.US, "%.2f", b.q) else "—") {
            Deslizador(
                posDesdeQ(if (conQ) b.q else 0.71f), 0f, 1000f, { Mesa.cambiarBanda(p, b, q = qDesdePos(it)) }, it,
                alReiniciar = { Mesa.cambiarBanda(p, b, q = 1.4f) }, descripcion = "Q banda", habilitado = conQ,
            )
        }
        Separador(Modifier.padding(top = 6.dp))
        FilaInterruptor("Banda activa", b.on, { Mesa.cambiarBanda(p, b, on = it) })
        Separador()
        FilaBoton("Quitar banda", { Mesa.quitarBanda(p, b) }, pal.rojo)
    }
}

/**
 * Curva del EQ: rejilla, respuesta total (relleno), la banda elegida punteada y un punto por
 * banda. Tocar elige un punto, arrastrar lo mueve, doble toque lo regresa a 0 dB.
 */
@Composable
private fun CurvaEq(p: Pista, sel: Int, alElegir: (Int) -> Unit, modifier: Modifier) {
    val pal = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    val pista by rememberUpdatedState(p)
    val elegir by rememberUpdatedState(alElegir)
    var arrastrando by remember { mutableIntStateOf(-1) }
    val margen = 16.dp

    Canvas(
        modifier
            .semantics { contentDescription = "Curva del ecualizador" }
            .pointerInput(Unit) {
                val m = margen.toPx()
                fun x(f: Float) = (ln(f / 20f) / ln(1000f)) * size.width
                fun y(db: Float) = size.height / 2f - db / 15f * (size.height / 2f - m)
                fun cercano(o: Offset, radio: Float): Int {
                    var mejor = -1
                    var d2 = radio * radio
                    pista.bandas.forEachIndexed { i, b ->
                        val dx = x(b.f) - o.x
                        val dy = y(if (Filtros.conGanancia(b.tipo)) b.g else 0f) - o.y
                        if (dx * dx + dy * dy <= d2) {
                            d2 = dx * dx + dy * dy
                            mejor = i
                        }
                    }
                    return mejor
                }
                detectTapGestures(
                    onTap = { o -> cercano(o, 30.dp.toPx()).let { if (it >= 0) elegir(it) } },
                    onDoubleTap = { o ->
                        val i = cercano(o, 30.dp.toPx())
                        if (i >= 0) {
                            elegir(i)
                            Mesa.cambiarBanda(pista, pista.bandas[i], g = 0f)
                        }
                    },
                )
            }
            .pointerInput(Unit) {
                val m = margen.toPx()
                fun f(px: Float) = 20f * 1000f.pow((px / size.width).coerceIn(0f, 1f))
                fun db(py: Float) = (size.height / 2f - py) / (size.height / 2f - m) * 15f
                detectDragGestures(
                    onDragStart = { o ->
                        var mejor = -1
                        var d2 = (34.dp.toPx()).let { it * it }
                        pista.bandas.forEachIndexed { i, b ->
                            val bx = (ln(b.f / 20f) / ln(1000f)) * size.width
                            val by = size.height / 2f -
                                (if (Filtros.conGanancia(b.tipo)) b.g else 0f) / 15f * (size.height / 2f - m)
                            val d = (bx - o.x) * (bx - o.x) + (by - o.y) * (by - o.y)
                            if (d <= d2) {
                                d2 = d
                                mejor = i
                            }
                        }
                        arrastrando = mejor
                        if (mejor >= 0) elegir(mejor)
                    },
                    onDragEnd = { arrastrando = -1 },
                    onDragCancel = { arrastrando = -1 },
                    onDrag = { cambio, _ ->
                        val i = arrastrando
                        val b = pista.bandas.getOrNull(i) ?: return@detectDragGestures
                        cambio.consume()
                        Mesa.cambiarBanda(
                            pista, b, f = f(cambio.position.x),
                            g = if (Filtros.conGanancia(b.tipo)) db(cambio.position.y).coerceIn(-15f, 15f) else b.g,
                        )
                    },
                )
            },
    ) {
        @Suppress("UNUSED_VARIABLE") val v = p.eqVersion  // redibujar con cada cambio
        val w = size.width
        val h = size.height
        val d = density
        val m = margen.toPx()
        fun x(f: Float) = (ln(f / 20f) / ln(1000f)) * w
        fun y(db: Float) = h / 2f - db / 15f * (h / 2f - m)
        val estiloTexto = TextStyle(color = pal.texto2, fontSize = 9.sp)

        // Con la música sonando, el espectro de la pista aparece al fondo.
        @Suppress("UNUSED_VARIABLE") val va = Analisis.version
        if (Mesa.reproduciendo && Analisis.activo) lineaEspectro(pal.verde.copy(alpha = 0.18f), relleno = true)

        for (db in listOf(-12, -6, 0, 6, 12)) {
            val yy = y(db.toFloat())
            drawLine(if (db == 0) pal.texto3 else pal.separador, Offset(0f, yy), Offset(w, yy), d)
            if (db != 0) textoSeguro(medidor, (if (db > 0) "+" else "") + db, Offset(4 * d, yy - 13 * d), estiloTexto)
        }
        for (f in listOf(50f, 100f, 200f, 500f, 1000f, 2000f, 5000f, 10000f)) {
            drawLine(pal.separador, Offset(x(f), 0f), Offset(x(f), h), d)
            textoSeguro(medidor, etiquetaHz(f), Offset(x(f) + 3 * d, h - 14 * d), estiloTexto)
        }

        val n = max(64, (w / 2).toInt())
        val fr = FloatArray(n) { 20f * 1000f.pow(it / (n - 1f)) }
        val total = FloatArray(n)
        Nucleo.eqResponse(p.id, -1, fr, total)
        val linea = Path()
        for (i in 0 until n) {
            val py = y(total[i].coerceIn(-15f, 15f))
            if (i == 0) linea.moveTo(x(fr[i]), py) else linea.lineTo(x(fr[i]), py)
        }
        val relleno = Path().apply {
            addPath(linea)
            lineTo(x(fr[n - 1]), y(0f))
            lineTo(x(fr[0]), y(0f))
            close()
        }
        drawPath(relleno, pal.acento.copy(alpha = 0.15f))
        drawPath(linea, pal.acento, style = Stroke(2.5f * d))

        val b = p.bandas.getOrNull(sel)
        if (b != null && b.on && !p.eqBypass) {
            val una = FloatArray(n)
            Nucleo.eqResponse(p.id, sel, fr, una)
            val pu = Path()
            for (i in 0 until n) {
                val py = y(una[i].coerceIn(-15f, 15f))
                if (i == 0) pu.moveTo(x(fr[i]), py) else pu.lineTo(x(fr[i]), py)
            }
            drawPath(pu, pal.texto2, style = Stroke(1.2f * d, pathEffect = PathEffect.dashPathEffect(floatArrayOf(4 * d, 3 * d))))
        }

        p.bandas.forEachIndexed { i, bb ->
            val activo = bb.on && !p.eqBypass
            val elegido = i == sel
            val c = Offset(x(bb.f), y(if (Filtros.conGanancia(bb.tipo)) bb.g else 0f))
            val r = (if (elegido) 11f else 9f) * d
            val relleno2 = when {
                !activo -> pal.tarjeta
                elegido -> pal.acento
                else -> Color.White
            }
            drawCircle(Color.Black.copy(alpha = 0.15f), r + 1 * d, c + Offset(0f, 1.2f * d))
            drawCircle(relleno2, r, c)
            drawCircle(if (activo) (if (elegido) Color.White else pal.acento) else pal.texto3, r, c, style = Stroke(1.6f * d))
            val etiqueta = medidor.measure(
                "${i + 1}",
                TextStyle(
                    color = if (!activo) pal.texto3 else if (elegido) Color.White else pal.acento,
                    fontSize = 10.sp, fontWeight = FontWeight.Bold,
                ),
            )
            drawText(etiqueta, topLeft = Offset(c.x - etiqueta.size.width / 2f, c.y - etiqueta.size.height / 2f))
        }
    }
}
