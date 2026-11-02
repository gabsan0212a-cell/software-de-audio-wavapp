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
import androidx.compose.runtime.derivedStateOf
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

/**
 * Escuchar lo que suena en el celular (lo maneja la plataforma): de forma nativa mientras la
 * fuente Celular está a la vista (si hay permiso), o con la captura completa si se pide.
 */
class EscuchaCelular(
    val permiso: Boolean,
    val pedirPermiso: () -> Unit,
    val nativoFallo: Boolean,
    val completaDisponible: Boolean,
    val completaPedida: Boolean,
    val empezarCompleta: () -> Unit,
    val detenerCompleta: () -> Unit,
)

/**
 * Pestaña "Análisis": qué se analiza (la mezcla, una pista, el micrófono o lo que suena en el
 * celular), volumen percibido, diagnóstico de fase y paneo, espectro con zumbido y espectrograma.
 */
@Composable
fun PestanaAnalisis(abajo: Dp) {
    val lista = rememberLazyListState()
    val fuente = Mesa.pistas.firstOrNull { it.id == Analisis.fuenteId }
    val celular = Analisis.fuenteId == Nucleo.ID_CELULAR
    val escucha = recordarEscucharCelular(celular)
    // De forma nativa Android da el sonido en mono y en pedazos: sin fase, paneo, zumbido ni LUFS.
    val completo = !celular || (Mesa.celular.escuchando && !Mesa.celular.nativo)
    LaunchedEffect(fuente?.id) { fuente?.let { Analisis.pedirEspectrograma(it) } }
    Box(Modifier.fillMaxSize()) {
        LazyColumn(
            Modifier.fillMaxSize().testTag("panelAnalisis"),
            state = lista,
            contentPadding = PaddingValues(bottom = abajo + 20.dp),
        ) {
            item(key = "espacio") { EspacioEncabezado() }
            item(key = "titulo") { TituloGrande("Análisis") }
            item(key = "fuentesTitulo") { EncabezadoSeccion("Analizar") }
            item(key = "fuentes") { Fuentes(fuente) }
            if (celular) {
                item(key = "celular") { TarjetaCelular(escucha) }
            } else if (Mesa.pistas.isEmpty() && Analisis.fuenteId != Nucleo.ID_ENTRADA) {
                item(key = "vacio") {
                    PieGrupo(
                        "La mezcla está vacía: agrega pistas en Mezcla, o toca Celular para ver lo que suena en otras " +
                            "apps (Spotify, YouTube…).",
                        Modifier.padding(start = 4.dp),
                    )
                }
            }
            if (completo) {
                item(key = "lufsTitulo") {
                    EncabezadoSeccion("Volumen percibido", detalle = if (celular) "LUFS · el celular" else "LUFS · la mezcla")
                }
                item(key = "lufs") { TarjetaLufs() }
                if (!celular && Mesa.pistas.isNotEmpty()) {
                    item(key = "lufsCancionTitulo") { EncabezadoSeccion("Canción completa") }
                    item(key = "lufsCancion") { TarjetaLufsCancion() }
                }
                item(key = "lufsPie") {
                    PieGrupo(
                        if (celular) {
                            "LUFS es el volumen como lo oye la gente (no solo los picos). Aquí se mide lo que suena en " +
                                "el celular, antes de su volumen. Así ves a cuánto deja Spotify o YouTube cada canción."
                        } else {
                            "LUFS es el volumen como lo oye la gente (no solo los picos). Se mide lo que sale, después " +
                                "del master. Spotify y YouTube ponen todo a −14 LUFS: más fuerte no suena más fuerte allá."
                        },
                    )
                }
                item(key = "diagTitulo") {
                    val detalle = when {
                        Analisis.fuenteId == Nucleo.ID_ENTRADA -> if (Mesa.entrada.abierta) "Micrófono en vivo" else "Micrófono (cerrado)"
                        celular -> "El celular en vivo"
                        else -> fuente?.nombre ?: "Mezcla completa"
                    }
                    EncabezadoSeccion("Diagnóstico", detalle = detalle)
                }
                diagnostico(this)
            }
            espectroYEspectrograma(this, fuente, celular, completo)
        }
        EncabezadoFlotante(lista, "Análisis")
    }
}

private fun diagnostico(lista: androidx.compose.foundation.lazy.LazyListScope) = with(lista) {
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
}

