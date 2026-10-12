package com.wavapp.mezcla.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
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
import com.wavapp.mezcla.NativeAudio
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

// Escalas de los deslizadores (0..1000), iguales a las del prototipo.
private fun fDesdePos(v: Float) = 20f * 1000f.pow(v / 1000f)
private fun posDesdeF(f: Float) = (ln(f / 20f) / ln(1000f)) * 1000f
private fun qDesdePos(v: Float) = 0.1f * 180f.pow(v / 1000f)
private fun posDesdeQ(q: Float) = (ln(q / 0.1f) / ln(180f)) * 1000f

/** EQ de una pista, a pantalla completa. */
@Composable
fun PanelEq(p: Pista, alCerrar: () -> Unit) {
    val pal = LocalPaleta.current
    var elegida by remember(p.id) { mutableIntStateOf(0) }
    val sel = elegida.coerceAtMost(max(0, p.bandas.size - 1))
    BackHandler { alCerrar() }

    Column(
        Modifier
            .fillMaxSize()
            .background(pal.bg)
            .safeDrawingPadding()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("EQ · ${p.id} · ${p.nombre}", color = pal.fg, fontSize = 17.sp, fontWeight = FontWeight.SemiBold)
                TextoCompensacion(p)
            }
            Boton("Cerrar", alCerrar)
        }
        Transporte(horizontal = 0.dp)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Boton("Auto-ganancia", { Mesa.alternarAutoGanancia(p) }, presionado = p.autoGanancia, tamano = 13.sp, relleno = 10.dp)
            Boton("Bypass", { Mesa.alternarEqBypass(p) }, presionado = p.eqBypass, tamano = 13.sp, relleno = 10.dp)
            Boton("Restablecer", { Mesa.restablecerEq(p) }, tamano = 13.sp, relleno = 10.dp)
        }
        CurvaEq(
            p, sel, alElegir = { elegida = it },
            Modifier.fillMaxWidth().height(250.dp).clip(RoundedCornerShape(4.dp)).alpha(if (p.eqBypass) 0.55f else 1f),
        )
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            p.bandas.forEachIndexed { i, b ->
                Boton(
                    "${i + 1} · ${etiquetaHz(b.f)}", { elegida = i }, presionado = i == sel, tamano = 12.sp, relleno = 9.dp,
                    modifier = Modifier.alpha(if (b.on) 1f else 0.45f),
                )
            }
            Boton(
                "+ Banda", {
                    if (Mesa.agregarBanda(p) != null) elegida = p.bandas.size - 1
                },
                habilitado = p.bandas.size < Mesa.MAX_BANDAS, tamano = 12.sp, relleno = 10.dp,
            )
        }
        val banda = p.bandas.getOrNull(sel)
        if (banda == null) {
            Text(
                "Sin bandas: el EQ no hace nada ni gasta batería. Toca \"+ Banda\" para agregar una " +
                    "(hasta ${Mesa.MAX_BANDAS}).",
                color = pal.tenue, fontSize = 13.sp,
            )
        } else {
            ControlesBanda(p, banda, alQuitar = { Mesa.quitarBanda(p, banda) })
        }
        Text(
            "Arrastra los nodos de la curva: horizontal cambia la frecuencia y vertical la ganancia. " +
                "Doble toque en un nodo lo regresa a 0 dB. Solo las bandas que agregas se procesan. " +
                "Con la música sonando, el espectro de la pista aparece al fondo.",
            color = pal.tenue, fontSize = 12.sp,
        )
    }
}

@Composable
private fun TextoCompensacion(p: Pista) {
    @Suppress("UNUSED_VARIABLE") val v = p.eqVersion  // se recalcula con cada cambio
    val comp = -NativeAudio.eqTrimDb(p.slot)
    val texto = when {
        p.bandas.isEmpty() -> "sin bandas"
        !p.autoGanancia -> "sin compensar"
        comp > 0.05f -> "compensando −" + String.format(Locale.US, "%.1f", comp) + " dB"
        else -> "sin subidas que compensar"
    }
    Text(texto, color = LocalPaleta.current.tenue, fontSize = 12.sp, fontFamily = Mono)
}

