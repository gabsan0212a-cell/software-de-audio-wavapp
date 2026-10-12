package com.wavapp.mezcla.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.Medidor
import com.wavapp.mezcla.MedidorMaster
import com.wavapp.mezcla.NativeAudio
import com.wavapp.mezcla.Pista
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

enum class Estilo { Normal, Reproducir, Mute, Solo }

/** Botón como los del prototipo: borde fino; "presionado" se resalta según su estilo. */
@Composable
fun Boton(
    texto: String,
    alTocar: () -> Unit,
    modifier: Modifier = Modifier,
    presionado: Boolean = false,
    estilo: Estilo = Estilo.Normal,
    tamano: TextUnit = 14.sp,
    relleno: Dp = 12.dp,
    habilitado: Boolean = true,
    descripcion: String? = null,
) {
    val p = LocalPaleta.current
    val forma = RoundedCornerShape(5.dp)
    val (fondo, borde, color) = when {
        estilo == Estilo.Reproducir -> Triple(p.verde, p.verde, Color(0xFF04140A))
        presionado && estilo == Estilo.Mute -> Triple(p.rojo, p.rojo, Color.White)
        presionado && estilo == Estilo.Solo -> Triple(p.ambar, p.ambar, Color(0xFF1A1000))
        presionado -> Triple(p.acentoSuave, p.acento, p.fg)
        else -> Triple(p.panel, p.linea, p.fg)
    }
    Box(
        modifier
            .clip(forma)
            .background(fondo)
            .border(1.dp, borde, forma)
            .clickable(enabled = habilitado, onClick = alTocar)
            .semantics {
                role = Role.Button
                selected = presionado
                if (descripcion != null) contentDescription = descripcion
            }
            .padding(horizontal = relleno, vertical = 7.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            texto, color = if (habilitado) color else color.copy(alpha = 0.4f), fontSize = tamano,
            fontWeight = if (estilo == Estilo.Normal) FontWeight.Normal else FontWeight.SemiBold,
            fontFamily = if (estilo == Estilo.Mute || estilo == Estilo.Solo) Mono else null,
        )
    }
}

/**
 * Deslizador horizontal (como el input range del prototipo). Arrastrar o tocar mueve el valor;
 * doble toque lo regresa a su valor de inicio.
 */
@Composable
fun Deslizador(
    valor: Float,
    minimo: Float,
    maximo: Float,
    alCambiar: (Float) -> Unit,
    alReiniciar: () -> Unit,
    modifier: Modifier = Modifier,
    desde: Float = minimo,
    descripcion: String = "",
) {
    val p = LocalPaleta.current
    val cambiar by rememberUpdatedState(alCambiar)
    val reiniciar by rememberUpdatedState(alReiniciar)
    Canvas(
        modifier
            .height(30.dp)
            .semantics { contentDescription = descripcion }
            .pointerInput(minimo, maximo) {
                val r = 9.dp.toPx()
                fun valorEn(x: Float): Float {
                    val u = ((x - r) / max(1f, size.width - 2 * r)).coerceIn(0f, 1f)
                    return minimo + u * (maximo - minimo)
                }
                detectTapGestures(onDoubleTap = { reiniciar() }, onTap = { cambiar(valorEn(it.x)) })
            }
            .pointerInput(minimo, maximo) {
                val r = 9.dp.toPx()
                detectHorizontalDragGestures { cambio, _ ->
                    cambio.consume()
                    val u = ((cambio.position.x - r) / max(1f, size.width - 2 * r)).coerceIn(0f, 1f)
                    cambiar(minimo + u * (maximo - minimo))
                }
            },
    ) {
        val r = 9.dp.toPx()
        val y = size.height / 2
        val ancho = size.width - 2 * r
        fun x(v: Float) = r + (v - minimo) / (maximo - minimo) * ancho
        val grosor = 4.dp.toPx()
        drawLine(p.linea, Offset(r, y), Offset(size.width - r, y), grosor, StrokeCap.Round)
        drawLine(p.acento, Offset(x(desde), y), Offset(x(valor), y), grosor, StrokeCap.Round)
        drawCircle(p.acento, r * 0.8f, Offset(x(valor), y))
        drawCircle(p.bg, r * 0.35f, Offset(x(valor), y))
    }
}

