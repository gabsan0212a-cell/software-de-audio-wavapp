package com.wavapp.mezcla.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.windowInsetsTopHeight
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import kotlin.math.max
import kotlin.math.roundToInt

/** Velo oscuro detrás de una hoja o alerta. Tocarlo cierra (si se da [alTocar]). */
@Composable
private fun BoxScope.Velo(visible: Boolean, alTocar: (() -> Unit)?) {
    val p = LocalPaleta.current
    val tocar by rememberUpdatedState(alTocar)
    AnimatedVisibility(visible, Modifier.matchParentSize(), enter = fadeIn(tween(250)), exit = fadeOut(tween(220))) {
        Box(Modifier.fillMaxSize().background(p.velo).pointerInput(Unit) { detectTapGestures { tocar?.invoke() } })
    }
}

/** Absorbe los toques para que no pasen a lo que está detrás. */
private fun Modifier.absorberToques(): Modifier = pointerInput(Unit) { detectTapGestures { } }

/**
 * Hoja que sube desde abajo, como las de iOS: agarradera, título al centro y botones a los lados.
 * Se cierra con "atrás", tocando fuera o bajándola con el dedo.
 */
@Composable
fun Hoja(
    visible: Boolean,
    alCerrar: () -> Unit,
    titulo: String,
    alta: Boolean = false,
    izquierda: (@Composable () -> Unit)? = null,
    derecha: (@Composable () -> Unit)? = null,
    contenido: @Composable ColumnScope.() -> Unit,
) {
    AlRegresar(visible) { alCerrar() }
    val cerrar by rememberUpdatedState(alCerrar)
    Box(Modifier.fillMaxSize()) {
        Velo(visible, alCerrar)
        AnimatedVisibility(
            visible,
            Modifier.align(Alignment.BottomCenter),
            enter = slideInVertically(tween(340, easing = FastOutSlowInEasing)) { it },
            exit = slideOutVertically(tween(260)) { it },
        ) {
            CompositionLocalProvider(LocalPaleta provides LocalPaleta.current.elevada()) {
                val p = LocalPaleta.current
                var arrastre by remember { mutableFloatStateOf(0f) }
                Column(
                    Modifier
                        .windowInsetsPadding(WindowInsets.statusBars)
                        .padding(top = 10.dp)
                        .fillMaxWidth()
                        .then(if (alta) Modifier.fillMaxHeight() else Modifier)
                        .offset { IntOffset(0, arrastre.roundToInt()) }
                        .clip(RoundedCornerShape(topStart = 14.dp, topEnd = 14.dp))
                        .background(p.fondoAgrupado)
                        .absorberToques(),
                ) {
                    Box(
                        Modifier
                            .fillMaxWidth()
                            .height(56.dp)
                            .pointerInput(Unit) {
                                detectVerticalDragGestures(
                                    onDragEnd = {
                                        if (arrastre > 110.dp.toPx()) cerrar()
                                        arrastre = 0f
                                    },
                                    onDragCancel = { arrastre = 0f },
                                ) { cambio, dy ->
                                    cambio.consume()
                                    arrastre = max(0f, arrastre + dy)
                                }
                            },
                    ) {
                        Box(
                            Modifier.align(Alignment.TopCenter).padding(top = 6.dp).size(36.dp, 5.dp)
                                .clip(CircleShape).background(p.texto3),
                        )
                        Row(
                            Modifier.fillMaxSize().padding(start = 8.dp, end = 8.dp, top = 8.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Box(Modifier.weight(1f), contentAlignment = Alignment.CenterStart) { izquierda?.invoke() }
                            Texto(titulo, Tipo.encabezado, p.texto, Modifier.widthIn(max = 230.dp), lineas = 1)
                            Box(Modifier.weight(1f), contentAlignment = Alignment.CenterEnd) { derecha?.invoke() }
                        }
                    }
                    Column(
                        Modifier
                            .weight(1f, fill = false)
                            .verticalScroll(rememberScrollState())
                            .windowInsetsPadding(WindowInsets.navigationBars)
                            .padding(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 24.dp),
                        content = contenido,
                    )
                }
            }
        }
    }
}

/** Una opción de un menú de acciones. */
class Accion(val texto: String, val destructiva: Boolean = false, val alTocar: () -> Unit)

/** Menú de acciones de iOS: opciones agrupadas abajo y "Cancelar" aparte. */
@Composable
fun HojaAcciones(visible: Boolean, alCerrar: () -> Unit, acciones: List<Accion>, titulo: String? = null) {
    AlRegresar(visible) { alCerrar() }
    val p = LocalPaleta.current
    val fondoCancelar = if (p.oscuro) p.elevado else Color.White
    Box(Modifier.fillMaxSize()) {
        Velo(visible, alCerrar)
        AnimatedVisibility(
            visible,
            Modifier.align(Alignment.BottomCenter),
            enter = slideInVertically(tween(300, easing = FastOutSlowInEasing)) { it },
            exit = slideOutVertically(tween(240)) { it },
        ) {
            Column(
                Modifier
                    .fillMaxWidth()
                    .absorberToques()
                    .windowInsetsPadding(WindowInsets.navigationBars)
                    .padding(8.dp),
            ) {
                Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(p.elevado)) {
                    if (titulo != null) {
                        Texto(
                            titulo, Tipo.nota.seminegrita, p.texto2, Modifier.fillMaxWidth().padding(14.dp), lineas = 2,
                            alineacion = TextAlign.Center,
                        )
                    }
                    acciones.forEachIndexed { i, a ->
                        if (i > 0 || titulo != null) Separador()
                        Box(
                            Modifier.fillMaxWidth().height(57.dp).pulsable {
                                alCerrar()
                                a.alTocar()
                            },
                            contentAlignment = Alignment.Center,
                        ) {
                            Texto(
                                a.texto, Tipo.titulo3.copy(fontWeight = FontWeight.Normal),
                                if (a.destructiva) p.rojo else p.acento, lineas = 1,
                            )
                        }
                    }
                }
                Spacer(Modifier.height(8.dp))
                Box(
                    Modifier.fillMaxWidth().pulsable(alTocar = alCerrar).height(57.dp)
                        .clip(RoundedCornerShape(14.dp)).background(fondoCancelar),
                    contentAlignment = Alignment.Center,
                ) { Texto("Cancelar", Tipo.titulo3, p.acento, lineas = 1) }
            }
        }
    }
}

