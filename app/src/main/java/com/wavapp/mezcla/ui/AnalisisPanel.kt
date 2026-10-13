package com.wavapp.mezcla.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
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
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.Analisis
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.Pista
import java.util.Locale
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

private const val SG_TOPE = 20000f  // el espectrograma del núcleo llega hasta 20 kHz

/** Fondo de las gráficas de análisis (como la pantalla de un instrumento). */
private fun fondoGrafica(p: Paleta) = if (p.oscuro) Color(0xFF0B0B0C) else Color(0xFFF6F6F8)

/** Pestaña "Análisis": diagnóstico de fase y paneo, espectro con zumbido y espectrograma. */
@Composable
fun PestanaAnalisis(abajo: Dp) {
    val lista = rememberLazyListState()
    val fuente = Mesa.pistas.firstOrNull { it.id == Analisis.fuenteId }
    LaunchedEffect(fuente?.id) { fuente?.let { Analisis.pedirEspectrograma(it) } }
    Box(Modifier.fillMaxSize()) {
        LazyColumn(
            Modifier.fillMaxSize().testTag("panelAnalisis"),
            state = lista,
            contentPadding = PaddingValues(bottom = abajo + 20.dp),
        ) {
            item(key = "espacio") { EspacioEncabezado() }
            item(key = "titulo") { TituloGrande("Análisis") }
            if (Mesa.pistas.isEmpty()) {
                item(key = "vacio") { PieGrupo("Agrega pistas en Mezcla para analizarlas.", Modifier.padding(top = 12.dp)) }
                return@LazyColumn
            }
            item(key = "lufsTitulo") { EncabezadoSeccion("Volumen percibido", detalle = "LUFS · la mezcla") }
            item(key = "lufs") { TarjetaLufs() }
            item(key = "lufsCancionTitulo") { EncabezadoSeccion("Canción completa") }
            item(key = "lufsCancion") { TarjetaLufsCancion() }
            item(key = "lufsPie") {
                PieGrupo(
                    "LUFS es el volumen como lo oye la gente (no solo los picos). Se mide lo que sale, después del " +
                        "master. Spotify y YouTube ponen todo a −14 LUFS: más fuerte no suena más fuerte allá.",
                )
            }
            item(key = "fuentesTitulo") { EncabezadoSeccion("Analizar") }
            item(key = "fuentes") { Fuentes(fuente) }
            item(key = "diagTitulo") {
                val detalle = when {
                    Analisis.fuenteId == Nucleo.ID_ENTRADA -> if (Mesa.entrada.abierta) "Micrófono en vivo" else "Micrófono (cerrado)"
                    else -> fuente?.nombre ?: "Mezcla completa"
                }
                EncabezadoSeccion("Diagnóstico", detalle = detalle)
            }
            item(key = "diag") {
                Tarjeta(Modifier.padding(horizontal = 16.dp)) {
                    Segmentado(
                        listOf("Por fuentes", "Clásico"), if (Analisis.modoClasico) 1 else 0,
                        { Analisis.modoClasico = it == 1 }, Modifier.fillMaxWidth(),
                    )
                    Spacer(Modifier.height(12.dp))
                    Goniometro(Modifier.fillMaxWidth().aspectRatio(1.6f).clip(RoundedCornerShape(10.dp)))
                    Spacer(Modifier.height(14.dp))
                    MedidoresFase()
                }
            }
            item(key = "espTitulo") { EncabezadoSeccion("Espectro y zumbido") }
            item(key = "esp") {
                Tarjeta(Modifier.padding(horizontal = 16.dp)) {
                    Espectro(Modifier.fillMaxWidth().height(150.dp).clip(RoundedCornerShape(10.dp)))
                    Spacer(Modifier.height(10.dp))
                    TextoZumbido()
                    Spacer(Modifier.height(12.dp))
                    Segmentado(
                        listOf("Sin notch", "50 Hz", "60 Hz"), when (Analisis.notch) { 50 -> 1; 60 -> 2; else -> 0 },
                        { Analisis.ponerNotch(listOf(0, 50, 60)[it]) }, Modifier.fillMaxWidth(),
                    )
                }
            }
            item(key = "espPie") {
                PieGrupo("El notch quita 50 o 60 Hz y 3 armónicos de toda la mezcla (también al exportar).")
            }
            item(key = "sgTitulo") {
                EncabezadoSeccion("Espectrograma", detalle = if (fuente != null) "antes del EQ" else null)
            }
            item(key = "sg") {
                Tarjeta(Modifier.padding(horizontal = 16.dp)) {
                    if (fuente == null) {
                        Texto("Elige una pista arriba para ver su espectrograma.", Tipo.subtitulo, LocalPaleta.current.texto2)
                    } else {
                        Espectrograma(fuente, Modifier.fillMaxWidth().height(220.dp).clip(RoundedCornerShape(10.dp)))
                    }
                }
            }
            if (fuente != null) {
                item(key = "sgPie") {
                    PieGrupo(
                        "El fondo es silencio; verde suena bajo, naranja medio y rojo fuerte. Una línea horizontal " +
                            "constante en 50/60 Hz o múltiplos suele ser zumbido. Toca para saltar a ese momento.",
                    )
                }
            }
        }
        EncabezadoFlotante(lista, "Análisis")
    }
}