/** Medidor de pista: L arriba y R abajo, 16 segmentos de 3 dB (-45 a +3), como el prototipo. */
@Composable
fun MedidorPista(m: Medidor, modifier: Modifier = Modifier) {
    val p = LocalPaleta.current
    Canvas(modifier.border(1.dp, p.linea, RoundedCornerShape(2.dp))) {
        drawRect(p.bg)
        val n = 16
        val gap = 1.dp.toPx()
        val sw = (size.width - gap * (n + 1)) / n
        val bh = (size.height - gap * 3) / 2
        for (c in 0..1) {
            val nivel = if (c == 0) m.l else m.r
            for (i in 0 until n) {
                val lo = -45f + 3 * i
                val up = lo + 3
                val col = if (up <= -12) p.verde else if (up <= -3) p.ambar else p.rojo
                drawRect(
                    col, Offset(gap + i * (sw + gap), gap + c * (bh + gap)), Size(sw, bh),
                    alpha = if (nivel > lo) 1f else 0.15f,
                )
            }
        }
    }
}

/** Medidores L/R del master: de -60 a +3 dB en segmentos de 3 dB, con marca de pico. */
@Composable
fun VuMaster(m: MedidorMaster, modifier: Modifier = Modifier) {
    val p = LocalPaleta.current
    Canvas(modifier.border(1.dp, p.linea, RoundedCornerShape(3.dp))) {
        drawRect(p.bg)
        val vmin = -60f
        val seg = 3f
        val n = 21
        val gap = 1.5f * density
        val sh = (size.height - gap * (n + 1)) / n
        val bw = (size.width - gap * 3) / 2
        for (c in 0..1) {
            val nivel = if (c == 0) m.l else m.r
            val tick = if (c == 0) m.tickL else m.tickR
            val hi = if (tick > vmin) min(n - 1, ((tick - vmin) / seg).toInt()) else -1
            val x = gap + c * (bw + gap)
            for (i in 0 until n) {
                val up = vmin + seg * (i + 1)
                val col = if (up <= -12) p.verde else if (up <= -3) p.ambar else p.rojo
                val yb = size.height - gap - i * (sh + gap)
                val encendido = nivel > vmin + seg * i || i == hi
                drawRect(col, Offset(x, yb - sh), Size(bw, sh), alpha = if (encendido) 1f else 0.16f)
            }
        }
    }
}

// Escala del fader master del prototipo: posición (0 arriba .. 1 abajo) → dB.
private val TAPER = listOf(0f to 6f, 0.12f to 0f, 0.35f to -12f, 0.55f to -24f, 0.75f to -40f, 0.93f to -60f, 1f to -70f)

fun dbDesdeU(u: Float): Float {
    for (i in 0 until TAPER.size - 1) {
        val (u0, d0) = TAPER[i]
        val (u1, d1) = TAPER[i + 1]
        if (u <= u1) return d0 + (d1 - d0) * (u - u0) / (u1 - u0)
    }
    return -70f
}

fun uDesdeDb(db: Float): Float {
    for (i in 0 until TAPER.size - 1) {
        val (u0, d0) = TAPER[i]
        val (u1, d1) = TAPER[i + 1]
        if (db >= d1) return u0 + (u1 - u0) * (d0 - db) / (d0 - d1)
    }
    return 1f
}

