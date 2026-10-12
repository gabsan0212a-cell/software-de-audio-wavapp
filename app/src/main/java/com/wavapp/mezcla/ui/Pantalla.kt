package com.wavapp.mezcla.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.withInfiniteAnimationFrameMillis
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.Analisis
import com.wavapp.mezcla.Edicion
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Pista
import kotlinx.coroutines.delay
import java.util.Locale
import kotlin.math.abs
import kotlin.math.max

private fun tiempo(s: Double): String {
    val m = (s / 60).toInt()
    return String.format(Locale.US, "%02d:%06.3f", m, s - m * 60)
}

private fun textoDb(db: Float, apagado: Float): String =
    if (db <= apagado) "-∞" else (if (db > 0) "+" else "") + String.format(Locale.US, "%.1f", db)

private fun textoPan(pan: Int): String = if (pan == 0) "C" else (if (pan < 0) "L" else "R") + abs(pan)

@Composable
fun PantallaMesa() {
    val p = LocalPaleta.current
    val context = LocalContext.current
    val elegir = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        Mesa.agregarArchivos(context, uris)
    }
    // Posición y medidores, en cada cuadro de pantalla. (Como animación infinita: en las
    // pruebas automáticas se pausa sola.)
    LaunchedEffect(Unit) {
        while (true) {
            withInfiniteAnimationFrameMillis { Mesa.tick(it) }
            if (!Mesa.activa()) delay(250)  // quieta: 4 veces por segundo basta (ahorra batería)
        }
    }
    // Mientras suena, la pantalla no se apaga (al apagarse se detendría el audio).
    val vista = LocalView.current
    val sonando = Mesa.reproduciendo
    DisposableEffect(sonando) {
        vista.keepScreenOn = sonando
        onDispose { vista.keepScreenOn = false }
    }
    var aQuitar by remember { mutableStateOf<Pista?>(null) }
    var dialogoExportar by remember { mutableStateOf(false) }
    val guardarComo = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("audio/wav")) { uri ->
        if (uri != null) Mesa.exportar(context, uri)
    }

    Box(Modifier.fillMaxSize()) {
    Column(Modifier.fillMaxSize().background(p.bg).safeDrawingPadding()) {
        Encabezado(
            alAgregar = { elegir.launch(arrayOf("audio/*")) },
            alExportar = { dialogoExportar = true },
        )
        Transporte()
        LazyColumn(
            Modifier.weight(1f).fillMaxWidth(),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            item { Estado() }
            items(Mesa.pistas, key = { it.id }) { pista -> TarjetaPista(pista, alQuitar = { aQuitar = pista }) }
            item {
                Text(
                    "M silencia, S deja sonar solo esa, Ø invierte la polaridad. Doble toque en un " +
                        "control lo regresa a 0. Toca la onda para saltar a ese momento.",
                    color = p.tenue, fontSize = 12.sp, modifier = Modifier.padding(vertical = 4.dp),
                )
            }
            item { PanelMaster() }
        }
    }
    // El EQ de una pista y el análisis se abren encima de todo, a pantalla completa.
    if (Analisis.abierto) PanelAnalisis(alCerrar = { Analisis.abierto = false })
    if (Edicion.abierta) PanelEdicion(alCerrar = { Edicion.abierta = false })
    Mesa.eqAbierta?.let { pista -> PanelEq(pista, alCerrar = { Mesa.eqAbierta = null }) }
    }

    if (dialogoExportar) {
        DialogoExportar(
            alCerrar = { dialogoExportar = false },
            alExportar = {
                dialogoExportar = false
                guardarComo.launch(Mesa.nombreExportacion())
            },
        )
    }

    aQuitar?.let { pista ->
        AlertDialog(
            onDismissRequest = { aQuitar = null },
            title = { Text("¿Quitar la pista ${pista.id} · ${pista.nombre}?") },
            text = { Text("Se borra de la mezcla. El archivo original en tu celular no se toca.") },
            confirmButton = {
                TextButton(onClick = { Mesa.quitar(pista); aQuitar = null }) { Text("Quitar", color = p.rojo) }
            },
            dismissButton = { TextButton(onClick = { aQuitar = null }) { Text("Cancelar") } },
            containerColor = p.panel,
        )
    }
}

