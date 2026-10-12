package com.wavapp.mezcla.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
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
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.Edicion
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.NativeAudio
import com.wavapp.mezcla.Pedazo
import com.wavapp.mezcla.Pista
import java.util.Locale
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToLong
import kotlin.math.sin

/** Tiempo corto para la edición: 1:05.250 o 5.250 s. */
internal fun textoCuadros(cuadros: Long): String {
    val s = cuadros / Mesa.RATE
    val m = (s / 60).toInt()
    return if (m > 0) String.format(Locale.US, "%d:%06.3f", m, s - m * 60) else String.format(Locale.US, "%.3f s", s)
}

/** Ganancia del fundido (sin², igual que el motor) en el cuadro [k] del pedazo. */
private fun gananciaFade(pz: Pedazo, k: Double): Float {
    var g = 1.0
    if (pz.fadeIn > 0 && k < pz.fadeIn) g *= sin2(k / pz.fadeIn)
    val resto = pz.largo - k
    if (pz.fadeOut > 0 && resto < pz.fadeOut) g *= sin2(resto / pz.fadeOut)
    return g.toFloat()
}

private fun sin2(x: Double): Double {
    val s = sin(x.coerceIn(0.0, 1.0) * Math.PI / 2)
    return s * s
}

/**
 * Dibuja la onda de [pz], que ocupa de [x0] a [x1] en el lienzo; solo se calcula la parte
 * visible entre [visDesde] y [visHasta]. Una columna por píxel; el fundido baja la onda.
 */
internal fun dibujarOndaPedazo(
    c: android.graphics.Canvas,
    pista: Pista,
    pz: Pedazo,
    x0: Float,
    x1: Float,
    arriba: Float,
    alto: Float,
    visDesde: Float,
    visHasta: Float,
    pintura: android.graphics.Paint,
) {
    val a = max(x0, visDesde)
    val b = min(x1, visHasta)
    if (b <= a || pz.largo <= 0 || x1 <= x0) return
    val cols = max(1, (b - a).toInt())
    val cuadrosPorPx = pz.largo / (x1 - x0).toDouble()
    val desde = pz.origen + ((a - x0) * cuadrosPorPx).toLong()
    val hasta = max(desde + 1, pz.origen + ((b - x0) * cuadrosPorPx).toLong())
    val d = NativeAudio.overviewRange(pista.slot, cols, desde, hasta) ?: return
    val ch = pista.canales
    val canales = min(2, ch)
    val lineas = FloatArray(cols * canales * 4)
    var k = 0
    val paso = (b - a) / cols
    for (cIdx in 0 until canales) {
        val cy = arriba + if (ch == 2) alto * (if (cIdx == 0) 0.25f else 0.75f) else alto / 2
        val amp = if (ch == 2) alto * 0.23f else alto * 0.46f
        for (x in 0 until cols) {
            val px = a + (x + 0.5f) * paso
            val g = gananciaFade(pz, (px - x0) * cuadrosPorPx)
            val mn = d[(x * ch + cIdx) * 2] * g
            val mx = d[(x * ch + cIdx) * 2 + 1] * g
            val y1 = cy - mx * amp
            lineas[k++] = px
            lineas[k++] = y1
            lineas[k++] = px
            lineas[k++] = max(y1 + 1f, cy - mn * amp)
        }
    }
    pintura.strokeWidth = max(1f, paso)
    c.drawLines(lineas, 0, k, pintura)
}

private enum class Modo { Nada, Desplazar, Mover, RecInicio, RecFin, FadeIn, FadeOut }