private fun espectroYEspectrograma(
    lista: androidx.compose.foundation.lazy.LazyListScope, fuente: Pista?, celular: Boolean, completo: Boolean,
) = with(lista) {
    item(key = "espTitulo") { EncabezadoSeccion(if (completo) "Espectro y zumbido" else "Espectro") }
    item(key = "esp") {
        Tarjeta(Modifier.padding(horizontal = 16.dp)) {
            Espectro(Modifier.fillMaxWidth().height(150.dp).clip(RoundedCornerShape(10.dp)))
            if (completo) {
                Spacer(Modifier.height(10.dp))
                TextoZumbido()
            }
            // El notch es de la mezcla: a lo que suena en otra app no le llega.
            if (!celular) {
                Spacer(Modifier.height(12.dp))
                Segmentado(
                    listOf("Sin notch", "50 Hz", "60 Hz"), when (Analisis.notch) { 50 -> 1; 60 -> 2; else -> 0 },
                    { Analisis.ponerNotch(listOf(0, 50, 60)[it]) }, Modifier.fillMaxWidth(),
                )
            }
        }
    }
    if (!celular) {
        item(key = "espPie") {
            PieGrupo("El notch quita 50 o 60 Hz y 3 armónicos de toda la mezcla (también al exportar).")
        }
    }
    item(key = "sgTitulo") {
        EncabezadoSeccion("Espectrograma", detalle = if (fuente != null) "antes del EQ" else "en vivo")
    }
    item(key = "sg") {
        Tarjeta(Modifier.padding(horizontal = 16.dp)) {
            if (fuente == null) {
                EspectrogramaVivo(Modifier.fillMaxWidth().height(220.dp).clip(RoundedCornerShape(10.dp)))
            } else {
                Espectrograma(fuente, Modifier.fillMaxWidth().height(220.dp).clip(RoundedCornerShape(10.dp)))
            }
        }
    }
    item(key = "sgPie") {
        PieGrupo(
            if (fuente != null) {
                "El fondo es silencio; verde suena bajo, naranja medio y rojo fuerte. Una línea horizontal " +
                    "constante en 50/60 Hz o múltiplos suele ser zumbido. Toca para saltar a ese momento."
            } else {
                "Los últimos 12 segundos de lo que se analiza; lo más nuevo, a la derecha. Abajo los graves y " +
                    "arriba los agudos: el bombo y el bajo abajo, la voz en medio, platillos y \"s\" arriba." +
                    if (celular) "" else " Elige una pista arriba para ver la canción completa."
            },
        )
    }
}

