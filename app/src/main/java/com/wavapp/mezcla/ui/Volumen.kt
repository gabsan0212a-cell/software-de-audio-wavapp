package com.wavapp.mezcla.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import java.util.Locale
import kotlin.math.abs

// El volumen percibido (LUFS) lo calcula el núcleo; aquí solo se muestra.

private val OBJETIVOS = listOf(-14, -16, -23)

/** "−14.0" o "—" si no hay dato. */
internal fun textoLufs(v: Float): String =
    if (v <= Nucleo.SIN_DATO + 1f) "—" else String.format(Locale.US, "%.1f", v).replace('-', '−')

/** "−14" (con el signo tipográfico). */
internal fun textoObjetivo(lufs: Int): String = if (lufs < 0) "−${-lufs}" else "$lufs"

/** Para quién es cada objetivo. */
internal fun nombreObjetivo(lufs: Int): String = when (lufs) {
    -16 -> "Apple Music"
    -23 -> "la radio y la TV"
    else -> "Spotify y YouTube"
}

/** Qué significa el número frente al objetivo (veredicto del núcleo). */
internal fun textoVeredictoLufs(codigo: Int, diferencia: Float, objetivo: Int): String {
    val d = String.format(Locale.US, "%.1f", abs(diferencia))
    val quien = nombreObjetivo(objetivo)
    return when (codigo) {
        2 -> "En el objetivo de $quien (${textoObjetivo(objetivo)} LUFS)."
        3 -> "$d dB más fuerte que ${textoObjetivo(objetivo)} LUFS: en $quien la bajarán (sonar más fuerte no gana nada)."
        1 -> "$d dB más baja que ${textoObjetivo(objetivo)} LUFS: en $quien sonará más bajita que otras canciones. " +
            "Puedes ajustarla al exportar."
        else -> "Reproduce desde el inicio para medir la canción."
    }
}

@Composable
private fun colorVeredicto(codigo: Int): Color {
    val p = LocalPaleta.current
    return when (codigo) {
        2 -> p.verde
        1, 3 -> p.naranja
        else -> p.texto2
    }
}

/** Renglón del master en Mezcla: integrado y corto plazo; tocarlo lleva al análisis. */
@Composable
fun FilaLufs(alTocar: () -> Unit) {
    val p = LocalPaleta.current
    val v = Mesa.volumen
    Row(
        Modifier.fillMaxWidth().heightIn(min = 52.dp).semantics { contentDescription = "Volumen percibido" }
            .pulsable(alTocar = alTocar),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Texto("Volumen percibido", Tipo.cuerpo, p.texto, lineas = 1)
            val corto = when (v.veredicto) {
                2 -> "En el objetivo (${textoObjetivo(Mesa.objetivoLufs)})"
                3 -> "Fuerte para ${nombreObjetivo(Mesa.objetivoLufs)}"
                1 -> "Bajo para ${nombreObjetivo(Mesa.objetivoLufs)}"
                else -> "Reproduce para medir"
            }
            Texto(corto, Tipo.nota, colorVeredicto(v.veredicto), lineas = 1)
        }
        Column(horizontalAlignment = Alignment.End) {
            Texto("${textoLufs(v.integrado)} LUFS", Tipo.subtitulo.numeros.seminegrita, p.texto, lineas = 1)
            Texto("corto ${textoLufs(v.corto)}", Tipo.nota.numeros, p.texto2, lineas = 1)
        }
        Icono(Iconos.chevronDerecha, p.texto3, Modifier.padding(start = 6.dp), tamano = 16.dp)
    }
}

