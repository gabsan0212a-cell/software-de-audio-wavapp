package com.wavapp.mezcla.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.changedToDown
import androidx.compose.ui.input.pointer.changedToUp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.Edicion
import com.wavapp.mezcla.Fila
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.Patron
import com.wavapp.mezcla.Pista
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.Locale
import kotlin.math.exp
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.roundToInt

/** Lo que se ve del editor (solo pantalla). */
internal object EditorVista {
    var modo by mutableIntStateOf(0)        // 0 notas, 1 tocar, 2 sonido
    var largo by mutableIntStateOf(2)       // pasos de una nota nueva
    var acordes by mutableStateOf(false)
    var soloEscala by mutableStateOf(true)
    var octava by mutableIntStateOf(0)      // de los pads de notas
}

/** Editor de un instrumento a pantalla completa: notas, tocar en vivo y sonido. */
@Composable
fun EditorInstrumento(pista: Pista?, alCerrar: () -> Unit) {
    val visible = pista != null
    AlRegresar(visible) { alCerrar() }
    val recordada = remember { arrayOfNulls<Pista>(1) }
    if (pista != null) recordada[0] = pista
    val p = pista ?: recordada[0]
    AnimatedVisibility(
        visible,
        enter = slideInVertically(tween(340, easing = FastOutSlowInEasing)) { it },
        exit = slideOutVertically(tween(260)) { it },
    ) {
        if (p != null) ContenidoEditor(p, alCerrar)
    }
}

@Composable
private fun ContenidoEditor(p: Pista, alCerrar: () -> Unit) {
    val pal = LocalPaleta.current
    Column(
        Modifier
            .fillMaxSize()
            .background(pal.fondoAgrupado)
            .testTag("editorInstrumento")
            .pointerInput(Unit) { detectTapGestures { } }
            .windowInsetsPadding(WindowInsets.systemBars),
    ) {
        // Barra de arriba
        Row(Modifier.fillMaxWidth().height(48.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            BotonTexto("Listo", alCerrar, estilo = Tipo.encabezado)
            Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                Texto(p.nombre, Tipo.encabezado, pal.texto, lineas = 1)
                val pat = p.patron
                val preset = pat?.let { Mesa.infoInstrumentos.tipos.getOrNull(it.tipo)?.presets?.getOrNull(it.preset) } ?: ""
                Texto(
                    if (pat?.preparando == true) "Preparando…" else preset, Tipo.leyenda, pal.texto2, lineas = 1,
                )
            }
            BotonIcono(Iconos.deshacer, "Deshacer", { Edicion.deshacer() }, habilitado = Mesa.puedeDeshacer, tamano = 22.dp)
            BotonReproducir(pal.acento, 26.dp, 44.dp)
        }
        Segmentado(
            listOf("Notas", "Tocar", "Sonido"), EditorVista.modo, { EditorVista.modo = it },
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp).testTag("modoEditor"),
        )
        when (EditorVista.modo) {
            0 -> {
                Cuadricula(p, Modifier.weight(1f).fillMaxWidth().padding(top = 4.dp))
                ControlesNotas(p)
            }
            1 -> {
                Pads(p, Modifier.weight(1f).fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp))
                ControlesTocar(p)
            }
            else -> Column(
                Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState())
                    .padding(start = 16.dp, end = 16.dp, bottom = 24.dp),
            ) { Sonido(p) }
        }
    }
}

// ---------------------------------------------------------------- cuadrícula de notas

private val ANCHO_PASO = 30.dp
private val ALTO_FILA = 32.dp
private val ANCHO_NOMBRES = 58.dp
private val ALTO_REGLA = 22.dp

@Composable
private fun filasDe(p: Pista): List<Fila> {
    val m = Mesa.musica
    val tipo = p.instrumento
    val solo = EditorVista.soloEscala && tipo != Nucleo.INST_BATERIA
    // Se vuelven a pedir si cambia la escala (el núcleo dice qué notas hay).
    return remember(p.id, solo, m.tonica, m.menor, m.tonicaDeCancion) { Mesa.filas(p, solo) }
}

