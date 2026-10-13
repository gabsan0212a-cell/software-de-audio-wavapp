package com.wavapp.mezcla

import android.Manifest
import android.content.res.Configuration
import android.graphics.Bitmap
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.click
import androidx.compose.ui.test.doubleClick
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToIndex
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipe
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.rule.GrantPermissionRule
import com.wavapp.mezcla.ui.Vista
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
    @get:Rule(order = 0)
    val microfono: GrantPermissionRule = GrantPermissionRule.grant(Manifest.permission.RECORD_AUDIO)

    @get:Rule(order = 1)
    val regla = createAndroidComposeRule<MainActivity>()

    @Test
    fun abreConPistasDePruebaYReproduce() {
        regla.onNodeWithTag("pestana-0").assertIsDisplayed()
        // La primera vez carga las pistas de prueba, como el prototipo.
        regla.waitUntil(20_000) { Mesa.pistas.size >= 4 && !Mesa.ocupada }
        regla.waitForIdle()
        captura("1-inicio")

        // Mini reproductor: reproducir.
        regla.onNodeWithContentDescription("Reproducir").performClick()
        regla.waitUntil(5_000) { Mesa.reproduciendo }
        regla.onNodeWithContentDescription("Pausar").assertIsDisplayed()
        avanzarMedidores()
        captura("2-reproduciendo")

        val lista = regla.onNodeWithTag("listaMezcla")
        lista.performScrollToNode(hasText("Limitador"))
        avanzarMedidores()
        captura("3-master")
        lista.performScrollToNode(hasText("Salida", substring = true))
        avanzarMedidores()
        captura("5-salida")

        // Mute y solo en la primera pista.
        lista.performScrollToIndex(0)
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
        regla.onNodeWithContentDescription("Agregar banda").performClick()
        regla.onNodeWithContentDescription("Agregar banda").performClick()
        regla.waitUntil(2_000) { Mesa.pistas.first().bandas.size == 2 }
        regla.runOnUiThread {
            val p = Mesa.pistas.first()
            Mesa.cambiarBanda(p, p.bandas[0], tipo = 1, f = 120f, g = 5f)
            Mesa.cambiarBanda(p, p.bandas[1], f = 3000f, g = -4f, q = 2f)
        }
        regla.waitForIdle()
        captura("6-eq")
        regla.onNodeWithText("Listo").performClick()
        regla.waitUntil(2_000) { Mesa.eqAbierta == null }
        regla.waitForIdle()
        regla.onNodeWithContentDescription("EQ pista $id").assertIsDisplayed()

        // Quitar mute y solo para que la mezcla suene en el análisis.
        regla.onNodeWithContentDescription("Mute pista $id").performClick()
        regla.onNodeWithContentDescription("Solo pista $id").performClick()
        regla.waitUntil(2_000) { !Mesa.pistas.first().mute && !Mesa.pistas.first().solo }

        // Reproductor a pantalla completa (se abre tocando el mini reproductor).
        regla.onNodeWithTag("miniReproductor").performTouchInput { click(Offset(width * 0.35f, height / 2f)) }
        regla.waitUntil(2_000) { Vista.reproductor }
        regla.waitForIdle()
        avanzarMedidores()
        captura("11-reproductor")
        regla.onNodeWithContentDescription("Cerrar reproductor").performClick()
        regla.waitUntil(2_000) { !Vista.reproductor }
        regla.waitForIdle()

        // Análisis de la mezcla mientras suena (la canción de prueba dura 12 s: desde el inicio).
        regla.runOnUiThread {
            Nucleo.seek(0.0)
            Nucleo.play()
        }
        regla.onNodeWithTag("pestana-2").performClick()
        regla.waitUntil(2_000) { Analisis.abierto }
        avanzarHasta(8_000, {
            "el análisis no arrancó: sonando=${Nucleo.isPlaying()}, posición=${Nucleo.positionSeconds()}, " +
                "salida=${Nucleo.outputInfo().joinToString()}, activo=${Analisis.activo}, fuerte=${Analisis.fuerte}"
        }) { Analisis.activo && Analisis.fuerte }
        avanzarMedidores()
        captura("7-analisis")
        val panel = regla.onNodeWithTag("panelAnalisis")
        panel.performScrollToNode(hasText("60 Hz"))
        regla.onNodeWithText("60 Hz").performClick()
        regla.waitUntil(2_000) { Analisis.notch == 60 }
        regla.onNodeWithText("Sin notch").performClick()
        regla.waitUntil(2_000) { Analisis.notch == 0 }
        // Espectrograma de la segunda pista (la voz).
        regla.runOnUiThread { Analisis.fuenteId = Mesa.pistas[1].id }
        regla.waitUntil(20_000) { Analisis.espectrogramas.containsKey(Mesa.pistas[1].id) }
        panel.performScrollToNode(hasText("El fondo es silencio", substring = true))
        avanzarMedidores()
        captura("8-espectrograma")

        // Edición en la primera pista: elegir, cortar, mover, borrar, deshacer y rehacer.
        regla.runOnUiThread {
            if (Nucleo.isPlaying()) Mesa.reproducirPausar()
            Mesa.saltar(6.0)
        }
        regla.onNodeWithTag("pestana-1").performClick()
        regla.waitUntil(2_000) { Edicion.abierta }
        val p0 = Mesa.pistas.first()
        val original = p0.pedazos
        regla.onNodeWithTag("carril-${p0.id}").performTouchInput { click() }
        regla.waitUntil(2_000) { Edicion.seleccion?.pistaId == p0.id }
        regla.onNodeWithContentDescription("Cortar").performClick()
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
        regla.onNodeWithContentDescription("Borrar").performClick()
        regla.waitUntil(2_000) { p0.pedazos.size == 1 }
        regla.onNodeWithContentDescription("Deshacer").performClick()
        regla.waitUntil(2_000) { p0.pedazos.size == 2 && p0.pedazos[1].inicio > 6L * 48000 }
        regla.onNodeWithContentDescription("Deshacer").performClick()
        regla.waitUntil(2_000) { p0.pedazos.size == 2 && p0.pedazos[1].inicio == 6L * 48000 }
        regla.onNodeWithContentDescription("Deshacer").performClick()
        regla.waitUntil(2_000) { p0.pedazos == original }
        regla.onNodeWithContentDescription("Rehacer").performClick()
        regla.waitUntil(2_000) { p0.pedazos.size == 2 }
        regla.onNodeWithContentDescription("Deshacer").performClick()
        regla.waitUntil(2_000) { p0.pedazos == original }
        assertEquals(12.0, Mesa.duracion, 0.001)

        // Automatización dibujada: tocar el carril pone un punto, arrastrarlo lo mueve, dos toques
        // lo quitan, y todo se puede deshacer.
        regla.onAllNodesWithText("Volumen").onFirst().performClick()
        regla.waitUntil(2_000) { Edicion.vista == Edicion.VOLUMEN }
        val carril = regla.onNodeWithTag("auto-${p0.id}")
        carril.performTouchInput { click(Offset(width * 0.25f, height * 0.5f)) }
        regla.waitUntil(2_000) { p0.autoVol.n == 1 }
        val antes = p0.autoVol.cuadros[0]
        carril.performTouchInput { swipe(Offset(width * 0.25f, height * 0.5f), Offset(width * 0.4f, height * 0.25f), 400) }
        regla.waitUntil(2_000) { p0.autoVol.cuadros[0] > antes }
        assertTrue(p0.autoVol.valores[0] > -6f)
        carril.performTouchInput { click(Offset(width * 0.75f, height * 0.7f)) }
        regla.waitUntil(2_000) { p0.autoVol.n == 2 }
        regla.waitForIdle()
        captura("17-automatizacion")
        carril.performTouchInput { doubleClick(Offset(width * 0.75f, height * 0.7f)) }
        regla.waitUntil(2_000) { p0.autoVol.n == 1 }
        regla.onNodeWithContentDescription("Deshacer").performClick()
        regla.waitUntil(2_000) { p0.autoVol.n == 2 }
        regla.onNodeWithContentDescription("Deshacer").performClick()
        regla.onNodeWithContentDescription("Deshacer").performClick()
        regla.waitUntil(2_000) { p0.autoVol.n == 1 && p0.autoVol.cuadros[0] == antes }
        regla.onNodeWithContentDescription("Deshacer").performClick()
        regla.waitUntil(2_000) { p0.autoVol.n == 0 }
        regla.onAllNodesWithText("Pedazos").onFirst().performClick()
        regla.waitUntil(2_000) { Edicion.vista == Edicion.PEDAZOS }
        regla.onNodeWithTag("pestana-0").performClick()
        regla.waitUntil(2_000) { !Edicion.abierta }

        // Grabar movimientos: botón A → Grabar, y mover el volumen mientras suena.
        regla.onNodeWithContentDescription("Automatización pista $id").performClick()
        regla.waitUntil(2_000) { Vista.automatizacion != null }
        regla.onNode(hasText("Grabar") and hasAnyAncestor(hasTestTag("modoAuto"))).performClick()
        regla.waitUntil(2_000) { p0.autoModo == Nucleo.AUTO_GRABAR }
        regla.waitForIdle()
        captura("18-hoja-automatizacion")
        regla.onNodeWithText("Listo").performClick()
        regla.waitUntil(2_000) { Vista.automatizacion == null }
        regla.runOnUiThread {
            Mesa.saltar(1.0)
            if (!Mesa.reproduciendo) Mesa.reproducirPausar()
        }
        regla.waitUntil(5_000) { Mesa.reproduciendo }
        val vol = regla.onNodeWithContentDescription("Volumen pista $id")
        vol.performTouchInput { down(Offset(width * 0.85f, height / 2f)) }
        for (k in 1..30) {
            vol.performTouchInput { moveTo(Offset(width * (0.85f - 0.015f * k), height / 2f)) }
            regla.runOnUiThread { Mesa.tick(System.currentTimeMillis()) }
            Thread.sleep(40)
        }
        assertTrue("no se estaba grabando el volumen", p0.grabando(Nucleo.AUTO_VOLUMEN))
        captura("19-grabando-movimientos")
        vol.performTouchInput { up() }
        regla.waitUntil(2_000) { p0.autoVol.n >= 2 }
        avanzarHasta(3_000, { "el volumen no sigue lo grabado" }) { p0.sigue(Nucleo.AUTO_VOLUMEN) }
        avanzarMedidores()
        captura("20-movimientos-grabados")
        regla.runOnUiThread {
            Mesa.ponerModoAuto(p0, Nucleo.AUTO_LEER)
            Edicion.verAuto(Nucleo.AUTO_VOLUMEN)
            Vista.pestana = Vista.EDITAR
        }
        regla.waitUntil(2_000) { Edicion.abierta }
        regla.waitForIdle()
        captura("21-automatizacion-grabada")
        regla.runOnUiThread {
            Mesa.borrarAuto(p0, Nucleo.AUTO_VOLUMEN)
            Edicion.vista = Edicion.PEDAZOS
            Vista.pestana = Vista.MEZCLA
        }
        regla.waitUntil(2_000) { p0.autoVol.n == 0 && !Edicion.abierta }

        // Hoja de exportar (sin abrir el selector de archivos del sistema).
        regla.onNodeWithContentDescription("Exportar").performClick()
        regla.onNodeWithText("Exportar la mezcla").assertIsDisplayed()
        regla.onNodeWithText("32 bits float").performClick()
        regla.waitUntil(2_000) { Mesa.exportBits == 32 }
        regla.onNodeWithText("24 bits").performClick()
        regla.waitUntil(2_000) { Mesa.exportBits == 24 }
        regla.waitForIdle()
        captura("9-exportar")
        regla.onNodeWithText("Cancelar").performClick()
        regla.waitUntil(2_000) { !Vista.exportar }

        // Menú de una pista y la alerta para quitarla (se cancela).
        regla.onNodeWithContentDescription("Opciones pista $id").performClick()
        regla.onNodeWithText("Quitar pista").assertIsDisplayed()
        captura("12-opciones")
        regla.onNodeWithText("Quitar pista").performClick()
        regla.onNodeWithText("¿Quitar la pista $id", substring = true).assertIsDisplayed()
        captura("13-quitar")
        regla.onNodeWithText("Cancelar").performClick()
        regla.waitUntil(2_000) { Vista.aQuitar == null }
        assertEquals(4, Mesa.pistas.size)

        // Menú de agregar (sin abrir el selector del sistema).
        regla.onNodeWithContentDescription("Agregar pistas").performClick()
        regla.onNodeWithText("Agregar archivos…").assertIsDisplayed()
        regla.onNodeWithText("Cancelar").performClick()
        regla.waitUntil(2_000) { !Vista.agregar }

        // Grabar: la pestaña abre la entrada; se graba un poco con la mezcla y queda una pista nueva.
        regla.onNodeWithTag("pestana-3").performClick()
        regla.waitUntil(5_000) { Mesa.entrada.abierta }
        avanzarMedidores()
        captura("14-grabar")
        regla.onNodeWithContentDescription("Grabar").performClick()
        regla.waitUntil(3_000) { Mesa.entrada.grabando }
        avanzarHasta(8_000, { "la grabación no avanzó: ${Mesa.entrada.segundos} s, ${Mesa.estado}" }) {
            Mesa.entrada.segundos > 1.2
        }
        captura("15-grabando")
        regla.onNodeWithContentDescription("Detener grabación").performClick()
        regla.waitUntil(5_000) { !Mesa.entrada.grabando && Mesa.pistas.size == 5 }
        assertEquals("Grabación 1", Mesa.pistas.last().nombre)
        regla.onNodeWithTag("pestana-0").performClick()
        regla.waitUntil(2_000) { !Mesa.entrada.abierta }
        regla.waitForIdle()
        captura("16-grabado")
        regla.runOnUiThread { Mesa.quitar(Mesa.pistas.last()) }
        regla.waitUntil(2_000) { Mesa.pistas.size == 4 }

        if (Mesa.reproduciendo) regla.onNodeWithContentDescription("Pausar").performClick()
        regla.waitUntil(5_000) { !Mesa.reproduciendo }
        regla.onNodeWithContentDescription("Detener").performClick()
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