/** Qué se analiza: la mezcla, el micrófono, lo que suena en el celular o una pista. */
@Composable
private fun Fuentes(fuente: Pista?) {
    val p = LocalPaleta.current
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Chip("Mezcla", Analisis.fuenteId == -1, { Analisis.fuenteId = -1 })
        Chip(
            "Celular", Analisis.fuenteId == Nucleo.ID_CELULAR, { Analisis.fuenteId = Nucleo.ID_CELULAR },
            Modifier.testTag("fuenteCelular"), icono = Iconos.celular,
        )
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

/**
 * Lo que suena en el celular (la música o los videos de otras apps): se escucha solo mientras se
 * mira, de forma nativa; la captura completa (con el permiso de grabar) queda como opción.
 */
@Composable
private fun TarjetaCelular(escucha: EscuchaCelular) {
    val p = LocalPaleta.current
    val c = Mesa.celular
    val enVivo = c.escuchando && !c.callado
    Tarjeta(Modifier.padding(horizontal = 16.dp, vertical = 4.dp).testTag("tarjetaCelular")) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            AzulejoIcono(Iconos.celular, if (enVivo) p.rojo else p.acento, 44.dp)
            Column(Modifier.weight(1f).padding(start = 12.dp)) {
                Texto("Lo que suena en el celular", Tipo.encabezado, p.texto, lineas = 1)
                Texto(
                    when {
                        !escucha.permiso -> "Spotify, YouTube, TikTok, juegos…"
                        c.escuchando && !c.nativo -> "● Captura completa · estéreo"
                        c.callado -> "Escuchando · no llega sonido"
                        c.escuchando -> "● Escuchando en vivo"
                        escucha.nativoFallo -> "Tu celular no deja escuchar así"
                        escucha.completaPedida -> "Captura completa (sin empezar)"
                        else -> "Conectando…"
                    },
                    Tipo.nota, if (enVivo) p.rojo else p.texto2, lineas = 1,
                )
            }
        }
        when {
            !escucha.permiso -> {
                Texto(
                    "Pon algo a sonar en otra app y míralo aquí en vivo. Android pide el permiso del micrófono para " +
                        "esto (una sola vez): no se usa el micrófono, solo lo que sale por el celular. No graba ni guarda nada.",
                    Tipo.subtitulo, p.texto2, Modifier.padding(top = 12.dp, bottom = 12.dp),
                )
                BotonRelleno(
                    "Permitir y escuchar", escucha.pedirPermiso, Modifier.fillMaxWidth().testTag("permitirEscuchar"),
                    icono = Iconos.celular,
                )
            }
            c.escuchando && !c.nativo -> {
                Texto(
                    "Con la captura completa también se ven fase, paneo, zumbido y LUFS.",
                    Tipo.subtitulo, p.texto2, Modifier.padding(top = 12.dp, bottom = 12.dp),
                )
                BotonRelleno(
                    "Volver a la escucha normal", escucha.detenerCompleta, Modifier.fillMaxWidth().testTag("dejarDeEscuchar"),
                    principal = false,
                )
            }
            c.escuchando -> {
                NivelCelular(Modifier.padding(top = 14.dp).fillMaxWidth().height(10.dp))
                if (c.callado) {
                    Texto(
                        "Pon música o un video en otra app. Si algo suena y no se mueve, tu celular no deja escuchar " +
                            "así: usa la captura completa.",
                        Tipo.subtitulo, p.texto2, Modifier.padding(top = 12.dp),
                    )
                }
                if (escucha.completaDisponible) {
                    BotonTexto(
                        "Captura completa: fase, paneo y LUFS…", escucha.empezarCompleta,
                        Modifier.padding(top = 6.dp).testTag("capturaCompleta"), estilo = Tipo.subtitulo,
                    )
                }
            }
            escucha.nativoFallo || escucha.completaPedida -> {
                Texto(
                    if (escucha.nativoFallo) "Este celular no deja escuchar lo que suena sin el permiso de grabar la pantalla."
                    else "La captura completa no está escuchando.",
                    Tipo.subtitulo, p.texto2, Modifier.padding(top = 12.dp, bottom = 12.dp),
                )
                if (escucha.completaPedida) {
                    BotonRelleno("Volver a la escucha normal", escucha.detenerCompleta, Modifier.fillMaxWidth(), principal = false)
                } else if (escucha.completaDisponible) {
                    BotonRelleno("Usar la captura completa", escucha.empezarCompleta, Modifier.fillMaxWidth(), principal = false)
                }
            }
        }
    }
}

/** Nivel de lo que suena: RMS (relleno) y pico (raya), de −60 a 0 dB. Se lee al dibujar. */
@Composable
private fun NivelCelular(modifier: Modifier) {
    val pal = LocalPaleta.current
    Canvas(modifier.semantics { contentDescription = "Nivel de lo que suena" }) {
        val c = Mesa.celular
        val pos = { db: Float -> ((db + 60f) / 60f).entre(0f, 1f) }
        val r = CornerRadius(size.height / 2)
        drawRoundRect(pal.relleno, cornerRadius = r)
        val rms = pos(c.rmsDb)
        if (rms > 0f) {
            drawRoundRect(
                Brush.horizontalGradient(0f to pal.verde, 0.75f to pal.amarillo, 1f to pal.rojo, endX = size.width),
                size = Size(size.width * rms, size.height), cornerRadius = r,
            )
        }
        val pk = pos(c.picoDb)
        if (pk > 0f) drawRect(pal.texto, Offset(size.width * pk - density, 0f), Size(2 * density, size.height))
    }
}

/**
 * Espectrograma en vivo (los últimos 12 s de lo que se analiza; el núcleo da los valores). Se
 * pinta solo lo nuevo (una columna por vez) sobre una imagen que se reusa, como un anillo.
 */
