package com.wavapp.mezcla.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.paint
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.rememberVectorPainter
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import java.util.Locale
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

// ---------------------------------------------------------------- textos y formatos

@Composable
fun Texto(
    texto: String,
    estilo: TextStyle,
    color: Color,
    modifier: Modifier = Modifier,
    lineas: Int = Int.MAX_VALUE,
    alineacion: TextAlign? = null,
) {
    BasicText(
        texto, modifier, if (alineacion == null) estilo.copy(color = color) else estilo.copy(color = color, textAlign = alineacion),
        overflow = TextOverflow.Ellipsis, maxLines = lineas,
    )
}

/** 1:05 (minutos:segundos). */
fun tiempoCorto(s: Double): String {
    val t = max(0.0, s).toInt()
    return "${t / 60}:" + String.format(Locale.US, "%02d", t % 60)
}

/** 1:05.250 o 5.250 s. */
fun tiempoFino(s: Double): String {
    val m = (max(0.0, s) / 60).toInt()
    return if (m > 0) String.format(Locale.US, "%d:%06.3f", m, s - m * 60) else String.format(Locale.US, "%.3f s", max(0.0, s))
}

fun textoDb(db: Float, apagado: Float): String =
    if (db <= apagado) "−∞ dB" else (if (db > 0.05f) "+" else "") + String.format(Locale.US, "%.1f dB", db).replace('-', '−')

fun textoPan(pan: Int): String = if (pan == 0) "Centro" else (if (pan < 0) "L " else "R ") + abs(pan)

// ---------------------------------------------------------------- toques

/** Tocable al estilo de iOS: sin onda; se aclara mientras el dedo está encima. */
fun Modifier.pulsable(
    habilitado: Boolean = true,
    rol: Role? = Role.Button,
    atenuado: Float = 0.4f,
    alTocar: () -> Unit,
): Modifier = composed {
    val fuente = remember { MutableInteractionSource() }
    val presionado by fuente.collectIsPressedAsState()
    val alfa by animateFloatAsState(
        when {
            !habilitado -> 0.35f
            presionado -> atenuado
            else -> 1f
        },
        tween(if (presionado) 0 else 220), label = "pulsar",
    )
    graphicsLayer { alpha = alfa }
        .clickable(interactionSource = fuente, indication = null, enabled = habilitado, role = rol, onClick = alTocar)
}

/** Doble toque: regresa un control a su valor de inicio. */
fun Modifier.dobleToque(accion: () -> Unit): Modifier = composed {
    val a by rememberUpdatedState(accion)
    pointerInput(Unit) { detectTapGestures(onDoubleTap = { a() }) }
}

// ---------------------------------------------------------------- íconos y botones

@Composable
fun Icono(icono: ImageVector, color: Color, modifier: Modifier = Modifier, tamano: Dp = 24.dp) {
    val pintor = rememberVectorPainter(icono)
    Box(modifier.size(tamano).paint(pintor, contentScale = ContentScale.Fit, colorFilter = ColorFilter.tint(color)))
}

/** Ícono tocable con área de 44 dp (la mínima de Apple). Opcional: un círculo de fondo. */
@Composable
fun BotonIcono(
    icono: ImageVector,
    descripcion: String,
    alTocar: () -> Unit,
    modifier: Modifier = Modifier,
    color: Color = LocalPaleta.current.acento,
    tamano: Dp = 24.dp,
    habilitado: Boolean = true,
    circulo: Color = Color.Transparent,
    diametro: Dp = 0.dp,
    lado: Dp = 44.dp,
) {
    Box(
        modifier
            .size(lado)
            .semantics { contentDescription = descripcion }
            .pulsable(habilitado, alTocar = alTocar)
            .drawBehind { if (diametro > 0.dp) drawCircle(circulo, diametro.toPx() / 2) },
        contentAlignment = Alignment.Center,
    ) { Icono(icono, color, tamano = tamano) }
}

@Composable
fun BotonTexto(
    texto: String,
    alTocar: () -> Unit,
    modifier: Modifier = Modifier,
    color: Color = LocalPaleta.current.acento,
    estilo: TextStyle = Tipo.cuerpo,
    habilitado: Boolean = true,
) {
    Box(
        modifier.heightIn(min = 44.dp).pulsable(habilitado, alTocar = alTocar).padding(horizontal = 8.dp),
        contentAlignment = Alignment.Center,
    ) { Texto(texto, estilo, color, lineas = 1) }
}

