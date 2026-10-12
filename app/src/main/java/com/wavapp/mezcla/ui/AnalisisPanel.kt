package com.wavapp.mezcla.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.Analisis
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Pista
import java.util.Locale
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sign
import kotlin.math.sin
import kotlin.math.sqrt

private const val RATE = 48000f

/** Panel de análisis a pantalla completa (Diagnóstico + espectro + espectrograma del prototipo). */
@Composable
fun PanelAnalisis(alCerrar: () -> Unit) {
    val pal = LocalPaleta.current
    BackHandler { alCerrar() }
    val fuente = Mesa.pistas.firstOrNull { it.id == Analisis.fuenteId }
    Column(
        Modifier
            .fillMaxSize()
            .background(pal.bg)
            .safeDrawingPadding()
            .verticalScroll(rememberScrollState())
            .testTag("panelAnalisis")
            .padding(vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Row(Modifier.padding(horizontal = 16.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Análisis", color = pal.fg, fontSize = 19.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
            Boton("Cerrar", alCerrar)
        }
        Transporte()
        Row(
            Modifier.padding(horizontal = 16.dp).horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Boton("Mezcla", { Analisis.fuenteId = -1 }, presionado = fuente == null, tamano = 12.sp, relleno = 10.dp)
            for (p in Mesa.pistas) {
                Boton("${p.id} · ${p.nombre}", { Analisis.fuenteId = p.id }, presionado = fuente == p,
                    tamano = 12.sp, relleno = 10.dp)
            }
        }
        Seccion("Diagnóstico · " + (fuente?.let { "${it.id} · ${it.nombre}" } ?: "mezcla completa")) {
            Row(horizontalArrangement = Arrangement.spacedBy(0.dp)) {
                Boton("Por fuentes", { Analisis.modoClasico = false }, presionado = !Analisis.modoClasico, tamano = 12.sp, relleno = 10.dp)
                Boton("Clásico", { Analisis.modoClasico = true }, presionado = Analisis.modoClasico, tamano = 12.sp, relleno = 10.dp)
            }
            Goniometro(Modifier.fillMaxWidth().aspectRatio(1.6f).clip(RoundedCornerShape(4.dp)))
            MedidoresFase()
        }
        Seccion("Espectro y zumbido") {
            Espectro(Modifier.fillMaxWidth().height(150.dp).clip(RoundedCornerShape(4.dp)))
            TextoZumbido()
            Row {
                Boton("Notch off", { Analisis.ponerNotch(0) }, presionado = Analisis.notch == 0, tamano = 12.sp, relleno = 10.dp)
                Boton("50 Hz", { Analisis.ponerNotch(50) }, presionado = Analisis.notch == 50, tamano = 12.sp, relleno = 10.dp)
                Boton("60 Hz", { Analisis.ponerNotch(60) }, presionado = Analisis.notch == 60, tamano = 12.sp, relleno = 10.dp)
            }
            Text(
                "El notch quita 50 o 60 Hz y 3 armónicos de toda la mezcla (también al exportar).",
                color = pal.tenue, fontSize = 11.sp,
            )
        }
        Seccion("Espectrograma · " + (fuente?.let { "${it.id} · ${it.nombre} · antes del EQ" } ?: "elige una pista")) {
            if (fuente == null) {
                Text("Elige una pista arriba para ver su espectrograma.", color = pal.tenue, fontSize = 13.sp)
            } else {
                Espectrograma(fuente, Modifier.fillMaxWidth().height(220.dp).clip(RoundedCornerShape(4.dp)))
                Text(
                    "El fondo es silencio; verde suena bajo, naranja medio y rojo fuerte. Una línea horizontal " +
                        "constante en 50/60 Hz o múltiplos suele ser zumbido. Toca para saltar a ese momento.",
                    color = pal.tenue, fontSize = 11.sp,
                )
            }
        }
    }
}

@Composable
private fun Seccion(titulo: String, contenido: @Composable () -> Unit) {
    val pal = LocalPaleta.current
    val forma = RoundedCornerShape(6.dp)
    Column(
        Modifier
            .padding(horizontal = 16.dp)
            .fillMaxWidth()
            .clip(forma)
            .background(pal.panel)
            .border(1.dp, pal.linea, forma)
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text(titulo.uppercase(), color = pal.tenue, fontSize = 12.sp, letterSpacing = 1.sp)
        contenido()
    }
}

// ---------------------------------------------------------------- goniómetro

private val MAXA = (75 * Math.PI / 180).toFloat()

@Composable
private fun Goniometro(modifier: Modifier) {
    val pal = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    // Estela: se dibuja sobre una imagen que se va desvaneciendo (como el canvas del prototipo).
    val estado = remember { EstadoGonio() }
    Canvas(modifier.background(pal.bg).semantics { contentDescription = "Goniómetro" }) {
        val v = Analisis.version
        val w = size.width.toInt()
        val h = size.height.toInt()
        if (w <= 0 || h <= 0) return@Canvas
        if (estado.img == null || estado.img!!.width != w || estado.img!!.height != h || estado.modo != Analisis.modoClasico) {
            estado.img = ImageBitmap(w, h)
            estado.modo = Analisis.modoClasico
            estado.limpiar(pal.bg)
        }
        val img = estado.img!!
        if (v != estado.version) {
            estado.version = v
            val lienzo = androidx.compose.ui.graphics.Canvas(img)
            val pintura = Paint().apply { color = pal.bg.copy(alpha = 0.35f) }
            lienzo.drawRect(0f, 0f, w.toFloat(), h.toFloat(), pintura)
            if (!Analisis.activo) estado.limpiar(pal.bg)
            else if (Analisis.modoClasico) clasico(lienzo, estado, w.toFloat(), h.toFloat(), density, pal.verde)
            else fuentes(lienzo, estado, w.toFloat(), h.toFloat(), density, pal.verde)
        }
        drawImage(img)
        val d = density
        val estilo = TextStyle(color = pal.tenue, fontSize = 10.sp, fontFamily = Mono)
        if (Analisis.modoClasico) {
            val lin = pal.linea
            drawLine(lin, Offset(size.width / 2, 0f), Offset(size.width / 2, size.height), d)
            drawLine(lin, Offset(0f, size.height / 2), Offset(size.width, size.height / 2), d)
            drawLine(lin, Offset(0f, size.height), Offset(size.width, 0f), d)
            drawLine(lin, Offset(0f, 0f), Offset(size.width, size.height), d)
            drawText(medidor, "M", Offset(size.width / 2 + 4 * d, 2 * d), estilo)
            drawText(medidor, "L", Offset(5 * d, 2 * d), estilo)
            drawText(medidor, "R", Offset(size.width - 12 * d, 2 * d), estilo)
            drawText(medidor, "S", Offset(size.width - 12 * d, size.height / 2 - 16 * d), estilo)
            if (Analisis.activo) {
                drawText(medidor, "auto +" + max(0f, 20 * kotlin.math.log10(1f / estado.picoVector)).roundToInt() + " dB",
                    Offset(5 * d, size.height - 16 * d), estilo.copy(fontSize = 9.sp))
            }
        } else {
            val (cx, cy, r) = geomFuentes(size.width, size.height, d)
            for (k in -3..3) {
                val a = k * 25 * Math.PI.toFloat() / 180
                drawLine(pal.linea, Offset(cx, cy), Offset(cx + sin(a) * r, cy - cos(a) * r), d)
            }
            for (f in listOf(0.5f, 1f)) {
                drawArc(pal.linea, -90f - 75f, 150f, false, Offset(cx - r * f, cy - r * f), Size(2 * r * f, 2 * r * f), style = Stroke(d))
            }
            drawText(medidor, "L", Offset(cx - sin(MAXA) * r - 4 * d, cy - cos(MAXA) * r - 18 * d), estilo)
            drawText(medidor, "R", Offset(cx + sin(MAXA) * r - 4 * d, cy - cos(MAXA) * r - 18 * d), estilo)
            drawText(medidor, "C", Offset(cx - 3 * d, cy - r - 16 * d), estilo)
        }
    }
}

private class EstadoGonio {
    var img: ImageBitmap? = null
    var modo = false
    var version = -1
    var picoFuentes = -60f
    var picoVector = 0.05f
    fun limpiar(bg: Color) {
        val i = img ?: return
        androidx.compose.ui.graphics.Canvas(i).drawRect(0f, 0f, i.width.toFloat(), i.height.toFloat(), Paint().apply { color = bg })
        picoFuentes = -60f
        picoVector = 0.05f
    }
}

private data class Geom(val cx: Float, val cy: Float, val r: Float)

private fun geomFuentes(w: Float, h: Float, d: Float): Geom {
    val cy = h - 16 * d
    return Geom(w / 2, cy, min(cy - 8 * d, (w / 2 - 6 * d) / sin(MAXA)))
}

/** Modo "Por fuentes": cada pico del espectro es un rayo en su ángulo de paneo. */
private fun fuentes(c: androidx.compose.ui.graphics.Canvas, e: EstadoGonio, w: Float, h: Float, d: Float, verde: Color) {
    val (cx, cy, r) = geomFuentes(w, h, d)
    val hz = RATE / 2048
    val a0 = max(1, kotlin.math.ceil(40 / hz).toInt())
    val a1 = min(Analisis.BINS_FUENTES - 1, (16000 / hz).toInt())
    val en = Analisis.fanE
    val pn = Analisis.fanP
    var pk = -200f
    for (i in a0..a1) if (en[i] > pk) pk = en[i]
    e.picoFuentes = if (pk > e.picoFuentes) pk else e.picoFuentes * 0.97f + pk * 0.03f
    if (pk < -85) return
    val span = 36f
    val top = e.picoFuentes
    val pintura = Paint().apply { color = verde; strokeWidth = d }
    for (i in a0..a1) {
        if (i > a0 && i < a1 && !(en[i] >= en[i - 1] && en[i] > en[i + 1])) continue  // solo picos
        val t = (en[i] - (top - span)) / span
        if (t < 0.04f) continue
        val p = pn[i]
        val ang = sign(p) * abs(p).pow(0.65f) * MAXA
        val rr = r * (0.08f + 0.92f * min(1f, t))
        val x = cx + sin(ang) * rr
        val y = cy - cos(ang) * rr
        pintura.alpha = 0.25f + 0.5f * min(1f, t)
        c.drawLine(Offset(cx, cy), Offset(x, y), pintura)
        pintura.alpha = 1f
        c.drawRect(x - 1.2f * d, y - 1.2f * d, x + 1.2f * d, y + 1.2f * d, pintura)
    }
}

/** Modo clásico: nube de puntos M/S con ganancia automática. */
private fun clasico(c: androidx.compose.ui.graphics.Canvas, e: EstadoGonio, w: Float, h: Float, d: Float, verde: Color) {
    val n = Analisis.TIEMPO
    val t = Analisis.tiempo
    val k = sqrt(0.5f)
    var a = 0f
    for (i in 0 until n) {
        val l = t[i]
        val r = t[n + i]
        a = max(a, max(abs(r - l) * k, abs(l + r) * k))
    }
    e.picoVector = max(a, max(e.picoVector * 0.985f, 0.003f))
    val sx = w * 0.45f / e.picoVector
    val sy = h * 0.45f / e.picoVector
    val pintura = Paint().apply { color = verde }
    for (i in 0 until n) {
        val l = t[i]
        val r = t[n + i]
        val x = w / 2 + (r - l) * k * sx
        val y = h / 2 - (l + r) * k * sy
        c.drawRect(x - d, y - d, x + d, y + d, pintura)
    }
}

// ---------------------------------------------------------------- medidores

@Composable
private fun MedidoresFase() {
    val pal = LocalPaleta.current
    @Suppress("UNUSED_VARIABLE") val v = Analisis.version
    val activo = Analisis.activo && Analisis.fuerte
    val pan = Analisis.paneo
    val cor = Analisis.correlacion
    Column(verticalArrangement = Arrangement.spacedBy(9.dp)) {
        FilaMedidor(
            "Pan", if (!activo) "—" else if (abs(pan) < 0.04f) "centro" else (if (pan < 0) "L " else "R ") + (abs(pan) * 100).roundToInt() + "%",
        ) {
            Canvas(it) {
                drawLine(pal.linea, Offset(size.width / 2, 0f), Offset(size.width / 2, size.height), density)
                val x = size.width * (0.5f + 0.5f * pan)
                drawRect(pal.fg, Offset(x - density, 0f), Size(2 * density, size.height))
            }
        }
        FilaMedidor("Fase", (if (cor >= 0) "+" else "") + String.format(Locale.US, "%.2f", cor)) {
            Canvas(it) {
                val rojo = pal.rojo.copy(alpha = 0.6f)
                val ambar = pal.ambar.copy(alpha = 0.6f)
                val verde = pal.verde.copy(alpha = 0.6f)
                drawRect(Brush.horizontalGradient(0f to rojo, 0.4f to rojo, 0.4f to ambar, 0.65f to ambar, 0.65f to verde, 1f to verde))
                drawLine(pal.fg.copy(alpha = 0.35f), Offset(size.width / 2, 0f), Offset(size.width / 2, size.height), density)
                val x = size.width * (cor + 1) / 2
                drawRect(pal.fg, Offset(x - density, 0f), Size(2 * density, size.height))
            }
        }
        val (texto, malo) = Analisis.veredicto()
        Text(
            texto, color = if (malo) pal.rojo else pal.fg, fontSize = 13.sp,
            fontWeight = if (malo) FontWeight.SemiBold else FontWeight.Normal,
        )
    }
}

@Composable
private fun FilaMedidor(etiqueta: String, valor: String, barra: @Composable (Modifier) -> Unit) {
    val pal = LocalPaleta.current
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(etiqueta, color = pal.tenue, fontFamily = Mono, fontSize = 12.sp, modifier = Modifier.width(40.dp))
        barra(
            Modifier.weight(1f).height(10.dp).clip(RoundedCornerShape(2.dp)).background(pal.bg)
                .border(1.dp, pal.linea, RoundedCornerShape(2.dp)),
        )
        Text(valor, color = pal.fg, fontFamily = Mono, fontSize = 12.sp, modifier = Modifier.width(70.dp).padding(start = 8.dp))
    }
}

// ---------------------------------------------------------------- espectro

/** Dibuja el espectro en vivo (de -110 a -10 dB, eje logarítmico de 20 Hz a 20 kHz). */
fun DrawScope.lineaEspectro(color: Color, relleno: Boolean, alto: Float = size.height) {
    val w = size.width
    val hz = RATE / 16384
    val sp = Analisis.espectro
    val ruta = Path()
    var x = 0f
    val paso = 2f
    var primero = true
    while (x <= w) {
        val f0 = 20f * 1000f.pow(x / w)
        val f1 = 20f * 1000f.pow((x + paso) / w)
        val a = max(0, (f0 / hz).toInt())
        val b = min(sp.size - 1, max(a, (f1 / hz).toInt()))
        var m = -200f
        for (i in a..b) if (sp[i] > m) m = sp[i]
        val y = alto - (max(-110f, m) + 110f) / 100f * alto
        if (primero) {
            if (relleno) { ruta.moveTo(0f, alto); ruta.lineTo(x, y) } else ruta.moveTo(x, y)
            primero = false
        } else ruta.lineTo(x, y)
        x += paso
    }
    if (relleno) {
        ruta.lineTo(w, alto)
        ruta.close()
        drawPath(ruta, color)
    } else {
        drawPath(ruta, color, style = Stroke(1.5f * density))
    }
}

@Composable
private fun Espectro(modifier: Modifier) {
    val pal = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    Canvas(modifier.background(pal.bg).semantics { contentDescription = "Espectro con marcas de zumbido" }) {
        @Suppress("UNUSED_VARIABLE") val v = Analisis.version
        val w = size.width
        val h = size.height
        val d = density
        fun x(f: Float) = ln(f / 20f) / ln(1000f) * w
        fun y(db: Float) = h - (db + 110f) / 100f * h
        val estilo = TextStyle(color = pal.tenue, fontSize = 9.sp, fontFamily = Mono)
        for (f in listOf(100f, 1000f, 10000f)) {
            drawLine(pal.linea, Offset(x(f), 0f), Offset(x(f), h), d)
            drawText(medidor, etiquetaHz(f), Offset(x(f) + 3 * d, h - 14 * d), estilo)
        }
        for (db in listOf(-90f, -60f, -30f)) {
            drawLine(pal.linea, Offset(0f, y(db)), Offset(w, y(db)), d)
            drawText(medidor, "${db.toInt()} dB", Offset(3 * d, y(db) - 13 * d), estilo)
        }
        val guion = PathEffect.dashPathEffect(floatArrayOf(3 * d, 3 * d))
        for (f in listOf(50f, 60f)) {
            drawLine(pal.linea, Offset(x(f), 0f), Offset(x(f), h), d, pathEffect = guion)
            drawText(medidor, "${f.toInt()}", Offset(x(f) + 2 * d, 0f), estilo)
        }
        if (Analisis.activo && Analisis.zumbidoHz > 0) {
            for (k in 1..4) {
                val f = Analisis.zumbidoHz * k.toFloat()
                drawLine(pal.rojo, Offset(x(f), 12 * d), Offset(x(f), h), 2 * d)
            }
        }
        if (Analisis.activo) lineaEspectro(pal.acento, relleno = false)
    }
}

@Composable
private fun TextoZumbido() {
    val pal = LocalPaleta.current
    @Suppress("UNUSED_VARIABLE") val v = Analisis.version
    val (texto, alerta) = Analisis.textoZumbido()
    Text(texto, color = if (alerta) pal.ambar else pal.tenue, fontSize = 13.sp,
        fontWeight = if (alerta) FontWeight.SemiBold else FontWeight.Normal)
}

// ---------------------------------------------------------------- espectrograma

/** Colores del prototipo: fondo → verde oscuro → verde → ámbar → rojo → caliente. */
private fun tablaColores(p: Paleta): IntArray {
    val bg = p.bg
    val oscuroFondo = (bg.red + bg.green + bg.blue) / 3 < 0.5f
    val caliente = if (oscuroFondo) Color(255, 245, 235) else Color(90, 12, 12)
    val oscuro = if (oscuroFondo) Color(14, 60, 34) else Color(40, 120, 70)
    val paradas = listOf(0f to bg, 0.28f to oscuro, 0.55f to p.verde, 0.78f to p.ambar, 0.92f to p.rojo, 1f to caliente)
    return IntArray(256) { v ->
        val t = v / 255f
        var i = 0
        while (i < paradas.size - 2 && t > paradas[i + 1].first) i++
        val (t0, c0) = paradas[i]
        val (t1, c1) = paradas[i + 1]
        val f = ((t - t0) / (t1 - t0)).coerceIn(0f, 1f)
        Color(
            c0.red + (c1.red - c0.red) * f, c0.green + (c1.green - c0.green) * f,
            c0.blue + (c1.blue - c0.blue) * f,
        ).toArgb()
    }
}

@Composable
private fun Espectrograma(p: Pista, modifier: Modifier) {
    val pal = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    LaunchedEffect(p.id) { Analisis.pedirEspectrograma(p) }
    val datos = Analisis.espectrogramas[p.id]
    val imagen = remember(datos, pal) {
        datos?.let {
            val lut = tablaColores(pal)
            val cols = Analisis.SG_COLS
            val filas = Analisis.SG_FILAS
            val px = IntArray(cols * filas)
            for (c in 0 until cols) for (r in 0 until filas) {
                px[(filas - 1 - r) * cols + c] = lut[it[c * filas + r].toInt() and 0xFF]
            }
            android.graphics.Bitmap.createBitmap(px, cols, filas, android.graphics.Bitmap.Config.ARGB_8888).asImageBitmap()
        }
    }
    val total by rememberUpdatedState(Mesa.duracion)
    BoxWithConstraints(modifier.background(pal.bg)) {
        if (imagen == null) {
            Text("calculando…", color = pal.tenue, fontSize = 12.sp, modifier = Modifier.align(Alignment.Center))
        }
        Canvas(
            Modifier
                .fillMaxSize()
                .semantics { contentDescription = "Espectrograma de ${p.nombre}. Toca para saltar." }
                .pointerInput(Unit) {
                    detectTapGestures { o -> if (total > 0) Mesa.saltar(o.x / size.width * total) }
                },
        ) {
            val w = size.width
            val h = size.height
            val d = density
            val dur = if (total > 0) total else p.segundos
            if (imagen != null) {
                drawImage(
                    imagen, srcOffset = IntOffset.Zero, srcSize = IntSize(imagen.width, imagen.height),
                    dstOffset = IntOffset.Zero, dstSize = IntSize((w * (p.segundos / dur)).toInt().coerceAtLeast(1), h.toInt()),
                )
            }
            val y = { f: Float -> h - ln(f / 20f) / ln(Analisis.SG_TOPE / 20f) * h }
            val estilo = TextStyle(color = pal.fg, fontSize = 9.sp, fontFamily = Mono, fontWeight = FontWeight.Medium)
            for (f in listOf(50f, 100f, 200f, 500f, 1000f, 2000f, 5000f, 10000f)) {
                drawLine(pal.tenue.copy(alpha = 0.3f), Offset(0f, y(f)), Offset(w, y(f)), d)
                drawText(medidor, etiquetaHz(f), Offset(4 * d, y(f) - 14 * d), estilo)
            }
            val pasos = listOf(1, 2, 5, 10, 15, 30, 60, 120, 300, 600)
            val paso = pasos.firstOrNull { it >= dur / 8 } ?: 600
            var t = paso
            while (t < dur) {
                val x = (t / dur * w).toFloat()
                drawLine(pal.tenue.copy(alpha = 0.3f), Offset(x, 0f), Offset(x, h), d)
                val etiqueta = if (t >= 60) "${t / 60}:" + String.format(Locale.US, "%02d", t % 60) else "${t}s"
                drawText(medidor, etiqueta, Offset(x + 3 * d, h - 15 * d), estilo)
                t += paso
            }
            val px = if (dur > 0) (Mesa.posicion / dur * w).toFloat() else 0f
            drawRect(pal.acento, Offset(px - d, 0f), Size(2 * d, h))
        }
    }
}
