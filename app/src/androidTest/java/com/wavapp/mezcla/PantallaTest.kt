package com.wavapp.mezcla

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** Abre la app como lo haría una persona y usa los controles principales. */
@RunWith(AndroidJUnit4::class)
class PantallaTest {
    @get:Rule
    val regla = createAndroidComposeRule<MainActivity>()

    @Test
    fun abreConPistasDePruebaYReproduce() {
        regla.onNodeWithText("Mesa de Audio").assertIsDisplayed()
        // La primera vez carga las pistas de prueba, como el prototipo.
        regla.waitUntil(20_000) { Mesa.pistas.size >= 4 && !Mesa.ocupada }
        regla.onNodeWithText("▶ Reproducir").performClick()
        regla.waitUntil(5_000) { Mesa.reproduciendo }
        regla.onNodeWithText("❚❚ Pausar").assertIsDisplayed()

        // Mute y solo en la primera pista.
        val id = Mesa.pistas.first().id
        regla.onNodeWithContentDescription("Mute pista $id").performClick()
        regla.waitUntil(2_000) { Mesa.pistas.first().mute }
        regla.onNodeWithContentDescription("Solo pista $id").performClick()
        regla.waitUntil(2_000) { Mesa.pistas.first().solo }

        regla.onNodeWithText("❚❚ Pausar").performClick()
        regla.waitUntil(5_000) { !Mesa.reproduciendo }
        regla.onNodeWithText("■ Detener").performClick()
    }
}
