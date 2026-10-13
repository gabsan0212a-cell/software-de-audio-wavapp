package com.wavapp.mezcla.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.Pista
import java.util.Locale

/** Pestaña "Mezcla": las pistas como tarjetas (estilo Apple Music) y el master. */
@Composable
fun PestanaMezcla(abajo: Dp, abrirArchivos: () -> Unit) {
    val lista = rememberLazyListState()
    Box(Modifier.fillMaxSize()) {
        LazyColumn(
            Modifier.fillMaxSize().testTag("listaMezcla"),
            state = lista,
            contentPadding = PaddingValues(bottom = abajo + 20.dp),
        ) {
            item(key = "espacio") { EspacioEncabezado() }
            item(key = "titulo") { TituloGrande("Mezcla") }
            item(key = "resumen") { Resumen() }
            if (Mesa.pistas.isEmpty()) item(key = "vacio") { SinPistas(abrirArchivos) }
            items(Mesa.pistas, key = { it.id }) { pista ->
                TarjetaPista(pista, Modifier.padding(horizontal = 16.dp, vertical = 6.dp))
            }
            if (Mesa.pistas.isNotEmpty()) {
                item(key = "masterTitulo") { EncabezadoSeccion("Master") }
                item(key = "master") { TarjetaMaster() }
                item(key = "salida") { TextoSalida() }
            }
        }
        EncabezadoFlotante(lista, "Mezcla") {
            BotonIcono(Iconos.mas, "Agregar pistas", { Vista.agregar = true }, habilitado = !Mesa.ocupada, tamano = 24.dp)
            BotonIcono(
                Iconos.compartir, "Exportar", { Vista.exportar = true },
                habilitado = !Mesa.ocupada && Mesa.pistas.isNotEmpty(), tamano = 24.dp,
            )
        }
    }
}

/** "4 pistas · 0:12", y lo que esté haciendo el núcleo (con su barra de progreso). */
@Composable
private fun Resumen() {
    val p = LocalPaleta.current
    Column(Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, bottom = 10.dp)) {
        val n = Mesa.pistas.size
        if (n > 0) {
            Texto(
                "$n ${if (n == 1) "pista" else "pistas"} · ${tiempoCorto(Mesa.duracion)} · 48 kHz", Tipo.subtitulo,
                p.texto2, lineas = 1,
            )
        }
        val texto = if (Mesa.ocupada) Mesa.ocupadaTexto else if (n > 0) Mesa.estado else ""
        if (texto.isNotEmpty()) {
            Texto(texto, Tipo.nota, if (Mesa.ocupada) p.texto else p.texto2, Modifier.padding(top = 4.dp))
        }
        if (Mesa.ocupada && Mesa.progreso >= 0f) BarraProgreso(Mesa.progreso, Modifier.padding(top = 8.dp))
        if (Mesa.exportando) {
            BotonTexto(
                "Cancelar exportación", { Mesa.cancelarExportacion() }, Modifier.padding(top = 2.dp), color = p.rojo,
                estilo = Tipo.subtitulo,
            )
        }
    }
}

