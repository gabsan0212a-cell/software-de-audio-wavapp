package com.wavapp.mezcla

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.net.Uri
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
        val salida = NativeAudio.outputInfo()
        assertTrue("la salida reporta su frecuencia (${salida.joinToString()})", salida[0] > 0 && salida[2] > 0)
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
    fun importaAacDecodificado() {
        val ctx = InstrumentationRegistry.getInstrumentation().targetContext
        val m4a = File(dir, "tono.m4a")
        codificarAac(m4a, segundos = 2, frecuencia = 44100)
        val wav = File(dir, "tono-dec.wav")
        val d = Decodificador.aWav(ctx, Uri.fromFile(m4a), wav) {}
        assertEquals("", d.error)
        assertEquals("AAC", d.codec)
        assertEquals(44100, d.frecuencia)
        val fd = ParcelFileDescriptor.open(wav, ParcelFileDescriptor.MODE_READ_ONLY).detachFd()
        val r = NativeAudio.importWav(fd, File(dir, "aac-int.wav").path)
        assertNotNull(NativeAudio.lastError(), r)
        espacios += r!![0].toInt()
        assertEquals(2L, r[1])
        // ~2 s a 48 kHz (el códec agrega un poco de relleno al inicio y al final)
        assertTrue("duración ${r[2]} cuadros", r[2] in 95_000L..110_000L)
    }

    @Test
    fun rechazaArchivoQueNoEsWav() {
        val src = File(dir, "texto.wav").apply { writeText("hola") }
        val fd = ParcelFileDescriptor.open(src, ParcelFileDescriptor.MODE_READ_ONLY).detachFd()
        assertNull(NativeAudio.importWav(fd, File(dir, "x.wav").path))
        assertEquals("No es un archivo WAV", NativeAudio.lastError())
    }

    /** Codifica un tono estéreo (L = -R) a AAC en M4A con el codificador del sistema. */
    private fun codificarAac(f: File, segundos: Int, frecuencia: Int) {
        val formato = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, frecuencia, 2).apply {
            setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
            setInteger(MediaFormat.KEY_BIT_RATE, 128_000)
        }
        val enc = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
        enc.configure(formato, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        enc.start()
        val mux = MediaMuxer(f.path, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
        var pistaMux = -1
        val total = segundos * frecuencia
        var hecho = 0
        var finEntrada = false
        val info = MediaCodec.BufferInfo()
        while (true) {
            if (!finEntrada) {
                val i = enc.dequeueInputBuffer(10_000)
                if (i >= 0) {
                    val buf = enc.getInputBuffer(i)!!.order(ByteOrder.LITTLE_ENDIAN)
                    buf.clear()
                    val cuadros = minOf(1024, buf.capacity() / 4, total - hecho)
                    val t = hecho * 1_000_000L / frecuencia
                    if (cuadros <= 0) {
                        enc.queueInputBuffer(i, 0, 0, t, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                        finEntrada = true
                    } else {
                        for (k in 0 until cuadros) {
                            val v = (sin(2 * PI * 440 * (hecho + k) / frecuencia) * 12000).toInt()
                            buf.putShort(v.toShort())
                            buf.putShort((-v).toShort())
                        }
                        enc.queueInputBuffer(i, 0, cuadros * 4, t, 0)
                        hecho += cuadros
                    }
                }
            }
            val o = enc.dequeueOutputBuffer(info, 10_000)
            if (o == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                pistaMux = mux.addTrack(enc.outputFormat)
                mux.start()
            } else if (o >= 0) {
                val buf = enc.getOutputBuffer(o)!!
                if ((info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0 && info.size > 0 && pistaMux >= 0) {
                    buf.position(info.offset)
                    buf.limit(info.offset + info.size)
                    mux.writeSampleData(pistaMux, buf, info)
                }
                enc.releaseOutputBuffer(o, false)
                if ((info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) break
            }
        }
        enc.stop()
        enc.release()
        mux.stop()
        mux.release()
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
