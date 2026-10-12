package com.wavapp.mezcla

import android.content.res.Configuration
import android.graphics.Bitmap
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.click
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performScrollToIndex
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipe
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.io.FileOutputStream

/** Abre la app como lo haría una persona, usa los controles principales y toma capturas. */
@RunWith(AndroidJUnit4::class)
class PantallaTest {
    @get:Rule
    val regla = createAndroidComposeRule<MainActivity>()

    @Test
    fun abreConPistasDePruebaYReproduce() {
        regla.onNodeWithText("Mesa de Audio").assertIsDisplayed()
        // La primera vez carga las pistas de prueba, como el prototipo.
        regla.waitUntil(20_000) { Mesa.pistas.size >= 4 && !Mesa.ocupada }
        regla.waitForIdle()
        captura("1-inicio")

        regla.onNodeWithText("▶ Reproducir").performClick()
        regla.waitUntil(5_000) { Mesa.reproduciendo }
        regla.onNodeWithText("❚❚ Pausar").assertIsDisplayed()
        avanzarMedidores()
        captura("2-reproduciendo")

        regla.onNode(hasScrollAction()).performScrollToNode(hasText("MASTER"))
        avanzarMedidores()
        captura("3-master")
        regla.onNode(hasScrollAction()).performScrollToNode(hasText("Salida", substring = true))
        avanzarMedidores()
        captura("5-salida")

        // Mute y solo en la primera pista.
        regla.onNode(hasScrollAction()).performScrollToIndex(0)
        val id = Mesa.pistas.first().id
        regla.onNodeWithContentDescription("Mute pista $id").performClick()
        regla.waitUntil(2_000) { Mesa.pistas.first().mute }
        regla.onNodeWithContentDescription("Solo pista $id").performClick()
        regla.waitUntil(2_000) { Mesa.pistas.first().solo }
        avanzarMedidores()
        captura("4-mute-solo")

        // EQ: empieza sin bandas; se agregan dos y se ajustan.
        regla.onNodeWithContentDescription("EQ pista $id").performClick()
        regla.waitUntil(2_000) { Mesa.eqAbierta != null }
        regla.onNodeWithText("+ Banda").performClick()
        regla.onNodeWithText("+ Banda").performClick()
        regla.waitUntil(2_000) { Mesa.pistas.first().bandas.size == 2 }
        regla.runOnUiThread {
            val p = Mesa.pistas.first()
            Mesa.cambiarBanda(p, p.bandas[0], tipo = 1, f = 120f, g = 5f)
            Mesa.cambiarBanda(p, p.bandas[1], f = 3000f, g = -4f, q = 2f)
        }
        regla.waitForIdle()
        captura("6-eq")
        regla.onNodeWithText("Cerrar").performClick()
        regla.waitUntil(2_000) { Mesa.eqAbierta == null }
        regla.onNodeWithContentDescription("EQ pista $id").assertIsDisplayed()

        // Quitar mute y solo para que la mezcla suene en el análisis.
        regla.onNodeWithContentDescription("Mute pista $id").performClick()
        regla.onNodeWithContentDescription("Solo pista $id").performClick()
        regla.waitUntil(2_000) { !Mesa.pistas.first().mute && !Mesa.pistas.first().solo }

        // Análisis de la mezcla mientras suena (la canción de prueba dura 12 s: desde el inicio).
        regla.runOnUiThread {
            Nucleo.seek(0.0)
            Nucleo.play()
        }
        regla.onNodeWithText("Análisis").performClick()
        regla.waitUntil(2_000) { Analisis.abierto }
        avanzarHasta(8_000, {
            "el análisis no arrancó: sonando=${Nucleo.isPlaying()}, posición=${Nucleo.positionSeconds()}, " +
                "salida=${Nucleo.outputInfo().joinToString()}, activo=${Analisis.activo}, fuerte=${Analisis.fuerte}"
        }) { Analisis.activo && Analisis.fuerte }
        avanzarMedidores()
        captura("7-analisis")
        regla.onNodeWithText("60 Hz").performScrollTo().performClick()
        regla.waitUntil(2_000) { Analisis.notch == 60 }
        regla.onNodeWithText("Notch off").performScrollTo().performClick()
        regla.waitUntil(2_000) { Analisis.notch == 0 }
        regla.onNodeWithTag("panelAnalisis").performScrollToNode(hasText("DIAGNÓSTICO", substring = true))
        // Espectrograma de la segunda pista (la voz).
        regla.runOnUiThread { Analisis.fuenteId = Mesa.pistas[1].id }
        regla.waitUntil(20_000) { Analisis.espectrogramas.containsKey(Mesa.pistas[1].id) }
        regla.onNodeWithTag("panelAnalisis").performScrollToNode(hasText("El fondo es silencio", substring = true))
        avanzarMedidores()
        captura("8-espectrograma")
        regla.onNodeWithText("Cerrar").performScrollTo().performClick()
        regla.waitUntil(2_000) { !Analisis.abierto }

        // Edición en la primera pista: elegir, cortar, mover, borrar, deshacer y rehacer.
        regla.runOnUiThread {
            if (Nucleo.isPlaying()) Mesa.reproducirPausar()
            Mesa.saltar(6.0)
        }
        regla.onNodeWithText("Editar").performClick()
        regla.waitUntil(2_000) { Edicion.abierta }
        val p0 = Mesa.pistas.first()
        val original = p0.pedazos
        regla.onNodeWithTag("carril-${p0.id}").performTouchInput { click() }
        regla.waitUntil(2_000) { Edicion.seleccion?.pistaId == p0.id }
        regla.onNodeWithText("✂ Cortar").performClick()
        regla.waitUntil(2_000) { p0.pedazos.size == 2 }
        assertEquals(6L * 48000, p0.pedazos[1].inicio)
        // El pedazo de la derecha quedó elegido: se arrastra a la derecha.
        regla.onNodeWithTag("carril-${p0.id}").performTouchInput {
            swipe(Offset(width * 0.75f, height * 0.7f), Offset(width * 0.9f, height * 0.7f), 400)
        }
        regla.waitUntil(2_000) { p0.pedazos[1].inicio > 6L * 48000 }
        assertTrue(Mesa.duracion > 12.0)
        regla.waitForIdle()
        captura("10-edicion")
        regla.onNodeWithText("Borrar").performClick()
        regla.waitUntil(2_000) { p0.pedazos.size == 1 }
        regla.onNodeWithText("↶ Deshacer").performClick()
        regla.waitUntil(2_000) { p0.pedazos.size == 2 && p0.pedazos[1].inicio > 6L * 48000 }
        regla.onNodeWithText("↶ Deshacer").performClick()
        regla.waitUntil(2_000) { p0.pedazos.size == 2 && p0.pedazos[1].inicio == 6L * 48000 }
        regla.onNodeWithText("↶ Deshacer").performClick()
        regla.waitUntil(2_000) { p0.pedazos == original }
        regla.onNodeWithText("↷ Rehacer").performClick()
        regla.waitUntil(2_000) { p0.pedazos.size == 2 }
        regla.onNodeWithText("↶ Deshacer").performClick()
        regla.waitUntil(2_000) { p0.pedazos == original }
        assertEquals(12.0, Mesa.duracion, 0.001)
        regla.onNodeWithText("Cerrar").performClick()
        regla.waitUntil(2_000) { !Edicion.abierta }

        // Diálogo de exportar (sin abrir el selector de archivos del sistema).
        regla.onNodeWithText("Exportar").performClick()
        regla.onNodeWithText("Exportar la mezcla").assertIsDisplayed()
        regla.onNodeWithText("32 bits float").performClick()
        regla.waitUntil(2_000) { Mesa.exportBits == 32 }
        regla.onNodeWithText("24 bits").performClick()
        regla.waitForIdle()
        capturaPantalla("9-exportar")
        regla.onNodeWithText("Cancelar").performClick()

        if (Mesa.reproduciendo) regla.onNodeWithText("❚❚ Pausar").performClick()
        regla.waitUntil(5_000) { !Mesa.reproduciendo }
        regla.onNodeWithText("■ Detener").performClick()
    }