/** Botón grande redondeado: relleno de acento (principal) o gris con letra de acento. */
@Composable
fun BotonRelleno(
    texto: String,
    alTocar: () -> Unit,
    modifier: Modifier = Modifier,
    icono: ImageVector? = null,
    principal: Boolean = true,
    habilitado: Boolean = true,
    alto: Dp = 50.dp,
) {
    val p = LocalPaleta.current
    val color = if (principal) Color.White else p.acento
    Row(
        modifier
            .pulsable(habilitado, alTocar = alTocar)
            .height(alto)
            .clip(RoundedCornerShape(12.dp))
            .background(if (principal) p.acento else p.relleno)
            .padding(horizontal = 16.dp),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (icono != null) {
            Icono(icono, color, tamano = 20.dp)
            Spacer(Modifier.width(8.dp))
        }
        Texto(texto, Tipo.encabezado, color, lineas = 1)
    }
}

/** Píldora que se prende y se apaga (M, S, Ø, EQ, fuentes del análisis...). */
@Composable
fun Chip(
    texto: String,
    activo: Boolean,
    alTocar: () -> Unit,
    modifier: Modifier = Modifier,
    colorActivo: Color = LocalPaleta.current.acento,
    textoActivo: Color = Color.White,
    colorInactivo: Color = LocalPaleta.current.texto,
    descripcion: String? = null,
    icono: ImageVector? = null,
    habilitado: Boolean = true,
) {
    val p = LocalPaleta.current
    val color = if (activo) textoActivo else colorInactivo
    Row(
        modifier
            .semantics {
                if (descripcion != null) contentDescription = descripcion
                selected = activo
            }
            .pulsable(habilitado, alTocar = alTocar)
            .height(32.dp)
            .widthIn(min = 42.dp)
            .clip(CircleShape)
            .background(if (activo) colorActivo else p.relleno)
            .padding(horizontal = 12.dp),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (icono != null) {
            Icono(icono, color, tamano = 15.dp)
            Spacer(Modifier.width(5.dp))
        }
        Texto(texto, Tipo.subtitulo.seminegrita, color, lineas = 1)
    }
}

// ---------------------------------------------------------------- controles de iOS

/** Interruptor de iOS (verde al prenderse). */
@Composable
fun Interruptor(encendido: Boolean, alCambiar: (Boolean) -> Unit, modifier: Modifier = Modifier, descripcion: String? = null) {
    val p = LocalPaleta.current
    val t by animateFloatAsState(if (encendido) 1f else 0f, tween(220), label = "interruptor")
    val apagado = if (p.oscuro) Color(0xFF39393D) else Color(0xFFE9E9EA)
    val verde = p.verde
    Canvas(
        modifier
            .size(51.dp, 31.dp)
            .semantics { if (descripcion != null) contentDescription = descripcion }
            .toggleable(
                value = encendido, interactionSource = remember { MutableInteractionSource() }, indication = null,
                role = Role.Switch, onValueChange = alCambiar,
            ),
    ) {
        val r = size.height / 2
        drawRoundRect(lerp(apagado, verde, t), cornerRadius = CornerRadius(r))
        val k = r - 2.dp.toPx()
        val cx = r + (size.width - 2 * r) * t
        drawCircle(Color.Black.copy(alpha = 0.12f), k + 0.5.dp.toPx(), Offset(cx, r + 1.5.dp.toPx()))
        drawCircle(Color.White, k, Offset(cx, r))
    }
}

