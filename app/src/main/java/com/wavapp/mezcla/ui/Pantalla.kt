package com.wavapp.mezcla.ui

import androidx.compose.animation.core.withInfiniteAnimationFrameMillis
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.windowInsetsBottomHeight
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.wavapp.mezcla.Analisis
import com.wavapp.mezcla.Edicion
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.Pista
import kotlinx.coroutines.delay

/** Lo que se está viendo (solo pantalla): la pestaña, el reproductor y las hojas abiertas. */
object Vista {
    const val MEZCLA = 0
    const val EDITAR = 1
    const val ANALISIS = 2
    const val GRABAR = 3
    const val CREAR = 4

    var pestana by mutableIntStateOf(MEZCLA)
    var reproductor by mutableStateOf(false)
    var exportar by mutableStateOf(false)
    var agregar by mutableStateOf(false)
    var opciones by mutableStateOf<Pista?>(null)
    var aQuitar by mutableStateOf<Pista?>(null)
    var automatizacion by mutableStateOf<Pista?>(null)
    var efectos by mutableStateOf<Pista?>(null)
    var preparar by mutableStateOf<Pista?>(null)
    var voz by mutableStateOf(false)
    var escenario by mutableStateOf(false)

    // Crear: elegir la tonalidad, repetir el patrón, el instrumento que se abre al aparecer y
    // a cuál se le borran las notas (pide confirmar).
    var tonalidad by mutableStateOf(false)
    var repetir by mutableStateOf(true)
    var abrirInstrumento by mutableIntStateOf(0)
    var borrarNotas by mutableStateOf<Pista?>(null)

    // Grabar: qué entrada (0 = la primera de la lista), en estéreo o no, y la última toma.
    var entradaId by mutableIntStateOf(0)
    var estereo by mutableStateOf(false)
    var ultimaGrabacion by mutableStateOf<Int?>(null)
}

/** El reporte de la vez anterior que la app se cerró por un error (null = no hubo) y cómo borrarlo. */
class UltimoError(val texto: String?, val borrar: () -> Unit)

/** Lo principal del reporte (qué error y dónde), para el aviso; completo se copia. */
private fun resumenError(texto: String): String =
    texto.lines().drop(1).map { it.replace('\t', ' ').trim() }.filter { it.isNotEmpty() }.take(3).joinToString("\n").take(260)

private val ALTO_PESTANAS = 50.dp
private val ALTO_MINI = 72.dp  // mini reproductor con su margen

