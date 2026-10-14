package com.wavapp.mezcla.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.Cancion
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.Pista
import com.wavapp.mezcla.Separacion
import java.util.Locale
import kotlin.math.roundToInt

/** Nombres de las notas (0 = Do), como los dice un músico. */
internal val NOTAS = listOf("Do", "Do♯", "Re", "Mi♭", "Mi", "Fa", "Fa♯", "Sol", "La♭", "La", "Si♭", "Si")

internal fun nombreTonalidad(tonica: Int, menor: Boolean): String =
    if (tonica in 0..11) "${NOTAS[tonica]} ${if (menor) "menor" else "mayor"}" else "No se pudo saber"

/** "unos 23 min", "menos de un minuto"... */
internal fun textoTiempo(segundos: Float): String {
    if (segundos < 60f) return "menos de un minuto"
    val m = (segundos / 60f).roundToInt()
    return if (m < 60) "unos $m min" else "más de ${m / 60} h ${m % 60} min"
}

/** Qué está haciendo la separación (fase del núcleo). */
internal fun textoFase(fase: Int): String = when (fase) {
    1 -> "Preparando la canción…"
    2 -> "Quitando la voz…"
    3 -> "Armando las pistas…"
    4 -> "Analizando la canción…"
    else -> ""
}

/**
 * Hoja "Preparar para cantar": le quita la voz a la canción con el modelo (en el núcleo) y deja el
 * instrumental y la voz original como pistas nuevas; después muestra lo que se sabe de la canción.
 */
@Composable
fun HojaPreparar(pista: Pista?, alCerrar: () -> Unit) {
    val recordada = remember { arrayOfNulls<Pista>(1) }
    if (pista != null) recordada[0] = pista
    val p = pista ?: recordada[0]
    Hoja(
        visible = pista != null,
        alCerrar = alCerrar,
        titulo = "Preparar para cantar",
        alta = true,
        derecha = { BotonTexto("Listo", alCerrar, estilo = Tipo.encabezado) },
    ) {
        if (p != null) ContenidoPreparar(p)
    }
}