@Composable
private fun EspectrogramaVivo(modifier: Modifier) {
    val pal = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    val fondo = fondoGrafica(pal)
    val lut = remember(pal) { tablaColores(pal, fondo) }
    val estado = remember { EstadoVivo() }
    Canvas(modifier.background(fondo).semantics { contentDescription = "Espectrograma en vivo" }) {
        val cols = Nucleo.SG_VIVO_COLUMNAS
        val filas = Nucleo.SG_VIVO_FILAS
        // Se lee al dibujar (no al componer): solo se vuelve a dibujar.
        val total = Analisis.columnasVivo
        val img = estado.imagen(cols, filas)
        if (estado.lut !== lut || total < estado.hecho) {  // otros colores o empezó de nuevo: todo
            estado.lut = lut
            estado.px.fill(lut[0])
            estado.hecho = 0L
            img.escribir(estado.px, 0, cols)
        }
        val desde = maxOf(estado.hecho, total - cols)
        if (desde < total) {
            val datos = Analisis.espectrogramaVivo
            var k = desde
            while (k < total) {
                val x = (k % cols).toInt()
                for (r in 0 until filas) estado.px[(filas - 1 - r) * cols + x] = lut[datos[x * filas + r].toInt() and 0xFF]
                img.escribir(estado.px, x, 1)
                k++
            }
            estado.hecho = total
        }
        val w = size.width
        val h = size.height
        val d = density
        // El anillo en dos partes: de la más vieja a la más nueva, de izquierda a derecha.
        val hay = minOf(total, cols.toLong()).toInt()
        if (hay > 0) {
            val inicio = ((total - hay) % cols).toInt()
            val primera = minOf(hay, cols - inicio)
            val x0 = w * (cols - hay) / cols
            val corte = x0 + w * primera / cols
            drawImage(
                img.imagen, IntOffset(inicio, 0), IntSize(primera, filas),
                IntOffset(x0.toInt(), 0), IntSize((corte - x0).toInt().coerceAtLeast(1), h.toInt()),
            )
            if (hay > primera) {
                drawImage(
                    img.imagen, IntOffset(0, 0), IntSize(hay - primera, filas),
                    IntOffset(corte.toInt(), 0), IntSize((w - corte).toInt().coerceAtLeast(1), h.toInt()),
                )
            }
        }
        val y = { f: Float -> h - ln(f / 20f) / ln(SG_TOPE / 20f) * h }
        val estilo = TextStyle(color = pal.texto, fontSize = 9.sp, fontWeight = FontWeight.Medium)
        for (f in listOf(50f, 100f, 200f, 500f, 1000f, 2000f, 5000f, 10000f)) {
            drawLine(pal.texto2.copy(alpha = 0.3f), Offset(0f, y(f)), Offset(w, y(f)), d)
            textoSeguro(medidor, etiquetaHz(f), Offset(4 * d, y(f) - 14 * d), estilo)
        }
        // Cada 2 s, contando desde ahora (a la derecha) hacia atrás.
        val segundos = cols * 0.05f
        var t = 2
        while (t < segundos) {
            val x = w - t / segundos * w
            drawLine(pal.texto2.copy(alpha = 0.3f), Offset(x, 0f), Offset(x, h), d)
            textoSeguro(medidor, "−${t}s", Offset(x + 3 * d, h - 15 * d), estilo)
            t += 2
        }
    }
}

