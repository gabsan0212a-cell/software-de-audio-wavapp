package com.wavapp.mezcla.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
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
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

private val Blanco2 = Color.White.copy(alpha = 0.62f)
private val Blanco3 = Color.White.copy(alpha = 0.42f)

/**
 * El escenario: para cantar en vivo sobre la canción preparada. Siempre oscuro y con todo grande:
 * cuándo entra la voz (cuenta regresiva), el afinador, el nivel del micrófono, los presets, la
 * canción y el botón para grabar la presentación (lo que sale hacia la consola).
 */
@Composable
fun Escenario(visible: Boolean, salida: Salida, permiso: PermisoMicrofono, alCerrar: () -> Unit, alGuardar: () -> Unit) {
    AlRegresar(visible) { alCerrar() }
    BarraDeEstadoClara(visible, LocalPaleta.current.oscuro)
    // Al entrar es para cantar ya: la voz se prende (y se escucha si hay consola o audífonos).
    LaunchedEffect(visible) {
        if (visible) Mesa.prenderVoz(true, salida != Salida.BOCINA)
    }
    AnimatedVisibility(
        visible,
        enter = slideInVertically(tween(380, easing = FastOutSlowInEasing)) { it },
        exit = slideOutVertically(tween(300)) { it },
    ) {
        CompositionLocalProvider(LocalPaleta provides PaletaOscura) {
            ContenidoEscenario(salida, permiso, alCerrar, alGuardar)
        }
    }
}

@Composable
private fun ContenidoEscenario(salida: Salida, permiso: PermisoMicrofono, alCerrar: () -> Unit, alGuardar: () -> Unit) {
    val p = LocalPaleta.current
    val cerrar by rememberUpdatedState(alCerrar)
    val fondo = Brush.verticalGradient(listOf(lerp(p.acento, Color.Black, 0.74f), Color(0xFF0B0B0D), Color.Black))
    BoxWithConstraints(
        Modifier
            .fillMaxSize()
            .background(fondo)
            .testTag("escenario")
            .pointerInput(Unit) { detectTapGestures { } }  // los toques no pasan a lo de atrás
            .windowInsetsPadding(WindowInsets.systemBars),
    ) {
        // En una pantalla muy baja todo se desliza y la cuenta tiene un alto fijo; si no, la cuenta
        // toma lo que sobra (lo de abajo no se mueve).
        val baja = maxHeight < 600.dp
        Column(
            Modifier
                .fillMaxSize()
                .then(if (baja) Modifier.verticalScroll(rememberScrollState()) else Modifier)
                .padding(horizontal = 20.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            BarraEscenario(cerrar, alGuardar)
            TituloCancion()
            CuentaGrande(if (baja) Modifier.fillMaxWidth().height(220.dp) else Modifier.fillMaxWidth().weight(1f))
            MapaVoz(Modifier.fillMaxWidth().height(44.dp))
            TiemposCancion()
            Spacer(Modifier.height(6.dp))
            if (permiso.concedido) VozEscenario(salida) else SinMicrofono(permiso)
            Spacer(Modifier.height(8.dp))
            PresetsVoz()
            Transporte()
            Controles(salida)
            Spacer(Modifier.height(8.dp))
        }
    }
}

// ---------------------------------------------------------------- arriba

@Composable
private fun BarraEscenario(alCerrar: () -> Unit, alGuardar: () -> Unit) {
    val pr = Mesa.presentacion
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.width(112.dp), contentAlignment = Alignment.CenterStart) {
            BotonIcono(Iconos.chevronAbajo, "Cerrar el escenario", alCerrar, color = Color.White, tamano = 26.dp)
        }
        Texto("En vivo", Tipo.encabezado, Color.White, Modifier.weight(1f), lineas = 1, alineacion = TextAlign.Center)
        Box(Modifier.width(112.dp), contentAlignment = Alignment.CenterEnd) {
            when {
                pr.grabando -> Grabando(pr.segundos)
                pr.lista -> GuardarPresentacion(alGuardar)
            }
        }
    }
}

/** "● 1:23" mientras se graba la presentación. */
@Composable
private fun Grabando(segundos: Int) {
    val p = LocalPaleta.current
    Row(
        Modifier
            .semantics(mergeDescendants = true) { contentDescription = "Grabando la presentación" }
            .clip(CircleShape)
            .background(p.rojo.copy(alpha = 0.22f))
            .padding(horizontal = 10.dp, vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(p.rojo))
        Spacer(Modifier.width(6.dp))
        Texto(tiempoCorto(segundos.toDouble()), Tipo.nota.numeros.seminegrita, p.rojo, lineas = 1)
    }
}