@Composable
private fun Cuadricula(p: Pista, modifier: Modifier) {
    val pal = LocalPaleta.current
    val pat = p.patron ?: return
    val info = Mesa.infoInstrumentos
    val filas = filasDe(p)
    val bateria = p.instrumento == Nucleo.INST_BATERIA
    val pasos = pat.compases * info.pasosCompas
    val dens = LocalDensity.current
    val wPaso = with(dens) { ANCHO_PASO.toPx() }
    // Los nombres de la batería son más largos ("Hi-hat abierto").
    val wNombres = with(dens) { (if (bateria) ANCHO_NOMBRES + 30.dp else ANCHO_NOMBRES).toPx() }
    val hRegla = with(dens) { ALTO_REGLA.toPx() }
    var x0 by remember(p.id) { mutableFloatStateOf(0f) }
    var y0 by remember(p.id) { mutableFloatStateOf(-1f) }  // -1: todavía no se centró
    var ancho by remember { mutableFloatStateOf(0f) }
    var alto by remember { mutableFloatStateOf(0f) }
    // Si caben todas las filas, se agrandan hasta llenar la vista (en la batería, pads más grandes).
    val hFila = max(with(dens) { ALTO_FILA.toPx() }, if (filas.isEmpty()) 0f else (alto - hRegla) / filas.size).coerceAtMost(with(dens) { 64.dp.toPx() })
    val color = colorInstrumento(p.instrumento)
    val medidor = rememberTextMeasurer()
    val estiloFila = TextStyle(color = pal.texto2, fontSize = 11.sp)
    val estiloTonica = TextStyle(color = pal.texto, fontSize = 11.sp, fontWeight = FontWeight.Bold)
    val estiloRegla = TextStyle(color = pal.texto2, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
    val scope = rememberCoroutineScope()
    val filasAct by rememberUpdatedState(filas)
    val patAct by rememberUpdatedState(pat)

    fun maxX() = max(0f, pasos * wPaso - (ancho - wNombres))
    fun maxY() = max(0f, filasAct.size * hFila - (alto - hRegla))
    // Al abrir: la zona donde suele tocarse el instrumento, centrada.
    LaunchedEffect(p.id, alto, filas.size) {
        if (y0 >= 0f || alto <= 0f) return@LaunchedEffect
        val inicio = info.tipos.getOrNull(p.instrumento)?.inicio ?: 60
        val k = filas.indexOfFirst { it.nota <= inicio + 7 }.coerceAtLeast(0)
        y0 = (k * hFila - (alto - hRegla) / 2f + hFila * 4f).entre(0f, maxY())
    }
    // Mientras suena, la vista sigue a la canción (pasa de página).
    LaunchedEffect(p.id) {
        snapshotFlow { Mesa.pasoAhora }.collect { paso ->
            if (paso < 0f || ancho <= 0f) return@collect
            val x = paso * wPaso
            val vista = ancho - wNombres
            if (x < x0 || x > x0 + vista - wPaso) x0 = (x - wPaso).entre(0f, maxX())
        }
    }

    fun celda(pos: Offset): Pair<Int, Fila?> {
        val col = floor((pos.x - wNombres + x0) / wPaso).toInt()
        val fila = floor((pos.y - hRegla + y0) / hFila).toInt()
        return col to filasAct.getOrNull(fila)
    }
    fun notaEn(col: Int, fila: Fila): Int {
        val pulso = col * info.pasoPulsos
        val pt = patAct
        for (i in 0 until pt.n) if (pt.nota(i) == fila.nota && pulso >= pt.pulso(i) && pulso < pt.pulso(i) + pt.largo(i)) return i
        return -1
    }
    fun probar(nota: Int) {
        Mesa.tocar(p, nota, true)
        scope.launch {
            delay(260)
            Mesa.tocar(p, nota, false)
        }
    }

    Canvas(
        modifier
            .onSizeChanged {
                ancho = it.width.toFloat()
                alto = it.height.toFloat()
            }
            .testTag("cuadricula")
            .semantics { contentDescription = "Cuadrícula de notas de ${p.nombre}" }
            .pointerInput(p.id) {
                detectDragGestures { cambio, d ->
                    cambio.consume()
                    x0 = (x0 - d.x).entre(0f, maxX())
                    y0 = (y0 - d.y).entre(0f, maxY())
                }
            }
            .pointerInput(p.id) {
                detectTapGestures(
                    onTap = { pos ->
                        val (col, fila) = celda(pos)
                        if (fila == null) return@detectTapGestures
                        if (pos.x < wNombres) {  // el nombre de la fila: solo se escucha
                            probar(fila.nota)
                            return@detectTapGestures
                        }
                        if (col < 0 || col >= pasos || pos.y < hRegla) return@detectTapGestures
                        val agregada = Mesa.alternarNota(
                            p, col * info.pasoPulsos, fila.nota,
                            (if (bateria) 1 else EditorVista.largo) * info.pasoPulsos, EditorVista.acordes && !bateria,
                        )
                        if (agregada) probar(fila.nota)
                    },
                    onLongPress = { pos ->
                        val (col, fila) = celda(pos)
                        if (fila == null || col < 0 || col >= pasos || pos.x < wNombres || pos.y < hRegla) return@detectTapGestures
                        if (bateria) {
                            // Redoble: 1, 2, 3 o 4 golpes en el paso (los hi-hats del trap).
                            Mesa.redoble(p, col, fila.nota)
                            probar(fila.nota)
                        } else {
                            // Una nota que ya está: más larga (1, 2, 4, 8, 16 pasos y vuelve).
                            val i = notaEn(col, fila)
                            if (i >= 0) {
                                val pt = patAct
                                val pasosNota = pt.largo(i) / info.pasoPulsos
                                val siguiente = listOf(1, 2, 4, 8, 16).firstOrNull { it > pasosNota } ?: 1
                                Mesa.cambiarNota(p, i, pt.pulso(i), siguiente * info.pasoPulsos, pt.nota(i), pt.fuerza(i))
                            }
                        }
                    },
                )
            },
    ) {
        val vistaX0 = x0
        val vistaY0 = y0.coerceAtLeast(0f)
        val gx = wNombres
        val gy = hRegla
        // Filas
        clipRect(gx, gy, size.width, size.height) {
            filas.forEachIndexed { k, f ->
                val y = gy + k * hFila - vistaY0
                if (y + hFila < gy || y > size.height) return@forEachIndexed
                val fondo = when {
                    f.tonica -> lerp(pal.tarjeta, color, if (pal.oscuro) 0.14f else 0.10f)
                    !f.enEscala -> lerp(pal.tarjeta, pal.fondoAgrupado, 0.75f)
                    else -> pal.tarjeta
                }
                drawRect(fondo, Offset(gx, y), Size(size.width - gx, hFila))
                drawLine(pal.separador, Offset(gx, y + hFila), Offset(size.width, y + hFila), 1f)
            }
            // Pasos, pulsos y compases
            val primero = floor(vistaX0 / wPaso).toInt().coerceAtLeast(0)
            for (s in primero..pasos) {
                val x = gx + s * wPaso - vistaX0
                if (x > size.width) break
                val c = when {
                    s % info.pasosCompas == 0 -> pal.texto3
                    s % 4 == 0 -> pal.separador.copy(alpha = 1f)
                    else -> pal.separador.copy(alpha = 0.45f)
                }
                drawLine(c, Offset(x, gy), Offset(x, size.height), if (s % info.pasosCompas == 0) 2f else 1f)
            }
            // Fuera del patrón
            val fin = gx + pasos * wPaso - vistaX0
            if (fin < size.width) drawRect(pal.fondoAgrupado, Offset(fin, gy), Size(size.width - fin, size.height - gy))
            // Notas
            val porFila = HashMap<Int, Int>(filas.size * 2)
            filas.forEachIndexed { k, f -> porFila[f.nota] = k }
            val wPulso = wPaso / info.pasoPulsos
            for (i in 0 until pat.n) {
                val k = porFila[pat.nota(i)] ?: continue
                val x = gx + pat.pulso(i) * wPulso - vistaX0
                val w = max(4f, pat.largo(i) * wPulso - 2f)
                if (x > size.width || x + w < gx) continue
                val y = gy + k * hFila - vistaY0
                if (y + hFila < gy || y > size.height) continue
                val a = 0.55f + 0.45f * (pat.fuerza(i) / 127f)
                drawRoundRect(
                    color.copy(alpha = a), Offset(x + 1f, y + 3f), Size(w, hFila - 6f), CornerRadius(5.dp.toPx()),
                )
            }
            // Dónde va la canción (se lee al dibujar).
            val paso = Mesa.pasoAhora
            if (paso >= 0f) {
                val x = gx + paso * wPaso - vistaX0
                drawLine(pal.rojo, Offset(x, gy), Offset(x, size.height), 2.dp.toPx())
            }
        }
        // Regla de compases arriba
        drawRect(pal.fondoAgrupado, Offset(0f, 0f), Size(size.width, gy))
        clipRect(gx, 0f, size.width, gy) {
            for (b in 0 until pat.compases) {
                val x = gx + b * info.pasosCompas * wPaso - vistaX0
                if (x > size.width) break
                if (x + info.pasosCompas * wPaso < gx) continue
                drawText(medidor, "${b + 1}", Offset(x + 4.dp.toPx(), 3.dp.toPx()), estiloRegla)
                for (beat in 1 until 4) {
                    val xb = x + beat * 4 * wPaso
                    drawLine(pal.texto3, Offset(xb, gy - 6.dp.toPx()), Offset(xb, gy - 1f), 1f)
                }
            }
        }
        // Nombres de las filas a la izquierda (fijos)
        drawRect(pal.fondoAgrupado, Offset(0f, gy), Size(gx, size.height - gy))
        clipRect(0f, gy, gx, size.height) {
            filas.forEachIndexed { k, f ->
                val y = gy + k * hFila - vistaY0
                if (y + hFila < gy || y > size.height) return@forEachIndexed
                val t = medidor.measure(f.nombre, if (f.tonica) estiloTonica else estiloFila, maxLines = 1)
                drawText(t, topLeft = Offset(6.dp.toPx(), y + (hFila - t.size.height) / 2f))
            }
        }
        drawLine(pal.separador, Offset(gx, 0f), Offset(gx, size.height), 1f)
    }
}

@Composable
private fun ControlesNotas(p: Pista) {
    val pal = LocalPaleta.current
    val pat = p.patron ?: return
    val bateria = p.instrumento == Nucleo.INST_BATERIA
    Column(Modifier.fillMaxWidth().background(pal.barra).padding(top = 8.dp, bottom = 6.dp)) {
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically,
        ) {
            if (!bateria) {
                for ((pasos, t) in listOf(1 to "1/16", 2 to "1/8", 4 to "1/4", 8 to "1/2", 16 to "1")) {
                    Chip(t, EditorVista.largo == pasos, { EditorVista.largo = pasos }, descripcion = "Notas de $t")
                }
                if (p.instrumento != Nucleo.INST_808) {
                    Chip("Acordes", EditorVista.acordes, { EditorVista.acordes = !EditorVista.acordes })
                }
                Chip("Escala", EditorVista.soloEscala, { EditorVista.soloEscala = !EditorVista.soloEscala }, descripcion = "Solo las notas de la escala")
            } else {
                Texto("Toca para poner o quitar; deja el dedo para un redoble.", Tipo.nota, pal.texto2, lineas = 1)
            }
        }
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically,
        ) {
            Texto("Compases", Tipo.nota, pal.texto2, lineas = 1)
            for (c in listOf(1, 2, 4, 8)) {
                Chip("$c", pat.compases == c, { Mesa.ponerPatron(p, c, pat.veces, pat.swing) }, descripcion = "$c compases")
            }
            Spacer(Modifier.width(4.dp))
            Texto("Veces", Tipo.nota, pal.texto2, lineas = 1)
            BotonIcono(Iconos.chevronIzquierda, "Menos veces", { Mesa.ponerPatron(p, pat.compases, pat.veces - 1, pat.swing) },
                habilitado = pat.veces > 1, tamano = 16.dp, lado = 34.dp)
            Texto("${pat.veces}", Tipo.subtitulo.numeros, pal.texto, lineas = 1)
            BotonIcono(Iconos.chevronDerecha, "Más veces", { Mesa.ponerPatron(p, pat.compases, pat.veces + 1, pat.swing) },
                habilitado = pat.veces < Mesa.infoInstrumentos.maxVeces, tamano = 16.dp, lado = 34.dp)
            Spacer(Modifier.width(4.dp))
            Chip("Otra idea", false, { Mesa.otraIdea(p) }, icono = Iconos.idea, colorInactivo = pal.acento)
            Chip("Borrar", false, { Vista.borrarNotas = p }, colorInactivo = pal.rojo)
        }
        if (bateria || pat.swing > 0f) {
            // Se aplica al soltar el dedo (cada cambio vuelve a preparar el instrumento).
            var borrador by remember(p.id) { mutableStateOf<Float?>(null) }
            val swing = borrador ?: pat.swing
            FilaDeslizador(
                "Swing", "${(swing * 100).roundToInt()} %", { Mesa.ponerPatron(p, pat.compases, pat.veces, 0f) },
                Modifier.padding(horizontal = 16.dp), anchoEtiqueta = 52.dp, anchoValor = 52.dp,
            ) {
                Deslizador(
                    swing, 0f, 1f, { v -> borrador = v }, it,
                    alReiniciar = { Mesa.ponerPatron(p, pat.compases, pat.veces, 0f) }, descripcion = "Swing",
                    alTocar = { abajo ->
                        if (!abajo) {
                            borrador?.let { v -> Mesa.ponerPatron(p, pat.compases, pat.veces, v) }
                            borrador = null
                        }
                    },
                )
            }
        }
    }
}

