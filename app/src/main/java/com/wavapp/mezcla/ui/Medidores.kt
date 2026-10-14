package com.wavapp.mezcla.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.Medidor
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.Pedazo
import com.wavapp.mezcla.Pista
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow

// ---------------------------------------------------------------- medidores

private const val MED_MIN = -48f
private const val MED_MAX = 3f

/** Posición (0..1) de un nivel en dB en los medidores. */
private fun uMed(db: Float) = ((db - MED_MIN) / (MED_MAX - MED_MIN)).coerceIn(0f, 1f)

/** Verde hasta -12 dB, amarillo hasta -3 y rojo arriba, a lo ancho del medidor. */
private fun brochaNivel(p: Paleta, ancho: Float) = Brush.horizontalGradient(
    0f to p.verde, uMed(-12f) to p.verde, uMed(-6f) to p.amarillo, uMed(-3f) to p.amarillo,
    uMed(-1f) to p.rojo, 1f to p.rojo, startX = 0f, endX = ancho,
)

/** Medidor estéreo de una pista: L arriba y R abajo. Los niveles se leen al dibujar. */
@Composable
fun MedidorPista(m: Medidor, modifier: Modifier = Modifier) {
    val p = LocalPaleta.current
    Spacer(
        modifier.drawWithCache {
            val gap = 2.dp.toPx()
            val h = (size.height - gap) / 2
            val brocha = brochaNivel(p, size.width)
            val radio = CornerRadius(h / 2)
            onDrawBehind {
                for (c in 0..1) {
                    val y = c * (h + gap)
                    drawRoundRect(p.relleno, Offset(0f, y), Size(size.width, h), radio)
                    val w = uMed(if (c == 0) m.l else m.r) * size.width
                    if (w > 0.5f) drawRoundRect(brocha, Offset(0f, y), Size(max(w, h), h), radio)
                }
            }
        },
    )
}

/** Medidores L/R del master con marca de pico y luces de saturación (tocar las borra). */
@Composable
fun MedidoresMaster(modifier: Modifier = Modifier) {
    val p = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    val m = Mesa.master
    Column(modifier) {
        Spacer(
            Modifier
                .fillMaxWidth()
                .height(24.dp)
                .semantics { contentDescription = "Medidores del master. Toca para borrar los picos." }
                .pulsable(atenuado = 0.7f) { Mesa.borrarClip() }
                .drawWithCache {
                    val etiqueta = 16.dp.toPx()
                    val luz = 14.dp.toPx()
                    val ancho = size.width - etiqueta - luz
                    val gap = 4.dp.toPx()
                    val h = (size.height - gap) / 2
                    val brocha = brochaNivel(p, ancho)
                    val radio = CornerRadius(h / 2)
                    val estilo = TextStyle(color = p.texto2, fontSize = 10.sp, fontWeight = FontWeight.SemiBold)
                    val l = medidor.measure("L", estilo)
                    val r = medidor.measure("R", estilo)
                    onDrawBehind {
                        for (c in 0..1) {
                            val y = c * (h + gap)
                            val t = if (c == 0) l else r
                            drawText(t, topLeft = Offset(0f, y + h / 2 - t.size.height / 2f))
                            translate(etiqueta, 0f) {
                                drawRoundRect(p.relleno, Offset(0f, y), Size(ancho, h), radio)
                                val nivel = if (c == 0) m.l else m.r
                                val w = uMed(nivel) * ancho
                                if (w > 0.5f) {
                                    drawRoundRect(brocha, Offset(0f, y), Size(max(w, h), h), radio)
                                }
                                val tick = if (c == 0) m.tickL else m.tickR
                                if (tick > MED_MIN) {
                                    val xt = uMed(tick) * ancho
                                    drawRect(p.texto, Offset(min(ancho - 2.dp.toPx(), xt), y), Size(2.dp.toPx(), h))
                                }
                            }
                            val clip = if (c == 0) m.clipL else m.clipR
                            drawCircle(if (clip) p.rojo else p.relleno, h / 2 + 1.dp.toPx(), Offset(size.width - luz / 2 + 2.dp.toPx(), y + h / 2))
                        }
                    }
                },
        )
        // Escala fija (no se redibuja con el audio).
        Canvas(Modifier.fillMaxWidth().height(14.dp).padding(top = 2.dp)) {
            val etiqueta = 16.dp.toPx()
            val luz = 14.dp.toPx()
            val ancho = size.width - etiqueta - luz
            val estilo = TextStyle(color = p.texto2, fontSize = 9.sp)
            for (db in listOf(-48, -24, -12, -6, -3, 0)) {
                val t = medidor.measure(if (db == 0) "0" else "$db".replace('-', '−'), estilo)
                val x = etiqueta + uMed(db.toFloat()) * ancho - t.size.width / 2f
                drawText(t, topLeft = Offset(x.entre(etiqueta - 4.dp.toPx(), size.width - luz - t.size.width), 0f))
            }
        }
    }
}