@Composable
fun PantallaMesa() {
    val p = LocalPaleta.current
    // Posición y medidores en cada cuadro de pantalla. (Como animación infinita: en las pruebas
    // automáticas se pausa sola.)
    LaunchedEffect(Unit) {
        while (true) {
            withInfiniteAnimationFrameMillis { Mesa.tick(it) }
            if (!Mesa.activa()) delay(250)  // quieta: 4 veces por segundo basta (ahorra batería)
        }
    }
    val entrada = Mesa.entrada
    PantallaEncendida(
        Mesa.reproduciendo || entrada.grabando || entrada.escucha || Vista.escenario || Mesa.presentacion.grabando,
    )
    SideEffect {
        val tapada = Vista.reproductor || Vista.escenario
        Analisis.abierto = Vista.pestana == Vista.ANALISIS && !tapada
        Analisis.fuenteForzada = if (Vista.pestana == Vista.GRABAR && !tapada) Nucleo.ID_ENTRADA else null
        Edicion.abierta = Vista.pestana == Vista.EDITAR
        Mesa.escenario = Vista.escenario
        // Crear: la salida despierta (lo que se toca suena al instante) y se repite el patrón.
        val abierto = Mesa.instrumentoAbierto
        val creando = !tapada && (Vista.pestana == Vista.CREAR || abierto > 0)
        val repetirId = if (!Vista.repetir) 0 else abierto.takeIf { it > 0 }
            ?: Mesa.pistas.firstOrNull { it.esInstrumento }?.id ?: 0
        Mesa.creando(creando, if (creando) repetirId else 0)
    }
    // Un instrumento recién agregado se abre en cuanto el núcleo lo tiene listo.
    LaunchedEffect(Vista.abrirInstrumento, Mesa.pistas.size) {
        val id = Vista.abrirInstrumento
        if (id > 0 && Mesa.pistas.any { it.id == id }) {
            Mesa.instrumentoAbierto = id
            Vista.abrirInstrumento = 0
        }
    }
    val abrirArchivos = recordarAbrirArchivos()
    val guardarMezcla = recordarGuardarMezcla()
    val guardarPresentacion = recordarGuardarPresentacion()
    val audio = recordarAudio()
    val permiso = recordarPermisoMicrofono()
    // La entrada se abre solo cuando hace falta: en Grabar o en el escenario, mientras te escuchas
    // o grabas, o si el análisis es del micrófono. Al cambiar de entrada (o mono/estéreo) se abre
    // de nuevo.
    val elegida = entradaElegida(audio)
    val quiere = permiso.concedido && (
        Vista.pestana == Vista.GRABAR || Vista.escenario || entrada.escucha || entrada.grabando ||
            (Vista.pestana == Vista.ANALISIS && Analisis.fuenteId == Nucleo.ID_ENTRADA)
        )
    // Sin lista (algunos aparatos no la dan): la entrada del sistema, en mono.
    val config = elegida?.let { it.id to (if (it.estereo && Vista.estereo) 2 else 1) } ?: (0 to 1)
    val abierta = remember { arrayOfNulls<Pair<Int, Int>>(1) }
    LaunchedEffect(quiere, config, entrada.abierta) {
        when {
            quiere && (!entrada.abierta || abierta[0] != config) -> {
                if (!entrada.grabando) {
                    abierta[0] = config
                    Mesa.abrirEntrada(config.first, config.second)
                }
            }
            !quiere && entrada.abierta -> {
                abierta[0] = null
                Mesa.cerrarEntrada()
            }
        }
    }
    val conMini = Vista.pestana != Vista.EDITAR && Vista.pestana != Vista.GRABAR && Mesa.pistas.isNotEmpty()
    val abajo = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() + ALTO_PESTANAS +
        (if (conMini) ALTO_MINI else 0.dp)

    Box(Modifier.fillMaxSize().background(p.fondoAgrupado)) {
        when (Vista.pestana) {
            Vista.EDITAR -> PestanaEdicion(abajo)
            Vista.ANALISIS -> PestanaAnalisis(abajo)
            Vista.GRABAR -> PestanaGrabar(abajo, audio, permiso)
            Vista.CREAR -> PestanaCrear(abajo)
            else -> PestanaMezcla(abajo, abrirArchivos)
        }
        Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth()) {
            if (conMini) MiniReproductor()
            BarraPestanas()
        }

        Reproductor(Vista.reproductor, alCerrar = { Vista.reproductor = false }, alExportar = { Vista.exportar = true })
        Escenario(
            Vista.escenario, audio.salida, permiso, alCerrar = { Vista.escenario = false }, alGuardar = guardarPresentacion,
        )
        EditorInstrumento(
            Mesa.pistas.firstOrNull { it.id == Mesa.instrumentoAbierto && it.esInstrumento },
            alCerrar = { Mesa.instrumentoAbierto = 0 },
        )
        HojaTonalidad(Vista.tonalidad, alCerrar = { Vista.tonalidad = false })
        HojaEq(Mesa.eqAbierta, alCerrar = { Mesa.eqAbierta = null })
        HojaAutomatizacion(
            Vista.automatizacion?.takeIf { it in Mesa.pistas }, alCerrar = { Vista.automatizacion = null },
        )
        HojaEfectos(Vista.efectos?.takeIf { it in Mesa.pistas }, alCerrar = { Vista.efectos = null })
        HojaPreparar(Vista.preparar?.takeIf { it in Mesa.pistas }, alCerrar = { Vista.preparar = null })
        HojaVoz(Vista.voz, alCerrar = { Vista.voz = false })
        HojaExportar(
            Vista.exportar, alCerrar = { Vista.exportar = false },
            alGuardar = {
                Vista.exportar = false
                guardarMezcla()
            },
        )
        HojaAcciones(
            Vista.agregar, alCerrar = { Vista.agregar = false }, titulo = "Agregar pistas",
            acciones = listOf(
                Accion("Agregar archivos…") { abrirArchivos() },
                Accion("Crear con instrumentos…") { Vista.pestana = Vista.CREAR },
                Accion("Pistas de prueba") { Mesa.cargarPrueba() },
            ),
        )
        OpcionesPista()
        val borrar = Vista.borrarNotas
        Alerta(
            visible = borrar != null,
            titulo = "¿Borrar todas las notas?",
            mensaje = "El instrumento se queda sin notas (se puede deshacer).",
            confirmar = "Borrar", destructiva = true,
            alConfirmar = { borrar?.let { Mesa.borrarNotas(it) } },
            alCerrar = { Vista.borrarNotas = null },
        )
        val quitar = Vista.aQuitar
        Alerta(
            visible = quitar != null,
            titulo = quitar?.let { "¿Quitar la pista ${it.id} · ${it.nombre}?" } ?: "",
            mensaje = "Se borra de la mezcla. El archivo original en tu celular no se toca.",
            confirmar = "Quitar", destructiva = true,
            alConfirmar = { quitar?.let { Mesa.quitar(it) } },
            alCerrar = { Vista.aQuitar = null },
        )
        // La vez anterior la app se cerró por un error: el reporte, listo para copiarlo y mandarlo.
        val fallo = recordarUltimoError()
        val portapapeles = LocalClipboardManager.current
        Alerta(
            visible = fallo.texto != null,
            titulo = "La app se cerró por un error",
            mensaje = "Quedó anotado qué pasó. Toca Copiar y pégalo en el chat para arreglarlo.\n\n" +
                (fallo.texto?.let(::resumenError) ?: ""),
            confirmar = "Copiar",
            alConfirmar = { fallo.texto?.let { portapapeles.setText(AnnotatedString(it)) } },
            alCerrar = { fallo.borrar() },
        )
    }
}