// ---------------------------------------------------------------- tocar en vivo

/** Las notas que tienen los pads: la batería completa o 16 notas de la escala desde la octava elegida. */
@Composable
private fun notasPads(p: Pista): List<Fila> {
    if (p.instrumento == Nucleo.INST_BATERIA) {
        val f = filasDe(p).associateBy { it.nota }
        // Abajo bombo, caja, aplauso y percusión; arriba los platillos y el tom.
        return listOf(3, 4, 5, 6, 0, 1, 2, 7).mapNotNull { f[it] }
    }
    val m = Mesa.musica
    val todas = remember(p.id, m.tonica, m.menor, m.tonicaDeCancion) { Mesa.filas(p, true).reversed() }  // de grave a agudo
    val inicio = (Mesa.infoInstrumentos.tipos.getOrNull(p.instrumento)?.inicio ?: 60) + 12 * EditorVista.octava
    val k = todas.indexOfFirst { it.nota >= inicio }.let { if (it < 0) max(0, todas.size - 16) else it }
    val desde = k.toFloat().entre(0f, max(0, todas.size - 16).toFloat()).toInt()
    // Dos filas de 8: arriba la octava de arriba.
    val notas = todas.subList(desde, minOf(todas.size, desde + 16))
    return if (notas.size == 16) notas.subList(8, 16) + notas.subList(0, 8) else notas
}