/** Edición de las pistas a pantalla completa: mover, recortar, fundidos, cortar, deshacer. */
@Composable
fun PanelEdicion(alCerrar: () -> Unit) {
    val pal = LocalPaleta.current
    BackHandler { alCerrar() }
    // Mientras suena, la vista sigue al cursor (salvo que estés arrastrando algo).
    LaunchedEffect(Unit) {
        snapshotFlow { Mesa.posicion }.collect { if (Mesa.reproduciendo && !Edicion.tocando) Edicion.seguirCursor() }
    }
    Column(
        Modifier
            .fillMaxSize()
            .background(pal.bg)
            .safeDrawingPadding()
            .padding(horizontal = 16.dp)
            .testTag("panelEdicion"),
    ) {
        Row(Modifier.padding(top = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("Edición", color = pal.fg, fontSize = 17.sp, fontWeight = FontWeight.SemiBold)
                Text("Mover, recortar, fundidos y cortes", color = pal.tenue, fontSize = 12.sp)
            }
            Boton("Cerrar", alCerrar)
        }
        Transporte(horizontal = 0.dp)
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Boton("↶ Deshacer", { Edicion.deshacer() }, habilitado = Edicion.puedeDeshacer, tamano = 12.sp, relleno = 9.dp)
            Boton("↷ Rehacer", { Edicion.rehacer() }, habilitado = Edicion.puedeRehacer, tamano = 12.sp, relleno = 9.dp)
            Boton("✂ Cortar", { Edicion.cortarEnCursor() }, tamano = 12.sp, relleno = 9.dp)
            Boton("Borrar", { Edicion.borrarSeleccionado() }, habilitado = Edicion.seleccion != null,
                tamano = 12.sp, relleno = 9.dp)
        }
        Row(Modifier.padding(top = 6.dp, bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            val centro = {
                val pos = Mesa.posicion
                val a = Edicion.anchoVista()
                if (pos >= Edicion.vistaInicio && pos <= Edicion.vistaInicio + a) pos else Edicion.vistaInicio + a / 2
            }
            Boton("−", { Edicion.acercar(0.5, centro()) }, tamano = 13.sp, relleno = 12.dp, descripcion = "Alejar")
            Boton("+", { Edicion.acercar(2.0, centro()) }, tamano = 13.sp, relleno = 12.dp, descripcion = "Acercar")
            Boton("Ajustar", { Edicion.ajustarVista() }, tamano = 12.sp, relleno = 9.dp)
            Boton("Imán", { Edicion.iman = !Edicion.iman }, presionado = Edicion.iman, tamano = 12.sp, relleno = 9.dp)
        }
        Regla(Modifier.fillMaxWidth().height(26.dp))
        Box(Modifier.weight(1f).fillMaxWidth()) {
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
                for (p in Mesa.pistas) Carril(p)
                if (Mesa.pistas.isEmpty()) {
                    Text("Sin pistas para editar.", color = pal.tenue, fontSize = 13.sp, modifier = Modifier.padding(top = 12.dp))
                }
            }
            CursorEdicion(Modifier.matchParentSize())
        }
        InfoPedazo()
        Text(
            Edicion.mensaje.ifEmpty {
                "Arrastra un pedazo para moverlo. Ya elegido: sus orillas (abajo) recortan y los " +
                    "cuadritos de arriba hacen el fade. Arrastra el fondo para recorrer y la regla para mover el cursor."
            },
            color = pal.tenue, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp, bottom = 10.dp),
        )
    }
}

/** Posición de un cuadro en el ancho [w] de la vista, y al revés. */
private fun xDe(cuadro: Long, w: Float): Float =
    ((cuadro / Mesa.RATE - Edicion.vistaInicio) / Edicion.anchoVista() * w).toFloat()

private fun cuadroDe(x: Float, w: Float): Long =
    ((Edicion.vistaInicio + x / w * Edicion.anchoVista()) * Mesa.RATE).roundToLong()

private fun cuadrosPorPx(w: Float): Double = Edicion.anchoVista() * Mesa.RATE / w

