package com.wavapp.mezcla.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asComposeImageBitmap
import androidx.compose.ui.graphics.toComposeImageBitmap
import org.jetbrains.skia.Bitmap
import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.Image
import org.jetbrains.skia.ImageInfo
import java.io.File

// Versión de escritorio de Plataforma.kt (solo para la vista previa).

@Composable
internal fun AlRegresar(activo: Boolean, accion: () -> Unit) {}

@Composable
internal fun PantallaEncendida(encendida: Boolean) {}

@Composable
internal fun BarraDeEstadoClara(activa: Boolean, temaOscuro: Boolean) {}

@Composable
internal fun recordarAbrirArchivos(): () -> Unit = {}

@Composable
internal fun recordarGuardarMezcla(): () -> Unit = {}

@Composable
internal fun recordarGuardarPresentacion(): () -> Unit = {}

/** En la vista previa no hay reportes de errores, salvo el de prueba de la escena "error". */
var errorDePrueba by mutableStateOf<String?>(null)

@Composable
internal fun recordarUltimoError(): UltimoError = UltimoError(errorDePrueba) { errorDePrueba = null }

// La misma imagen que la app (se corre desde tests/previa).
private val portada: ImageBitmap by lazy {
    Image.makeFromEncoded(File("../../app/src/main/res/drawable-nodpi/portada.webp").readBytes()).toComposeImageBitmap()
}

@Composable
internal fun imagenPortada(): ImageBitmap = portada

internal fun imagenDePixeles(px: IntArray, ancho: Int, alto: Int): ImageBitmap {
    val bmp = Bitmap()
    bmp.allocPixels(ImageInfo(ancho, alto, ColorType.BGRA_8888, ColorAlphaType.PREMUL))
    val bytes = ByteArray(px.size * 4)
    for (i in px.indices) {
        val c = px[i]
        bytes[4 * i] = (c and 0xFF).toByte()
        bytes[4 * i + 1] = ((c shr 8) and 0xFF).toByte()
        bytes[4 * i + 2] = ((c shr 16) and 0xFF).toByte()
        bytes[4 * i + 3] = ((c ushr 24) and 0xFF).toByte()
    }
    bmp.installPixels(bytes)
    return bmp.asComposeImageBitmap()
}

// Una entrada simulada (la de la vista previa genera una voz de prueba) y audífonos con cable.
@Composable
internal fun recordarAudio(): EstadoAudio =
    EstadoAudio(listOf(EntradaAudio(1, "Celular", true), EntradaAudio(2, "USB · Interfaz", true)), Salida.CABLE)

@Composable
internal fun recordarPermisoMicrofono(): PermisoMicrofono = PermisoMicrofono(true, false) {}

@Composable
internal fun recordarPermisoAvisos(): () -> Unit = {}

@Composable
internal fun recordarEscucharCelular(): EscuchaCelular =
    EscuchaCelular(true, { CelularDePrueba.empezar() }, { CelularDePrueba.detener() })

/**
 * En la vista previa no hay otras apps: "lo que suena en el celular" es una canción de prueba
 * (bombo, bajo, acordes y hi-hats a 85 BPM en Sol menor) que se manda al núcleo a tiempo real.
 */
internal object CelularDePrueba {
    @Volatile private var seguir = false
    private var hilo: Thread? = null

    fun empezar() {
        if (seguir) return
        seguir = true
        com.wavapp.mezcla.Nucleo.mediaOpened(2)
        hilo = Thread {
            val n = 960
            val buf = ShortArray(2 * n)
            var t = 0L
            val inicio = System.nanoTime()
            val rnd = java.util.Random(7)
            while (seguir) {
                for (i in 0 until n) {
                    val (l, r) = muestra(t + i, rnd)
                    buf[2 * i] = (l.coerceIn(-1.0, 1.0) * 32767).toInt().toShort()
                    buf[2 * i + 1] = (r.coerceIn(-1.0, 1.0) * 32767).toInt().toShort()
                }
                com.wavapp.mezcla.Nucleo.mediaPush(buf, n)
                t += n
                val falta = inicio + t * 1_000_000_000L / 48000 - System.nanoTime()
                if (falta > 0) Thread.sleep(falta / 1_000_000)
            }
        }.apply {
            isDaemon = true
            start()
        }
    }

    fun detener() {
        seguir = false
        hilo?.join()
        hilo = null
        com.wavapp.mezcla.Nucleo.mediaClosed()
    }

    private fun hz(midi: Int) = 440.0 * Math.pow(2.0, (midi - 69) / 12.0)

    private fun muestra(f: Long, rnd: java.util.Random): Pair<Double, Double> {
        val sr = 48000.0
        val pulso = sr * 60 / 85
        val s = f / sr
        val enPulso = (f % pulso.toLong()) / sr
        val compas = (f / (pulso * 4)).toInt() % 4
        val acordes = listOf(listOf(55, 58, 62), listOf(48, 51, 55), listOf(58, 62, 65), listOf(51, 55, 58))
        val acorde = acordes[compas]
        var l = 0.0
        var r = 0.0
        // Bombo en cada pulso.
        val k = Math.exp(-enPulso * 18) * Math.sin(2 * Math.PI * (50 * enPulso + 60 * (1 - Math.exp(-enPulso * 30)) / 30))
        l += 0.5 * k
        r += 0.5 * k
        // Bajo: la fundamental dos octavas abajo.
        val b = 0.18 * Math.sin(2 * Math.PI * hz(acorde[0] - 24) * s)
        l += b
        r += b
        // Acordes (un poco desafinados entre lados: estéreo) con armónicos.
        for (nota in acorde) {
            for (h in 1..4) {
                val a = 0.05 / h
                l += a * Math.sin(2 * Math.PI * hz(nota) * h * 1.001 * s)
                r += a * Math.sin(2 * Math.PI * hz(nota) * h * 0.999 * s)
            }
        }
        // Hi-hat a contratiempo, a la derecha.
        val medio = ((f + (pulso / 2).toLong()) % pulso.toLong()) / sr
        val hh = Math.exp(-medio * 60) * (rnd.nextDouble() * 2 - 1) * 0.12
        l += 0.4 * hh
        r += hh
        return l to r
    }
}