@Composable
private fun Pads(p: Pista, modifier: Modifier) {
    val pal = LocalPaleta.current
    val notas = notasPads(p)
    val color = colorInstrumento(p.instrumento)
    val apretados = remember { mutableStateMapOf<Int, Boolean>() }
    val medidor = rememberTextMeasurer()
    val estilo = TextStyle(color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, textAlign = TextAlign.Center)
    val notasAct by rememberUpdatedState(notas)
    val columnas = if (p.instrumento == Nucleo.INST_BATERIA) 4 else 8
    BoxWithConstraints(modifier) {
        val filasN = (notas.size + columnas - 1) / columnas
        Canvas(
            Modifier
                .fillMaxSize()
                .testTag("pads")
                .semantics { contentDescription = "Pads de ${p.nombre}" }
                .pointerInput(p.id, columnas) {
                    // Varios dedos a la vez: cada uno toca su pad mientras está apoyado.
                    val dedos = HashMap<PointerId, Int>()
                    awaitPointerEventScope {
                        while (true) {
                            val e = awaitPointerEvent()
                            for (c in e.changes) {
                                if (c.changedToDown()) {
                                    val lista = notasAct
                                    val fn = (lista.size + columnas - 1) / columnas
                                    val col = (c.position.x / (size.width / columnas.toFloat())).toInt()
                                    val fila = (c.position.y / (size.height / fn.coerceAtLeast(1).toFloat())).toInt()
                                    val i = fila * columnas + col
                                    val f = lista.getOrNull(i) ?: continue
                                    dedos[c.id] = f.nota
                                    apretados[f.nota] = true
                                    Mesa.tocar(p, f.nota, true)
                                    c.consume()
                                } else if (c.changedToUp()) {
                                    val nota = dedos.remove(c.id) ?: continue
                                    apretados.remove(nota)
                                    Mesa.tocar(p, nota, false)
                                    c.consume()
                                }
                            }
                        }
                    }
                },
        ) {
            val sep = 8.dp.toPx()
            val w = (size.width - sep * (columnas - 1)) / columnas
            val h = (size.height - sep * (filasN - 1)) / filasN.coerceAtLeast(1)
            notas.forEachIndexed { i, f ->
                val col = i % columnas
                val fila = i / columnas
                val x = col * (w + sep)
                val y = fila * (h + sep)
                val on = apretados[f.nota] == true
                val base = if (f.tonica) color else lerp(color, if (pal.oscuro) Color.Black else Color.White, 0.18f)
                drawRoundRect(if (on) lerp(base, Color.White, 0.45f) else base, Offset(x, y), Size(w, h), CornerRadius(12.dp.toPx()))
                val t = medidor.measure(f.nombre, estilo, maxLines = 2, constraints = androidx.compose.ui.unit.Constraints(maxWidth = w.toInt().coerceAtLeast(1)))
                drawText(t, topLeft = Offset(x + (w - t.size.width) / 2f, y + (h - t.size.height) / 2f))
            }
        }
    }
}