@Composable
private fun Regla(modifier: Modifier) {
    val pal = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    val estilo = TextStyle(color = pal.tenue, fontSize = 9.sp, fontFamily = Mono)
    Canvas(
        modifier
            .testTag("regla")
            .semantics { contentDescription = "Regla de tiempo. Toca o arrastra para mover el cursor." }
            .pointerInput(Unit) {
                detectTapGestures { o -> Mesa.saltar(cuadroDe(o.x, size.width.toFloat()) / Mesa.RATE) }
            }
            .pointerInput(Unit) {
                detectHorizontalDragGestures(
                    onDragStart = { Edicion.tocando = true },
                    onDragEnd = { Edicion.tocando = false },
                    onDragCancel = { Edicion.tocando = false },
                ) { cambio, _ ->
                    cambio.consume()
                    Mesa.saltar(cuadroDe(cambio.position.x, size.width.toFloat()) / Mesa.RATE)
                }
            },
    ) {
        val w = size.width
        val ancho = Edicion.anchoVista()
        // Paso de las marcas: el menor que deje al menos 70 dp entre etiquetas.
        val pasos = doubleArrayOf(0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0)
        val minimo = 70.dp.toPx() / w * ancho
        val paso = pasos.firstOrNull { it >= minimo } ?: 600.0
        drawLine(pal.linea, Offset(0f, size.height - 1), Offset(w, size.height - 1), density)
        var t = floor(Edicion.vistaInicio / paso) * paso
        while (t <= Edicion.vistaInicio + ancho) {
            val x = ((t - Edicion.vistaInicio) / ancho * w).toFloat()
            if (x >= 0) {
                drawLine(pal.tenue, Offset(x, size.height * 0.55f), Offset(x, size.height), density)
                val m = (t / 60).toInt()
                val s = t - m * 60
                val dec = if (paso < 0.1) 2 else if (paso < 1) 1 else 0
                val texto = if (m > 0) String.format(Locale.US, "%d:%0${if (dec > 0) dec + 3 else 2}.${dec}f", m, s)
                else String.format(Locale.US, "%.${dec}f", s)
                drawText(medidor, texto, Offset(x + 3.dp.toPx(), 0f), estilo)
            }
            t += paso
        }
        val px = xDe((Mesa.posicion * Mesa.RATE).roundToLong(), w)
        if (px in 0f..w) drawRect(pal.acento, Offset(px - density, 0f), Size(2 * density, size.height))
    }
}

/** Cursor de reproducción encima de los carriles (no recibe toques). */
@Composable
private fun CursorEdicion(modifier: Modifier) {
    val pal = LocalPaleta.current
    Canvas(modifier) {
        val px = xDe((Mesa.posicion * Mesa.RATE).roundToLong(), size.width)
        if (px in 0f..size.width) drawRect(pal.acento, Offset(px - density, 0f), Size(2 * density, size.height))
    }
}