@Composable
private fun ControlesBanda(p: Pista, b: Banda, alQuitar: () -> Unit) {
    val pal = LocalPaleta.current
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text("Tipo de filtro", color = pal.tenue, fontSize = 12.sp)
        for (fila in 0..1) {
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                for (t in fila * 3 until fila * 3 + 3) {
                    Boton(
                        Filtros.nombres[t], { Mesa.cambiarBanda(p, b, tipo = t) }, presionado = b.tipo == t,
                        tamano = 12.sp, relleno = 9.dp,
                    )
                }
            }
        }
        FilaEq("Frecuencia", etiquetaHz(b.f) + " Hz") {
            Deslizador(
                posDesdeF(b.f), 0f, 1000f, { Mesa.cambiarBanda(p, b, f = fDesdePos(it)) },
                { Mesa.cambiarBanda(p, b, f = 1000f) }, it, descripcion = "Frecuencia banda",
            )
        }
        val conG = Filtros.conGanancia(b.tipo)
        FilaEq("Ganancia", if (conG) (if (b.g > 0) "+" else "") + String.format(Locale.US, "%.1f dB", b.g) else "—") {
            if (conG) {
                Deslizador(
                    b.g, -15f, 15f, { Mesa.cambiarBanda(p, b, g = it) }, { Mesa.cambiarBanda(p, b, g = 0f) }, it,
                    desde = 0f, descripcion = "Ganancia banda",
                )
            } else {
                Deslizador(0f, -15f, 15f, {}, {}, it.alpha(0.3f), desde = 0f)
            }
        }
        val conQ = Filtros.conQ(b.tipo)
        FilaEq("Q (ancho)", if (conQ) String.format(Locale.US, "%.2f", b.q) else "—") {
            if (conQ) {
                Deslizador(
                    posDesdeQ(b.q), 0f, 1000f, { Mesa.cambiarBanda(p, b, q = qDesdePos(it)) },
                    { Mesa.cambiarBanda(p, b, q = 1.4f) }, it, descripcion = "Q banda",
                )
            } else {
                Deslizador(posDesdeQ(0.71f), 0f, 1000f, {}, {}, it.alpha(0.3f))
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Boton(
                if (b.on) "Banda activa" else "Banda apagada", { Mesa.cambiarBanda(p, b, on = !b.on) },
                presionado = b.on, tamano = 13.sp, relleno = 10.dp,
            )
            Boton("Quitar banda", alQuitar, tamano = 13.sp, relleno = 10.dp)
        }
    }
}

@Composable
private fun FilaEq(etiqueta: String, valor: String, control: @Composable (Modifier) -> Unit) {
    val pal = LocalPaleta.current
    Column {
        Row {
            Text(etiqueta, color = pal.tenue, fontSize = 12.sp, modifier = Modifier.weight(1f))
            Text(valor, color = pal.fg, fontSize = 12.sp, fontFamily = Mono)
        }
        control(Modifier.fillMaxWidth())
    }
}

/**
 * Curva del EQ: rejilla, respuesta total (relleno), la banda elegida punteada y un nodo por
 * banda. Tocar elige un nodo, arrastrar lo mueve, doble toque lo regresa a 0 dB.
 */