@Composable
private fun Encabezado(alAgregar: () -> Unit, alExportar: () -> Unit) {
    val p = LocalPaleta.current
    Column(Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = 12.dp)) {
        Text("Mesa de Audio", color = p.fg, fontSize = 19.sp, fontWeight = FontWeight.SemiBold)
        Text(
            "Multipista · EQ por pista · análisis · edición · master con limitador · exportar WAV",
            color = p.tenue, fontSize = 12.sp,
        )
        Row(Modifier.padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Boton("+ Agregar pistas", alTocar = alAgregar, habilitado = !Mesa.ocupada)
            Boton("Pistas de prueba", alTocar = { Mesa.cargarPrueba() }, habilitado = !Mesa.ocupada)
        }
        Row(Modifier.padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Boton("Exportar", alTocar = alExportar, habilitado = !Mesa.ocupada && Mesa.pistas.isNotEmpty())
            Boton("Análisis", alTocar = { Analisis.abierto = true }, habilitado = Mesa.pistas.isNotEmpty())
            Boton("Editar", alTocar = { Edicion.abierta = true }, habilitado = Mesa.pistas.isNotEmpty())
        }
    }
}

@Composable
private fun DialogoExportar(alCerrar: () -> Unit, alExportar: () -> Unit) {
    val p = LocalPaleta.current
    AlertDialog(
        onDismissRequest = alCerrar,
        title = { Text("Exportar la mezcla") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text("Formato WAV", color = p.tenue, fontSize = 12.sp)
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    for (b in listOf(16, 24, 32)) {
                        Boton(if (b == 32) "32 bits float" else "$b bits", { Mesa.ponerExportacion(b, Mesa.exportRate) },
                            presionado = Mesa.exportBits == b, tamano = 12.sp, relleno = 9.dp)
                    }
                }
                Text("Frecuencia", color = p.tenue, fontSize = 12.sp)
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    for (r in listOf(48000, 44100)) {
                        Boton("$r Hz", { Mesa.ponerExportacion(Mesa.exportBits, r) },
                            presionado = Mesa.exportRate == r, tamano = 12.sp, relleno = 9.dp)
                    }
                }
                val mb = Mesa.tamanoExportacion() / 1048576.0
                Text(
                    "Se exporta la canción completa (${tiempo(Mesa.duracion)}) tal como suena: volumen, paneo, " +
                        "mute/solo, EQ, notch y master con limitador. Tamaño ≈ " +
                        String.format(Locale.US, "%.1f", mb) + " MB.",
                    color = p.fg, fontSize = 13.sp,
                )
            }
        },
        confirmButton = { TextButton(onClick = alExportar) { Text("Elegir dónde guardar…") } },
        dismissButton = { TextButton(onClick = alCerrar) { Text("Cancelar") } },
        containerColor = p.panel,
    )
}

/** Botones a la izquierda y tiempo a la derecha; si no cabe, el tiempo baja completo al renglón siguiente. */
@Composable
internal fun Transporte(horizontal: Dp = 16.dp) {
    Layout(
        content = {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Boton(
                    if (Mesa.reproduciendo) "❚❚ Pausar" else "▶ Reproducir",
                    alTocar = { Mesa.reproducirPausar() }, estilo = Estilo.Reproducir,
                    reservar = "▶ Reproducir",
                )
                Boton("■ Detener", alTocar = { Mesa.detener() })
            }
            TextoTiempo()
        },
        modifier = Modifier.fillMaxWidth().padding(horizontal = horizontal, vertical = 10.dp),
    ) { medibles, restricciones ->
        val libre = restricciones.copy(minWidth = 0, minHeight = 0)
        val botones = medibles[0].measure(libre)
        val tiempo = medibles[1].measure(libre)
        val ancho = restricciones.maxWidth
        val sep = 8.dp.roundToPx()
        val unaLinea = botones.width + sep + tiempo.width <= ancho
        val alto = if (unaLinea) max(botones.height, tiempo.height) else botones.height + sep / 2 + tiempo.height
        layout(ancho, alto) {
            if (unaLinea) {
                botones.placeRelative(0, (alto - botones.height) / 2)
                tiempo.placeRelative(ancho - tiempo.width, (alto - tiempo.height) / 2)
            } else {
                botones.placeRelative(0, 0)
                tiempo.placeRelative(ancho - tiempo.width, botones.height + sep / 2)
            }
        }
    }
}