@Composable
private fun Carril(p: Pista) {
    val pal = LocalPaleta.current
    val color = ColoresPista[p.color]
    Text(
        "${p.id} · ${p.nombre}" + if (p.pedazos.size != 1) " · ${p.pedazos.size} pedazos" else "",
        color = if (Mesa.suena(p)) pal.fg else pal.tenue, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
        modifier = Modifier.padding(top = 6.dp, bottom = 2.dp),
    )
    val forma = RoundedCornerShape(4.dp)
    Canvas(
        Modifier
            .fillMaxWidth()
            .height(64.dp)
            .clip(forma)
            .background(pal.panel)
            .border(1.dp, pal.linea, forma)
            .testTag("carril-${p.id}")
            .semantics { contentDescription = "Carril de ${p.nombre}" }
            .pointerInput(p.id) {
                detectTapGestures { o ->
                    val i = pedazoEn(p, o.x, size.width.toFloat(), 6.dp.toPx())
                    if (i >= 0) {
                        Edicion.seleccion = Edicion.Seleccion(p.id, i)
                        Edicion.mensaje = ""
                    } else {
                        Edicion.seleccion = null
                        Mesa.saltar(cuadroDe(o.x, size.width.toFloat()) / Mesa.RATE)
                    }
                }
            }
            .pointerInput(p.id) {
                var modo = Modo.Nada
                var indice = -1
                var base: Pedazo? = null
                var lista: List<Pedazo> = emptyList()
                var acumulado = 0f
                var inicioVista0 = 0.0
                var escala = 1.0
                var umbral = 0L
                var puntos: List<Long> = emptyList()
                fun terminar() {
                    if (modo != Modo.Nada && modo != Modo.Desplazar) Edicion.terminarGesto()
                    modo = Modo.Nada
                    Edicion.tocando = false
                }
                detectHorizontalDragGestures(
                    onDragStart = { o ->
                        val w = size.width.toFloat()
                        Edicion.tocando = true
                        acumulado = 0f
                        escala = cuadrosPorPx(w)
                        umbral = (10.dp.toPx() * escala).roundToLong()
                        inicioVista0 = Edicion.vistaInicio
                        val (m, i) = modoEn(p, o, w, size.height.toFloat(), 18.dp.toPx())
                        modo = m
                        indice = i
                        if (m != Modo.Desplazar) {
                            Edicion.seleccion = Edicion.Seleccion(p.id, i)
                            Edicion.mensaje = ""
                            lista = p.pedazos
                            base = lista[i]
                            puntos = Edicion.puntosIman(Edicion.Seleccion(p.id, i))
                            Edicion.empezarGesto()
                        }
                    },
                    onDragEnd = { terminar() },
                    onDragCancel = { terminar() },
                ) { cambio, dx ->
                    cambio.consume()
                    acumulado += dx
                    val w = size.width.toFloat()
                    val b = base
                    if (modo == Modo.Desplazar || b == null) {
                        Edicion.ponerInicio(inicioVista0 - acumulado / w * Edicion.anchoVista())
                        return@detectHorizontalDragGestures
                    }
                    val d = (acumulado * escala).roundToLong()
                    val nuevo = when (modo) {
                        Modo.Mover -> {
                            val n = b.inicio + d
                            Edicion.mover(b, n + Edicion.corrimientoIman(listOf(n, n + b.largo), puntos, umbral))
                        }
                        Modo.RecInicio -> {
                            val n = b.inicio + d
                            Edicion.recortarInicio(b, n + Edicion.corrimientoIman(listOf(n), puntos, umbral))
                        }
                        Modo.RecFin -> {
                            val n = b.fin + d
                            Edicion.recortarFin(b, n + Edicion.corrimientoIman(listOf(n), puntos, umbral), p.cuadros)
                        }
                        Modo.FadeIn -> Edicion.ponerFades(b, min(b.fadeIn + d, b.largo - b.fadeOut), b.fadeOut)
                        Modo.FadeOut -> Edicion.ponerFades(b, b.fadeIn, b.fadeOut - d)
                        else -> b
                    }
                    if (indice in lista.indices) Edicion.cambiar(p, lista.toMutableList().also { it[indice] = nuevo })
                }
            },
    ) {
        val w = size.width
        val h = size.height
        val sel = Edicion.seleccion?.takeIf { it.pistaId == p.id }?.indice ?: -1
        val suena = Mesa.suena(p)
        val pintura = android.graphics.Paint().apply {
            this.color = color.copy(alpha = if (suena) 1f else 0.35f).toArgb()
            style = android.graphics.Paint.Style.STROKE
        }
        val orden = p.pedazos.indices.sortedBy { if (it == sel) 1 else 0 }
        drawIntoCanvas { lienzo ->
            for (i in orden) {
                val pz = p.pedazos[i]
                val x0 = xDe(pz.inicio, w)
                val x1 = xDe(pz.fin, w)
                if (x1 < 0 || x0 > w) continue
                drawRect(color.copy(alpha = if (i == sel) 0.26f else 0.13f), Offset(x0, 0f), Size(x1 - x0, h))
                dibujarOndaPedazo(lienzo.nativeCanvas, p, pz, x0, x1, 0f, h, 0f, w, pintura)
                drawRect(
                    if (i == sel) pal.acento else color.copy(alpha = 0.7f), Offset(x0, 0f), Size(x1 - x0, h),
                    style = Stroke(if (i == sel) 2.dp.toPx() else density),
                )
            }
        }
        // Agarraderas del pedazo elegido: fundidos arriba, recortes en las orillas de abajo.
        p.pedazos.getOrNull(sel)?.let { pz ->
            val x0 = xDe(pz.inicio, w)
            val x1 = xDe(pz.fin, w)
            val xi = xDe(pz.inicio + pz.fadeIn, w)
            val xo = xDe(pz.fin - pz.fadeOut, w)
            val grosor = 1.5f * density
            if (pz.fadeIn > 0) drawLine(pal.fg, Offset(x0, h), Offset(xi, 0f), grosor)
            if (pz.fadeOut > 0) drawLine(pal.fg, Offset(xo, 0f), Offset(x1, h), grosor)
            val c = 9.dp.toPx()
            drawRect(pal.acento, Offset(xi - c / 2, 0f), Size(c, c))
            drawRect(pal.acento, Offset(xo - c / 2, 0f), Size(c, c))
            val ag = 5.dp.toPx()
            drawRect(pal.acento, Offset(x0, h * 0.45f), Size(ag, h * 0.4f))
            drawRect(pal.acento, Offset(x1 - ag, h * 0.45f), Size(ag, h * 0.4f))
        }
    }
}

/** Índice del pedazo bajo x (el elegido primero, luego el de más arriba), o -1. */
private fun pedazoEn(p: Pista, x: Float, w: Float, holgura: Float): Int {
    val sel = Edicion.seleccion?.takeIf { it.pistaId == p.id }?.indice ?: -1
    val orden = listOf(sel).filter { it in p.pedazos.indices } + p.pedazos.indices.reversed().filter { it != sel }
    return orden.firstOrNull { i ->
        val pz = p.pedazos[i]
        x >= xDe(pz.inicio, w) - holgura && x <= xDe(pz.fin, w) + holgura
    } ?: -1
}

