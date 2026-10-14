package com.wavapp.mezcla.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.PointerInputScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.CurvaAuto
import com.wavapp.mezcla.Edicion
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.Pista
import java.util.Locale
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToLong

/** Tiempo corto para la edición: 1:05.250 o 5.250 s. */
internal fun textoCuadros(cuadros: Long): String = tiempoFino(cuadros / Mesa.RATE)

// Qué hace un arrastre. Los de edición usan los mismos números que el núcleo (Nucleo.MOVER...).
private const val NADA = 0
private const val DESPLAZAR = -1

private val MARGEN = 12.dp  // a los lados de la regla y los carriles (deben coincidir)

/**
 * Pestaña "Editar": mover, recortar, fundidos, cortar y deshacer, con regiones de color; y la
 * automatización de volumen y paneo (puntos sobre la línea de tiempo).
 */
@Composable
fun PestanaEdicion(abajo: Dp) {
    val pal = LocalPaleta.current
    // Mientras suena, la vista sigue al cursor (salvo que estés arrastrando algo).
    LaunchedEffect(Unit) {
        snapshotFlow { Mesa.posicion }.collect { if (Mesa.reproduciendo && !Edicion.tocando) Edicion.seguirCursor() }
    }
    Column(
        Modifier
            .fillMaxSize()
            .windowInsetsPadding(WindowInsets.statusBars)
            .padding(bottom = abajo)
            .testTag("panelEdicion"),
    ) {
        BarraControl()
        Segmentado(
            listOf("Pedazos", "Volumen", "Paneo"), Edicion.vista,
            {
                Edicion.vista = it
                Edicion.punto = null
                Edicion.mensaje = ""
            },
            Modifier.padding(horizontal = MARGEN).fillMaxWidth().testTag("vistaEdicion"),
        )
        Herramientas()
        Regla(Modifier.padding(horizontal = MARGEN).fillMaxWidth().height(28.dp))
        val param = Edicion.vista - 1
        Box(Modifier.weight(1f).fillMaxWidth()) {
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(bottom = 8.dp)) {
                for (p in Mesa.pistas) if (param < 0) Carril(p) else CarrilAuto(p, param)
                if (Mesa.pistas.isEmpty()) {
                    PieGrupo("Sin pistas para editar. Agrégalas en Mezcla.", Modifier.padding(top = 12.dp))
                }
            }
            CursorEdicion(Modifier.matchParentSize().padding(horizontal = MARGEN))
        }
        if (param < 0) InfoPedazo() else InfoPunto(param)
    }
}

/** Transporte, tiempo (como la pantalla de GarageBand) y deshacer/rehacer. */
@Composable
private fun BarraControl() {
    val pal = LocalPaleta.current
    Box(Modifier.fillMaxWidth().height(48.dp).padding(horizontal = 4.dp)) {
        Row(Modifier.align(Alignment.CenterStart), verticalAlignment = Alignment.CenterVertically) {
            BotonReproducir(pal.texto, 26.dp, 44.dp)
            BotonIcono(Iconos.detener, "Detener", { Mesa.detener() }, color = pal.texto, tamano = 24.dp)
        }
        TiempoEdicion(Modifier.align(Alignment.Center))
        Row(Modifier.align(Alignment.CenterEnd), verticalAlignment = Alignment.CenterVertically) {
            BotonIcono(Iconos.deshacer, "Deshacer", { Edicion.deshacer() }, habilitado = Edicion.puedeDeshacer)
            BotonIcono(Iconos.rehacer, "Rehacer", { Edicion.rehacer() }, habilitado = Edicion.puedeRehacer)
        }
    }
}

@Composable
private fun TiempoEdicion(modifier: Modifier) {
    val pal = LocalPaleta.current
    Box(
        modifier.clip(RoundedCornerShape(8.dp)).background(pal.tarjeta).padding(horizontal = 12.dp, vertical = 5.dp),
    ) {
        // Cambia en cada cuadro (milésimas): se dibuja sin recomponer.
        TextoAlDibujar({ tiempoFino(Mesa.posicion) }, Tipo.encabezado.numeros, pal.texto, "88:88.888")
    }
}