@Composable
private fun ContenidoPreparar(p: Pista) {
    val pal = LocalPaleta.current
    Row(Modifier.padding(start = 4.dp, top = 4.dp, bottom = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Portada(p.id, pal.pistas[p.color], 56.dp)
        Column(Modifier.padding(start = 14.dp)) {
            Texto(p.nombre, Tipo.encabezado, pal.texto, lineas = 1)
            Texto("${tiempoCorto(p.segundos)} · ${if (p.canales == 2) "estéreo" else "mono"}", Tipo.nota.numeros, pal.texto2, lineas = 1)
        }
    }
    val sep = Mesa.separacion
    val cancion = Mesa.cancion
    when {
        sep.activa && sep.pista == p.id -> EnCurso(sep)
        cancion.lista && cancion.fuente == p.id -> Lista(cancion)
        else -> Antes(p)
    }
}

@Composable
private fun Antes(p: Pista) {
    val pal = LocalPaleta.current
    val pedirAvisos = recordarPermisoAvisos()
    var calidad by remember { mutableIntStateOf(Nucleo.CALIDAD_ALTA) }
    // [trozos ya listos de una vez anterior, trozos en total]
    val guardados = remember(p.id, calidad, Mesa.separacion.fase, Mesa.ocupada) { Mesa.guardadosSeparacion(p, calidad) }
    Tarjeta {
        Texto("Le quita la voz a la canción con inteligencia artificial", Tipo.encabezado, pal.texto)
        Texto(
            "Quedan dos pistas nuevas: el instrumental, para cantar encima, y la voz original como guía (en " +
                "silencio). Después la app saca la tonalidad, el tempo y dónde entra la voz.",
            Tipo.subtitulo, pal.texto2, Modifier.padding(top = 4.dp),
        )
    }
    EncabezadoGrupo("Calidad")
    Segmentado(listOf("Alta", "Máxima"), calidad, { calidad = it }, Modifier.fillMaxWidth().testTag("calidadPreparar"))
    PieGrupo(
        if (calidad == Nucleo.CALIDAD_MAXIMA) {
            "Une los trozos con más cuidado (cada parte se calcula 4 veces). Tarda el doble."
        } else {
            "Como el modelo original: cada parte de la canción se calcula 2 veces y se mezcla."
        },
    )
    EncabezadoGrupo("Cuánto tarda")
    val faltan = (guardados[1] - guardados[0]).coerceAtLeast(0)
    Tarjeta(relleno = PaddingValues(horizontal = 16.dp, vertical = 2.dp)) {
        FilaDato("Trozos de 8 segundos", "${guardados[1]}")
        if (guardados[0] > 0) {
            Separador()
            FilaDato("Ya listos de una vez anterior", "${guardados[0]}")
        }
        Separador()
        // En celulares de 2020 en adelante, cada trozo tarda de unos 15 a 45 segundos.
        FilaDato("En un celular", if (faltan == 0) "—" else "${(faltan * 15 + 59) / 60}–${(faltan * 45 + 59) / 60} min")
    }
    PieGrupo(
        "Conecta el cargador. Puedes salir de la app o apagar la pantalla: sigue trabajando con un aviso arriba. " +
            "Si se interrumpe, al volver sigue donde quedó.",
    )
    val sinLugar = Mesa.pistas.size > Mesa.MAX_PISTAS - 2
    when {
        !Mesa.modeloVoz -> PieGrupo("Esta compilación no trae el modelo para quitar la voz.")
        sinLugar -> PieGrupo("Hacen falta dos lugares libres: quita alguna pista (máximo 12).")
        Mesa.ocupada -> PieGrupo("Espera a que termine lo que está haciendo la app.")
    }
    BotonRelleno(
        if (guardados[0] > 0) "Seguir quitando la voz" else "Quitar la voz",
        {
            pedirAvisos()
            Mesa.prepararCancion(p, calidad)
        },
        Modifier.padding(top = 18.dp).fillMaxWidth(),
        habilitado = Mesa.modeloVoz && !Mesa.ocupada && !sinLugar,
    )
}

@Composable
private fun EnCurso(sep: Separacion) {
    val pal = LocalPaleta.current
    Column(Modifier.fillMaxWidth().padding(top = 12.dp, bottom = 8.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Box(Modifier.size(168.dp), contentAlignment = Alignment.Center) {
            Canvas(Modifier.size(168.dp).semantics { contentDescription = "Avance ${(sep.progreso * 100).roundToInt()} %" }) {
                val grosor = 10.dp.toPx()
                val lado = size.minDimension - grosor
                val esquina = Offset((size.width - lado) / 2, (size.height - lado) / 2)
                drawArc(pal.relleno, 0f, 360f, false, esquina, Size(lado, lado), style = Stroke(grosor))
                drawArc(
                    pal.acento, -90f, 360f * sep.progreso.coerceIn(0f, 1f), false, esquina, Size(lado, lado),
                    style = Stroke(grosor, cap = StrokeCap.Round),
                )
            }
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Texto(
                    "${(sep.progreso * 100).roundToInt()} %",
                    Tipo.tituloGrande.copy(fontSize = 34.sp, fontWeight = FontWeight.Bold).numeros, pal.texto,
                )
                if (sep.fase == 2 && sep.total > 0) {
                    Texto("${sep.hechos} de ${sep.total}", Tipo.nota.numeros, pal.texto2)
                }
            }
        }
        Spacer(Modifier.height(16.dp))
        Texto(textoFase(sep.fase), Tipo.encabezado, pal.texto)
        Texto(
            when {
                sep.fase != 2 -> "Un momento…"
                sep.faltan >= 0f -> "Faltan ${textoTiempo(sep.faltan)}"
                else -> "Calculando cuánto falta…"
            },
            Tipo.subtitulo.numeros, pal.texto2, Modifier.padding(top = 2.dp),
        )
    }
    PieGrupo(
        "Puedes salir de la app o apagar la pantalla: sigue trabajando y avisa arriba. Mejor con el cargador " +
            "conectado. Si se interrumpe, lo ya hecho queda guardado.",
    )
    BotonRelleno(
        "Cancelar", { Mesa.cancelarTrabajo() }, Modifier.padding(top = 14.dp).fillMaxWidth(), principal = false,
    )
}

@Composable
private fun Lista(c: Cancion) {
    val pal = LocalPaleta.current
    Tarjeta {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Canvas(Modifier.size(30.dp)) {
                drawCircle(pal.verde)
                val t = Path().apply {
                    moveTo(size.width * 0.28f, size.height * 0.52f)
                    lineTo(size.width * 0.44f, size.height * 0.68f)
                    lineTo(size.width * 0.74f, size.height * 0.36f)
                }
                drawPath(t, androidx.compose.ui.graphics.Color.White, style = Stroke(2.6.dp.toPx(), cap = StrokeCap.Round))
            }
            Column(Modifier.padding(start = 12.dp)) {
                Texto("Lista para cantar", Tipo.encabezado, pal.texto)
                Texto("El instrumental y la voz original quedaron debajo de la canción.", Tipo.nota, pal.texto2)
            }
        }
    }
    EncabezadoGrupo("La canción")
    Tarjeta(relleno = PaddingValues(horizontal = 16.dp, vertical = 2.dp)) {
        FilaDato("Tonalidad", nombreTonalidad(c.tonica, c.menor))
        Separador()
        FilaDato("Tempo", if (c.bpm > 0f) String.format(Locale.US, "%.0f BPM", c.bpm) else "No se pudo saber")
        Separador()
        FilaDato("La voz original canta", if (c.partes.isEmpty()) "No se oye" else "${c.partes.size} ${if (c.partes.size == 1) "parte" else "partes"}")
    }
    if (c.escala.isNotEmpty()) {
        EncabezadoGrupo("Notas de la canción")
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            for (n in c.escala) {
                val usada = c.notas.getOrElse(n) { 0f }
                Box(
                    Modifier.weight(1f).heightIn(min = 44.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Canvas(Modifier.matchParentSize()) {
                        drawRoundRect(
                            if (usada > 0.5f) pal.acento else pal.relleno, cornerRadius = CornerRadius(10.dp.toPx()),
                        )
                    }
                    Texto(NOTAS[n], Tipo.subtitulo, if (usada > 0.5f) androidx.compose.ui.graphics.Color.White else pal.texto, lineas = 1)
                }
            }
        }
        PieGrupo("En rojo, las que más canta la voz original. El auto-tune en vivo afinará a estas notas.")
    }
    if (c.partes.isNotEmpty()) {
        EncabezadoGrupo("Dónde canta la voz original")
        Tarjeta {
            GraficaVoz(c, Modifier.fillMaxWidth().height(64.dp))
            Row(Modifier.fillMaxWidth().padding(top = 6.dp)) {
                Texto("0:00", Tipo.leyenda.numeros, pal.texto2)
                Spacer(Modifier.weight(1f))
                Texto(tiempoCorto(Mesa.duracion), Tipo.leyenda.numeros, pal.texto2)
            }
        }
        PieGrupo("La voz original quedó en silencio: súbela en Mezcla si quieres cantar con guía.")
    }
}