private class EstadoVivo {
    var lut: IntArray? = null
    var hecho = 0L
    var px = IntArray(0)
    private var img: ImagenViva? = null
    fun imagen(cols: Int, filas: Int): ImagenViva {
        img?.let { if (it.ancho == cols && it.alto == filas) return it }
        px = IntArray(cols * filas)
        lut = null  // se pinta todo de nuevo
        return ImagenViva(cols, filas).also { img = it }
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
            textoSeguro(medidor, "M", Offset(size.width / 2 + 5 * d, 4 * d), estilo)
            textoSeguro(medidor, "L", Offset(6 * d, 4 * d), estilo)
            textoSeguro(medidor, "R", Offset(size.width - 14 * d, 4 * d), estilo)
            textoSeguro(medidor, "S", Offset(size.width - 14 * d, size.height / 2 - 17 * d), estilo)
            if (Analisis.activo) {
                textoSeguro(
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
            textoSeguro(medidor, "L", Offset(cx - sin(MAXA) * r - 4 * d, cy - cos(MAXA) * r - 18 * d), estilo)
            textoSeguro(medidor, "R", Offset(cx + sin(MAXA) * r - 4 * d, cy - cos(MAXA) * r - 18 * d), estilo)
            textoSeguro(medidor, "C", Offset(cx - 3 * d, cy - r - 17 * d), estilo)
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
    // Los textos se vuelven a componer solo cuando cambia lo que dicen; las barras, al dibujar.
    val textoPaneo by remember {
        derivedStateOf {
            val pan = Analisis.paneo
            if (!(Analisis.activo && Analisis.fuerte)) "—"
            else if (abs(pan) < 0.04f) "Centro" else (if (pan < 0) "L " else "R ") + (abs(pan) * 100).roundToInt() + "%"
        }
    }
    val textoFase by remember {
        derivedStateOf {
            val cor = Analisis.correlacion
            (if (cor >= 0) "+" else "−") + String.format(Locale.US, "%.2f", abs(cor))
        }
    }
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        FilaMedidor("Paneo", textoPaneo) {
            Canvas(it) {
                val pan = Analisis.paneo
                val r = CornerRadius(size.height / 2)
                drawRoundRect(pal.relleno, cornerRadius = r)
                drawLine(pal.texto3, Offset(size.width / 2, 0f), Offset(size.width / 2, size.height), density)
                val x = size.width * (0.5f + 0.5f * pan).coerceIn(0f, 1f)
                drawCircle(Color.White, size.height / 2 + 2 * density, Offset(x, size.height / 2))
                drawCircle(pal.texto, size.height / 2 + 2 * density, Offset(x, size.height / 2), style = Stroke(1.5f * density))
            }
        }
        FilaMedidor("Fase", textoFase) {
            Canvas(it) {
                val cor = Analisis.correlacion
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
    val (linea, area) = rutasEspectro(alto)
    if (relleno) drawPath(area, color) else drawPath(linea, color, style = Stroke(1.6f * density))
}

// Lo que se reusa entre cuadros (sin reservar memoria en cada uno).
private var columnasEspectro = FloatArray(0)

/** La curva del espectro de este cuadro y el área debajo (una sola lectura al núcleo). */
internal fun DrawScope.rutasEspectro(alto: Float): Pair<Path, Path> {
    val w = size.width
    val n = max(2, (w / 2).toInt())
    if (columnasEspectro.size != n) columnasEspectro = FloatArray(n)
    val sp = columnasEspectro
    Nucleo.spectrumColumns(sp)
    val linea = Path()
    val area = Path()
    area.moveTo(0f, alto)
    for (c in 0 until n) {
        val x = c * w / (n - 1)
        val y = alto - (max(-110f, sp[c]) + 110f) / 100f * alto
        if (c == 0) linea.moveTo(x, y) else linea.lineTo(x, y)
        area.lineTo(x, y)
    }
    area.lineTo(w, alto)
    area.close()
    return linea to area
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
            textoSeguro(medidor, etiquetaHz(f), Offset(x(f) + 3 * d, h - 14 * d), estilo)
        }
        for (db in listOf(-90f, -60f, -30f)) {
            drawLine(pal.separador, Offset(0f, y(db)), Offset(w, y(db)), d)
            textoSeguro(medidor, "${db.toInt()} dB".replace('-', '−'), Offset(4 * d, y(db) - 13 * d), estilo)
        }
        val guion = PathEffect.dashPathEffect(floatArrayOf(3 * d, 3 * d))
        for (f in listOf(50f, 60f)) {
            drawLine(pal.texto3, Offset(x(f), 0f), Offset(x(f), h), d, pathEffect = guion)
            textoSeguro(medidor, "${f.toInt()}", Offset(x(f) + 2 * d, 2 * d), estilo)
        }
        if (Analisis.activo && Analisis.zumbidoHz > 0) {
            for (k in 1..4) {
                val f = Analisis.zumbidoHz * k.toFloat()
                drawLine(pal.rojo, Offset(x(f), 14 * d), Offset(x(f), h), 2 * d)
            }
        }
        if (Analisis.activo) {
            val (linea, area) = rutasEspectro(h)
            drawPath(area, pal.acento.copy(alpha = 0.14f))
            drawPath(linea, pal.acento, style = Stroke(1.6f * d))
        }
    }
}

@Composable
private fun TextoZumbido() {
    val pal = LocalPaleta.current
    // Sin leer la versión: lo que usa cambia poco (no se vuelve a componer en cada análisis).
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
                textoSeguro(medidor, etiquetaHz(f), Offset(4 * d, y(f) - 14 * d), estilo)
            }
            val pasos = listOf(1, 2, 5, 10, 15, 30, 60, 120, 300, 600)
            val paso = pasos.firstOrNull { it >= dur / 8 } ?: 600
            var t = paso
            while (t < dur) {
                val x = (t / dur * w).toFloat()
                drawLine(pal.texto2.copy(alpha = 0.3f), Offset(x, 0f), Offset(x, h), d)
                val etiqueta = if (t >= 60) "${t / 60}:" + String.format(Locale.US, "%02d", t % 60) else "${t}s"
                textoSeguro(medidor, etiqueta, Offset(x + 3 * d, h - 15 * d), estilo)
                t += paso
            }
            val px = if (dur > 0) (Mesa.posicion / dur * w).toFloat() else 0f
            drawRect(pal.acento, Offset(px - d, 0f), Size(2 * d, h))
        }
    }
}
