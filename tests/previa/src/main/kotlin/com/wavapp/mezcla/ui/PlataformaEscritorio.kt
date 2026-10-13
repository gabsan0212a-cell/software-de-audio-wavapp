package com.wavapp.mezcla.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asComposeImageBitmap
import org.jetbrains.skia.Bitmap
import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.ImageInfo

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