@Composable
private fun TextoTiempo() {
    Text(
        "${tiempo(Mesa.posicion)} / ${tiempo(Mesa.duracion)}",
        color = LocalPaleta.current.fg, fontFamily = Mono, fontSize = 13.sp, maxLines = 1, softWrap = false,
    )
}

@Composable
private fun Estado() {
    val p = LocalPaleta.current
    Column(Modifier.fillMaxWidth().padding(bottom = 2.dp)) {
        Text(if (Mesa.ocupada) Mesa.ocupadaTexto else Mesa.estado, color = p.tenue, fontSize = 12.sp)
        if (Mesa.ocupada && Mesa.progreso >= 0f) {
            Box(Modifier.padding(top = 4.dp).fillMaxWidth().height(4.dp).background(p.linea)) {
                Box(Modifier.fillMaxWidth(Mesa.progreso.coerceIn(0f, 1f)).fillMaxHeight().background(p.acento))
            }
        }
        if (Mesa.exportando) {
            Boton("Cancelar exportación", { Mesa.cancelarExportacion() }, modifier = Modifier.padding(top = 6.dp),
                tamano = 12.sp, relleno = 10.dp)
        }
    }
}

@Composable
private fun TarjetaPista(pista: Pista, alQuitar: () -> Unit) {
    val p = LocalPaleta.current
    val color = ColoresPista[pista.color]
    val forma = RoundedCornerShape(5.dp)
    Column(
        Modifier
            .fillMaxWidth()
            .clip(forma)
            .background(p.panel)
            .border(1.dp, p.linea, forma)
            .drawBehind { drawRect(color, size = Size(5.dp.toPx(), size.height)) }
            .padding(start = 11.dp, end = 6.dp, top = 6.dp, bottom = 6.dp),
        verticalArrangement = Arrangement.spacedBy(5.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(
                    "${pista.id} · ${pista.nombre}", color = p.fg, fontWeight = FontWeight.SemiBold,
                    fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                )
                Text(pista.info, color = p.tenue, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            Boton("×", alTocar = alQuitar, relleno = 10.dp, descripcion = "Quitar pista ${pista.id}")
        }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            Boton("M", { Mesa.alternarMute(pista) }, presionado = pista.mute, estilo = Estilo.Mute,
                tamano = 12.sp, relleno = 9.dp, descripcion = "Mute pista ${pista.id}")
            Boton("S", { Mesa.alternarSolo(pista) }, presionado = pista.solo, estilo = Estilo.Solo,
                tamano = 12.sp, relleno = 9.dp, descripcion = "Solo pista ${pista.id}")
            Boton("Ø", { Mesa.alternarFase(pista) }, presionado = pista.fase,
                tamano = 12.sp, relleno = 9.dp, descripcion = "Invertir polaridad pista ${pista.id}")
            Boton("EQ", { Mesa.eqAbierta = pista }, presionado = pista.eqActivo,
                tamano = 12.sp, relleno = 8.dp, descripcion = "EQ pista ${pista.id}")
            MedidorPista(pista.medidor, Modifier.weight(1f).height(16.dp))
        }
        FilaControl(
            "Vol", textoDb(pista.volDb, Mesa.VOL_MIN + 0.1f),
            alReiniciar = { Mesa.ponerVolumen(pista, 0f) },
        ) { mod ->
            Deslizador(
                pista.volDb, Mesa.VOL_MIN, Mesa.VOL_MAX, { Mesa.ponerVolumen(pista, it) },
                { Mesa.ponerVolumen(pista, 0f) }, mod, descripcion = "Volumen pista ${pista.id}",
            )
        }
        FilaControl("Pan", textoPan(pista.pan), alReiniciar = { Mesa.ponerPan(pista, 0f) }) { mod ->
            Deslizador(
                pista.pan.toFloat(), -100f, 100f, { Mesa.ponerPan(pista, it) }, { Mesa.ponerPan(pista, 0f) },
                mod, desde = 0f, descripcion = "Paneo pista ${pista.id}",
            )
        }
        Onda(
            pista, Mesa.duracion, posicion = { Mesa.posicion }, alSaltar = { Mesa.saltar(it) },
            modifier = Modifier.fillMaxWidth().height(86.dp).clip(RoundedCornerShape(4.dp))
                .alpha(if (Mesa.suena(pista)) 1f else 0.3f),
        )
    }
}