/** Alerta de iOS: título, mensaje y dos botones (el de confirmar puede ir en rojo). */
@Composable
fun Alerta(
    visible: Boolean,
    titulo: String,
    mensaje: String,
    confirmar: String,
    alConfirmar: () -> Unit,
    alCerrar: () -> Unit,
    destructiva: Boolean = false,
) {
    AlRegresar(visible) { alCerrar() }
    val p = LocalPaleta.current
    val fondo = if (p.oscuro) Color(0xFF2C2C2E) else Color(0xFFF2F2F2)
    val linea = if (p.oscuro) Color(0xFF48484A) else Color(0xFFC8C8CC)
    Box(Modifier.fillMaxSize()) {
        Velo(visible, null)
        AnimatedVisibility(
            visible,
            Modifier.align(Alignment.Center),
            enter = fadeIn(tween(200)) + scaleIn(tween(250), initialScale = 1.12f),
            exit = fadeOut(tween(180)),
        ) {
            Column(
                Modifier.width(280.dp).clip(RoundedCornerShape(14.dp)).background(fondo).absorberToques(),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Column(
                    Modifier.padding(start = 16.dp, end = 16.dp, top = 20.dp, bottom = 18.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    Texto(titulo, Tipo.encabezado, p.texto, Modifier.fillMaxWidth(), alineacion = TextAlign.Center)
                    Texto(mensaje, Tipo.nota, p.texto, Modifier.fillMaxWidth(), alineacion = TextAlign.Center)
                }
                Separador(color = linea)
                Row(Modifier.fillMaxWidth().height(46.dp)) {
                    Box(Modifier.weight(1f).fillMaxHeight().pulsable(alTocar = alCerrar), contentAlignment = Alignment.Center) {
                        Texto("Cancelar", Tipo.cuerpo, p.acento, lineas = 1)
                    }
                    val px = with(LocalDensity.current) { 1f.toDp() }
                    Box(Modifier.width(px).fillMaxHeight().background(linea))
                    Box(
                        Modifier.weight(1f).fillMaxHeight().pulsable {
                            alCerrar()
                            alConfirmar()
                        },
                        contentAlignment = Alignment.Center,
                    ) {
                        Texto(confirmar, Tipo.encabezado, if (destructiva) p.rojo else p.acento, lineas = 1)
                    }
                }
            }
        }
    }
}

/**
 * Barra de arriba de iOS sobre una lista con título grande: las acciones se quedan fijas a la
 * derecha y, al subir la lista, aparecen el fondo translúcido y el título chico al centro.
 */
@Composable
fun EncabezadoFlotante(lista: LazyListState, titulo: String, acciones: @Composable RowScope.() -> Unit = {}) {
    val p = LocalPaleta.current
    val umbral = with(LocalDensity.current) { 36.dp.toPx() }
    val desplazada by remember { derivedStateOf { lista.firstVisibleItemIndex > 0 || lista.firstVisibleItemScrollOffset > 0 } }
    val tituloFuera by remember {
        derivedStateOf { lista.firstVisibleItemIndex > 1 || lista.firstVisibleItemScrollOffset > umbral }
    }
    val fondo by animateFloatAsState(if (desplazada || tituloFuera) 1f else 0f, tween(140), label = "fondoBarra")
    val barra = p.barra
    Column(Modifier.fillMaxWidth()) {
        Spacer(
            Modifier.fillMaxWidth().windowInsetsTopHeight(WindowInsets.statusBars)
                .drawBehind { drawRect(barra, alpha = fondo) },
        )
        Box(Modifier.fillMaxWidth().height(44.dp)) {
            TituloChico(tituloFuera, titulo, Modifier.matchParentSize())
            Row(
                Modifier.align(Alignment.CenterEnd).padding(end = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
                content = acciones,
            )
        }
    }
}

@Composable
private fun TituloChico(visible: Boolean, titulo: String, modifier: Modifier) {
    val p = LocalPaleta.current
    AnimatedVisibility(visible, modifier, enter = fadeIn(tween(160)), exit = fadeOut(tween(160))) {
        Box(Modifier.fillMaxSize().background(p.barra)) {
            Texto(titulo, Tipo.encabezado, p.texto, Modifier.align(Alignment.Center), lineas = 1)
            Separador(Modifier.align(Alignment.BottomCenter))
        }
    }
}

/** Para listas con [EncabezadoFlotante]: deja libre la barra de estado y la barra de arriba. */
@Composable
fun EspacioEncabezado() {
    Column {
        Spacer(Modifier.windowInsetsTopHeight(WindowInsets.statusBars))
        Spacer(Modifier.height(44.dp))
    }
}