/** Qué se analiza: la mezcla o una pista. */
@Composable
private fun Fuentes(fuente: Pista?) {
    val p = LocalPaleta.current
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Chip("Mezcla", fuente == null && !Analisis.deEntrada, { Analisis.fuenteId = -1 })
        Chip(
            "Micrófono", Analisis.fuenteId == Nucleo.ID_ENTRADA, { Analisis.fuenteId = Nucleo.ID_ENTRADA },
            icono = Iconos.microfono,
        )
        for (pista in Mesa.pistas) {
            Chip(
                "${pista.id} · ${pista.nombre}", fuente == pista, { Analisis.fuenteId = pista.id },
                colorActivo = p.pistas[pista.color],
            )
        }
    }
}

// ---------------------------------------------------------------- goniómetro

private val MAXA = (75 * Math.PI / 180).toFloat()

@Composable
private fun Goniometro(modifier: Modifier) {
    val pal = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    val fondo = fondoGrafica(pal)
    val linea = pal.separador
    // Estela: se dibuja sobre una imagen que se va desvaneciendo (como el canvas del prototipo).
    val estado = remember { EstadoGonio() }
    Canvas(modifier.background(fondo).semantics { contentDescription = "Goniómetro" }) {
        val v = Analisis.version
        val w = size.width.toInt()
        val h = size.height.toInt()
        if (w <= 0 || h <= 0) return@Canvas
        val img0 = estado.img
        if (img0 == null || img0.width != w || img0.height != h || estado.modo != Analisis.modoClasico || estado.fondo != fondo) {
            estado.img = ImageBitmap(w, h)
            estado.modo = Analisis.modoClasico
            estado.fondo = fondo
            estado.limpiar()
        }
        val img = estado.img ?: return@Canvas
        if (v != estado.version) {
            estado.version = v
            val lienzo = androidx.compose.ui.graphics.Canvas(img)
            lienzo.drawRect(0f, 0f, w.toFloat(), h.toFloat(), Paint().apply { color = fondo.copy(alpha = 0.35f) })
            if (!Analisis.activo) estado.limpiar()
            else if (Analisis.modoClasico) clasico(lienzo, estado, w.toFloat(), h.toFloat(), density, pal.verde)
            else fuentes(lienzo, estado, w.toFloat(), h.toFloat(), density, pal.verde)
        }
        drawImage(img)
        val d = density
        val estilo = TextStyle(color = pal.texto2, fontSize = 10.sp, fontWeight = FontWeight.SemiBold)
        if (Analisis.modoClasico) {
            drawLine(linea, Offset(size.width / 2, 0f), Offset(size.width / 2, size.height), d)
            drawLine(linea, Offset(0f, size.height / 2), Offset(size.width, size.height / 2), d)
            drawLine(linea, Offset(0f, size.height), Offset(size.width, 0f), d)
            drawLine(linea, Offset(0f, 0f), Offset(size.width, size.height), d)
            drawText(medidor, "M", Offset(size.width / 2 + 5 * d, 4 * d), estilo)
            drawText(medidor, "L", Offset(6 * d, 4 * d), estilo)
            drawText(medidor, "R", Offset(size.width - 14 * d, 4 * d), estilo)
            drawText(medidor, "S", Offset(size.width - 14 * d, size.height / 2 - 17 * d), estilo)
            if (Analisis.activo) {
                drawText(
                    medidor, "auto +" + Analisis.gananciaGonio + " dB", Offset(6 * d, size.height - 18 * d),
                    estilo.copy(fontSize = 9.sp, fontWeight = FontWeight.Normal),
                )
            }
        } else {
            val (cx, cy, r) = geomFuentes(size.width, size.height, d)
            for (k in -3..3) {
                val a = k * 25 * Math.PI.toFloat() / 180
                drawLine(linea, Offset(cx, cy), Offset(cx + sin(a) * r, cy - cos(a) * r), d)
            }
            for (f in listOf(0.5f, 1f)) {
                drawArc(
                    linea, -90f - 75f, 150f, false, Offset(cx - r * f, cy - r * f), Size(2 * r * f, 2 * r * f),
                    style = Stroke(d),
                )
            }
            drawText(medidor, "L", Offset(cx - sin(MAXA) * r - 4 * d, cy - cos(MAXA) * r - 18 * d), estilo)
            drawText(medidor, "R", Offset(cx + sin(MAXA) * r - 4 * d, cy - cos(MAXA) * r - 18 * d), estilo)
            drawText(medidor, "C", Offset(cx - 3 * d, cy - r - 17 * d), estilo)
        }
    }
}