@Composable
private fun ControlesTocar(p: Pista) {
    val pal = LocalPaleta.current
    val grabando = Mesa.grabandoNotas
    Column(Modifier.fillMaxWidth().background(pal.barra).padding(horizontal = 12.dp, vertical = 8.dp)) {
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Chip(
                if (grabando) "Parar" else "Grabar notas", grabando, { Mesa.grabarNotas(p, !grabando) },
                Modifier.testTag("grabarNotas"), colorActivo = pal.rojo, icono = if (grabando) Iconos.detener else Iconos.grabar,
                colorInactivo = pal.rojo,
            )
            Chip("Metrónomo", Mesa.musica.metronomo, { Mesa.ponerMetronomo(!Mesa.musica.metronomo) }, icono = Iconos.metronomo)
            Chip("Repetir", Vista.repetir, { Vista.repetir = !Vista.repetir }, icono = Iconos.repetir)
            if (p.instrumento != Nucleo.INST_BATERIA) {
                BotonIcono(Iconos.chevronIzquierda, "Octava abajo", { EditorVista.octava-- }, habilitado = EditorVista.octava > -2,
                    tamano = 16.dp, lado = 34.dp)
                Texto("Octava", Tipo.nota, pal.texto2, lineas = 1)
                BotonIcono(Iconos.chevronDerecha, "Octava arriba", { EditorVista.octava++ }, habilitado = EditorVista.octava < 2,
                    tamano = 16.dp, lado = 34.dp)
            }
        }
        Texto(
            if (grabando) "Toca encima de lo que suena: cada nota entra al patrón en el paso más cercano."
            else "Toca los pads para probar. Con \"Grabar notas\" lo que toques entra al patrón mientras suena.",
            Tipo.nota, pal.texto2, Modifier.padding(top = 6.dp),
        )
    }
}

