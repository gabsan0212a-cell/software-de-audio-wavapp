package com.wavapp.mezcla

import android.graphics.Bitmap
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
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

        // Mute y solo en la primera pista.
        val id = Mesa.pistas.first().id
        regla.onNodeWithContentDescription("Mute pista $id").performClick()
        regla.waitUntil(2_000) { Mesa.pistas.first().mute }
        regla.onNodeWithContentDescription("Solo pista $id").performClick()
        regla.waitUntil(2_000) { Mesa.pistas.first().solo }
        avanzarMedidores()
        captura("3-mute-solo")

        regla.onNode(hasScrollAction()).performScrollToNode(hasText("MASTER"))
        avanzarMedidores()
        captura("4-master")

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
        try {
            val bmp = regla.onRoot().captureToImage().asAndroidBitmap()
            File(dir).mkdirs()
            FileOutputStream(File(dir, "$nombre.png")).use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        } catch (e: Exception) {
            println("No se pudo guardar la captura $nombre: $e")
        }
    }
}