private class EstadoGonio {
    var img: ImageBitmap? = null
    var modo = false
    var version = -1
    var fondo = Color.Transparent
    val datos = FloatArray(4096)  // lo que manda el núcleo: puntos o rayos
    fun limpiar() {
        val i = img ?: return
        androidx.compose.ui.graphics.Canvas(i).drawRect(0f, 0f, i.width.toFloat(), i.height.toFloat(), Paint().apply { color = fondo })
    }
}

private data class Geom(val cx: Float, val cy: Float, val r: Float)

private fun geomFuentes(w: Float, h: Float, d: Float): Geom {
    val cy = h - 16 * d
    return Geom(w / 2, cy, min(cy - 10 * d, (w / 2 - 8 * d) / sin(MAXA)))
}

/** Modo "Por fuentes": cada parte del sonido es un rayo en su ángulo de paneo (los calcula el núcleo). */
private fun fuentes(c: androidx.compose.ui.graphics.Canvas, e: EstadoGonio, w: Float, h: Float, d: Float, verde: Color) {
    val (cx, cy, r) = geomFuentes(w, h, d)
    val n = Nucleo.goniometer(e.datos)
    val pintura = Paint().apply { color = verde; strokeWidth = d }
    for (i in 0 until n) {
        val ang = e.datos[3 * i]
        val rr = r * e.datos[3 * i + 1]
        val x = cx + sin(ang) * rr
        val y = cy - cos(ang) * rr
        pintura.alpha = e.datos[3 * i + 2]
        c.drawLine(Offset(cx, cy), Offset(x, y), pintura)
        pintura.alpha = 1f
        c.drawRect(x - 1.2f * d, y - 1.2f * d, x + 1.2f * d, y + 1.2f * d, pintura)
    }
}

/** Modo clásico: nube de puntos M/S, ya con la ganancia automática del núcleo (1 = 45 % del ancho). */
private fun clasico(c: androidx.compose.ui.graphics.Canvas, e: EstadoGonio, w: Float, h: Float, d: Float, verde: Color) {
    val n = Nucleo.goniometer(e.datos)
    val pintura = Paint().apply { color = verde }
    for (i in 0 until n) {
        val x = w / 2 + e.datos[2 * i] * w * 0.45f
        val y = h / 2 - e.datos[2 * i + 1] * h * 0.45f
        c.drawRect(x - d, y - d, x + d, y + d, pintura)
    }
}

// ---------------------------------------------------------------- fase y paneo