@Composable
private fun CurvaEq(p: Pista, sel: Int, alElegir: (Int) -> Unit, modifier: Modifier) {
    val pal = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    val pista by rememberUpdatedState(p)
    val elegir by rememberUpdatedState(alElegir)
    var arrastrando by remember { mutableIntStateOf(-1) }

    Box(modifier.background(pal.bg).semantics { contentDescription = "Curva del ecualizador" }) {
        Canvas(
            Modifier
                .fillMaxSize()
                .pointerInput(Unit) {
                    fun x(f: Float) = (ln(f / 20f) / ln(1000f)) * size.width
                    fun y(db: Float) = size.height / 2f - db / 15f * (size.height / 2f - 14.dp.toPx())
                    fun cercano(o: Offset, radio: Float): Int {
                        var mejor = -1
                        var d2 = radio * radio
                        pista.bandas.forEachIndexed { i, b ->
                            val dx = x(b.f) - o.x
                            val dy = y(if (Filtros.conGanancia(b.tipo)) b.g else 0f) - o.y
                            if (dx * dx + dy * dy <= d2) { d2 = dx * dx + dy * dy; mejor = i }
                        }
                        return mejor
                    }
                    detectTapGestures(
                        onTap = { o -> cercano(o, 28.dp.toPx()).let { if (it >= 0) elegir(it) } },
                        onDoubleTap = { o ->
                            val i = cercano(o, 28.dp.toPx())
                            if (i >= 0) { elegir(i); Mesa.cambiarBanda(pista, pista.bandas[i], g = 0f) }
                        },
                    )
                }
                .pointerInput(Unit) {
                    fun f(px: Float) = 20f * 1000f.pow((px / size.width).coerceIn(0f, 1f))
                    fun db(py: Float) = (size.height / 2f - py) / (size.height / 2f - 14.dp.toPx()) * 15f
                    detectDragGestures(
                        onDragStart = { o ->
                            var mejor = -1
                            var d2 = (32.dp.toPx()).let { it * it }
                            pista.bandas.forEachIndexed { i, b ->
                                val bx = (ln(b.f / 20f) / ln(1000f)) * size.width
                                val by = size.height / 2f -
                                    (if (Filtros.conGanancia(b.tipo)) b.g else 0f) / 15f * (size.height / 2f - 14.dp.toPx())
                                val d = (bx - o.x) * (bx - o.x) + (by - o.y) * (by - o.y)
                                if (d <= d2) { d2 = d; mejor = i }
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
                                g = if (Filtros.conGanancia(b.tipo)) db(cambio.position.y) else b.g,
                            )
                        },
                    )
                },
        ) {
            @Suppress("UNUSED_VARIABLE") val v = p.eqVersion  // redibujar con cada cambio
            val w = size.width
            val h = size.height
            val d = density
            fun x(f: Float) = (ln(f / 20f) / ln(1000f)) * w
            fun y(db: Float) = h / 2f - db / 15f * (h / 2f - 14 * d)
            val estiloTexto = TextStyle(color = pal.tenue, fontSize = 9.sp, fontFamily = Mono)

            // Con la música sonando, el espectro de la pista aparece al fondo (como el prototipo).
            @Suppress("UNUSED_VARIABLE") val va = Analisis.version
            if (Mesa.reproduciendo && Analisis.activo) lineaEspectro(pal.verde.copy(alpha = 0.16f), relleno = true)

            // Rejilla
            for (db in listOf(-12, -6, 0, 6, 12)) {
                drawLine(if (db == 0) pal.tenue else pal.linea, Offset(0f, y(db.toFloat())), Offset(w, y(db.toFloat())), d)
                drawText(medidor, (if (db > 0) "+" else "") + db, Offset(3 * d, y(db.toFloat()) - 13 * d), estiloTexto)
            }
            for (f in listOf(50f, 100f, 200f, 500f, 1000f, 2000f, 5000f, 10000f)) {
                drawLine(pal.linea, Offset(x(f), 0f), Offset(x(f), h), d)
                drawText(medidor, etiquetaHz(f), Offset(x(f) + 3 * d, h - 14 * d), estiloTexto)
            }

            // Respuesta total
            val n = max(64, (w / 2).toInt())
            val fr = FloatArray(n) { 20f * 1000f.pow(it / (n - 1f)) }
            val total = FloatArray(n)
            NativeAudio.eqResponse(p.slot, -1, fr, total)
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
            drawPath(relleno, pal.acento.copy(alpha = 0.14f))
            drawPath(linea, pal.acento, style = Stroke(2 * d))

            // Banda elegida, punteada
            val b = p.bandas.getOrNull(sel)
            if (b != null && b.on && !p.eqBypass) {
                val una = FloatArray(n)
                NativeAudio.eqResponse(p.slot, b.indice, fr, una)
                val pu = Path()
                for (i in 0 until n) {
                    val py = y(una[i].coerceIn(-15f, 15f))
                    if (i == 0) pu.moveTo(x(fr[i]), py) else pu.lineTo(x(fr[i]), py)
                }
                drawPath(pu, pal.fg, style = Stroke(d, pathEffect = PathEffect.dashPathEffect(floatArrayOf(4 * d, 3 * d))))
            }

            // Nodos
            p.bandas.forEachIndexed { i, bb ->
                val activo = bb.on && !p.eqBypass
                val elegido = i == sel
                val c = Offset(x(bb.f), y(if (Filtros.conGanancia(bb.tipo)) bb.g else 0f))
                val r = (if (elegido) 9f else 7f) * d
                drawCircle(if (activo) (if (elegido) pal.acento else pal.fg) else pal.bg, r, c)
                drawCircle(if (activo) pal.bg else pal.tenue, r, c, style = Stroke((if (elegido) 2f else 1.5f) * d))
                val etiqueta = medidor.measure("${i + 1}", TextStyle(color = if (activo) pal.bg else pal.tenue,
                    fontSize = 9.sp, fontFamily = Mono, fontWeight = FontWeight.Medium))
                drawText(etiqueta, topLeft = Offset(c.x - etiqueta.size.width / 2f, c.y - etiqueta.size.height / 2f))
            }
        }
    }
}