/** Sección de Análisis: números, historia de 30 s, objetivo y la canción completa. */
@Composable
fun TarjetaLufs() {
    val p = LocalPaleta.current
    val v = Mesa.volumen
    Tarjeta(Modifier.padding(horizontal = 16.dp).testTag("tarjetaLufs")) {
        Row(Modifier.fillMaxWidth()) {
            Numero("Momentáneo", v.momentaneo, Modifier.weight(1f))
            Numero("Corto plazo", v.corto, Modifier.weight(1f))
            Numero("Integrado", v.integrado, Modifier.weight(1f), grande = true)
        }
        Spacer(Modifier.height(12.dp))
        GraficaLufs(Modifier.fillMaxWidth().height(120.dp).clip(RoundedCornerShape(10.dp)))
        Row(Modifier.padding(top = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Texto(
                "Rango ${textoLufs(v.rango)} LU · pico real ${textoLufs(v.picoReal)} dBTP", Tipo.nota.numeros, p.texto2,
                Modifier.weight(1f), lineas = 2,
            )
            BotonTexto("Reiniciar", { Mesa.reiniciarLufs() }, estilo = Tipo.subtitulo, habilitado = v.segundos > 0f)
        }
        Texto(
            textoVeredictoLufs(v.veredicto, v.diferencia, Mesa.objetivoLufs), Tipo.subtitulo, colorVeredicto(v.veredicto),
            Modifier.padding(top = 4.dp),
        )
        Spacer(Modifier.height(12.dp))
        Segmentado(
            listOf("−14 Spotify", "−16 Apple", "−23 TV"), OBJETIVOS.indexOf(Mesa.objetivoLufs).coerceAtLeast(0),
            { Mesa.ponerObjetivoLufs(OBJETIVOS[it]) }, Modifier.fillMaxWidth(),
        )
    }
}

/** La canción completa, medida sin tener que escucharla. */
@Composable
fun TarjetaLufsCancion() {
    val p = LocalPaleta.current
    val v = Mesa.volumen
    Tarjeta(Modifier.padding(horizontal = 16.dp).testTag("lufsCancion")) {
        when (v.cancion) {
            0 -> {
                Texto("Mide la canción entera en unos segundos, sin tener que escucharla.", Tipo.subtitulo, p.texto2)
                BotonRelleno(
                    "Medir la canción completa", { Mesa.medirCancion() }, Modifier.padding(top = 12.dp).fillMaxWidth(),
                    principal = false, habilitado = !Mesa.ocupada && Mesa.pistas.isNotEmpty(),
                )
            }
            1 -> {
                Texto("Midiendo la canción…", Tipo.subtitulo, p.texto)
                if (Mesa.progreso >= 0f) BarraProgreso(Mesa.progreso, Modifier.padding(top = 10.dp))
            }
            else -> {
                Row(Modifier.fillMaxWidth()) {
                    Numero("Integrado", v.cancionIntegrado, Modifier.weight(1f), grande = true)
                    Numero("Rango (LU)", v.cancionRango, Modifier.weight(1f))
                    Numero("Pico real", v.cancionPico, Modifier.weight(1f))
                }
                GraficaCancion(Modifier.padding(top = 12.dp).fillMaxWidth().height(80.dp).clip(RoundedCornerShape(10.dp)))
                Texto(
                    textoVeredictoLufs(v.cancionVeredicto, v.cancionDiferencia, Mesa.objetivoLufs), Tipo.subtitulo,
                    colorVeredicto(v.cancionVeredicto), Modifier.padding(top = 8.dp),
                )
                if (v.cancion == 3) {
                    Texto("La mezcla cambió después de medir.", Tipo.nota, p.texto2, Modifier.padding(top = 4.dp))
                }
                BotonTexto(
                    "Medir otra vez", { Mesa.medirCancion() }, Modifier.padding(top = 2.dp), estilo = Tipo.subtitulo,
                    habilitado = !Mesa.ocupada,
                )
            }
        }
    }
}

@Composable
private fun Numero(etiqueta: String, valor: Float, modifier: Modifier, grande: Boolean = false) {
    val p = LocalPaleta.current
    Column(modifier) {
        Texto(etiqueta, Tipo.nota, p.texto2, lineas = 1)
        Texto(
            textoLufs(valor), (if (grande) Tipo.titulo2 else Tipo.titulo3).numeros, p.texto, lineas = 1,
            alineacion = TextAlign.Start,
        )
    }
}

private const val ARRIBA = 0f
private const val ABAJO = -40f

/** Rejilla de −10, −20 y −30 LUFS y la franja del objetivo (±1). */
private fun DrawScope.fondoLufs(p: Paleta, objetivo: Int, y: (Float) -> Float) {
    drawRect(if (p.oscuro) Color(0xFF0B0B0C) else Color(0xFFF6F6F8))
    for (db in listOf(-10f, -20f, -30f)) drawLine(p.separador, Offset(0f, y(db)), Offset(size.width, y(db)), density)
    val y1 = y(objetivo + 1f)
    drawRect(p.verde.copy(alpha = 0.14f), Offset(0f, y1), Size(size.width, y(objetivo - 1f) - y1))
    drawLine(
        p.verde, Offset(0f, y(objetivo.toFloat())), Offset(size.width, y(objetivo.toFloat())), 1.2f * density,
        pathEffect = PathEffect.dashPathEffect(floatArrayOf(5 * density, 4 * density)),
    )
}

private fun linea(valores: FloatArray, x: (Int) -> Float, y: (Float) -> Float): Path {
    val path = Path()
    var dentro = false
    for (i in valores.indices) {
        val v = valores[i]
        if (v <= Nucleo.SIN_DATO + 1f) {
            dentro = false
            continue
        }
        val py = y(v.coerceIn(ABAJO, ARRIBA))
        if (dentro) path.lineTo(x(i), py) else path.moveTo(x(i), py)
        dentro = true
    }
    return path
}

/** Los últimos 30 s: momentáneo (fino) y corto plazo (grueso). Se lee al dibujar. */
@Composable
private fun GraficaLufs(modifier: Modifier) {
    val p = LocalPaleta.current
    val medidor = rememberTextMeasurer()
    val estilo = TextStyle(color = p.texto2, fontSize = 9.sp)
    val m = FloatArray(300)
    val c = FloatArray(300)
    Canvas(modifier.semantics { contentDescription = "Historia del volumen percibido" }) {
        @Suppress("UNUSED_VARIABLE") val cambio = Mesa.volumen.segundos  // redibujar con cada bloque nuevo
        val h = size.height
        fun y(db: Float) = (ARRIBA - db) / (ARRIBA - ABAJO) * h
        fun x(i: Int) = i / (m.size - 1f) * size.width
        fondoLufs(p, Mesa.objetivoLufs, ::y)
        for (db in listOf(-10, -20, -30)) {
            drawText(medidor, textoObjetivo(db), Offset(4 * density, y(db.toFloat()) - 12 * density), estilo)
        }
        Nucleo.loudnessHistory(m, c)
        drawPath(linea(m, ::x, ::y), p.texto2.copy(alpha = 0.55f), style = Stroke(1.2f * density))
        drawPath(linea(c, ::x, ::y), p.acento, style = Stroke(2.5f * density, join = StrokeJoin.Round))
    }
}

/** La canción completa (corto plazo) con el cursor; tocar salta a ese momento. */
@Composable
private fun GraficaCancion(modifier: Modifier) {
    val p = LocalPaleta.current
    Canvas(
        modifier
            .semantics { contentDescription = "Volumen a lo largo de la canción. Toca para mover el cursor." }
            .pointerInput(Unit) {
                detectTapGestures { o ->
                    val d = Mesa.duracion
                    if (d > 0) Mesa.saltar(o.x / size.width * d)
                }
            },
    ) {
        val h = size.height
        fun y(db: Float) = (ARRIBA - db) / (ARRIBA - ABAJO) * h
        val curva = Mesa.volumen.curva
        fondoLufs(p, Mesa.objetivoLufs, ::y)
        if (curva.isNotEmpty()) {
            fun x(i: Int) = (i + 0.5f) / curva.size * size.width
            drawPath(linea(curva, ::x, ::y), p.acento, style = Stroke(2f * density, join = StrokeJoin.Round))
        }
        val d = Mesa.duracion
        if (d > 0) {
            val px = (Mesa.posicion / d * size.width).toFloat()
            drawRect(p.acento.copy(alpha = 0.7f), Offset(px - density, 0f), Size(2 * density, h))
        }
    }
}

/** Para el renglón de ajuste en la hoja de exportar. */
@Composable
fun AjusteLufsExportar() {
    val p = LocalPaleta.current
    Tarjeta(relleno = PaddingValues(horizontal = 16.dp, vertical = 2.dp)) {
        FilaInterruptor("Ajustar el volumen", Mesa.ajustarAlExportar, { Mesa.ponerAjusteAlExportar(it) }) {
            Texto(
                "A ${textoObjetivo(Mesa.objetivoLufs)} LUFS (${nombreObjetivo(Mesa.objetivoLufs)})", Tipo.nota, p.texto2,
                lineas = 1,
            )
        }
        if (Mesa.ajustarAlExportar) {
            Separador()
            Segmentado(
                listOf("−14", "−16", "−23"), OBJETIVOS.indexOf(Mesa.objetivoLufs).coerceAtLeast(0),
                { Mesa.ponerObjetivoLufs(OBJETIVOS[it]) }, Modifier.fillMaxWidth().padding(vertical = 10.dp),
            )
        }
    }
}