@Composable
private fun TituloCancion() {
    val c = Mesa.cancion
    val detalle = if (!c.lista) {
        "Sin preparar"
    } else {
        listOfNotNull(
            nombreTonalidad(c.tonica, c.menor).takeIf { c.tonica >= 0 },
            "${c.bpm.roundToInt()} BPM".takeIf { c.bpm > 0f },
        ).joinToString(" · ").ifEmpty { "Lista para cantar" }
    }
    Column(Modifier.fillMaxWidth().padding(top = 2.dp)) {
        Texto(Mesa.nombreCancion() ?: "Mezcla", Tipo.titulo2, Color.White, lineas = 1)
        Texto(detalle, Tipo.cuerpo, Blanco2, lineas = 1)
        val error = Mesa.presentacion.error
        if (error.isNotEmpty()) Texto(error, Tipo.nota, LocalPaleta.current.rojo, Modifier.padding(top = 2.dp), lineas = 2)
    }
}

// ---------------------------------------------------------------- la cuenta para entrar

/**
 * Lo más grande de la pantalla: cuánto falta para que entre la voz (los últimos segundos en rojo
 * y con un golpe), "¡Canta!" mientras dura la parte, y un aro con el avance.
 */
@Composable
private fun CuentaGrande(modifier: Modifier) {
    val c = Mesa.cuenta
    BoxWithConstraints(modifier.padding(vertical = 10.dp).testTag("cuenta"), contentAlignment = Alignment.Center) {
        if (c.estado == Nucleo.CUE_NADA) {
            SinCuenta(conIcono = maxHeight > 170.dp)
            return@BoxWithConstraints
        }
        val lado = min(min(maxWidth.value, maxHeight.value), 270f).dp
        Box(Modifier.size(lado), contentAlignment = Alignment.Center) {
            AroCuenta(Modifier.matchParentSize())
            TextoCuenta(lado)
        }
    }
}

@Composable
private fun AroCuenta(modifier: Modifier) {
    val p = LocalPaleta.current
    val c = Mesa.cuenta
    val acento = p.acento
    Canvas(modifier) {
        val grosor = 9.dp.toPx()
        val lado = size.minDimension - grosor
        val esquina = Offset((size.width - lado) / 2, (size.height - lado) / 2)
        drawArc(Color.White.copy(alpha = 0.12f), 0f, 360f, false, esquina, Size(lado, lado), style = Stroke(grosor))
        val color = when {
            c.estado == Nucleo.CUE_CANTANDO -> acento
            c.estado == Nucleo.CUE_ESPERANDO && c.segundos <= 3 -> acento
            c.estado == Nucleo.CUE_ESPERANDO -> Color.White.copy(alpha = 0.85f)
            else -> Color.White.copy(alpha = 0.3f)
        }
        val avance = if (c.estado == Nucleo.CUE_FIN) 1f else c.avance.coerceIn(0f, 1f)  // se lee al dibujar
        drawArc(color, -90f, 360f * avance, false, esquina, Size(lado, lado), style = Stroke(grosor, cap = StrokeCap.Round))
    }
}

@Composable
private fun TextoCuenta(lado: Dp) {
    val p = LocalPaleta.current
    val c = Mesa.cuenta
    val numero = with(LocalDensity.current) { (lado * 0.36f).toSp() }
    val grande = with(LocalDensity.current) { (lado * 0.17f).toSp() }
    val ultimos = c.estado == Nucleo.CUE_ESPERANDO && c.segundos <= 3
    // Un golpe en cada uno de los últimos segundos.
    val golpe = remember { Animatable(1f) }
    LaunchedEffect(c.segundos, ultimos) {
        if (ultimos) {
            golpe.snapTo(1.18f)
            golpe.animateTo(1f, tween(450, easing = FastOutSlowInEasing))
        }
    }
    val parte = if (c.partes > 0) "Parte ${min(c.parte + 1, c.partes)} de ${c.partes}" else ""
    Column(
        Modifier.semantics(mergeDescendants = true) {},
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        when (c.estado) {
            Nucleo.CUE_ESPERANDO -> {
                Texto("Entra en", Tipo.encabezado, Blanco2, lineas = 1)
                Texto(
                    if (c.segundos < 60) "${c.segundos}" else tiempoCorto(c.segundos.toDouble()),
                    Tipo.tituloGrande.copy(fontSize = if (c.segundos < 60) numero else grande, lineHeight = numero, fontWeight = FontWeight.Bold).numeros,
                    if (ultimos) p.acento else Color.White,
                    Modifier.graphicsLayer {
                        scaleX = golpe.value
                        scaleY = golpe.value
                    },
                    lineas = 1,
                )
                Texto(parte, Tipo.subtitulo, Blanco2, lineas = 1)
            }
            Nucleo.CUE_CANTANDO -> {
                Texto(parte, Tipo.subtitulo, Blanco2, lineas = 1)
                Texto("¡Canta!", Tipo.tituloGrande.copy(fontSize = grande, lineHeight = grande * 1.2f), p.acento, lineas = 1)
                Texto("queda ${tiempoCorto(c.segundos.toDouble())}", Tipo.encabezado.numeros, Blanco2, lineas = 1)
            }
            else -> {
                Texto("Fin", Tipo.tituloGrande.copy(fontSize = grande, lineHeight = grande * 1.2f), Color.White, lineas = 1)
                Texto("La voz ya no vuelve a entrar", Tipo.subtitulo, Blanco2, lineas = 1)
            }
        }
    }
}

