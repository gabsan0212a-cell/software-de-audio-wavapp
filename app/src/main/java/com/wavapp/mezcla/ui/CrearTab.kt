package com.wavapp.mezcla.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.Pista
import kotlin.math.roundToInt

/** Ícono de cada instrumento. */
internal fun iconoInstrumento(tipo: Int): ImageVector = when (tipo) {
    Nucleo.INST_BATERIA -> Iconos.bateria
    Nucleo.INST_808 -> Iconos.subGrave
    Nucleo.INST_BAJO, Nucleo.INST_GUITARRA -> Iconos.guitarra
    Nucleo.INST_PIANO -> Iconos.piano
    else -> Iconos.teclado
}

/** Color de cada instrumento (el mismo en la lista, los pads y la cuadrícula). */
@Composable
internal fun colorInstrumento(tipo: Int): Color {
    val p = LocalPaleta.current
    return when (tipo) {
        Nucleo.INST_BATERIA -> p.naranja
        Nucleo.INST_808 -> p.rojo
        Nucleo.INST_BAJO -> p.azul
        Nucleo.INST_GUITARRA -> p.verde
        Nucleo.INST_PIANO -> p.pistas[11]  // índigo
        else -> p.pistas[3]  // morado
    }
}

internal fun nombreTipo(tipo: Int): String = Mesa.infoInstrumentos.tipos.getOrNull(tipo)?.nombre ?: "Instrumento"

/** Pestaña "Crear": tempo, tonalidad y los instrumentos para hacer música. */
@Composable
fun PestanaCrear(abajo: Dp) {
    val lista = rememberLazyListState()
    val instrumentos = Mesa.pistas.filter { it.esInstrumento }
    Box(Modifier.fillMaxSize()) {
        LazyColumn(
            Modifier.fillMaxSize().testTag("listaCrear"),
            state = lista,
            contentPadding = PaddingValues(bottom = abajo + 20.dp),
        ) {
            item(key = "espacio") { EspacioEncabezado() }
            item(key = "titulo") { TituloGrande("Crear") }
            item(key = "musica") { TarjetaMusica() }
            item(key = "instTitulo") {
                val n = Mesa.musica.preparando
                EncabezadoSeccion("Instrumentos", detalle = if (n > 0) "Preparando…" else null)
            }
            if (instrumentos.isEmpty()) {
                item(key = "vacio") {
                    PieGrupo(
                        "Elige un instrumento: empieza con una idea que suena en la tonalidad y el tempo de arriba. " +
                            "Después cambia las notas, tócalo en vivo o graba lo que toques. O empieza con un estilo " +
                            "completo de abajo.",
                        Modifier.padding(start = 4.dp, bottom = 10.dp),
                    )
                }
            }
            items(instrumentos, key = { it.id }) { TarjetaInstrumento(it) }
            item(key = "agregar") { AgregarInstrumento() }
            item(key = "estilosTitulo") { EncabezadoSeccion("Empezar con un estilo") }
            item(key = "estilos") { Estilos(instrumentos.isNotEmpty()) }
            item(key = "pie") {
                PieGrupo(
                    "Cada instrumento es una pista más: en Mezcla le pones volumen, EQ y efectos; en Editar lo cortas y " +
                        "lo mueves; en Grabar cantas encima. También se exporta.",
                    Modifier.padding(top = 6.dp),
                )
            }
        }
        EncabezadoFlotante(lista, "Crear") {
            BotonIcono(
                Iconos.metronomo, "Metrónomo",
                { Mesa.ponerMetronomo(!Mesa.musica.metronomo) },
                color = if (Mesa.musica.metronomo) LocalPaleta.current.acento else LocalPaleta.current.texto2, tamano = 24.dp,
            )
            BotonReproducir(LocalPaleta.current.acento, 26.dp, 44.dp)
        }
    }
}