/** Qué hace un arrastre que empieza en [o]: agarraderas del elegido, mover un pedazo o recorrer. */
private fun modoEn(p: Pista, o: Offset, w: Float, h: Float, zona: Float): Pair<Modo, Int> {
    val sel = Edicion.seleccion?.takeIf { it.pistaId == p.id }?.indice ?: -1
    p.pedazos.getOrNull(sel)?.let { pz ->
        val x0 = xDe(pz.inicio, w)
        val x1 = xDe(pz.fin, w)
        if (o.x >= x0 - zona && o.x <= x1 + zona) {
            if (o.y < h * 0.4f) {
                val di = abs(o.x - xDe(pz.inicio + pz.fadeIn, w))
                val dout = abs(o.x - xDe(pz.fin - pz.fadeOut, w))
                if (min(di, dout) < zona) return (if (di <= dout) Modo.FadeIn else Modo.FadeOut) to sel
            } else {
                val d0 = abs(o.x - x0)
                val d1 = abs(o.x - x1)
                if (min(d0, d1) < zona) return (if (d0 <= d1) Modo.RecInicio else Modo.RecFin) to sel
            }
        }
    }
    val i = pedazoEn(p, o.x, w, 0f)
    return if (i >= 0) Modo.Mover to i else Modo.Desplazar to -1
}

/** Datos y ajustes finos del pedazo elegido. */
@Composable
private fun InfoPedazo() {
    val pal = LocalPaleta.current
    val forma = RoundedCornerShape(6.dp)
    Column(
        Modifier
            .padding(top = 8.dp)
            .fillMaxWidth()
            .clip(forma)
            .background(pal.panel)
            .border(1.dp, pal.linea, forma)
            .padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        val sel = Edicion.seleccionado()
        if (sel == null) {
            Text(
                "Toca un pedazo para elegirlo. \"✂ Cortar\" sin pedazo elegido corta todas las pistas en el cursor.",
                color = pal.tenue, fontSize = 12.sp,
            )
            return@Column
        }
        val (p, i) = sel
        val pz = p.pedazos[i]
        Text(
            "${p.id} · ${p.nombre} · pedazo ${i + 1} de ${p.pedazos.size}",
            color = pal.fg, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis,
        )
        Text(
            "empieza ${textoCuadros(pz.inicio)} · dura ${textoCuadros(pz.largo)} · del archivo desde ${textoCuadros(pz.origen)}",
            color = pal.tenue, fontFamily = Mono, fontSize = 11.sp,
        )
        FilaFade("Fade in", pz.fadeIn, min(pz.largo - pz.fadeOut, 480000L)) { Edicion.ponerFadeSeleccionado(true, it) }
        FilaFade("Fade out", pz.fadeOut, min(pz.largo - pz.fadeIn, 480000L)) { Edicion.ponerFadeSeleccionado(false, it) }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Boton("◀ 10 ms", { Edicion.empujar(-10) }, tamano = 12.sp, relleno = 9.dp)
            Boton("10 ms ▶", { Edicion.empujar(10) }, tamano = 12.sp, relleno = 9.dp)
            Boton("Pista original", { Edicion.restaurarPista(p) }, habilitado = p.editada, tamano = 12.sp, relleno = 9.dp)
        }
    }
}

@Composable
private fun FilaFade(etiqueta: String, cuadros: Long, maximo: Long, alCambiar: (Long) -> Unit) {
    val pal = LocalPaleta.current
    val maxS = max(0.01f, (maximo / Mesa.RATE).toFloat())
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(etiqueta, color = pal.tenue, fontSize = 12.sp, modifier = Modifier.width(64.dp))
        Deslizador(
            (cuadros / Mesa.RATE).toFloat().coerceAtMost(maxS), 0f, maxS,
            { alCambiar((it * Mesa.RATE).roundToLong().coerceAtMost(maximo)) }, { alCambiar(0) },
            Modifier.weight(1f), descripcion = etiqueta,
        )
        Text(
            String.format(Locale.US, "%.2f s", cuadros / Mesa.RATE), color = pal.fg, fontFamily = Mono, fontSize = 12.sp,
            modifier = Modifier.width(58.dp).padding(start = 6.dp),
        )
    }
}
