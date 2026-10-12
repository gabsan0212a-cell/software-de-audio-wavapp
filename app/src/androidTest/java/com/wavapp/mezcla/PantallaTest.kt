package com.wavapp.mezcla

import android.content.res.Configuration
import android.graphics.Bitmap
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.isRoot
import androidx.compose.ui.test.onLast
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToIndex
import androidx.compose.ui.test.performScrollToNode
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
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

        // Análisis de la mezcla mientras suena.
        regla.onNodeWithText("Análisis").performClick()
        regla.waitUntil(2_000) { Analisis.abierto }
        avanzarMedidores()
        regla.waitUntil(5_000) { Analisis.activo }
        captura("7-analisis")
        regla.onNodeWithText("60 Hz").performClick()
        regla.waitUntil(2_000) { Analisis.notch == 60 }
        regla.onNodeWithText("Notch off").performClick()
        // Espectrograma de la segunda pista (la voz).
        regla.runOnUiThread { Analisis.fuenteId = Mesa.pistas[1].id }
        regla.waitUntil(20_000) { Analisis.espectrogramas.containsKey(Mesa.pistas[1].id) }
        regla.onNodeWithTag("panelAnalisis").performScrollToNode(hasText("ESPECTROGRAMA", substring = true))
        avanzarMedidores()
        captura("8-espectrograma")
        regla.onNodeWithText("Cerrar").performClick()
        regla.waitUntil(2_000) { !Analisis.abierto }

        // Diálogo de exportar (sin abrir el selector de archivos del sistema).
        regla.onNodeWithText("Exportar").performClick()
        regla.onNodeWithText("Exportar la mezcla").assertIsDisplayed()
        regla.onNodeWithText("32 bits float").performClick()
        regla.waitUntil(2_000) { Mesa.exportBits == 32 }
        regla.onNodeWithText("24 bits").performClick()
        captura("9-exportar")
        regla.onNodeWithText("Cancelar").performClick()

        regla.onNodeWithText("❚❚ Pausar").performClick()
        regla.waitUntil(5_000) { !Mesa.reproduciendo }
        regla.onNodeWithText("■ Detener").performClick()
    }

    /** En las pruebas el bucle de cuadros está pausado: lo avanzamos a mano ~1 s. */
    private fun avanzarMedidores() {
        repeat(30) {
            regla.runOnUiThread { Mesa.tick(System.currentTimeMillis()) }
            Thread.sleep(33)
        }
        regla.waitForIdle()
    }

    /** Guarda una captura donde Gradle la recoge (build/outputs/connected_android_test_additional_output). */
    private fun captura(nombre: String) {
        val dir = InstrumentationRegistry.getArguments().getString("additionalTestOutputDir") ?: return
        val modo = regla.activity.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
        val prefijo = if (modo == Configuration.UI_MODE_NIGHT_YES) "oscuro-" else "claro-"
        try {
            // La última raíz es la ventana de encima (p. ej. un diálogo).
            val bmp = regla.onAllNodes(isRoot()).onLast().captureToImage().asAndroidBitmap()
            File(dir).mkdirs()
            FileOutputStream(File(dir, "$prefijo$nombre.png")).use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        } catch (e: Exception) {
            println("No se pudo guardar la captura $nombre: $e")
        }
    }
}
