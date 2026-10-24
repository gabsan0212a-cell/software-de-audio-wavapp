package com.wavapp.mezcla.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
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
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import com.wavapp.mezcla.Mesa
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** Reproductor a pantalla completa, como "Reproduciendo" de Apple Music. */
@Composable
fun Reproductor(visible: Boolean, alCerrar: () -> Unit, alExportar: () -> Unit) {
    AlRegresar(visible) { alCerrar() }
    BarraDeEstadoClara(visible, LocalPaleta.current.oscuro)
    AnimatedVisibility(
        visible,
        enter = slideInVertically(tween(380, easing = FastOutSlowInEasing)) { it },
        exit = slideOutVertically(tween(300)) { it },
    ) {
        ContenidoReproductor(alCerrar, alExportar)
    }
}

private val Blanco2 = Color.White.copy(alpha = 0.62f)
private val Blanco3 = Color.White.copy(alpha = 0.45f)

@Composable
private fun ContenidoReproductor(alCerrar: () -> Unit, alExportar: () -> Unit) {
    val pistas = Mesa.pistas
    // Los colores de la portada (gris oscuro, como su fondo), como hace Apple Music.
    val fondo = Brush.verticalGradient(listOf(Color(0xFF2C2C30), Color(0xFF161618), Color(0xFF0B0B0D)))
    val cerrar by rememberUpdatedState(alCerrar)
    var arrastre by remember { mutableFloatStateOf(0f) }
    Column(
        Modifier
            .fillMaxSize()
            .offset { IntOffset(0, arrastre.roundToInt()) }
            .background(fondo)
            .testTag("reproductor")
            .pointerInput(Unit) { detectTapGestures { } }
            .pointerInput(Unit) {
                // Bajarlo con el dedo lo cierra.
                detectVerticalDragGestures(
                    onDragEnd = {
                        if (arrastre > 140.dp.toPx()) cerrar()
                        arrastre = 0f
                    },
                    onDragCancel = { arrastre = 0f },
                ) { cambio, dy ->
                    cambio.consume()
                    arrastre = max(0f, arrastre + dy)
                }
            }
            .windowInsetsPadding(WindowInsets.systemBars)
            .padding(horizontal = 28.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .height(40.dp)
                .semantics { contentDescription = "Cerrar reproductor" }
                .pulsable(rol = Role.Button) { cerrar() },
            contentAlignment = Alignment.Center,
        ) {
            Box(Modifier.size(38.dp, 5.dp).clip(CircleShape).background(Blanco3))
        }
        Spacer(Modifier.weight(0.5f))
        // Portada: se encoge al pausar, como en Apple Music.
        val escala by animateFloatAsState(
            if (Mesa.reproduciendo) 1f else 0.84f, spring(dampingRatio = 0.72f, stiffness = 260f), label = "portada",
        )
        BoxWithConstraints(Modifier.fillMaxWidth().weight(4f, fill = false), contentAlignment = Alignment.Center) {
            val lado = min(min(maxWidth.value, maxHeight.value), 380f).dp
            PortadaApp(
                lado,
                Modifier.graphicsLayer {
                    scaleX = escala
                    scaleY = escala
                    shadowElevation = 28.dp.toPx()
                    shape = RoundedCornerShape(12.dp)
                    clip = true
                },
                radio = 12.dp,
            )
        }
        Spacer(Modifier.weight(0.5f))
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Texto("Mezcla", Tipo.titulo2, Color.White, lineas = 1)
                val n = pistas.size
                Texto("$n ${if (n == 1) "pista" else "pistas"} · 48 kHz", Tipo.cuerpo, Blanco2, lineas = 1)
            }
            BotonIcono(
                Iconos.compartir, "Exportar", alExportar, color = Color.White, tamano = 19.dp,
                circulo = Color.White.copy(alpha = 0.14f), diametro = 34.dp, habilitado = !Mesa.ocupada && pistas.isNotEmpty(),
            )
        }
        Spacer(Modifier.height(18.dp))
        BarraTiempo()
        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly, verticalAlignment = Alignment.CenterVertically) {
            BotonIcono(Iconos.alInicio, "Al inicio", { Mesa.saltar(0.0) }, color = Color.White, tamano = 36.dp, lado = 64.dp)
            BotonReproducir(Color.White, 52.dp, 80.dp)
            BotonIcono(Iconos.detener, "Detener", { Mesa.detener() }, color = Color.White, tamano = 36.dp, lado = 64.dp)
        }
        Spacer(Modifier.height(10.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icono(Iconos.bocina, Blanco2, tamano = 15.dp)
            DeslizadorFino(
                { posicionMaster(Mesa.masterDb) }, { Mesa.ponerMaster(dbMaster(it)) },
                Modifier.weight(1f).padding(horizontal = 10.dp), descripcion = "Volumen maestro",
            )
            Icono(Iconos.bocinaFuerte, Blanco2, tamano = 20.dp)
        }
        MedidoresFinos(Modifier.fillMaxWidth().padding(horizontal = 25.dp).height(9.dp))
        Spacer(Modifier.weight(0.4f))
        Row(Modifier.fillMaxWidth().padding(bottom = 6.dp), horizontalArrangement = Arrangement.SpaceEvenly) {
            BotonIcono(Iconos.mezclador, "Ir a Mezcla", { Vista.pestana = Vista.MEZCLA; cerrar() }, color = Blanco2)
            BotonIcono(Iconos.tijeras, "Ir a Editar", { Vista.pestana = Vista.EDITAR; cerrar() }, color = Blanco2)
            BotonIcono(Iconos.onda, "Ir a Análisis", { Vista.pestana = Vista.ANALISIS; cerrar() }, color = Blanco2)
        }
    }
}