@Composable
private fun MedidoresFase() {
    val pal = LocalPaleta.current
    @Suppress("UNUSED_VARIABLE") val v = Analisis.version
    val activo = Analisis.activo && Analisis.fuerte
    val pan = Analisis.paneo
    val cor = Analisis.correlacion
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        FilaMedidor(
            "Paneo",
            if (!activo) "—" else if (abs(pan) < 0.04f) "Centro" else (if (pan < 0) "L " else "R ") + (abs(pan) * 100).roundToInt() + "%",
        ) {
            Canvas(it) {
                val r = CornerRadius(size.height / 2)
                drawRoundRect(pal.relleno, cornerRadius = r)
                drawLine(pal.texto3, Offset(size.width / 2, 0f), Offset(size.width / 2, size.height), density)
                val x = size.width * (0.5f + 0.5f * pan).coerceIn(0f, 1f)
                drawCircle(Color.White, size.height / 2 + 2 * density, Offset(x, size.height / 2))
                drawCircle(pal.texto, size.height / 2 + 2 * density, Offset(x, size.height / 2), style = Stroke(1.5f * density))
            }
        }
        FilaMedidor("Fase", (if (cor >= 0) "+" else "−") + String.format(Locale.US, "%.2f", abs(cor))) {
            Canvas(it) {
                val r = CornerRadius(size.height / 2)
                drawRoundRect(
                    Brush.horizontalGradient(
                        0f to pal.rojo, 0.38f to pal.rojo, 0.46f to pal.amarillo, 0.62f to pal.amarillo, 0.7f to pal.verde,
                        1f to pal.verde,
                    ),
                    cornerRadius = r, alpha = 0.75f,
                )
                val x = size.width * ((cor + 1) / 2).coerceIn(0f, 1f)
                drawCircle(Color.White, size.height / 2 + 2 * density, Offset(x, size.height / 2))
                drawCircle(pal.texto, size.height / 2 + 2 * density, Offset(x, size.height / 2), style = Stroke(1.5f * density))
            }
        }
        val (texto, malo) = Analisis.veredicto()
        Texto(texto, if (malo) Tipo.subtitulo.seminegrita else Tipo.subtitulo, if (malo) pal.rojo else pal.texto)
    }
}

@Composable
private fun FilaMedidor(etiqueta: String, valor: String, barra: @Composable (Modifier) -> Unit) {
    val pal = LocalPaleta.current
    Row(verticalAlignment = Alignment.CenterVertically) {
        Texto(etiqueta, Tipo.nota, pal.texto2, Modifier.width(52.dp), lineas = 1)
        barra(Modifier.weight(1f).height(8.dp))
        Texto(valor, Tipo.nota.numeros, pal.texto, Modifier.width(70.dp).padding(start = 10.dp), lineas = 1)
    }
}

// ---------------------------------------------------------------- espectro

/** Dibuja el espectro en vivo (de -110 a -10 dB, eje logarítmico de 20 Hz a 20 kHz). El núcleo lo da por columnas. */
fun DrawScope.lineaEspectro(color: Color, relleno: Boolean, alto: Float = size.height) {
    val w = size.width
    val n = max(2, (w / 2).toInt())
    val sp = FloatArray(n)
    Nucleo.spectrumColumns(sp)
    val ruta = Path()
    for (c in 0 until n) {
        val x = c * w / (n - 1)
        val y = alto - (max(-110f, sp[c]) + 110f) / 100f * alto
        if (c == 0) {
            if (relleno) {
                ruta.moveTo(0f, alto)
                ruta.lineTo(x, y)
            } else {
                ruta.moveTo(x, y)
            }
        } else {
            ruta.lineTo(x, y)
        }
    }
    if (relleno) {
        ruta.lineTo(w, alto)
        ruta.close()
        drawPath(ruta, color)
    } else {
        drawPath(ruta, color, style = Stroke(1.6f * density))
    }
}

@Composable
private fun Espectro(modifier: Modifier) {
    val pal = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    val fondo = fondoGrafica(pal)
    Canvas(modifier.background(fondo).semantics { contentDescription = "Espectro con marcas de zumbido" }) {
        @Suppress("UNUSED_VARIABLE") val v = Analisis.version
        val w = size.width
        val h = size.height
        val d = density
        fun x(f: Float) = ln(f / 20f) / ln(1000f) * w
        fun y(db: Float) = h - (db + 110f) / 100f * h
        val estilo = TextStyle(color = pal.texto2, fontSize = 9.sp)
        for (f in listOf(100f, 1000f, 10000f)) {
            drawLine(pal.separador, Offset(x(f), 0f), Offset(x(f), h), d)
            drawText(medidor, etiquetaHz(f), Offset(x(f) + 3 * d, h - 14 * d), estilo)
        }
        for (db in listOf(-90f, -60f, -30f)) {
            drawLine(pal.separador, Offset(0f, y(db)), Offset(w, y(db)), d)
            drawText(medidor, "${db.toInt()} dB".replace('-', '−'), Offset(4 * d, y(db) - 13 * d), estilo)
        }
        val guion = PathEffect.dashPathEffect(floatArrayOf(3 * d, 3 * d))
        for (f in listOf(50f, 60f)) {
            drawLine(pal.texto3, Offset(x(f), 0f), Offset(x(f), h), d, pathEffect = guion)
            drawText(medidor, "${f.toInt()}", Offset(x(f) + 2 * d, 2 * d), estilo)
        }
        if (Analisis.activo && Analisis.zumbidoHz > 0) {
            for (k in 1..4) {
                val f = Analisis.zumbidoHz * k.toFloat()
                drawLine(pal.rojo, Offset(x(f), 14 * d), Offset(x(f), h), 2 * d)
            }
        }
        if (Analisis.activo) {
            lineaEspectro(pal.acento.copy(alpha = 0.14f), relleno = true)
            lineaEspectro(pal.acento, relleno = false)
        }
    }
}