/** Tempo, tonalidad, metrónomo y repetir. */
@Composable
private fun TarjetaMusica() {
    val p = LocalPaleta.current
    val m = Mesa.musica
    var borrador by remember { mutableStateOf<Float?>(null) }
    val bpm = borrador ?: m.bpm
    Tarjeta(Modifier.padding(horizontal = 16.dp), PaddingValues(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 6.dp)) {
        FilaAjuste("Tempo", "${bpm.roundToInt()} BPM" + if (m.bpmDeCancion && borrador == null) " · de la canción" else "") {
            Row(it, verticalAlignment = Alignment.CenterVertically) {
                BotonIcono(Iconos.chevronIzquierda, "Más lento", { Mesa.ponerTempo(m.bpm - 1f) }, color = p.acento, tamano = 18.dp)
                Deslizador(
                    bpm, 40f, 240f, { borrador = it.roundToInt().toFloat() }, Modifier.weight(1f),
                    alReiniciar = { Mesa.ponerTempo(90f) }, descripcion = "Tempo",
                    // El tempo vuelve a preparar los instrumentos: se aplica al soltar el dedo.
                    alTocar = { abajo ->
                        if (!abajo) {
                            borrador?.let { Mesa.ponerTempo(it) }
                            borrador = null
                        }
                    },
                )
                BotonIcono(Iconos.chevronDerecha, "Más rápido", { Mesa.ponerTempo(m.bpm + 1f) }, color = p.acento, tamano = 18.dp)
            }
        }
        Separador(Modifier.padding(top = 8.dp))
        Row(
            Modifier.fillMaxWidth().height(48.dp).testTag("tonalidad").pulsable { Vista.tonalidad = true },
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Texto("Tonalidad", Tipo.cuerpo, p.texto, Modifier.weight(1f), lineas = 1)
            Texto(
                nombreTonalidad(m.tonica, m.menor) + if (m.tonicaDeCancion) " · de la canción" else "", Tipo.cuerpo, p.texto2,
                lineas = 1,
            )
            Icono(Iconos.chevronDerecha, p.texto3, Modifier.padding(start = 6.dp), tamano = 16.dp)
        }
        Separador()
        FilaInterruptor("Metrónomo", m.metronomo, { Mesa.ponerMetronomo(it) }) {
            Texto("Un clic en cada pulso mientras suena (no se exporta)", Tipo.nota, p.texto2, lineas = 1)
        }
        Separador()
        FilaInterruptor("Repetir el patrón", Vista.repetir, { Vista.repetir = it }) {
            Texto("Mientras creas, el patrón vuelve a empezar", Tipo.nota, p.texto2, lineas = 1)
        }
    }
}

/** Un instrumento de la lista: su sonido y lo que toca, en chiquito. */
@Composable
private fun TarjetaInstrumento(pista: Pista) {
    val p = LocalPaleta.current
    val color = colorInstrumento(pista.instrumento)
    val pat = pista.patron
    Row(
        Modifier
            .padding(horizontal = 16.dp, vertical = 5.dp)
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(p.tarjeta)
            .testTag("instrumento-${pista.id}")
            .semantics { contentDescription = "Abrir ${pista.nombre}" }
            .pulsable(atenuado = 0.6f) { Mesa.instrumentoAbierto = pista.id }
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        AzulejoIcono(iconoInstrumento(pista.instrumento), color, 46.dp)
        Column(Modifier.weight(1f).padding(start = 12.dp)) {
            Texto(pista.nombre, Tipo.encabezado, p.texto, lineas = 1)
            val preset = pat?.let { Mesa.infoInstrumentos.tipos.getOrNull(it.tipo)?.presets?.getOrNull(it.preset) } ?: ""
            val detalle = if (pat == null) "" else "$preset · ${pat.compases} ${if (pat.compases == 1) "compás" else "compases"} × ${pat.veces}"
            Texto(
                if (pat?.preparando == true) "Preparando…" else detalle, Tipo.nota, p.texto2, lineas = 1,
            )
            if (pat != null) MiniPatron(pista, color, Modifier.padding(top = 6.dp).fillMaxWidth().height(18.dp))
        }
        if (pista.mute) Texto("Silencio", Tipo.leyenda, p.texto3, Modifier.padding(start = 6.dp), lineas = 1)
        Icono(Iconos.chevronDerecha, p.texto3, Modifier.padding(start = 6.dp), tamano = 16.dp)
    }
}

/** Las notas del patrón como rayitas (dónde suena algo a lo largo de los compases). */
@Composable
private fun MiniPatron(pista: Pista, color: Color, modifier: Modifier) {
    val pal = LocalPaleta.current
    val pat = pista.patron ?: return
    val pasos = (pat.compases * Mesa.infoInstrumentos.pasosCompas).coerceAtLeast(1)
    val pulsos = pasos * Mesa.infoInstrumentos.pasoPulsos
    Canvas(modifier) {
        drawRoundRect(pal.relleno, cornerRadius = CornerRadius(4.dp.toPx()))
        if (pat.n == 0) return@Canvas
        var lo = Int.MAX_VALUE
        var hi = Int.MIN_VALUE
        for (i in 0 until pat.n) {
            lo = minOf(lo, pat.nota(i))
            hi = maxOf(hi, pat.nota(i))
        }
        val rango = (hi - lo).coerceAtLeast(1)
        val alto = size.height - 4.dp.toPx()
        for (i in 0 until pat.n) {
            if (pat.pulso(i) >= pulsos) continue
            val x = size.width * pat.pulso(i) / pulsos
            // Una nota que sigue sonando después del final (se encima con la vuelta siguiente) se
            // dibuja hasta el borde.
            val w = minOf(size.width * pat.largo(i) / pulsos, size.width - x).coerceAtLeast(1.5.dp.toPx())
            val y = 2.dp.toPx() + alto * (1f - (pat.nota(i) - lo).toFloat() / rango) - 1.dp.toPx()
            drawRect(color, Offset(x, y.entre(0f, size.height - 2.dp.toPx())), Size(w, 2.dp.toPx()))
        }
    }
}