/** Menú "⋯" de una pista. */
@Composable
private fun OpcionesPista() {
    val pista = Vista.opciones
    val acciones = if (pista == null) emptyList() else buildList {
        if (pista.esInstrumento) add(Accion("Editar instrumento…") { Mesa.instrumentoAbierto = pista.id })
        else add(Accion("Preparar para cantar…") { Vista.preparar = pista })
        add(Accion("Ecualizador") { Mesa.eqAbierta = pista })
        add(Accion("Efectos") { Vista.efectos = pista })
        add(Accion("Automatización") { Vista.automatizacion = pista })
        add(Accion("Editar pedazos") {
            Edicion.seleccion = Edicion.Seleccion(pista.id, 0)
            Vista.pestana = Vista.EDITAR
        })
        if (pista.editada) add(Accion("Volver al archivo original") { Edicion.restaurarPista(pista) })
        add(Accion("Quitar pista", destructiva = true) { Vista.aQuitar = pista })
    }
    HojaAcciones(
        pista != null, alCerrar = { Vista.opciones = null }, acciones = acciones,
        titulo = pista?.let { "${it.id} · ${it.nombre}" },
    )
}

// ---------------------------------------------------------------- barra de pestañas

@Composable
private fun BarraPestanas() {
    val p = LocalPaleta.current
    Column(Modifier.fillMaxWidth().background(p.barra)) {
        Separador()
        Row(Modifier.fillMaxWidth().height(ALTO_PESTANAS)) {
            Pestana(Iconos.mezclador, "Mezcla", Vista.MEZCLA)
            Pestana(Iconos.teclado, "Crear", Vista.CREAR)
            Pestana(Iconos.microfono, "Grabar", Vista.GRABAR)
            Pestana(Iconos.tijeras, "Editar", Vista.EDITAR)
            Pestana(Iconos.onda, "Análisis", Vista.ANALISIS)
        }
        Spacer(Modifier.windowInsetsBottomHeight(WindowInsets.navigationBars))
    }
}