/** Barra de progreso de la canción con el tiempo que va y el que falta. Arrastrar salta. */
@Composable
private fun BarraTiempo() {
    var arrastrando by remember { mutableStateOf(false) }
    var mientras by remember { mutableFloatStateOf(0f) }  // posición mientras se arrastra
    Column(Modifier.fillMaxWidth()) {
        DeslizadorFino(
            valor = {
                val d = Mesa.duracion
                if (arrastrando) mientras else if (d > 0) (Mesa.posicion / d).toFloat() else 0f
            },
            alCambiar = {
                mientras = it
                if (!arrastrando) Mesa.saltar(it * Mesa.duracion)
            },
            modifier = Modifier.fillMaxWidth(),
            descripcion = "Posición de la canción",
            alTocarInicio = { arrastrando = true },
            alSoltar = {
                arrastrando = false
                Mesa.saltar(mientras * Mesa.duracion)
            },
        )
        TiemposBarra(arrastrando, mientras)
    }
}

@Composable
private fun TiemposBarra(arrastrando: Boolean, mientras: Float) {
    // Los textos cambian una vez por segundo (no en cada cuadro como la posición).
    val arrastre by rememberUpdatedState(arrastrando)
    val donde by rememberUpdatedState(mientras)
    val textos by remember {
        derivedStateOf {
            val pos = if (arrastre) donde * Mesa.duracion else Mesa.posicion
            tiempoCorto(pos) to "−" + tiempoCorto(max(0.0, Mesa.duracion - pos))
        }
    }
    Row(Modifier.fillMaxWidth()) {
        Texto(textos.first, Tipo.leyenda.numeros.seminegrita, Blanco3, Modifier.weight(1f), lineas = 1)
        Texto(textos.second, Tipo.leyenda.numeros.seminegrita, Blanco3, Modifier.weight(1f), lineas = 1, alineacion = TextAlign.End)
    }
}

/** Medidores L/R del master en dos líneas finas (se leen al dibujar). */
@Composable
private fun MedidoresFinos(modifier: Modifier) {
    val p = LocalPaleta.current
    val m = Mesa.master
    Spacer(
        modifier.drawWithCache {
            val gap = 3.dp.toPx()
            val h = (size.height - gap) / 2
            val brocha = Brush.horizontalGradient(
                0f to Color.White.copy(alpha = 0.55f), 0.72f to Color.White.copy(alpha = 0.75f), 0.86f to p.amarillo,
                0.95f to p.rojo, startX = 0f, endX = size.width,
            )
            val riel = Color.White.copy(alpha = 0.12f)
            val radio = CornerRadius(h / 2)
            onDrawBehind {
                for (c in 0..1) {
                    val y = c * (h + gap)
                    drawRoundRect(riel, Offset(0f, y), Size(size.width, h), radio)
                    val nivel = if (c == 0) m.l else m.r
                    val u = ((nivel + 48f) / 51f).coerceIn(0f, 1f)
                    if (u > 0f) drawRoundRect(brocha, Offset(0f, y), Size(max(u * size.width, h), h), radio)
                }
            }
        },
    )
}