/** Fader vertical clásico del master. Doble toque lo regresa a 0 dB. */
@Composable
fun FaderMaster(db: Float, alCambiar: (Float) -> Unit, modifier: Modifier = Modifier) {
    val p = LocalPaleta.current
    val cambiar by rememberUpdatedState(alCambiar)
    val alto = 200.dp
    val capAlto = 18.dp
    Box(
        modifier
            .size(72.dp, alto)
            .semantics { contentDescription = "Volumen maestro" }
            .pointerInput(Unit) {
                val cap = capAlto.toPx()
                detectTapGestures(
                    onDoubleTap = { cambiar(0f) },
                    onTap = { cambiar(dbDesdeU(((it.y - cap / 2) / (size.height - cap)).coerceIn(0f, 1f))) },
                )
            }
            .pointerInput(Unit) {
                val cap = capAlto.toPx()
                detectVerticalDragGestures { cambio, _ ->
                    cambio.consume()
                    cambiar(dbDesdeU(((cambio.position.y - cap / 2) / (size.height - cap)).coerceIn(0f, 1f)))
                }
            },
    ) {
        Canvas(Modifier.matchParentSize()) {
            val cap = capAlto.toPx()
            val xRiel = 48.dp.toPx()
            drawRect(p.bg, Offset(xRiel - 2.dp.toPx(), 9.dp.toPx()), Size(4.dp.toPx(), size.height - 18.dp.toPx()))
            val y = uDesdeDb(db) * (size.height - cap)
            val xCap = 31.dp.toPx()
            val anchoCap = 34.dp.toPx()
            drawRect(p.tenue, Offset(xCap, y), Size(anchoCap, cap))
            drawRect(p.linea, Offset(xCap + 1, y + 1), Size(anchoCap - 2, cap - 2))
            drawRect(p.fg, Offset(xCap + 3.dp.toPx(), y + cap / 2 - 1.dp.toPx()), Size(anchoCap - 6.dp.toPx(), 2.dp.toPx()))
        }
        for ((u, d) in TAPER.take(6)) {
            val centro = capAlto / 2 + (alto - capAlto) * u
            Text(
                if (d > 0) "+${d.roundToInt()}" else "${d.roundToInt()}",
                modifier = Modifier.offset(y = centro - 7.dp).width(26.dp).height(14.dp),
                textAlign = TextAlign.End, fontFamily = Mono, fontSize = 10.sp, lineHeight = 14.sp,
                color = if (d == 0f) p.fg else p.tenue,
                fontWeight = if (d == 0f) FontWeight.Medium else FontWeight.Normal,
            )
        }
    }
}

/**
 * Carril con la forma de onda de la pista sobre la línea de tiempo común. Tocar salta a ese
 * punto. La posición se lee al dibujar para no recomponer en cada cuadro.
 */
@Composable
fun Onda(
    pista: Pista,
    duracionTotal: Double,
    posicion: () -> Double,
    alSaltar: (Double) -> Unit,
    modifier: Modifier = Modifier,
) {
    val p = LocalPaleta.current
    val color = ColoresPista[pista.color]
    var ancho by remember { mutableIntStateOf(0) }
    val columnas = if (ancho <= 0) 0
    else if (duracionTotal > 0) max(1, (ancho * pista.segundos / duracionTotal).roundToInt()).coerceAtMost(ancho)
    else ancho
    val datos = remember(pista.slot, pista.id, columnas) {
        if (columnas > 0) NativeAudio.overview(pista.slot, columnas) else null
    }
    val saltar by rememberUpdatedState(alSaltar)
    val total by rememberUpdatedState(duracionTotal)
    Canvas(
        modifier
            .onSizeChanged { ancho = it.width }
            .semantics { contentDescription = "Forma de onda de ${pista.nombre}. Toca para mover el cursor." }
            .pointerInput(Unit) {
                detectTapGestures { o -> if (total > 0) saltar(o.x / size.width * total) }
            },
    ) {
        drawRect(p.bg)
        drawLine(p.linea, Offset(0f, size.height / 2), Offset(size.width, size.height / 2), density)
        val d = datos
        val ch = pista.canales
        if (d != null && ch > 0) {
            val cols = d.size / (ch * 2)
            for (c in 0 until min(2, ch)) {
                val cy = if (ch == 2) size.height * (if (c == 0) 0.25f else 0.75f) else size.height / 2
                val amp = if (ch == 2) size.height * 0.23f else size.height * 0.46f
                for (x in 0 until cols) {
                    val mn = d[(x * ch + c) * 2]
                    val mx = d[(x * ch + c) * 2 + 1]
                    drawRect(color, Offset(x.toFloat(), cy - mx * amp), Size(1f, max(1f, (mx - mn) * amp)))
                }
            }
        }
        val px = if (total > 0) (posicion() / total * size.width).toFloat() else 0f
        drawRect(p.fg.copy(alpha = 0.08f), Offset.Zero, Size(px, size.height))
        drawRect(p.acento, Offset(px - density, 0f), Size(2 * density, size.height))
    }
}