// ---------------------------------------------------------------- escala del volumen master

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

/** Volumen master en un deslizador horizontal (0 = silencio, 1 = +6 dB). */
fun posicionMaster(db: Float): Float = 1f - uDesdeDb(db)
fun dbMaster(pos: Float): Float = dbDesdeU(1f - pos.coerceIn(0f, 1f))

// ---------------------------------------------------------------- ondas

/**
 * Agrega a [ruta] las barras de la onda de un pedazo (como Notas de Voz): una barra redondeada
 * cada [paso] px, simétrica, con el pico de los canales. El núcleo da la onda ya con sus fundidos.
 */
internal fun barrasPedazo(
    ruta: Path, pista: Pista, indice: Int, pz: Pedazo,
    x0: Float, x1: Float, arriba: Float, alto: Float, paso: Float, grosor: Float,
) {
    if (x1 - x0 < 1f || pz.largo <= 0) return
    val cols = max(1, ((x1 - x0) / paso).toInt())
    val d = Nucleo.regionWave(pista.id, indice, pz.inicio, pz.fin, cols) ?: return
    val ch = max(1, pista.canales)
    if (d.size < cols * ch * 2) return
    val cy = arriba + alto / 2
    val dx = (x1 - x0) / cols
    val g = min(grosor, dx * 0.8f)
    val radio = CornerRadius(g / 2)
    for (c in 0 until cols) {
        var pico = 0f
        for (k in 0 until ch) {
            val o = (c * ch + k) * 2
            pico = max(pico, max(-d[o], d[o + 1]))
        }
        val h = max(g, pico.coerceIn(0f, 1f).pow(0.75f) * alto)
        val x = x0 + (c + 0.5f) * dx
        ruta.addRoundRect(RoundRect(x - g / 2, cy - h / 2, x + g / 2, cy + h / 2, radio))
    }
}

/**
 * Dibuja la onda detallada de un pedazo (relleno continuo, un canal arriba y otro abajo si es
 * estéreo) entre [x0] y [x1]; solo se pide al núcleo la parte visible del lienzo.
 */
