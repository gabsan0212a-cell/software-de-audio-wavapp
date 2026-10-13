package com.wavapp.mezcla.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
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
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.wavapp.mezcla.Analisis
import com.wavapp.mezcla.Edicion
import com.wavapp.mezcla.Mesa
import java.util.Locale
import kotlin.math.max
import kotlin.math.pow

/** Una entrada de audio del celular (la lista la da Android). */
data class EntradaAudio(val id: Int, val nombre: String, val estereo: Boolean)

/** Por dónde sale el sonido: importa para escucharse en vivo. */
enum class Salida { BOCINA, CABLE, BLUETOOTH }

class EstadoAudio(val entradas: List<EntradaAudio>, val salida: Salida)

class PermisoMicrofono(val concedido: Boolean, val negado: Boolean, val pedir: () -> Unit)

/** La entrada elegida (o la primera de la lista). */
fun entradaElegida(audio: EstadoAudio): EntradaAudio? =
    audio.entradas.firstOrNull { it.id == Vista.entradaId } ?: audio.entradas.firstOrNull()

private val ALTO_GRABADOR = 196.dp

/** Pestaña "Grabar": la entrada en vivo (con EQ y escucha) y el botón para grabar, como Notas de Voz. */
@Composable
fun PestanaGrabar(abajo: Dp, audio: EstadoAudio, permiso: PermisoMicrofono) {
    val lista = rememberLazyListState()
    val e = Mesa.entrada
    Box(Modifier.fillMaxSize()) {
        LazyColumn(
            Modifier.fillMaxSize().testTag("panelGrabar"),
            state = lista,
            contentPadding = PaddingValues(bottom = abajo + (if (permiso.concedido) ALTO_GRABADOR else 0.dp) + 16.dp),
        ) {
            item(key = "espacio") { EspacioEncabezado() }
            item(key = "titulo") { TituloGrande("Grabar") }
            if (!permiso.concedido) {
                item(key = "permiso") { SinPermiso(permiso) }
                return@LazyColumn
            }
            item(key = "entradaTitulo") {
                EncabezadoSeccion(
                    "Entrada",
                    detalle = if (e.abierta) (if (e.canales == 2) "Estéreo · 48 kHz" else "Mono · 48 kHz") else "Cerrada",
                )
            }
            item(key = "entrada") { TarjetaEntrada(audio) }
            item(key = "escuchaTitulo") { EncabezadoSeccion("Escucha") }
            item(key = "escucha") { TarjetaEscucha(audio.salida) }
            item(key = "escuchaPie") { PieGrupo(avisoEscucha(audio.salida)) }
            item(key = "eqTitulo") { EncabezadoSeccion("Ecualizador en vivo") }
            item(key = "eq") { TarjetaEqEntrada() }
            item(key = "eqPie") {
                PieGrupo(
                    "Ecualiza lo que escuchas en tiempo real. La grabación se guarda sin efecto y la pista nueva " +
                        "recibe este EQ: lo puedes cambiar después.",
                )
            }
            item(key = "espectroTitulo") { EncabezadoSeccion("Así suena", detalle = "más en Análisis") }
            item(key = "espectro") { TarjetaEspectroEntrada() }
        }
        EncabezadoFlotante(lista, "Grabar")
        if (permiso.concedido) Grabador(Modifier.align(Alignment.BottomCenter).padding(bottom = abajo))
    }
}