@Composable
private fun SinPistas(abrirArchivos: () -> Unit) {
    val p = LocalPaleta.current
    Column(
        Modifier.fillMaxWidth().padding(horizontal = 32.dp, vertical = 40.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Icono(Iconos.nota, p.texto3, tamano = 64.dp)
        Texto("Sin pistas", Tipo.titulo2, p.texto, Modifier.padding(top = 16.dp), lineas = 1)
        Texto(
            "Agrega audio de tu celular (WAV, MP3, FLAC, M4A…) para empezar a mezclar.", Tipo.subtitulo, p.texto2,
            Modifier.padding(top = 6.dp), alineacion = TextAlign.Center,
        )
        BotonRelleno(
            "Agregar archivos", abrirArchivos, Modifier.padding(top = 22.dp).fillMaxWidth(), icono = Iconos.mas,
            habilitado = !Mesa.ocupada,
        )
        BotonRelleno(
            "Pistas de prueba", { Mesa.cargarPrueba() }, Modifier.padding(top = 10.dp).fillMaxWidth(), principal = false,
            habilitado = !Mesa.ocupada,
        )
    }
}

// ---------------------------------------------------------------- pista

@Composable
private fun TarjetaPista(pista: Pista, modifier: Modifier) {
    val p = LocalPaleta.current
    val color = p.pistas[pista.color]
    Column(modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(p.tarjeta)) {
        Row(Modifier.padding(start = 12.dp, top = 12.dp, end = 2.dp), verticalAlignment = Alignment.CenterVertically) {
            Portada(pista.id, color, 48.dp)
            Column(Modifier.weight(1f).padding(start = 12.dp)) {
                Texto(pista.nombre, Tipo.encabezado, p.texto, lineas = 1)
                Texto(pista.info, Tipo.nota, p.texto2, lineas = 1)
                MedidorPista(pista.medidor, Modifier.padding(top = 6.dp).fillMaxWidth().height(8.dp))
            }
            BotonIcono(Iconos.masOpciones, "Opciones pista ${pista.id}", { Vista.opciones = pista }, tamano = 22.dp)
        }
        Row(
            Modifier.padding(top = 12.dp).fillMaxWidth().horizontalScroll(rememberScrollState())
                .padding(start = 12.dp, end = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Chip(
                "M", pista.mute, { Mesa.alternarMute(pista) }, colorActivo = p.azul, colorInactivo = p.texto2,
                descripcion = "Mute pista ${pista.id}",
            )
            Spacer(Modifier.width(6.dp))
            Chip(
                "S", pista.solo, { Mesa.alternarSolo(pista) }, colorActivo = p.amarillo, textoActivo = Color.Black,
                colorInactivo = p.texto2, descripcion = "Solo pista ${pista.id}",
            )
            Spacer(Modifier.width(6.dp))
            Chip(
                "Ø", pista.fase, { Mesa.alternarFase(pista) }, colorActivo = p.naranja, colorInactivo = p.texto2,
                descripcion = "Invertir polaridad pista ${pista.id}",
            )
            Spacer(Modifier.width(6.dp))
            Chip(
                "EQ", pista.eqActivo, { Mesa.eqAbierta = pista }, colorActivo = p.acento, colorInactivo = p.texto2,
                descripcion = "EQ pista ${pista.id}",
            )
            Spacer(Modifier.width(6.dp))
            Chip(
                "FX", pista.fxActivo, { Vista.efectos = pista }, colorActivo = p.acento, colorInactivo = p.texto2,
                descripcion = "Efectos pista ${pista.id}",
            )
            Spacer(Modifier.width(6.dp))
            ChipAuto(pista)
        }
        Column(Modifier.padding(start = 12.dp, end = 14.dp, top = 8.dp)) {
            FilaVolumen(pista, color)
            FilaPaneo(pista, color)
        }
        OndaPista(pista, color, Modifier.padding(top = 10.dp).fillMaxWidth().height(64.dp))
    }
}

/**
 * "A": automatización. Roja si mover los deslizadores graba; verde si los deslizadores siguen
 * puntos ya puestos.
 */
@Composable
private fun ChipAuto(pista: Pista) {
    val p = LocalPaleta.current
    val graba = pista.autoModo == Nucleo.AUTO_GRABAR
    val activa = graba || (pista.autoModo == Nucleo.AUTO_LEER && pista.tieneAuto)
    Chip(
        "A", activa, { Vista.automatizacion = pista }, colorActivo = if (graba) p.rojo else p.verde,
        colorInactivo = p.texto2, descripcion = "Automatización pista ${pista.id}",
    )
}

/** Color del valor de un deslizador: rojo mientras se graba, verde si sigue la automatización. */
@Composable
private fun colorValor(pista: Pista, param: Int): Color {
    val p = LocalPaleta.current
    return when {
        pista.grabando(param) -> p.rojo
        pista.sigue(param) -> p.verde
        else -> p.texto
    }
}

// Lo que marcan cambia en cada cuadro si hay automatización: cada fila se recompone sola.
@Composable
private fun FilaVolumen(pista: Pista, color: Color) {
    val v = pista.volMostrado
    FilaDeslizador(
        "Vol", textoDb(v, Mesa.VOL_MIN + 0.1f), { Mesa.ponerVolumen(pista, 0f) },
        colorValor = colorValor(pista, Nucleo.AUTO_VOLUMEN),
    ) { mod ->
        Deslizador(
            v, Mesa.VOL_MIN, Mesa.VOL_MAX, { Mesa.ponerVolumen(pista, it) }, mod,
            alReiniciar = { Mesa.ponerVolumen(pista, 0f) }, color = color, descripcion = "Volumen pista ${pista.id}",
            alTocar = { Mesa.tocarDeslizador(pista, Nucleo.AUTO_VOLUMEN, it) },
        )
    }
}

@Composable
private fun FilaPaneo(pista: Pista, color: Color) {
    val v = pista.panMostrado
    FilaDeslizador(
        "Pan", textoPan(v), { Mesa.ponerPan(pista, 0f) }, colorValor = colorValor(pista, Nucleo.AUTO_PANEO),
    ) { mod ->
        Deslizador(
            v.toFloat(), -100f, 100f, { Mesa.ponerPan(pista, it) }, mod,
            alReiniciar = { Mesa.ponerPan(pista, 0f) }, desde = 0f, color = color,
            descripcion = "Paneo pista ${pista.id}",
            alTocar = { Mesa.tocarDeslizador(pista, Nucleo.AUTO_PANEO, it) },
        )
    }
}

/**
 * La onda de la pista sobre la línea de tiempo de la canción, en barras como Notas de Voz: lo
 * que ya sonó va en su color y lo que falta, más claro. Tocar salta a ese momento.
 */
@Composable
private fun OndaPista(pista: Pista, color: Color, modifier: Modifier) {
    val p = LocalPaleta.current
    Spacer(
        modifier
            .semantics { contentDescription = "Forma de onda de ${pista.nombre}. Toca para mover el cursor." }
            .pointerInput(Unit) {
                detectTapGestures { o ->
                    val d = Mesa.duracion
                    if (d > 0) Mesa.saltar(o.x / size.width * d)
                }
            }
            .drawWithCache {
                // Se rehace solo si cambian los pedazos, la duración, el tamaño o el mute/solo.
                val dur = Mesa.duracion
                val pedazos = pista.pedazos
                val suena = Mesa.suena(pista)
                val editada = pista.editada
                val ruta = Path()
                val zonas = ArrayList<Rect>()
                if (dur > 0) {
                    pedazos.forEachIndexed { i, pz ->
                        val x0 = (pz.inicio / Mesa.RATE / dur * size.width).toFloat()
                        val x1 = (pz.fin / Mesa.RATE / dur * size.width).toFloat()
                        zonas += Rect(x0, 0f, x1, size.height)
                        barrasPedazo(ruta, pista, i, pz, x0 + 2.dp.toPx(), x1 - 2.dp.toPx(), 8.dp.toPx(), size.height - 16.dp.toPx(), 3.dp.toPx(), 2.dp.toPx())
                    }
                }
                val fondo = color.copy(alpha = if (p.oscuro) 0.12f else 0.08f)
                val zona = color.copy(alpha = if (p.oscuro) 0.14f else 0.10f)
                val alfa = if (suena) 1f else 0.3f
                val pendiente = color.copy(alpha = 0.38f * alfa)
                val sonado = color.copy(alpha = alfa)
                onDrawBehind {
                    drawRect(fondo)
                    if (editada) zonas.forEach { r -> drawRoundRect(zona, r.topLeft, r.size, CornerRadius(4.dp.toPx())) }
                    val px = if (dur > 0) (Mesa.posicion / dur * size.width).toFloat() else 0f
                    clipRect(right = px) { drawPath(ruta, sonado) }
                    clipRect(left = px) { drawPath(ruta, pendiente) }
                    drawRect(p.acento, Offset(px - 1.dp.toPx(), 0f), Size(2.dp.toPx(), size.height))
                }
            },
    )
}

// ---------------------------------------------------------------- master

@Composable
private fun TarjetaMaster() {
    val p = LocalPaleta.current
    Tarjeta(Modifier.padding(horizontal = 16.dp), PaddingValues(start = 16.dp, end = 16.dp, top = 14.dp, bottom = 4.dp)) {
        MedidoresMaster(Modifier.fillMaxWidth())
        Spacer(Modifier.height(10.dp))
        FilaDeslizador("Vol", textoDb(Mesa.masterDb, Mesa.MASTER_MIN + 0.5f), { Mesa.ponerMaster(0f) }) { mod ->
            Deslizador(
                posicionMaster(Mesa.masterDb), 0f, 1f, { Mesa.ponerMaster(dbMaster(it)) }, mod,
                alReiniciar = { Mesa.ponerMaster(0f) }, descripcion = "Volumen maestro",
            )
        }
        TextoPicos()
        Separador(Modifier.padding(top = 10.dp))
        FilaLufs { Vista.pestana = Vista.ANALISIS }
        Separador()
        FilaInterruptor("Limitador", Mesa.limitador, { Mesa.alternarLimitador() }) { TextoReduccion() }
        Separador()
        FilaInterruptor("Silenciar salida", Mesa.masterMute, { Mesa.alternarMasterMute() })
    }
}

@Composable
private fun TextoPicos() {
    val m = Mesa.master
    val f = { x: Float -> if (x > -100f) String.format(Locale.US, "%.1f", x).replace('-', '−') else "—" }
    Texto(
        "Pico máx.  L ${f(m.picoL)}  ·  R ${f(m.picoR)} dB", Tipo.nota.numeros, LocalPaleta.current.texto2,
        Modifier.padding(top = 2.dp), lineas = 1,
    )
}

@Composable
private fun TextoReduccion() {
    val r = Mesa.master.reduccion
    Texto(
        if (Mesa.limitador) "Reducción " + String.format(Locale.US, "%.1f", r) + " dB" else "Apagado",
        Tipo.nota.numeros, LocalPaleta.current.texto2, lineas = 1,
    )
}

@Composable
private fun TextoSalida() {
    if (Mesa.salida.isEmpty()) return
    PieGrupo(Mesa.salida, Modifier.padding(start = 16.dp, end = 16.dp, top = 2.dp))
}