@Composable
private fun RowScope.Pestana(icono: androidx.compose.ui.graphics.vector.ImageVector, texto: String, i: Int) {
    val p = LocalPaleta.current
    val elegida = Vista.pestana == i
    val color = if (elegida) p.acento else p.texto2
    Column(
        Modifier
            .weight(1f)
            .fillMaxHeight()
            .testTag("pestana-$i")
            .semantics { selected = elegida }
            .pulsable(rol = Role.Tab, atenuado = 0.6f) {
                Vista.pestana = i
                Vista.reproductor = false
            },
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Icono(icono, color, tamano = 25.dp)
        Spacer(Modifier.height(3.dp))
        Texto(texto, Tipo.pestana, color, lineas = 1)
    }
}

// ---------------------------------------------------------------- mini reproductor

@Composable
private fun MiniReproductor() {
    val p = LocalPaleta.current
    val forma = RoundedCornerShape(14.dp)
    Box(
        Modifier
            .padding(start = 8.dp, end = 8.dp, bottom = 8.dp)
            .fillMaxWidth()
            .height(ALTO_MINI - 8.dp)
            .shadow(if (p.oscuro) 0.dp else 12.dp, forma, ambientColor = Color.Black.copy(alpha = 0.2f), spotColor = Color.Black.copy(alpha = 0.2f))
            .clip(forma)
            .background(p.elevado)
            .testTag("miniReproductor")
            .semantics { contentDescription = "Abrir reproductor" }
            .pulsable(atenuado = 0.75f) { Vista.reproductor = true },
    ) {
        Row(Modifier.fillMaxSize().padding(start = 8.dp, end = 2.dp), verticalAlignment = Alignment.CenterVertically) {
            Mosaico(Mesa.pistas, 48.dp, radio = 7.dp)
            Column(Modifier.weight(1f).padding(start = 12.dp)) {
                Texto("Mezcla", Tipo.subtitulo.seminegrita, p.texto, lineas = 1)
                TiempoMini()
            }
            BotonReproducir(p.texto, 30.dp, 48.dp)
            BotonIcono(Iconos.detener, "Detener", { Mesa.detener() }, color = p.texto, tamano = 26.dp)
        }
        // Progreso de la canción, abajo (se lee al dibujar: no recompone en cada cuadro).
        val acento = p.acento
        Spacer(
            Modifier.align(Alignment.BottomStart).padding(horizontal = 14.dp).fillMaxWidth().height(2.dp).drawBehind {
                val d = Mesa.duracion
                val f = if (d > 0) (Mesa.posicion / d).toFloat().coerceIn(0f, 1f) else 0f
                drawRect(acento, size = Size(size.width * f, size.height))
            },
        )
    }
}

@Composable
private fun TiempoMini() {
    // Solo cambia una vez por segundo (la posición cambia en cada cuadro).
    val texto by remember { derivedStateOf { tiempoCorto(Mesa.posicion) + " / " + tiempoCorto(Mesa.duracion) } }
    Texto(texto, Tipo.nota.numeros, LocalPaleta.current.texto2, lineas = 1)
}

/** Reproducir / pausar (el mismo en el mini reproductor, el reproductor y la edición). */
@Composable
fun BotonReproducir(color: Color, tamano: Dp, lado: Dp) {
    val sonando = Mesa.reproduciendo
    BotonIcono(
        if (sonando) Iconos.pausa else Iconos.reproducir, if (sonando) "Pausar" else "Reproducir",
        { Mesa.reproducirPausar() }, color = color, tamano = tamano, lado = lado,
        habilitado = Mesa.pistas.isNotEmpty(),
    )
}