/** Sin canción preparada (o sin voz original): se canta igual, sin cuenta. */
@Composable
private fun SinCuenta(conIcono: Boolean) {
    val lista = Mesa.cancion.lista
    Column(Modifier.padding(horizontal = 16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        if (conIcono) Icono(Iconos.microfono, Blanco3, Modifier.padding(bottom = 12.dp), tamano = 54.dp)
        Texto(
            if (lista) "No se encontró dónde canta la voz original" else "Canta sobre la mezcla",
            Tipo.titulo3, Color.White, alineacion = TextAlign.Center,
        )
        Texto(
            if (lista) "Igual puedes cantar: la pista y tu voz salen juntas por la consola."
            else "Prepara la canción (Mezcla → ⋯ → Preparar para cantar) y aquí verás cuándo entra la voz.",
            Tipo.subtitulo, Blanco2, Modifier.padding(top = 6.dp), alineacion = TextAlign.Center,
        )
    }
}

// ---------------------------------------------------------------- la canción

/**
 * La voz original a lo largo de la canción (dónde canta, resaltado) con la línea de dónde va. Lo
 * que ya pasó se ve más claro. Tocar salta ahí (menos mientras se graba la presentación).
 */
@Composable
private fun MapaVoz(modifier: Modifier) {
    val p = LocalPaleta.current
    val c = Mesa.cancion
    val columnas = 90
    val env = remember(c.lista, c.instrumental, c.partes, Mesa.duracion) {
        if (c.lista) Mesa.envolventeVoz(columnas) else FloatArray(0)
    }
    val partes = c.partes
    val acento = p.acento
    val puedeSaltar = !Mesa.presentacion.grabando && Mesa.pistas.isNotEmpty()
    Canvas(
        modifier
            .padding(top = 6.dp)
            .semantics { contentDescription = "Dónde canta la voz" }
            .pointerInput(puedeSaltar) {
                if (puedeSaltar) detectTapGestures { o -> Mesa.saltar((o.x / size.width).coerceIn(0f, 1f) * Mesa.duracion) }
            },
    ) {
        val total = Mesa.duracion.toFloat()
        val ahora = if (total > 0f) (Mesa.posicion / Mesa.duracion).toFloat().coerceIn(0f, 1f) else 0f
        if (env.isNotEmpty() && total > 0f) {
            for ((a, b) in partes) {
                drawRoundRect(
                    acento.copy(alpha = 0.14f), Offset(size.width * a / total, 0f), Size(size.width * (b - a) / total, size.height),
                    CornerRadius(4.dp.toPx()),
                )
            }
            val ancho = size.width / env.size
            for (k in env.indices) {
                val h = max(2.dp.toPx(), env[k] * size.height * 0.9f)
                val f = (k + 0.5f) / env.size
                val canta = partes.any { (a, b) -> f * total in a..b }
                val color = when {
                    canta && f <= ahora -> acento
                    canta -> acento.copy(alpha = 0.5f)
                    f <= ahora -> Color.White.copy(alpha = 0.5f)
                    else -> Color.White.copy(alpha = 0.2f)
                }
                drawRoundRect(color, Offset(k * ancho + ancho * 0.18f, (size.height - h) / 2), Size(ancho * 0.64f, h), CornerRadius(ancho * 0.32f))
            }
        } else {
            val g = 4.dp.toPx()
            val y = size.height / 2 - g / 2
            drawRoundRect(Color.White.copy(alpha = 0.2f), Offset(0f, y), Size(size.width, g), CornerRadius(g / 2))
            drawRoundRect(Color.White.copy(alpha = 0.7f), Offset(0f, y), Size(size.width * ahora, g), CornerRadius(g / 2))
        }
        val x = size.width * ahora
        drawLine(Color.White, Offset(x, 0f), Offset(x, size.height), 2.dp.toPx(), cap = StrokeCap.Round)
    }
}

@Composable
private fun TiemposCancion() {
    // Solo cambian una vez por segundo (no en cada cuadro).
    val va by remember { derivedStateOf { tiempoCorto(Mesa.posicion) } }
    val falta by remember { derivedStateOf { "−" + tiempoCorto(max(0.0, Mesa.duracion - Mesa.posicion)) } }
    Row(Modifier.fillMaxWidth().padding(top = 4.dp)) {
        Texto(va, Tipo.leyenda.numeros.seminegrita, Blanco3, Modifier.weight(1f), lineas = 1)
        Texto(falta, Tipo.leyenda.numeros.seminegrita, Blanco3, Modifier.weight(1f), lineas = 1, alineacion = TextAlign.End)
    }
}

// ---------------------------------------------------------------- la voz

@Composable
private fun VozEscenario(salida: Salida) {
    val p = LocalPaleta.current
    val v = Mesa.voz
    val e = Mesa.entrada
    if (v.encendida) {
        Afinador(Modifier.fillMaxWidth().height(78.dp), grande = true)
    } else {
        Row(Modifier.fillMaxWidth().height(78.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f).padding(end = 12.dp)) {
                Texto("Voz en vivo apagada", Tipo.encabezado, Color.White, lineas = 1)
                Texto("Prende la voz para el auto-tune y los efectos", Tipo.nota, Blanco2, lineas = 1)
            }
            Interruptor(false, { Mesa.prenderVoz(true, salida != Salida.BOCINA) }, descripcion = "Voz en vivo")
        }
    }
    MedidorEntrada(Modifier.fillMaxWidth().height(16.dp))
    when {
        !e.abierta -> Texto("Abriendo la entrada…", Tipo.nota, Blanco2, Modifier.fillMaxWidth().padding(top = 4.dp), lineas = 1)
        e.satura -> Texto(
            "Saturó: baja la ganancia del micrófono en Ajustes (toca el medidor para borrar el aviso).",
            Tipo.nota, p.rojo, Modifier.fillMaxWidth().padding(top = 4.dp),
        )
    }
}