@Composable
private fun Herramientas() {
    val centro = {
        val pos = Mesa.posicion
        val a = Edicion.anchoVista()
        if (pos >= Edicion.vistaInicio && pos <= Edicion.vistaInicio + a) pos else Edicion.vistaInicio + a / 2
    }
    val pedazos = Edicion.vista == Edicion.PEDAZOS
    Row(
        Modifier.fillMaxWidth().padding(horizontal = MARGEN, vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(7.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (pedazos) {
            Herramienta(Iconos.tijeras, "Cortar") { Edicion.cortarEnCursor() }
            Herramienta(Iconos.basura, "Borrar", habilitado = Edicion.seleccion != null) { Edicion.borrarSeleccionado() }
        } else {
            Herramienta(Iconos.basura, "Quitar punto", habilitado = Edicion.puntoSeleccionado() != null) {
                Edicion.borrarPunto()
            }
        }
        Spacer(Modifier.weight(1f))
        Herramienta(Iconos.alejar, null, "Alejar") { Edicion.acercar(0.5, centro()) }
        Herramienta(Iconos.acercar, null, "Acercar") { Edicion.acercar(2.0, centro()) }
        Herramienta(Iconos.ajustar, null, "Ajustar") { Edicion.ajustarVista() }
        if (pedazos) Herramienta(Iconos.iman, null, "Imán", activo = Edicion.iman) { Edicion.iman = !Edicion.iman }
    }
}

@Composable
private fun Herramienta(
    icono: ImageVector,
    texto: String?,
    descripcion: String = texto ?: "",
    activo: Boolean = false,
    habilitado: Boolean = true,
    alTocar: () -> Unit,
) {
    val p = LocalPaleta.current
    val color = if (activo) Color.White else p.acento
    Row(
        Modifier
            .semantics {
                contentDescription = descripcion
                selected = activo
            }
            .pulsable(habilitado, alTocar = alTocar)
            .height(36.dp)
            .widthIn(min = 36.dp)
            .clip(CircleShape)
            .background(if (activo) p.acento else p.tarjeta)
            .padding(horizontal = if (texto != null) 12.dp else 9.dp),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icono(icono, color, tamano = 18.dp)
        if (texto != null) {
            Spacer(Modifier.width(6.dp))
            Texto(texto, Tipo.subtitulo.seminegrita, color, lineas = 1)
        }
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
    val estilo = TextStyle(color = pal.texto2, fontSize = 10.sp)
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
        drawLine(pal.separador, Offset(0f, size.height - 1), Offset(w, size.height - 1), density)
        var t = floor(Edicion.vistaInicio / paso) * paso
        while (t <= Edicion.vistaInicio + ancho) {
            val x = ((t - Edicion.vistaInicio) / ancho * w).toFloat()
            if (x >= 0) {
                drawLine(pal.texto3, Offset(x, size.height * 0.55f), Offset(x, size.height), density)
                val m = (t / 60).toInt()
                val s = t - m * 60
                val dec = if (paso < 0.1) 2 else if (paso < 1) 1 else 0
                val texto = if (m > 0) String.format(Locale.US, "%d:%0${if (dec > 0) dec + 3 else 2}.${dec}f", m, s)
                else String.format(Locale.US, "%.${dec}f", s)
                drawText(medidor, texto, Offset(x + 3.dp.toPx(), 1.dp.toPx()), estilo)
            }
            t += paso
        }
        val px = xDe((Mesa.posicion * Mesa.RATE).roundToLong(), w)
        if (px in 0f..w) {
            drawRect(pal.acento, Offset(px - density, 0f), Size(2 * density, size.height))
            drawCircle(pal.acento, 4.dp.toPx(), Offset(px, size.height - 4.dp.toPx()))
        }
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
    val color = pal.pistas[p.color]
    val suena = Mesa.suena(p)
    Row(Modifier.padding(start = MARGEN + 2.dp, end = MARGEN, top = 8.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(color))
        Texto(
            "${p.id} · ${p.nombre}", Tipo.leyenda.seminegrita, if (suena) pal.texto else pal.texto2,
            Modifier.padding(start = 6.dp).weight(1f, fill = false), lineas = 1,
        )
        if (p.pedazos.size != 1) Texto(" · ${p.pedazos.size} pedazos", Tipo.leyenda, pal.texto2, lineas = 1)
    }
    val cuerpo = color.copy(alpha = if (pal.oscuro) 0.30f else 0.20f)
    val cuerpoSel = color.copy(alpha = if (pal.oscuro) 0.46f else 0.34f)
    val onda = if (pal.oscuro) lerp(color, Color.White, 0.18f) else lerp(color, Color.Black, 0.12f)
    Canvas(
        Modifier
            .padding(horizontal = MARGEN)
            .fillMaxWidth()
            .height(56.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(pal.tarjeta)
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
                var modo = NADA
                var acumulado = 0f
                var inicioVista0 = 0.0
                var escala = 1.0
                var umbral = 0L
                fun terminar() {
                    if (modo > 0) Edicion.terminar()
                    modo = NADA
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
                        if (m > 0) {
                            Edicion.seleccion = Edicion.Seleccion(p.id, i)
                            Edicion.mensaje = ""
                            if (!Edicion.empezar(p, i, m)) modo = NADA
                        }
                    },
                    onDragEnd = { terminar() },
                    onDragCancel = { terminar() },
                ) { cambio, dx ->
                    cambio.consume()
                    acumulado += dx
                    when {
                        modo == DESPLAZAR ->
                            Edicion.ponerInicio(inicioVista0 - acumulado / size.width * Edicion.anchoVista())
                        // El núcleo calcula el nuevo pedazo (con el imán y sus límites).
                        modo > 0 -> Edicion.arrastrar((acumulado * escala).roundToLong(), umbral)
                    }
                }
            },
    ) {
        val w = size.width
        val h = size.height
        val sel = Edicion.seleccion?.takeIf { it.pistaId == p.id }?.indice ?: -1
        val alfa = if (suena) 1f else 0.35f
        val radio = CornerRadius(6.dp.toPx())
        val orden = p.pedazos.indices.sortedBy { if (it == sel) 1 else 0 }
        for (i in orden) {
            val pz = p.pedazos[i]
            val x0 = xDe(pz.inicio, w)
            val x1 = xDe(pz.fin, w)
            if (x1 < 0 || x0 > w) continue
            val elegido = i == sel
            val ancho = max(1f, x1 - x0)
            drawRoundRect(if (elegido) cuerpoSel else cuerpo, Offset(x0, 0f), Size(ancho, h), radio, alpha = alfa)
            // Franja de arriba en el color de la pista, como las regiones de GarageBand.
            clipRect(x0, 0f, x0 + ancho, 4.dp.toPx()) {
                drawRoundRect(color, Offset(x0, 0f), Size(ancho, h), radio, alpha = alfa)
            }
            clipRect(x0, 0f, x0 + ancho, h) {
                ondaPedazo(p, i, pz, x0, x1, 6.dp.toPx(), h - 8.dp.toPx(), onda.copy(alpha = alfa))
            }
            if (elegido) drawRoundRect(pal.texto, Offset(x0, 0f), Size(ancho, h), radio, style = Stroke(2.dp.toPx()))
        }
        // Agarraderas del pedazo elegido: fundidos arriba, recortes en las orillas de abajo.
        p.pedazos.getOrNull(sel)?.let { pz ->
            val x0 = xDe(pz.inicio, w)
            val x1 = xDe(pz.fin, w)
            val xi = xDe(pz.inicio + pz.fadeIn, w)
            val xo = xDe(pz.fin - pz.fadeOut, w)
            val grosor = 1.5f * density
            if (pz.fadeIn > 0) drawLine(pal.texto, Offset(x0, h), Offset(xi, 0f), grosor)
            if (pz.fadeOut > 0) drawLine(pal.texto, Offset(xo, 0f), Offset(x1, h), grosor)
            for (x in listOf(xi, xo)) {
                val c = Offset(x.entre(x0 + 7.dp.toPx(), x1 - 7.dp.toPx()), 8.dp.toPx())
                drawCircle(Color.White, 6.dp.toPx(), c)
                drawCircle(pal.texto, 6.dp.toPx(), c, style = Stroke(2.dp.toPx()))
            }
            val ag = 5.dp.toPx()
            drawRoundRect(pal.texto, Offset(x0 + 3.dp.toPx(), h * 0.42f), Size(ag, h * 0.42f), CornerRadius(ag / 2))
            drawRoundRect(pal.texto, Offset(x1 - 3.dp.toPx() - ag, h * 0.42f), Size(ag, h * 0.42f), CornerRadius(ag / 2))
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
private fun modoEn(p: Pista, o: Offset, w: Float, h: Float, zona: Float): Pair<Int, Int> {
    val sel = Edicion.seleccion?.takeIf { it.pistaId == p.id }?.indice ?: -1
    p.pedazos.getOrNull(sel)?.let { pz ->
        val x0 = xDe(pz.inicio, w)
        val x1 = xDe(pz.fin, w)
        if (o.x >= x0 - zona && o.x <= x1 + zona) {
            if (o.y < h * 0.4f) {
                val di = abs(o.x - xDe(pz.inicio + pz.fadeIn, w))
                val dout = abs(o.x - xDe(pz.fin - pz.fadeOut, w))
                if (min(di, dout) < zona) return (if (di <= dout) Nucleo.FADE_IN else Nucleo.FADE_OUT) to sel
            } else {
                val d0 = abs(o.x - x0)
                val d1 = abs(o.x - x1)
                if (min(d0, d1) < zona) return (if (d0 <= d1) Nucleo.RECORTAR_INICIO else Nucleo.RECORTAR_FIN) to sel
            }
        }
    }
    val i = pedazoEn(p, o.x, w, 0f)
    return if (i >= 0) Nucleo.MOVER to i else DESPLAZAR to -1
}

/** Datos y ajustes finos del pedazo elegido. */
@Composable
private fun InfoPedazo() {
    val pal = LocalPaleta.current
    Tarjeta(
        Modifier.padding(start = MARGEN, end = MARGEN, top = 6.dp, bottom = 8.dp),
        PaddingValues(horizontal = 14.dp, vertical = 10.dp),
    ) {
        if (Edicion.mensaje.isNotEmpty()) {
            Texto(Edicion.mensaje, Tipo.nota.seminegrita, pal.acento, Modifier.padding(bottom = 4.dp), lineas = 2)
        }
        val sel = Edicion.seleccionado()
        if (sel == null) {
            Texto(
                "Toca un pedazo para elegirlo y arrástralo para moverlo. Ya elegido, sus orillas recortan y los " +
                    "puntos de arriba hacen el fade. Cortar sin elegir corta todas las pistas en el cursor.",
                Tipo.nota, pal.texto2,
            )
            return@Tarjeta
        }
        val (p, i) = sel
        val pz = p.pedazos[i]
        Texto(
            "${p.id} · ${p.nombre} · pedazo ${i + 1} de ${p.pedazos.size}", Tipo.nota.seminegrita, pal.texto, lineas = 1,
        )
        Texto(
            "en ${textoCuadros(pz.inicio)} · dura ${textoCuadros(pz.largo)} · archivo ${textoCuadros(pz.origen)}",
            Tipo.leyenda.numeros, pal.texto2, Modifier.padding(top = 2.dp), lineas = 1,
        )
        FilaFade("Fade in", pz.fadeIn, min(pz.largo - pz.fadeOut, 480000L)) { Edicion.ponerFadeSeleccionado(true, it) }
        FilaFade("Fade out", pz.fadeOut, min(pz.largo - pz.fadeIn, 480000L)) { Edicion.ponerFadeSeleccionado(false, it) }
        Row(Modifier.padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Chip(
                "10 ms", false, { Edicion.empujar(-10) }, icono = Iconos.chevronIzquierda, colorInactivo = pal.acento,
                descripcion = "Mover 10 ms antes",
            )
            Spacer(Modifier.width(8.dp))
            Chip(
                "10 ms", false, { Edicion.empujar(10) }, icono = Iconos.chevronDerecha, colorInactivo = pal.acento,
                descripcion = "Mover 10 ms después",
            )
            Spacer(Modifier.weight(1f))
            BotonTexto(
                "Original", { Edicion.restaurarPista(p) }, estilo = Tipo.subtitulo.seminegrita, habilitado = p.editada,
            )
        }
    }
}

@Composable
private fun FilaFade(etiqueta: String, cuadros: Long, maximo: Long, alCambiar: (Long) -> Unit) {
    val maxS = max(0.01f, (maximo / Mesa.RATE).toFloat())
    FilaDeslizador(
        etiqueta, String.format(Locale.US, "%.2f s", cuadros / Mesa.RATE), { alCambiar(0) },
        anchoEtiqueta = 64.dp, anchoValor = 56.dp,
    ) { mod ->
        Deslizador(
            (cuadros / Mesa.RATE).toFloat().coerceAtMost(maxS), 0f, maxS,
            { alCambiar((it * Mesa.RATE).roundToLong().coerceAtMost(maximo)) }, mod,
            alReiniciar = { alCambiar(0) }, descripcion = etiqueta,
        )
    }
}

// ---------------------------------------------------------------- automatización

private val ALTO_AUTO = 96.dp
private val BORDE_AUTO = 10.dp  // arriba y abajo dentro del carril: los puntos de las orillas se ven completos

/** Texto del valor de un punto: "−6.0 dB" o "L 30". */
internal fun textoAuto(param: Int, valor: Float): String =
    if (param == Nucleo.AUTO_PANEO) textoPan(Math.round(valor)) else textoDb(valor, Mesa.VOL_MIN + 0.1f)

private fun textoModo(modo: Int): String = when (modo) {
    Nucleo.AUTO_APAGADA -> "apagada"
    Nucleo.AUTO_GRABAR -> "grabar movimientos"
    else -> "leer"
}

/**
 * Carril de automatización: la curva sobre los pedazos (tenues, para ubicarse). Tocar pone un
 * punto; arrastrar un punto lo mueve; tocarlo dos veces lo quita. Arrastrar fuera de los puntos
 * recorre la vista (de lado) o la lista (de arriba abajo).
 */
@Composable
private fun CarrilAuto(p: Pista, param: Int) {
    val pal = LocalPaleta.current
    val color = pal.pistas[p.color]
    val curva = p.curva(param)
    val nombreParam = if (param == Nucleo.AUTO_PANEO) "paneo" else "volumen"
    Row(
        Modifier.padding(start = MARGEN + 2.dp, end = MARGEN, top = 8.dp, bottom = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(color))
        Texto(
            "${p.id} · ${p.nombre}", Tipo.leyenda.seminegrita, pal.texto, Modifier.padding(start = 6.dp).weight(1f, fill = false),
            lineas = 1,
        )
        val n = curva.n
        val puntos = if (n == 0) "" else if (n == 1) " · 1 punto" else " · $n puntos"
        Texto(
            " · ${textoModo(p.autoModo)}$puntos", Tipo.leyenda,
            if (p.autoModo == Nucleo.AUTO_GRABAR) pal.rojo else pal.texto2, lineas = 1,
        )
    }
    val medidor = rememberTextMeasurer()
    val estiloGuia = TextStyle(color = pal.texto2, fontSize = 10.sp)
    val estiloValor = TextStyle(color = pal.texto, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
    val cuerpo = color.copy(alpha = if (pal.oscuro) 0.14f else 0.09f)
    val onda = color.copy(alpha = if (pal.oscuro) 0.30f else 0.24f)
    // La curva un poco más oscura en claro (el amarillo casi no se ve sobre su fondo).
    val trazo = if (pal.oscuro) color else lerp(color, Color.Black, 0.22f)
    val fondo = pal.tarjeta
    Canvas(
        Modifier
            .padding(horizontal = MARGEN)
            .fillMaxWidth()
            .height(ALTO_AUTO)
            .clip(RoundedCornerShape(8.dp))
            .background(pal.tarjeta)
            .testTag("auto-${p.id}")
            .semantics { contentDescription = "Automatización de $nombreParam de ${p.nombre}" }
            .pointerInput(p.id, param) { gestosAuto(p, param) },
    ) {
        val w = size.width
        val h = size.height
        val arriba = BORDE_AUTO.toPx()
        val alto = h - 2 * arriba
        fun y(pos: Float) = arriba + (1f - pos.coerceIn(0f, 1f)) * alto
        val radio = CornerRadius(6.dp.toPx())
        p.pedazos.forEachIndexed { i, pz ->
            val x0 = xDe(pz.inicio, w)
            val x1 = xDe(pz.fin, w)
            if (x1 < 0 || x0 > w) return@forEachIndexed
            drawRoundRect(cuerpo, Offset(x0, 0f), Size(max(1f, x1 - x0), h), radio)
            clipRect(x0, 0f, x1, h) { ondaPedazo(p, i, pz, x0, x1, 8.dp.toPx(), h - 16.dp.toPx(), onda) }
        }
        // Guía: 0 dB, o el centro del paneo con izquierda arriba y derecha abajo.
        val raya = PathEffect.dashPathEffect(floatArrayOf(4.dp.toPx(), 4.dp.toPx()))
        val yg = y(curva.guia)
        drawLine(pal.texto3, Offset(0f, yg), Offset(w, yg), density, pathEffect = raya)
        val margen = 4.dp.toPx()
        if (param == Nucleo.AUTO_PANEO) {
            drawText(medidor, "L", Offset(margen, 1.dp.toPx()), estiloGuia)
            drawText(medidor, "R", Offset(margen, h - 14.dp.toPx()), estiloGuia)
        } else {
            drawText(medidor, "0 dB", Offset(margen, yg - 14.dp.toPx()), estiloGuia)
        }
        val activa = p.autoModo != Nucleo.AUTO_APAGADA
        val alfa = if (activa) 1f else 0.45f
        if (curva.n == 0) {
            // Sin puntos suena el valor fijo del deslizador.
            val yf = y(curva.posFija)
            drawLine(color.copy(alpha = 0.7f * alfa), Offset(0f, yf), Offset(w, yf), 2.dp.toPx(), pathEffect = raya)
            return@Canvas
        }
        val linea = Path()
        linea.moveTo(min(0f, xDe(curva.cuadros[0], w)), y(curva.pos[0]))
        for (k in 0 until curva.n) linea.lineTo(xDe(curva.cuadros[k], w), y(curva.pos[k]))
        linea.lineTo(max(w, xDe(curva.cuadros[curva.n - 1], w)), y(curva.pos[curva.n - 1]))
        val relleno = Path()
        relleno.addPath(linea)
        relleno.lineTo(max(w, xDe(curva.cuadros[curva.n - 1], w)), h)
        relleno.lineTo(min(0f, xDe(curva.cuadros[0], w)), h)
        relleno.close()
        drawPath(relleno, color.copy(alpha = (if (pal.oscuro) 0.20f else 0.14f) * alfa))
        drawPath(linea, trazo.copy(alpha = alfa), style = Stroke(2.dp.toPx(), join = StrokeJoin.Round))
        val sel = Edicion.punto?.takeIf { it.pistaId == p.id && it.param == param }?.indice ?: -1
        val r = 4.5.dp.toPx()
        // Los puntos muy juntos (lo grabado, con la vista alejada) no se dibujan todos: se
        // encimarían. Al acercar la vista aparecen.
        val separacion = 3 * r
        var ultimoX = Float.NEGATIVE_INFINITY
        var ultimoY = 0f
        for (k in 0 until curva.n) {
            val x = xDe(curva.cuadros[k], w)
            if (x < -r || x > w + r || k == sel) continue
            val yk = y(curva.pos[k])
            if (x - ultimoX < separacion && abs(yk - ultimoY) < separacion && k != curva.n - 1) continue
            ultimoX = x
            ultimoY = yk
            val c = Offset(x, yk)
            drawCircle(fondo, r, c)
            drawCircle(trazo.copy(alpha = alfa), r, c, style = Stroke(2.dp.toPx()))
        }
        if (sel in 0 until curva.n) {
            val c = Offset(xDe(curva.cuadros[sel], w), y(curva.pos[sel]))
            drawCircle(trazo, 7.dp.toPx(), c)
            drawCircle(Color.White, 2.5.dp.toPx(), c)
            val t = medidor.measure(textoAuto(param, curva.valores[sel]), estiloValor)
            val tx = (c.x - t.size.width / 2f).entre(2.dp.toPx(), w - t.size.width - 2.dp.toPx())
            val ty = if (c.y - 10.dp.toPx() - t.size.height > 0) c.y - 10.dp.toPx() - t.size.height else c.y + 10.dp.toPx()
            drawRoundRect(
                fondo.copy(alpha = 0.92f), Offset(tx - 4.dp.toPx(), ty - 1.dp.toPx()),
                Size(t.size.width + 8.dp.toPx(), t.size.height + 2.dp.toPx()), CornerRadius(4.dp.toPx()),
            )
            drawText(t, topLeft = Offset(tx, ty))
        }
    }
}

/** Índice del punto más cercano a [o] (a [radio] o menos), o -1. */
private fun puntoEn(curva: CurvaAuto, o: Offset, w: Float, arriba: Float, alto: Float, radio: Float): Int {
    var mejor = -1
    var dist = radio
    for (k in 0 until curva.n) {
        val dx = xDe(curva.cuadros[k], w) - o.x
        val dy = arriba + (1f - curva.pos[k]) * alto - o.y
        val d = kotlin.math.sqrt(dx * dx + dy * dy)
        if (d <= dist) {
            dist = d
            mejor = k
        }
    }
    return mejor
}

private suspend fun PointerInputScope.gestosAuto(p: Pista, param: Int) {
    var ultimo = -1      // último punto tocado (para el doble toque)
    var ultimoMs = 0L
    awaitEachGesture {
        val down = awaitFirstDown(requireUnconsumed = false)
        val w = size.width.toFloat()
        val arriba = BORDE_AUTO.toPx()
        val alto = size.height - 2 * arriba
        val slop = viewConfiguration.touchSlop
        val i = puntoEn(p.curva(param), down.position, w, arriba, alto, 22.dp.toPx())
        if (i >= 0) {
            // Sobre un punto: arrastrarlo lo mueve; dos toques seguidos lo quitan.
            down.consume()
            Edicion.punto = Edicion.PuntoSel(p.id, param, i)
            Edicion.mensaje = ""
            val doble = i == ultimo && down.uptimeMillis - ultimoMs < 400
            var arrastre = false
            var solto = false
            val escala = cuadrosPorPx(w)
            try {
                while (true) {
                    val c = awaitPointerEvent().changes.firstOrNull { it.id == down.id } ?: break
                    if (!c.pressed) {
                        c.consume()
                        solto = true
                        break
                    }
                    val d = c.position - down.position
                    if (!arrastre && d.getDistance() > slop) {
                        arrastre = Mesa.empezarPunto(p, param, i)
                        Edicion.tocando = arrastre
                    }
                    if (arrastre) Mesa.arrastrarPunto((d.x * escala).roundToLong(), -d.y / alto)
                    c.consume()
                }
            } finally {
                if (arrastre) {
                    Mesa.terminarPunto()
                    Edicion.tocando = false
                }
            }
            when {
                arrastre || !solto -> ultimo = -1
                doble -> {
                    Mesa.quitarPunto(p, param, i)
                    Edicion.punto = null
                    ultimo = -1
                }
                else -> {
                    ultimo = i
                    ultimoMs = down.uptimeMillis
                }
            }
        } else {
            // Fuera de los puntos: un toque pone uno; de lado recorre la vista; de arriba abajo
            // se deja a la lista.
            ultimo = -1
            val inicio0 = Edicion.vistaInicio
            var recorre = false
            try {
                while (true) {
                    val c = awaitPointerEvent().changes.firstOrNull { it.id == down.id } ?: break
                    if (!recorre && c.isConsumed) break
                    if (!c.pressed) {
                        if (!recorre) {
                            val k = Mesa.agregarPunto(
                                p, param, cuadroDe(down.position.x, w), 1f - (down.position.y - arriba) / alto,
                            )
                            Edicion.punto = if (k >= 0) Edicion.PuntoSel(p.id, param, k) else null
                            Edicion.mensaje = ""
                        }
                        break
                    }
                    val d = c.position - down.position
                    if (!recorre) {
                        if (abs(d.y) > slop && abs(d.y) > abs(d.x)) break
                        if (abs(d.x) > slop) {
                            recorre = true
                            Edicion.tocando = true
                        }
                    }
                    if (recorre) {
                        c.consume()
                        Edicion.ponerInicio(inicio0 - d.x / w * Edicion.anchoVista())
                    }
                }
            } finally {
                if (recorre) Edicion.tocando = false
            }
        }
    }
}

/** El punto elegido: dónde está, cuánto vale y quitarlo. */
@Composable
private fun InfoPunto(param: Int) {
    val pal = LocalPaleta.current
    Tarjeta(
        Modifier.padding(start = MARGEN, end = MARGEN, top = 6.dp, bottom = 8.dp),
        PaddingValues(horizontal = 14.dp, vertical = 10.dp),
    ) {
        if (Edicion.mensaje.isNotEmpty()) {
            Texto(Edicion.mensaje, Tipo.nota.seminegrita, pal.acento, Modifier.padding(bottom = 4.dp), lineas = 2)
        }
        val sel = Edicion.puntoSeleccionado()
        if (sel == null) {
            Texto(
                "Toca el carril para poner un punto, arrástralo para moverlo y tócalo dos veces para quitarlo. " +
                    "Para grabar movimientos: en Mezcla toca A → Grabar y mueve Vol o Pan mientras suena.",
                Tipo.nota, pal.texto2,
            )
            return@Tarjeta
        }
        val (p, i) = sel
        val c = p.curva(param)
        Texto(
            "${p.id} · ${p.nombre} · punto ${i + 1} de ${c.n}", Tipo.nota.seminegrita, pal.texto, lineas = 1,
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            Texto(
                "en ${textoCuadros(c.cuadros[i])} · ${textoAuto(param, c.valores[i])}", Tipo.leyenda.numeros, pal.texto2,
                Modifier.weight(1f), lineas = 1,
            )
            BotonTexto("Quitar", { Edicion.borrarPunto() }, color = pal.rojo, estilo = Tipo.subtitulo.seminegrita)
        }
    }
}