internal fun DrawScope.ondaPedazo(
    pista: Pista, indice: Int, pz: Pedazo, x0: Float, x1: Float, arriba: Float, alto: Float, color: Color,
) {
    val a = max(x0, 0f)
    val b = min(x1, size.width)
    if (b - a < 1f || pz.largo <= 0 || x1 <= x0) return
    val cols = max(1, ((b - a) / 1.5f).toInt())
    val cuadrosPorPx = pz.largo / (x1 - x0).toDouble()
    val desde = pz.inicio + ((a - x0) * cuadrosPorPx).toLong()
    val hasta = max(desde + 1, pz.inicio + ((b - x0) * cuadrosPorPx).toLong())
    val d = Nucleo.regionWave(pista.id, indice, desde, hasta, cols) ?: return
    val ch = max(1, pista.canales)
    if (d.size < cols * ch * 2) return
    val canales = min(2, ch)
    val paso = (b - a) / cols
    val minimo = 0.5f
    for (k in 0 until canales) {
        val cy = arriba + if (canales == 2) alto * (if (k == 0) 0.25f else 0.75f) else alto / 2
        val amp = if (canales == 2) alto * 0.235f else alto * 0.47f
        val ruta = Path()
        ruta.moveTo(a, min(cy - d[k * 2 + 1] * amp, cy - minimo))
        for (c in 0 until cols) {
            ruta.lineTo(a + (c + 0.5f) * paso, min(cy - d[(c * ch + k) * 2 + 1] * amp, cy - minimo))
        }
        ruta.lineTo(b, min(cy - d[((cols - 1) * ch + k) * 2 + 1] * amp, cy - minimo))
        ruta.lineTo(b, max(cy - d[((cols - 1) * ch + k) * 2] * amp, cy + minimo))
        for (c in cols - 1 downTo 0) {
            ruta.lineTo(a + (c + 0.5f) * paso, max(cy - d[(c * ch + k) * 2] * amp, cy + minimo))
        }
        ruta.lineTo(a, max(cy - d[k * 2] * amp, cy + minimo))
        ruta.close()
        drawPath(ruta, color)
    }
}

// ---------------------------------------------------------------- portadas

/** "Portada" de una pista: su color en degradado con su número, como un álbum. */
@Composable
fun Portada(numero: Int, color: Color, lado: Dp, modifier: Modifier = Modifier, radio: Dp = 8.dp, nombre: String? = null) {
    val tam = with(LocalDensity.current) { (lado * 0.44f).toSp() }
    Box(
        modifier
            .size(lado)
            .clip(RoundedCornerShape(radio))
            .drawBehind {
                drawRect(
                    Brush.linearGradient(
                        listOf(lerp(color, Color.White, 0.22f), color, lerp(color, Color.Black, 0.28f)),
                        start = Offset.Zero, end = Offset(size.width, size.height),
                    ),
                )
            },
    ) {
        Texto(
            "$numero", TextStyle(fontSize = tam, fontWeight = FontWeight.Bold), Color.White,
            Modifier.align(Alignment.Center), lineas = 1,
        )
        if (nombre != null) {
            Texto(
                nombre, Tipo.nota.seminegrita, Color.White.copy(alpha = 0.92f),
                Modifier.align(Alignment.BottomStart).padding(10.dp), lineas = 1,
            )
        }
    }
}

/** Portada de la mezcla: mosaico de 2×2 con las primeras pistas (como las listas de Apple Music). */
@Composable
fun Mosaico(pistas: List<Pista>, lado: Dp, modifier: Modifier = Modifier, radio: Dp = 8.dp, nombres: Boolean = false) {
    val p = LocalPaleta.current
    Box(modifier.size(lado).clip(RoundedCornerShape(radio))) {
        when {
            pistas.isEmpty() -> Box(
                Modifier.fillMaxSize().background(
                    Brush.linearGradient(listOf(lerp(p.acento, Color.White, 0.2f), lerp(p.acento, Color.Black, 0.25f))),
                ),
                contentAlignment = Alignment.Center,
            ) { Icono(Iconos.nota, Color.White, tamano = lado * 0.46f) }
            pistas.size < 4 -> {
                val a = pistas[0]
                Portada(a.id, p.pistas[a.color], lado, radio = 0.dp, nombre = if (nombres) a.nombre else null)
            }
            else -> Column {
                for (fila in 0..1) {
                    Row {
                        for (col in 0..1) {
                            val t = pistas[fila * 2 + col]
                            Portada(t.id, p.pistas[t.color], lado / 2, radio = 0.dp, nombre = if (nombres) t.nombre else null)
                        }
                    }
                }
            }
        }
    }
}