// ---------------------------------------------------------------- sonido

@Composable
private fun Sonido(p: Pista) {
    val pal = LocalPaleta.current
    val pat = p.patron ?: return
    val tipo = Mesa.infoInstrumentos.tipos.getOrNull(p.instrumento) ?: return
    EncabezadoGrupo("Sonido")
    Tarjeta(relleno = PaddingValues(horizontal = 10.dp, vertical = 10.dp)) {
        // Los presets en filas de a 2.
        tipo.presets.chunked(2).forEachIndexed { fila, par ->
            Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                par.forEachIndexed { k, nombre ->
                    val i = fila * 2 + k
                    Chip(nombre, pat.preset == i, { Mesa.ponerPresetInst(p, i) }, Modifier.weight(1f), descripcion = "Sonido $nombre")
                }
                if (par.size == 1) Spacer(Modifier.weight(1f))
            }
        }
    }
    EncabezadoGrupo("Ajustes")
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 16.dp, top = 2.dp, bottom = 10.dp)) {
        for (a in tipo.ajustes) {
            val (etiqueta, texto) = when (a) {
                Nucleo.INST_BRILLO -> "Brillo" to { v: Float -> (if (v > 0.005f) "+" else "") + "${(v * 100).roundToInt()} %".replace('-', '−') }
                Nucleo.INST_ATAQUE -> "Ataque suave" to { v: Float -> "${(v * 100).roundToInt()} %" }
                Nucleo.INST_DURACION -> "Duración" to { v: Float -> String.format(Locale.US, "×%.2f", v) }
                Nucleo.INST_DISTORSION -> "Distorsión" to { v: Float -> "${(v * 100).roundToInt()} %" }
                Nucleo.INST_DESLIZAR -> "Deslizar entre notas" to { v: Float -> "${(v * 100).roundToInt()} %" }
                else -> "Afinación" to { v: Float -> (if (v > 0.5f) "+" else "") + "${v.roundToInt()} st".replace('-', '−') }
            }
            AjusteInst(p, pat, a, etiqueta, texto)
        }
    }
    PieGrupo(
        when (p.instrumento) {
            Nucleo.INST_808 -> "Deja dos notas encimadas para que una se deslice a la otra (con \"Deslizar\"). La distorsión hace que se oiga en bocinas chicas."
            Nucleo.INST_GUITARRA -> "Los acordes suenan rasgueados (una cuerda tras otra). La distorsión es la del amplificador."
            Nucleo.INST_BATERIA -> "Afinación sube o baja toda la batería; Duración alarga o acorta cada golpe."
            else -> "Cambia el sonido sin perder las notas: cada cambio se vuelve a preparar al momento."
        },
    )
    EncabezadoGrupo("Pista")
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 16.dp, top = 2.dp, bottom = 2.dp)) {
        FilaAjuste("Volumen", textoDb(p.volDb, Mesa.VOL_MIN)) {
            Deslizador(
                p.volDb, Mesa.VOL_MIN, Mesa.VOL_MAX, { Mesa.ponerVolumen(p, it) }, it,
                alReiniciar = { Mesa.ponerVolumen(p, 0f) }, desde = 0f, descripcion = "Volumen de ${p.nombre}",
            )
        }
        Spacer(Modifier.height(6.dp))
        Separador()
        FilaBoton("Efectos de la pista…", { Vista.efectos = p }, pal.acento)
        Separador()
        FilaBoton("Ecualizador…", { Mesa.eqAbierta = p }, pal.acento)
        Separador()
        FilaBoton("Quitar instrumento", { Vista.aQuitar = p }, pal.rojo)
    }
}