@Composable
private fun SinMicrofono(permiso: PermisoMicrofono) {
    Column(Modifier.fillMaxWidth().padding(vertical = 8.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Texto(
            if (permiso.negado) "Android no dio permiso al micrófono. Actívalo en Ajustes → Apps → WavApp → Permisos."
            else "Para escucharte en vivo, la app necesita el micrófono o tu interfaz USB.",
            Tipo.subtitulo, Blanco2, alineacion = TextAlign.Center,
        )
        BotonRelleno("Permitir micrófono", permiso.pedir, Modifier.padding(top = 10.dp).fillMaxWidth(), icono = Iconos.microfono)
    }
}

// ---------------------------------------------------------------- transporte y controles

@Composable
private fun Transporte() {
    Row(
        Modifier.fillMaxWidth().padding(top = 4.dp),
        horizontalArrangement = Arrangement.SpaceEvenly,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        BotonIcono(
            Iconos.alInicio, "Al inicio", { Mesa.saltar(0.0) }, color = Color.White, tamano = 32.dp, lado = 64.dp,
            habilitado = Mesa.pistas.isNotEmpty() && !Mesa.presentacion.grabando,
        )
        BotonReproducir(Color.White, 52.dp, 76.dp)
        BotonPresentacion()
    }
}

/** Grabar la presentación: círculo rojo para empezar, cuadro redondeado para terminar. */
@Composable
private fun BotonPresentacion() {
    val p = LocalPaleta.current
    val grabando = Mesa.presentacion.grabando
    val t by animateFloatAsState(if (grabando) 1f else 0f, tween(220), label = "presentacion")
    val rojo = p.rojo
    Canvas(
        Modifier
            .size(64.dp)
            .semantics { contentDescription = if (grabando) "Detener presentación" else "Grabar presentación" }
            .pulsable(grabando || !Mesa.ocupada, rol = Role.Button, atenuado = 0.6f) {
                if (grabando) Mesa.pararPresentacion() else Mesa.grabarPresentacion()
            },
    ) {
        val r = size.minDimension / 2
        drawCircle(Color.White.copy(alpha = 0.55f), r - 2.dp.toPx(), center, style = Stroke(3.dp.toPx()))
        val lado = (2 * (r - 7.dp.toPx())) * (1f - 0.45f * t)
        val esquina = lado / 2 * (1f - 0.7f * t)
        drawRoundRect(rojo, Offset(center.x - lado / 2, center.y - lado / 2), Size(lado, lado), CornerRadius(esquina))
    }
}

@Composable
private fun Controles(salida: Salida) {
    val v = Mesa.voz
    val e = Mesa.entrada
    val guia = Mesa.pistaGuia()
    Row(Modifier.fillMaxWidth().padding(top = 10.dp), horizontalArrangement = Arrangement.SpaceEvenly) {
        Control(Iconos.microfono, "Mi voz", e.escucha, habilitado = e.escucha || salida != Salida.BOCINA) {
            Mesa.ponerEscucha(!e.escucha)
        }
        Control(Iconos.nota, "Auto-tune", v.encendida && v.on(Nucleo.VOZ_AUTOTUNE), habilitado = v.encendida) {
            Mesa.alternarVoz(Nucleo.VOZ_AUTOTUNE)
        }
        Control(Iconos.audifonos, "Guía", guia != null && !guia.mute, habilitado = guia != null) {
            guia?.let { Mesa.alternarMute(it) }
        }
        Control(Iconos.mezclador, "Ajustes", null) { Vista.voz = true }
    }
    if (salida == Salida.BOCINA && !e.escucha) {
        Texto(
            "Conecta la consola o audífonos para escucharte", Tipo.nota, Blanco2, Modifier.fillMaxWidth().padding(top = 6.dp),
            lineas = 1, alineacion = TextAlign.Center,
        )
    }
}

/** Botón redondo con su nombre abajo (como el Centro de control). [activo] null = no se prende. */
@Composable
private fun Control(icono: ImageVector, texto: String, activo: Boolean?, habilitado: Boolean = true, alTocar: () -> Unit) {
    val prendido = activo == true
    Column(
        Modifier
            .width(76.dp)
            .semantics(mergeDescendants = true) {
                contentDescription = texto
                if (activo != null) selected = prendido
                role = if (activo != null) Role.Switch else Role.Button
            }
            .pulsable(habilitado, rol = null, atenuado = 0.6f, alTocar = alTocar),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier.size(50.dp).clip(CircleShape).background(if (prendido) Color.White else Color.White.copy(alpha = 0.14f)),
            contentAlignment = Alignment.Center,
        ) { Icono(icono, if (prendido) Color.Black else Color.White, tamano = 24.dp) }
        Texto(texto, Tipo.leyenda.seminegrita, if (prendido) Color.White else Blanco2, Modifier.padding(top = 5.dp), lineas = 1)
    }
}