@Composable
private fun SinPermiso(permiso: PermisoMicrofono) {
    val p = LocalPaleta.current
    Column(
        Modifier.fillMaxWidth().padding(horizontal = 32.dp, vertical = 40.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Icono(Iconos.microfono, p.texto3, tamano = 64.dp)
        Texto("Micrófono", Tipo.titulo2, p.texto, Modifier.padding(top = 16.dp), lineas = 1)
        Texto(
            if (permiso.negado) "Android no dio permiso. Actívalo en Ajustes → Apps → WavApp → Permisos → Micrófono."
            else "Para grabar y escucharte en vivo, la app necesita usar el micrófono o tu interfaz USB.",
            Tipo.subtitulo, p.texto2, Modifier.padding(top = 6.dp), alineacion = TextAlign.Center,
        )
        BotonRelleno(
            "Permitir micrófono", permiso.pedir, Modifier.padding(top = 22.dp).fillMaxWidth(), icono = Iconos.microfono,
        )
    }
}

// ---------------------------------------------------------------- entrada

@Composable
private fun TarjetaEntrada(audio: EstadoAudio) {
    val p = LocalPaleta.current
    val e = Mesa.entrada
    val elegida = entradaElegida(audio)
    Tarjeta(Modifier.padding(horizontal = 16.dp)) {
        if (audio.entradas.size > 1) {
            Row(
                Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(bottom = 12.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                for (d in audio.entradas) {
                    Chip(
                        d.nombre, d == elegida, { Vista.entradaId = d.id }, habilitado = !e.grabando,
                        icono = if (d.nombre.startsWith("Audífonos")) Iconos.audifonos else Iconos.microfono,
                    )
                }
            }
        } else if (audio.entradas.isEmpty()) {
            Texto("Se usa la entrada de audio del sistema.", Tipo.subtitulo, p.texto2, Modifier.padding(bottom = 10.dp))
        }
        if (elegida?.estereo == true) {
            Segmentado(
                listOf("Mono", "Estéreo"), if (Vista.estereo) 1 else 0, { if (!e.grabando) Vista.estereo = it == 1 },
                Modifier.fillMaxWidth().padding(bottom = 12.dp),
            )
        }
        MedidorEntrada(Modifier.fillMaxWidth())
        Spacer(Modifier.height(8.dp))
        FilaDeslizador(
            "Ganancia", textoGanancia(e.gananciaDb), { Mesa.ponerGananciaEntrada(0f) },
            anchoEtiqueta = 70.dp, anchoValor = 64.dp,
        ) { mod ->
            Deslizador(
                e.gananciaDb, -24f, 24f, { Mesa.ponerGananciaEntrada(it) }, mod, alReiniciar = { Mesa.ponerGananciaEntrada(0f) },
                desde = 0f, descripcion = "Ganancia de entrada",
            )
        }
        if (e.satura) {
            Texto(
                "Saturó: baja la ganancia hasta que el medidor no llegue al rojo (toca el medidor para borrar el aviso).",
                Tipo.nota, p.rojo, Modifier.padding(top = 4.dp),
            )
        }
    }
}

private fun textoGanancia(db: Float): String =
    (if (db > 0.05f) "+" else "") + String.format(Locale.US, "%.1f dB", db).replace('-', '−')

/** Medidor grande de la entrada: L y R (o una barra si es mono) y la luz de saturación. */
@Composable
private fun MedidorEntrada(modifier: Modifier) {
    val p = LocalPaleta.current
    val e = Mesa.entrada
    Spacer(
        modifier
            .height(26.dp)
            .semantics { contentDescription = "Nivel de entrada. Toca para borrar el aviso de saturación." }
            .pulsable(atenuado = 0.7f) { Mesa.borrarClip() }
            .drawWithCache {
                val luz = 16.dp.toPx()
                val ancho = size.width - luz
                val gap = 4.dp.toPx()
                val brocha = Brush.horizontalGradient(
                    0f to p.verde, 0.72f to p.verde, 0.86f to p.amarillo, 0.93f to p.amarillo, 0.97f to p.rojo, 1f to p.rojo,
                    startX = 0f, endX = ancho,
                )
                onDrawBehind {
                    val estereo = e.canales == 2
                    val filas = if (estereo) 2 else 1
                    val h = (size.height - gap * (filas - 1)) / filas
                    val radio = CornerRadius(h / 2)
                    for (c in 0 until filas) {
                        val y = c * (h + gap)
                        drawRoundRect(p.relleno, Offset(0f, y), Size(ancho, h), radio)
                        val nivel = if (c == 0) e.medidor.l else e.medidor.r
                        val u = ((nivel + 48f) / 51f).coerceIn(0f, 1f)
                        if (u > 0f) drawRoundRect(brocha, Offset(0f, y), Size(max(u * ancho, h), h), radio)
                    }
                    drawCircle(if (e.satura) p.rojo else p.relleno, 6.dp.toPx(), Offset(size.width - luz / 2 + 2.dp.toPx(), size.height / 2))
                }
            },
    )
}

// ---------------------------------------------------------------- escucha

@Composable
private fun TarjetaEscucha(salida: Salida) {
    val e = Mesa.entrada
    Tarjeta(Modifier.padding(horizontal = 16.dp), PaddingValues(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 8.dp)) {
        FilaInterruptor(
            "Escucharme en vivo", e.escucha, { Mesa.ponerEscucha(it) }, habilitado = salida != Salida.BOCINA || e.escucha,
        ) {
            val t = when {
                !e.abierta -> "La entrada está cerrada"
                e.retrasoMs > 0 -> "Retraso ≈ ${e.retrasoMs} ms"
                else -> "Con el EQ en vivo"
            }
            Texto(t, Tipo.nota.numeros, LocalPaleta.current.texto2, lineas = 1)
        }
        Separador()
        FilaDeslizador(
            "Volumen", textoDb(e.escuchaDb, Mesa.VOL_MIN + 0.1f), { Mesa.ponerVolumenEscucha(0f) },
            Modifier.padding(top = 4.dp), anchoEtiqueta = 70.dp, anchoValor = 64.dp,
        ) { mod ->
            Deslizador(
                e.escuchaDb, Mesa.VOL_MIN, Mesa.VOL_MAX, { Mesa.ponerVolumenEscucha(it) }, mod,
                alReiniciar = { Mesa.ponerVolumenEscucha(0f) }, descripcion = "Volumen de escucha",
            )
        }
    }
}

private fun avisoEscucha(salida: Salida): String = when (salida) {
    Salida.BOCINA -> "Conecta audífonos para escucharte: con la bocina del celular el sonido se acopla (chilla)."
    Salida.BLUETOOTH -> "Con audífonos Bluetooth te escucharás con un retraso de unos 0.2 s que no se puede quitar. " +
        "Para escucharte al instante usa audífonos con cable."
    Salida.CABLE -> "Te escuchas con audífonos con cable: el retraso es el mínimo que permite tu celular."
}

// ---------------------------------------------------------------- EQ en vivo y espectro

@Composable
private fun TarjetaEqEntrada() {
    val p = LocalPaleta.current
    val pista = Mesa.entrada.pista
    val n = pista.bandas.size
    Row(
        Modifier
            .padding(horizontal = 16.dp)
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(p.tarjeta)
            .semantics { contentDescription = "Ecualizador en vivo" }
            .pulsable(atenuado = 0.6f) { Mesa.eqAbierta = pista }
            .padding(horizontal = 14.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        AzulejoIcono(Iconos.microfono, p.acento, 40.dp)
        Column(Modifier.weight(1f).padding(start = 12.dp)) {
            Texto("Ecualizador", Tipo.encabezado, p.texto, lineas = 1)
            Texto(
                when {
                    n == 0 -> "Sin bandas: no cambia nada"
                    pista.eqBypass -> "$n ${if (n == 1) "banda" else "bandas"} · en bypass"
                    else -> "$n ${if (n == 1) "banda" else "bandas"} · se oye en vivo"
                },
                Tipo.nota, p.texto2, lineas = 1,
            )
        }
        Icono(Iconos.chevronDerecha, p.texto3, tamano = 18.dp)
    }
}

/** Ícono blanco sobre un cuadro de color (como las portadas). */
@Composable
fun AzulejoIcono(icono: androidx.compose.ui.graphics.vector.ImageVector, color: Color, lado: Dp, radio: Dp = 8.dp) {
    Box(
        Modifier
            .size(lado)
            .clip(RoundedCornerShape(radio))
            .background(Brush.linearGradient(listOf(lerp(color, Color.White, 0.2f), lerp(color, Color.Black, 0.25f)))),
        contentAlignment = Alignment.Center,
    ) { Icono(icono, Color.White, tamano = lado * 0.55f) }
}

@Composable
private fun TarjetaEspectroEntrada() {
    val p = LocalPaleta.current
    Tarjeta(Modifier.padding(horizontal = 16.dp)) {
        Canvas(
            Modifier.fillMaxWidth().height(120.dp).clip(RoundedCornerShape(10.dp))
                .background(if (p.oscuro) Color(0xFF0B0B0C) else Color(0xFFF6F6F8))
                .semantics { contentDescription = "Espectro de la entrada" },
        ) {
            @Suppress("UNUSED_VARIABLE") val v = Analisis.version
            if (Analisis.activo && Mesa.entrada.abierta) {
                lineaEspectro(p.acento.copy(alpha = 0.16f), relleno = true)
                lineaEspectro(p.acento, relleno = false)
            }
        }
        Texto(
            if (Mesa.entrada.abierta) "En vivo, después del EQ. En Análisis → Micrófono ves fase, paneo y zumbido."
            else "La entrada está cerrada.",
            Tipo.nota, p.texto2, Modifier.padding(top = 8.dp),
        )
    }
}

// ---------------------------------------------------------------- grabador

/** Panel de abajo: la toma en vivo, el tiempo y el botón rojo (como Notas de Voz). */
@Composable
private fun Grabador(modifier: Modifier) {
    val p = LocalPaleta.current
    val e = Mesa.entrada
    val forma = RoundedCornerShape(18.dp)
    Column(
        modifier
            .padding(start = 8.dp, end = 8.dp, bottom = 8.dp)
            .fillMaxWidth()
            .height(ALTO_GRABADOR - 8.dp)
            .shadow(if (p.oscuro) 0.dp else 12.dp, forma)
            .clip(forma)
            .background(p.elevado)
            .testTag("grabador")
            .padding(horizontal = 16.dp, vertical = 10.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        OndaToma(Modifier.fillMaxWidth().height(46.dp))
        TiempoGrabador()
        Row(
            Modifier.fillMaxWidth().weight(1f),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Box(Modifier.width(92.dp), contentAlignment = Alignment.CenterStart) {
                if (e.grabando) {
                    BotonTexto("Descartar", { Mesa.descartarGrabacion() }, color = p.rojo, estilo = Tipo.subtitulo)
                } else {
                    BotonTexto(
                        "Al inicio", { Mesa.saltar(0.0) }, estilo = Tipo.subtitulo,
                        habilitado = Mesa.posicion > 0.0 && Mesa.pistas.isNotEmpty(),
                    )
                }
            }
            BotonGrabar()
            Box(Modifier.width(92.dp), contentAlignment = Alignment.CenterEnd) {
                val id = Vista.ultimaGrabacion
                if (!e.grabando && id != null && Mesa.pistas.any { it.id == id }) {
                    BotonTexto("Ver", {
                        Edicion.seleccion = Edicion.Seleccion(id, 0)
                        Vista.pestana = Vista.EDITAR
                    }, estilo = Tipo.subtitulo.seminegrita)
                }
            }
        }
    }
}

@Composable
private fun TiempoGrabador() {
    val p = LocalPaleta.current
    val e = Mesa.entrada
    val texto = when {
        e.grabando -> tiempoGrabado(e.segundos)
        !e.abierta -> "Entrada cerrada"
        Mesa.pistas.isEmpty() -> "Listo para grabar"
        else -> "Desde " + tiempoCorto(Mesa.posicion) + " con la mezcla"
    }
    Texto(
        texto, if (e.grabando) Tipo.titulo3.numeros else Tipo.subtitulo.numeros, if (e.grabando) p.texto else p.texto2,
        Modifier.padding(top = 2.dp), lineas = 1,
    )
}

private fun tiempoGrabado(s: Double): String {
    val m = (s / 60).toInt()
    return String.format(Locale.US, "%d:%04.1f", m, s - m * 60)
}

/** La toma mientras se graba: barras rojas que avanzan de derecha a izquierda (últimos 4 s). */
@Composable
private fun OndaToma(modifier: Modifier) {
    val p = LocalPaleta.current
    val e = Mesa.entrada
    Canvas(modifier.semantics { contentDescription = "Onda de la grabación" }) {
        val paso = 3.dp.toPx()
        val cols = max(1, (size.width / paso).toInt())
        val g = 2.dp.toPx()
        val cy = size.height / 2
        if (e.grabando) {
            @Suppress("UNUSED_VARIABLE") val s = e.segundos  // redibujar mientras avanza
            val v = Mesa.ondaGrabacion(cols, 4f)
            for (c in v.indices) {
                val h = max(g, v[c].pow(0.6f) * size.height)
                val x = c * paso + paso / 2
                drawRoundRect(p.rojo, Offset(x - g / 2, cy - h / 2), Size(g, h), CornerRadius(g / 2))
            }
        } else {
            // En espera: el nivel de la entrada como una línea que respira.
            val u = ((e.medidor.l + 48f) / 51f).coerceIn(0f, 1f)
            val h = max(g, u * size.height * 0.8f)
            drawRoundRect(p.texto3, Offset(0f, cy - g / 2), Size(size.width, g), CornerRadius(g / 2))
            if (e.abierta) drawRoundRect(p.verde, Offset(size.width / 2 - g, cy - h / 2), Size(2 * g, h), CornerRadius(g))
        }
    }
}

/** El botón rojo: círculo para grabar, cuadro redondeado para detener. */
@Composable
private fun BotonGrabar() {
    val p = LocalPaleta.current
    val e = Mesa.entrada
    val grabando = e.grabando
    val t by animateFloatAsState(if (grabando) 1f else 0f, tween(220), label = "grabar")
    val habilitado = e.abierta && (grabando || (!Mesa.ocupada && Mesa.pistas.size < Mesa.MAX_PISTAS))
    val aro = p.texto3
    val rojo = p.rojo
    Canvas(
        Modifier
            .size(72.dp)
            .semantics { contentDescription = if (grabando) "Detener grabación" else "Grabar" }
            .pulsable(habilitado, rol = Role.Button, atenuado = 0.6f) {
                val id = Mesa.grabarOParar()
                if (id > 0) Vista.ultimaGrabacion = id
            },
    ) {
        val r = size.minDimension / 2
        drawCircle(aro, r - 2.dp.toPx(), center, style = Stroke(3.dp.toPx()))
        // De círculo a cuadro redondeado.
        val lado = (2 * (r - 7.dp.toPx())) * (1f - 0.45f * t)
        val esquina = lado / 2 * (1f - 0.7f * t)
        drawRoundRect(
            rojo, Offset(center.x - lado / 2, center.y - lado / 2), Size(lado, lado), CornerRadius(esquina),
        )
    }
}