    /** Avanza cuadros a mano (el bucle de pantalla está pausado en las pruebas) hasta que se cumpla [cond]. */
    private fun avanzarHasta(ms: Long, mensaje: () -> String, cond: () -> Boolean) {
        val fin = System.currentTimeMillis() + ms
        while (System.currentTimeMillis() < fin) {
            regla.runOnUiThread { Mesa.tick(System.currentTimeMillis()) }
            if (cond()) return
            Thread.sleep(33)
        }
        regla.waitForIdle()
        assertTrue(mensaje(), cond())
    }

    /** En las pruebas el bucle de cuadros está pausado: lo avanzamos a mano ~1 s. */
    private fun avanzarMedidores() {
        repeat(30) {
            regla.runOnUiThread { Mesa.tick(System.currentTimeMillis()) }
            Thread.sleep(33)
        }
        regla.waitForIdle()
    }

    /** Captura de la pantalla completa (incluye diálogos), con UiAutomation. */
    private fun capturaPantalla(nombre: String) {
        val dir = InstrumentationRegistry.getArguments().getString("additionalTestOutputDir") ?: return
        val modo = regla.activity.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
        val prefijo = if (modo == Configuration.UI_MODE_NIGHT_YES) "oscuro-" else "claro-"
        try {
            val bmp = InstrumentationRegistry.getInstrumentation().uiAutomation.takeScreenshot() ?: return
            File(dir).mkdirs()
            FileOutputStream(File(dir, "$prefijo$nombre.png")).use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        } catch (e: Exception) {
            println("No se pudo guardar la captura $nombre: $e")
        }
    }

    /** Guarda una captura donde Gradle la recoge (build/outputs/connected_android_test_additional_output). */
    private fun captura(nombre: String) {
        val dir = InstrumentationRegistry.getArguments().getString("additionalTestOutputDir") ?: return
        val modo = regla.activity.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
        val prefijo = if (modo == Configuration.UI_MODE_NIGHT_YES) "oscuro-" else "claro-"
        try {
            val bmp = regla.onRoot().captureToImage().asAndroidBitmap()
            File(dir).mkdirs()
            FileOutputStream(File(dir, "$prefijo$nombre.png")).use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        } catch (e: Exception) {
            println("No se pudo guardar la captura $nombre: $e")
        }
    }
}