/** Control segmentado de iOS. */
@Composable
fun Segmentado(opciones: List<String>, elegido: Int, alElegir: (Int) -> Unit, modifier: Modifier = Modifier) {
    val p = LocalPaleta.current
    val fondo = if (p.oscuro) Color(0xFF2E2E32) else Color(0xFFEEEEEF)
    val marca = if (p.oscuro) Color(0xFF5B5B5F) else Color.White
    val linea = if (p.oscuro) Color(0xFF48484C) else Color(0xFFD3D3D6)
    val forma = RoundedCornerShape(7.dp)
    Row(
        modifier
            .height(32.dp)
            .clip(RoundedCornerShape(9.dp))
            .background(fondo)
            .drawBehind {
                // Rayitas entre opciones que no están elegidas, como en iOS.
                val n = opciones.size
                val w = (size.width - 4.dp.toPx()) / n
                for (i in 1 until n) {
                    if (i == elegido || i - 1 == elegido) continue
                    val x = 2.dp.toPx() + i * w
                    drawLine(linea, Offset(x, size.height * 0.3f), Offset(x, size.height * 0.7f), 1.dp.toPx())
                }
            }
            .padding(2.dp),
    ) {
        opciones.forEachIndexed { i, texto ->
            val activo = i == elegido
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .then(if (activo) Modifier.shadow(1.5.dp, forma).background(marca, forma) else Modifier)
                    .semantics { selected = activo }
                    .pulsable(rol = Role.Tab, atenuado = 0.6f) { alElegir(i) },
                contentAlignment = Alignment.Center,
            ) {
                Texto(
                    texto, Tipo.nota.copy(fontWeight = if (activo) FontWeight.SemiBold else FontWeight.Medium), p.texto,
                    Modifier.padding(horizontal = 4.dp), lineas = 1,
                )
            }
        }
    }
}

/**
 * Deslizador de iOS: riel delgado y perilla blanca. Tocar o arrastrar pone el valor; doble toque
 * lo regresa a su inicio. [desde]: de dónde sale el relleno (p. ej. el centro en el paneo).
 */
@Composable
fun Deslizador(
    valor: Float,
    minimo: Float,
    maximo: Float,
    alCambiar: (Float) -> Unit,
    modifier: Modifier = Modifier,
    alReiniciar: (() -> Unit)? = null,
    desde: Float = minimo,
    color: Color = LocalPaleta.current.acento,
    riel: Color = LocalPaleta.current.relleno,
    descripcion: String = "",
    habilitado: Boolean = true,
) {
    val cambiar by rememberUpdatedState(alCambiar)
    val reiniciar by rememberUpdatedState(alReiniciar)
    val sombra = Color.Black.copy(alpha = if (LocalPaleta.current.oscuro) 0.35f else 0.16f)
    val gestos = if (!habilitado) Modifier else Modifier
        .pointerInput(minimo, maximo, alReiniciar != null) {
            val r = 13.dp.toPx()
            fun valorEn(x: Float) = minimo + ((x - r) / max(1f, size.width - 2 * r)).coerceIn(0f, 1f) * (maximo - minimo)
            if (alReiniciar != null) {
                detectTapGestures(onDoubleTap = { reiniciar?.invoke() }, onTap = { cambiar(valorEn(it.x)) })
            } else {
                detectTapGestures(onTap = { cambiar(valorEn(it.x)) })
            }
        }
        .pointerInput(minimo, maximo) {
            val r = 13.dp.toPx()
            detectHorizontalDragGestures { cambio, _ ->
                cambio.consume()
                cambiar(minimo + ((cambio.position.x - r) / max(1f, size.width - 2 * r)).coerceIn(0f, 1f) * (maximo - minimo))
            }
        }
    Canvas(
        modifier
            .height(36.dp)
            .semantics {
                contentDescription = descripcion
                progressBarRangeInfo = ProgressBarRangeInfo(valor.coerceIn(minimo, maximo), minimo..maximo)
                if (habilitado) setProgress { v -> cambiar(v.coerceIn(minimo, maximo)); true }
            }
            .then(gestos)
            .graphicsLayer { alpha = if (habilitado) 1f else 0.4f },
    ) {
        val r = 13.dp.toPx()
        val y = size.height / 2
        val g = 4.dp.toPx()
        fun x(v: Float) = r + ((v - minimo) / (maximo - minimo)).coerceIn(0f, 1f) * (size.width - 2 * r)
        drawRoundRect(riel, Offset(0f, y - g / 2), Size(size.width, g), CornerRadius(g / 2))
        val b = x(valor)
        val a = if (desde <= minimo) 0f else x(desde)
        drawRoundRect(color, Offset(min(a, b), y - g / 2), Size(abs(b - a), g), CornerRadius(g / 2))
        if (habilitado) {
            drawCircle(sombra, r + 0.5.dp.toPx(), Offset(b, y + 1.5.dp.toPx()))
            drawCircle(Color.White, r, Offset(b, y))
        }
    }
}