@Composable
private fun TextoZumbido() {
    val pal = LocalPaleta.current
    @Suppress("UNUSED_VARIABLE") val v = Analisis.version
    val (texto, alerta) = Analisis.textoZumbido()
    Texto(texto, if (alerta) Tipo.subtitulo.seminegrita else Tipo.subtitulo, if (alerta) pal.naranja else pal.texto2)
}

// ---------------------------------------------------------------- espectrograma

/** Colores del prototipo: fondo → verde oscuro → verde → ámbar → rojo → caliente. */
private fun tablaColores(p: Paleta, bg: Color): IntArray {
    val caliente = if (p.oscuro) Color(255, 245, 235) else Color(90, 12, 12)
    val oscuro = if (p.oscuro) Color(14, 60, 34) else Color(40, 120, 70)
    val paradas = listOf(0f to bg, 0.28f to oscuro, 0.55f to p.verde, 0.78f to p.naranja, 0.92f to p.rojo, 1f to caliente)
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
    val fondo = fondoGrafica(pal)
    LaunchedEffect(p.id) { Analisis.pedirEspectrograma(p) }
    val datos = Analisis.espectrogramas[p.id]
    val imagen = remember(datos, pal) {
        datos?.let {
            val lut = tablaColores(pal, fondo)
            val cols = Nucleo.SG_COLUMNAS
            val filas = Nucleo.SG_FILAS
            val px = IntArray(cols * filas)
            for (c in 0 until cols) for (r in 0 until filas) {
                px[(filas - 1 - r) * cols + c] = lut[it[c * filas + r].toInt() and 0xFF]
            }
            imagenDePixeles(px, cols, filas)
        }
    }
    val total by rememberUpdatedState(Mesa.duracion)
    BoxWithConstraints(modifier.background(fondo)) {
        if (imagen == null) {
            Texto("Calculando…", Tipo.nota, pal.texto2, Modifier.align(Alignment.Center), lineas = 1)
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
            val y = { f: Float -> h - ln(f / 20f) / ln(SG_TOPE / 20f) * h }
            val estilo = TextStyle(color = pal.texto, fontSize = 9.sp, fontWeight = FontWeight.Medium)
            for (f in listOf(50f, 100f, 200f, 500f, 1000f, 2000f, 5000f, 10000f)) {
                drawLine(pal.texto2.copy(alpha = 0.3f), Offset(0f, y(f)), Offset(w, y(f)), d)
                drawText(medidor, etiquetaHz(f), Offset(4 * d, y(f) - 14 * d), estilo)
            }
            val pasos = listOf(1, 2, 5, 10, 15, 30, 60, 120, 300, 600)
            val paso = pasos.firstOrNull { it >= dur / 8 } ?: 600
            var t = paso
            while (t < dur) {
                val x = (t / dur * w).toFloat()
                drawLine(pal.texto2.copy(alpha = 0.3f), Offset(x, 0f), Offset(x, h), d)
                val etiqueta = if (t >= 60) "${t / 60}:" + String.format(Locale.US, "%02d", t % 60) else "${t}s"
                drawText(medidor, etiqueta, Offset(x + 3 * d, h - 15 * d), estilo)
                t += paso
            }
            val px = if (dur > 0) (Mesa.posicion / dur * w).toFloat() else 0f
            drawRect(pal.acento, Offset(px - d, 0f), Size(2 * d, h))
        }
    }
}