@Composable
private fun AjusteInst(p: Pista, pat: Patron, param: Int, etiqueta: String, texto: (Float) -> String) {
    val info = Mesa.infoAjustesInst
    if (info.size < 4 * (param + 1) || pat.ajustes.size <= param) return
    val min = info[4 * param]
    val max = info[4 * param + 1]
    val inicial = info[4 * param + 2]
    val log = info[4 * param + 3] != 0f
    val valor = pat.ajustes[param]
    fun pos(v: Float) = if (log) ln(v / min) / ln(max / min) else (v - min) / (max - min)
    fun desde(x: Float) = if (log) min * exp(x * ln(max / min)) else min + x * (max - min)
    // Cada cambio vuelve a preparar el instrumento: mientras el dedo está encima se ve el valor y
    // se aplica al soltar.
    var borrador by remember(p.id, param) { mutableStateOf<Float?>(null) }
    val mostrado = borrador ?: valor
    FilaAjuste(etiqueta, texto(mostrado)) {
        Deslizador(
            pos(mostrado), 0f, 1f, { borrador = desde(it) }, it,
            alReiniciar = { Mesa.ponerAjusteInst(p, param, inicial) }, descripcion = "$etiqueta de ${p.nombre}",
            desde = if (min < 0f) pos(0f) else 0f,
            alTocar = { abajo ->
                if (!abajo) {
                    borrador?.let { Mesa.ponerAjusteInst(p, param, it) }
                    borrador = null
                }
            },
        )
    }
}