/**
 * Barra delgada sin perilla, como el progreso y el volumen de Apple Music: se engruesa mientras
 * el dedo está encima. [valor] (0..1) se lee al dibujar, así no recompone en cada cuadro.
 */
@Composable
fun DeslizadorFino(
    valor: () -> Float,
    alCambiar: (Float) -> Unit,
    modifier: Modifier = Modifier,
    color: Color = Color.White,
    riel: Color = Color.White.copy(alpha = 0.22f),
    descripcion: String = "",
    alTocarInicio: (() -> Unit)? = null,
    alSoltar: (() -> Unit)? = null,
) {
    val cambiar by rememberUpdatedState(alCambiar)
    val inicio by rememberUpdatedState(alTocarInicio)
    val soltar by rememberUpdatedState(alSoltar)
    var tocando by remember { mutableStateOf(false) }
    val grosor by animateFloatAsState(if (tocando) 12f else 6f, tween(160), label = "grosor")
    Canvas(
        modifier
            .height(32.dp)
            .semantics {
                contentDescription = descripcion
                progressBarRangeInfo = ProgressBarRangeInfo(valor().coerceIn(0f, 1f), 0f..1f)
                setProgress { v -> cambiar(v.coerceIn(0f, 1f)); true }
            }
            .pointerInput(Unit) {
                detectTapGestures(
                    onPress = { tocando = true; tryAwaitRelease(); tocando = false },
                    onTap = { inicio?.invoke(); cambiar((it.x / size.width).coerceIn(0f, 1f)); soltar?.invoke() },
                )
            }
            .pointerInput(Unit) {
                detectHorizontalDragGestures(
                    onDragStart = { tocando = true; inicio?.invoke() },
                    onDragEnd = { tocando = false; soltar?.invoke() },
                    onDragCancel = { tocando = false; soltar?.invoke() },
                ) { cambio, _ ->
                    cambio.consume()
                    cambiar((cambio.position.x / size.width).coerceIn(0f, 1f))
                }
            },
    ) {
        val g = grosor.dp.toPx()
        val y = size.height / 2 - g / 2
        drawRoundRect(riel, Offset(0f, y), Size(size.width, g), CornerRadius(g / 2))
        val w = size.width * valor().coerceIn(0f, 1f)
        if (w > 0f) drawRoundRect(color, Offset(0f, y), Size(max(w, g), g), CornerRadius(g / 2))
    }
}

@Composable
fun BarraProgreso(progreso: Float, modifier: Modifier = Modifier) {
    val p = LocalPaleta.current
    Canvas(modifier.fillMaxWidth().height(4.dp)) {
        val r = CornerRadius(size.height / 2)
        drawRoundRect(p.relleno, cornerRadius = r)
        drawRoundRect(p.acento, size = Size(size.width * progreso.coerceIn(0f, 1f), size.height), cornerRadius = r)
    }
}

// ---------------------------------------------------------------- contenedores

/** Tarjeta redondeada (como las listas agrupadas de iOS). */
@Composable
fun Tarjeta(
    modifier: Modifier = Modifier,
    relleno: PaddingValues = PaddingValues(horizontal = 16.dp, vertical = 12.dp),
    contenido: @Composable ColumnScope.() -> Unit,
) {
    Column(
        modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(LocalPaleta.current.tarjeta).padding(relleno),
        content = contenido,
    )
}

/** Línea de un píxel. */
@Composable
fun Separador(modifier: Modifier = Modifier, inicio: Dp = 0.dp, color: Color = LocalPaleta.current.separador) {
    val px = with(LocalDensity.current) { 1f.toDp() }
    Box(modifier.fillMaxWidth().padding(start = inicio).height(px).background(color))
}

/** Título grande de iOS (34 pt). Las acciones van en la barra de arriba ([EncabezadoFlotante]). */
@Composable
fun TituloGrande(texto: String, modifier: Modifier = Modifier) {
    Texto(
        texto, Tipo.tituloGrande, LocalPaleta.current.texto,
        modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = 2.dp, bottom = 4.dp), lineas = 1,
    )
}