// ---------------------------------------------------------------- la presentación grabada

/**
 * Arriba a la derecha, cuando ya hay una presentación grabada: guardarla en el teléfono (con el
 * formato de exportar). Mientras se guarda dice cuánto va; al terminar, si salió bien.
 */
@Composable
private fun GuardarPresentacion(alGuardar: () -> Unit) {
    val p = LocalPaleta.current
    val pr = Mesa.presentacion
    val guardando = Mesa.exportando
    val texto = when {
        guardando -> "${(Mesa.progreso.coerceIn(0f, 1f) * 100).roundToInt()} %"
        pr.guardada -> "Guardada"
        else -> "Guardar"
    }
    val fondo = if (pr.guardada && !guardando) p.verde.copy(alpha = 0.22f) else p.acento
    val color = if (pr.guardada && !guardando) p.verde else Color.White
    Row(
        Modifier
            .semantics(mergeDescendants = true) {
                contentDescription = "Guardar la presentación (${tiempoCorto(pr.duracion.toDouble())}) en el teléfono"
            }
            .pulsable(!Mesa.ocupada, atenuado = 0.6f, alTocar = alGuardar)
            .clip(CircleShape)
            .background(fondo)
            .padding(start = 10.dp, end = 12.dp, top = 6.dp, bottom = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icono(Iconos.compartir, color, tamano = 15.dp)
        Spacer(Modifier.width(5.dp))
        Texto(texto, Tipo.nota.seminegrita.numeros, color, lineas = 1)
    }
}

/** Para las tarjetas que abren el escenario: "La menor · 96 BPM", o qué falta. */
internal fun detalleCancion(): String {
    val c = Mesa.cancion
    if (!c.lista) return "Prepara una canción para ver cuándo entrar"
    return listOfNotNull(
        Mesa.nombreCancion(),
        nombreTonalidad(c.tonica, c.menor).takeIf { c.tonica >= 0 },
        "${c.bpm.roundToInt()} BPM".takeIf { c.bpm > 0f },
    ).joinToString(" · ")
}