/** Los instrumentos para agregar, de a tres por fila. */
@Composable
private fun AgregarInstrumento() {
    val p = LocalPaleta.current
    val tipos = Mesa.infoInstrumentos.tipos
    Tarjeta(Modifier.padding(horizontal = 16.dp, vertical = 8.dp), PaddingValues(horizontal = 6.dp, vertical = 12.dp)) {
        Texto("Agregar instrumento", Tipo.subtitulo.seminegrita, p.texto, Modifier.padding(start = 10.dp, bottom = 10.dp), lineas = 1)
        tipos.indices.chunked(3).forEach { fila ->
            Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), horizontalArrangement = Arrangement.SpaceEvenly) {
                for (i in fila) {
                    val t = tipos[i]
                    Column(
                        Modifier
                            .width(96.dp)
                            .testTag("agregarInstrumento-$i")
                            .semantics { contentDescription = "Agregar ${t.nombre}" }
                            .pulsable(habilitado = Mesa.pistas.size < Mesa.MAX_PISTAS, atenuado = 0.6f) {
                                val id = Mesa.agregarInstrumento(i)
                                if (id > 0) Vista.abrirInstrumento = id
                            },
                        horizontalAlignment = Alignment.CenterHorizontally,
                    ) {
                        AzulejoIcono(iconoInstrumento(i), colorInstrumento(i), 50.dp, radio = 12.dp)
                        Spacer(Modifier.height(6.dp))
                        Texto(t.nombre, Tipo.leyenda, p.texto, lineas = 1, alineacion = TextAlign.Center)
                    }
                }
                repeat(3 - fila.size) { Spacer(Modifier.width(96.dp)) }
            }
        }
    }
}

/**
 * Estilos: con un toque agrega varios instrumentos con ideas que suenan juntas (y, sin canción
 * preparada, su tempo y su tonalidad). Si ya hay instrumentos, antes pregunta.
 */
@Composable
private fun Estilos(preguntar: Boolean) {
    val p = LocalPaleta.current
    val estilos = Mesa.infoInstrumentos.estilos
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 2.dp),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        estilos.forEachIndexed { i, e ->
            Column(
                Modifier
                    .width(232.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(p.tarjeta)
                    .testTag("estilo-$i")
                    .semantics { contentDescription = "Estilo ${e.nombre}" }
                    .pulsable(habilitado = Mesa.pistas.size + e.partes.size <= Mesa.MAX_PISTAS, atenuado = 0.6f) {
                        if (preguntar) Vista.estilo = i else Mesa.agregarEstilo(i)
                    }
                    .padding(14.dp),
            ) {
                Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                    for (tipo in e.partes) AzulejoIcono(iconoInstrumento(tipo), colorInstrumento(tipo), 30.dp, radio = 8.dp)
                }
                Texto(e.nombre, Tipo.encabezado, p.texto, Modifier.padding(top = 10.dp), lineas = 1)
                Texto("${e.bpm} BPM · ${nombreTonalidad(e.tonica, e.menor)}", Tipo.nota.numeros, p.acento, lineas = 1)
                Texto(e.detalle, Tipo.nota, p.texto2, Modifier.padding(top = 4.dp).height(36.dp), lineas = 2)
            }
        }
    }
    PieGrupo(
        "Cada estilo agrega sus instrumentos con ideas en los mismos acordes. Sin canción preparada, también pone su tempo " +
            "y su tonalidad. Después cambia lo que quieras.",
        Modifier.padding(start = 4.dp),
    )
}

/** Elegir la tonalidad de lo que se crea (la cuadrícula muestra sus notas y el auto-tune la sigue). */
@Composable
fun HojaTonalidad(visible: Boolean, alCerrar: () -> Unit) {
    val m = Mesa.musica
    Hoja(
        visible = visible, alCerrar = alCerrar, titulo = "Tonalidad",
        derecha = { BotonTexto("Listo", alCerrar, estilo = Tipo.encabezado) },
    ) {
        Segmentado(
            listOf("Mayor", "Menor"), if (m.menor) 1 else 0, { Mesa.ponerTonalidad(m.tonica, it == 1) },
            Modifier.fillMaxWidth().padding(top = 4.dp),
        )
        Spacer(Modifier.height(14.dp))
        for (fila in 0 until 3) {
            Row(Modifier.fillMaxWidth().padding(bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for (col in 0 until 4) {
                    val i = fila * 4 + col
                    Chip(
                        NOTAS[i], i == m.tonica, { Mesa.ponerTonalidad(i, m.menor) }, Modifier.weight(1f),
                        descripcion = "Tónica ${NOTAS[i]}",
                    )
                }
            }
        }
        PieGrupo(
            "${nombreTonalidad(m.tonica, m.menor)}: la cuadrícula muestra sus notas, las ideas la siguen y el auto-tune " +
                "(\"de la canción\") afina hacia ella. Las notas que ya pusiste no cambian.",
            Modifier.padding(start = 0.dp),
        )
    }
}