/** Título de sección como en Apple Music (22 pt, negritas) con un detalle gris a la derecha. */
@Composable
fun EncabezadoSeccion(texto: String, modifier: Modifier = Modifier, detalle: String? = null) {
    val p = LocalPaleta.current
    Row(
        modifier.fillMaxWidth().padding(start = 20.dp, end = 20.dp, top = 22.dp, bottom = 8.dp),
        verticalAlignment = Alignment.Bottom,
    ) {
        Texto(texto, Tipo.titulo2, p.texto, Modifier.weight(1f), lineas = 1)
        if (detalle != null) Texto(detalle, Tipo.subtitulo, p.texto2, Modifier.padding(start = 8.dp, bottom = 2.dp), lineas = 1)
    }
}

/** Encabezado de un grupo de ajustes (MAYÚSCULAS grises, como en Configuración). */
@Composable
fun EncabezadoGrupo(texto: String, modifier: Modifier = Modifier) {
    Texto(
        texto.uppercase(), Tipo.nota, LocalPaleta.current.texto2,
        modifier.padding(start = 16.dp, end = 16.dp, top = 20.dp, bottom = 7.dp), lineas = 1,
    )
}

/** Nota gris debajo de un grupo. */
@Composable
fun PieGrupo(texto: String, modifier: Modifier = Modifier) {
    Texto(texto, Tipo.nota, LocalPaleta.current.texto2, modifier.padding(start = 16.dp, end = 16.dp, top = 7.dp))
}

/** Renglón con texto (y subtítulo opcional) y un interruptor a la derecha. */
@Composable
fun FilaInterruptor(
    titulo: String,
    encendido: Boolean,
    alCambiar: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    subtitulo: (@Composable () -> Unit)? = null,
) {
    Row(modifier.fillMaxWidth().heightIn(min = 50.dp).padding(vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f).padding(end = 12.dp)) {
            Texto(titulo, Tipo.cuerpo, LocalPaleta.current.texto, lineas = 1)
            subtitulo?.invoke()
        }
        Interruptor(encendido, alCambiar, descripcion = titulo)
    }
}

/** Renglón que actúa como botón (p. ej. "Quitar banda" en rojo). */
@Composable
fun FilaBoton(texto: String, alTocar: () -> Unit, color: Color, modifier: Modifier = Modifier, habilitado: Boolean = true) {
    Box(
        modifier.fillMaxWidth().heightIn(min = 48.dp).pulsable(habilitado, alTocar = alTocar),
        contentAlignment = Alignment.CenterStart,
    ) { Texto(texto, Tipo.cuerpo, color, lineas = 1) }
}

/** Etiqueta, control y valor en un renglón. Doble toque en la etiqueta o el valor reinicia. */
@Composable
fun FilaDeslizador(
    etiqueta: String,
    valor: String,
    alReiniciar: () -> Unit,
    modifier: Modifier = Modifier,
    anchoEtiqueta: Dp = 38.dp,
    anchoValor: Dp = 70.dp,
    control: @Composable RowScope.(Modifier) -> Unit,
) {
    val p = LocalPaleta.current
    Row(modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Texto(etiqueta, Tipo.nota, p.texto2, Modifier.width(anchoEtiqueta).dobleToque(alReiniciar), lineas = 1)
        control(Modifier.weight(1f))
        Texto(
            valor, Tipo.nota.numeros, p.texto, Modifier.width(anchoValor).dobleToque(alReiniciar), lineas = 1,
            alineacion = TextAlign.End,
        )
    }
}

/** Etiqueta arriba a la izquierda, valor arriba a la derecha y el control abajo (ajustes del EQ). */
@Composable
fun FilaAjuste(etiqueta: String, valor: String, modifier: Modifier = Modifier, control: @Composable (Modifier) -> Unit) {
    val p = LocalPaleta.current
    Column(modifier.fillMaxWidth().padding(top = 8.dp)) {
        Row {
            Texto(etiqueta, Tipo.subtitulo, p.texto, Modifier.weight(1f), lineas = 1)
            Texto(valor, Tipo.subtitulo.numeros, p.texto2, lineas = 1)
        }
        control(Modifier.fillMaxWidth())
    }
}
