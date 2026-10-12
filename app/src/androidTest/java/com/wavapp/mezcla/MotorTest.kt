package com.wavapp.mezcla

import android.os.ParcelFileDescriptor
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.PI
import kotlin.math.roundToInt
import kotlin.math.sin

/** Prueba el motor nativo en un Android real (emulador): audio, pistas, importación. */
@RunWith(AndroidJUnit4::class)
class MotorTest {
    private val espacios = mutableListOf<Int>()
    private lateinit var dir: File

    @Before
    fun preparar() {
        dir = File(InstrumentationRegistry.getInstrumentation().targetContext.cacheDir, "prueba-motor")
        dir.mkdirs()
        assertTrue("el motor de audio abre la salida", NativeAudio.start())
    }

    @After
    fun limpiar() {
        NativeAudio.stopTransport()
        espacios.forEach { NativeAudio.removeTrack(it) }
        dir.deleteRecursively()
    }

    @Test
    fun pistasDePruebaSuenanYSeMiden() {
        for (i in 0 until 4) {
            val r = NativeAudio.addDemoTrack(i, File(dir, "demo$i.wav").path)
            assertNotNull(NativeAudio.lastError(), r)
            espacios += r!![0].toInt()
            assertEquals(1L, r[1])
            assertEquals(12L * 48000, r[2])
        }
        assertEquals(12.0, NativeAudio.durationSeconds(), 0.001)
        NativeAudio.seek(0.0)
        NativeAudio.play()
        Thread.sleep(1500)
        val pos = NativeAudio.positionSeconds()
        assertTrue("la posición avanza (va en $pos s)", pos > 0.5)
        val m = FloatArray(NativeAudio.meterCount())
        NativeAudio.readMeters(m)
        assertTrue("el master tiene señal (pico ${m[0]})", m[0] > 0.01f)
        NativeAudio.seek(6.0)
        Thread.sleep(300)
        assertTrue(NativeAudio.positionSeconds() >= 6.0)
        // Quitar una pista mientras suena no debe cerrar la app.
        NativeAudio.removeTrack(espacios.removeAt(0))
        Thread.sleep(200)
        NativeAudio.pause()
        Thread.sleep(200)
        assertFalse(NativeAudio.isPlaying())
    }

    @Test
    fun importaWavDelCelular() {
        // Estéreo 16 bits a 44.1 kHz con el encabezado mal escrito (bloque = 2), como el del celular.
        val src = File(dir, "celular.wav")
        escribirWav(src, cuadros = 44100)
        val fd = ParcelFileDescriptor.open(src, ParcelFileDescriptor.MODE_READ_ONLY).detachFd()
        val r = NativeAudio.importWav(fd, File(dir, "interno.wav").path)
        assertNotNull(NativeAudio.lastError(), r)
        espacios += r!![0].toInt()
        assertEquals(2L, r[1])      // estéreo
        assertEquals(48000L, r[2])  // 1 s a 48 kHz
        assertEquals(44100L, r[3])  // frecuencia original
        assertEquals(16L, r[5])     // bits
        val onda = NativeAudio.overview(r[0].toInt(), 100)
        assertNotNull(onda)
        assertEquals(100 * 2 * 2, onda!!.size)
    }

    @Test
    fun rechazaArchivoQueNoEsWav() {
        val src = File(dir, "texto.wav").apply { writeText("hola") }
        val fd = ParcelFileDescriptor.open(src, ParcelFileDescriptor.MODE_READ_ONLY).detachFd()
        assertNull(NativeAudio.importWav(fd, File(dir, "x.wav").path))
        assertEquals("No es un archivo WAV", NativeAudio.lastError())
    }

    private fun escribirWav(f: File, cuadros: Int) {
        val datos = ByteBuffer.allocate(cuadros * 4).order(ByteOrder.LITTLE_ENDIAN)
        for (i in 0 until cuadros) {
            val v = (sin(2 * PI * 1000 * i / 44100.0) * 16000).roundToInt().toShort()
            datos.putShort(v)
            datos.putShort((-v).toShort())
        }
        val h = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN)
        h.put("RIFF".toByteArray()).putInt(36 + cuadros * 4).put("WAVEfmt ".toByteArray())
        h.putInt(16).putShort(1).putShort(2).putInt(44100).putInt(44100 * 4)
        h.putShort(2)  // blockAlign mal escrito por la grabadora
        h.putShort(16).put("data".toByteArray()).putInt(cuadros * 4)
        f.writeBytes(h.array() + datos.array())
    }
}