@Composable
private fun FilaControl(
    etiqueta: String,
    valor: String,
    alReiniciar: () -> Unit,
    control: @Composable (Modifier) -> Unit,
) {
    val p = LocalPaleta.current
    val reiniciar by rememberUpdatedState(alReiniciar)
    val dobleToque = Modifier.pointerInput(Unit) { detectTapGestures(onDoubleTap = { reiniciar() }) }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            etiqueta, color = p.tenue, fontFamily = Mono, fontSize = 12.sp,
            modifier = Modifier.width(32.dp).then(dobleToque),
        )
        control(Modifier.weight(1f))
        Text(
            valor, color = p.fg, fontFamily = Mono, fontSize = 12.sp, textAlign = TextAlign.End,
            modifier = Modifier.width(48.dp).then(dobleToque),
        )
    }
}

@Composable
private fun PanelMaster() {
    val p = LocalPaleta.current
    val forma = RoundedCornerShape(6.dp)
    val m = Mesa.master
    Column(
        Modifier
            .padding(top = 8.dp)
            .fillMaxWidth()
            .clip(forma)
            .background(p.panel)
            .border(1.dp, p.linea, forma)
            .padding(12.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text("MASTER", color = p.tenue, fontSize = 12.sp, letterSpacing = 1.sp, fontWeight = FontWeight.Medium)
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            LuzClip("L", m.clipL)
            LuzClip("R", m.clipR)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FaderMaster(Mesa.masterDb, { Mesa.ponerMaster(it) })
            VuMaster(m, Modifier.size(44.dp, 200.dp))
        }
        Text(
            textoDb(Mesa.masterDb, Mesa.MASTER_MIN + 0.5f) + " dB",
            color = p.fg, fontFamily = Mono, fontSize = 15.sp, fontWeight = FontWeight.Medium,
        )
        TextoPicos()
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Boton("Mute", { Mesa.alternarMasterMute() }, presionado = Mesa.masterMute)
            Boton("Limitador", { Mesa.alternarLimitador() }, presionado = Mesa.limitador)
        }
        TextoReduccion()
        TextoSalida()
    }
}

@Composable
private fun TextoSalida() {
    Text(
        Mesa.salida, color = LocalPaleta.current.tenue, fontFamily = Mono, fontSize = 10.sp,
        textAlign = TextAlign.Center,
    )
}

@Composable
private fun TextoPicos() {
    val m = Mesa.master
    val f = { x: Float -> if (x > -100f) String.format(Locale.US, "%.1f", x) else "—" }
    Text(
        "pico máx (dB)\nL ${f(m.picoL)} · R ${f(m.picoR)}",
        color = LocalPaleta.current.tenue, fontFamily = Mono, fontSize = 11.sp, textAlign = TextAlign.Center,
    )
}

@Composable
private fun TextoReduccion() {
    Text(
        "reducción " + String.format(Locale.US, "%.1f", Mesa.master.reduccion) + " dB",
        color = LocalPaleta.current.tenue, fontFamily = Mono, fontSize = 11.sp,
    )
}

@Composable
private fun LuzClip(canal: String, encendida: Boolean) {
    val p = LocalPaleta.current
    val forma = RoundedCornerShape(2.dp)
    Box(
        Modifier
            .size(34.dp, 18.dp)
            .clip(forma)
            .background(if (encendida) p.rojo else p.panel)
            .border(1.dp, if (encendida) p.rojo else p.linea, forma)
            .clickable { Mesa.borrarClip() },
        contentAlignment = Alignment.Center,
    ) {
        Text(
            canal, fontFamily = Mono, fontSize = 10.sp, fontWeight = FontWeight.Medium,
            color = if (encendida) Color.White else p.tenue,
        )
    }
}