/** La voz original a lo largo de la canción; las partes donde canta, resaltadas. */
@Composable
private fun GraficaVoz(c: Cancion, modifier: Modifier) {
    val pal = LocalPaleta.current
    val columnas = 120
    val env = remember(c.instrumental, c.partes) { Mesa.envolventeVoz(columnas) }
    val total = Mesa.duracion.toFloat().coerceAtLeast(1f)
    Canvas(modifier.semantics { contentDescription = "Dónde canta la voz original" }) {
        for ((a, b) in c.partes) {
            drawRect(pal.acento.copy(alpha = 0.12f), Offset(size.width * a / total, 0f), Size(size.width * (b - a) / total, size.height))
        }
        val ancho = size.width / columnas
        for (k in env.indices) {
            val h = (env[k] * size.height).coerceAtLeast(1.dp.toPx())
            val t = (k + 0.5f) / columnas * total
            val canta = c.partes.any { (a, b) -> t in a..b }
            drawRoundRect(
                if (canta) pal.acento else pal.texto3, Offset(k * ancho + ancho * 0.2f, (size.height - h) / 2),
                Size(ancho * 0.6f, h), CornerRadius(ancho * 0.3f),
            )
        }
    }
}

@Composable
private fun FilaDato(etiqueta: String, valor: String) {
    val pal = LocalPaleta.current
    Row(Modifier.fillMaxWidth().heightIn(min = 44.dp), verticalAlignment = Alignment.CenterVertically) {
        Texto(etiqueta, Tipo.cuerpo, pal.texto, Modifier.weight(1f), lineas = 1)
        Spacer(Modifier.width(8.dp))
        Texto(valor, Tipo.cuerpo.numeros, pal.texto2, lineas = 1)
    }
}
