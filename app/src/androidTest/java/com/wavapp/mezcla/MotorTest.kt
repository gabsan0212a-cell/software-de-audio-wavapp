package com.wavapp.mezcla

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertArrayEquals
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

/**
 * Prueba el núcleo en un Android real (emulador), por el mismo puente que usa la pantalla:
 * audio, pistas, importación (WAV y comprimidos con MediaCodec desde C++), exportación,
 * análisis, EQ, edición y la sesión. Usa su propia carpeta de proyecto.
 */
@RunWith(AndroidJUnit4::class)
class MotorTest {
    private val ctx get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var proyecto: File
    private lateinit var archivos: File

    @Before
    fun preparar() {
        proyecto = File(ctx.filesDir, "prueba-motor").apply { mkdirs() }
        archivos = File(ctx.cacheDir, "prueba-motor-archivos").apply { deleteRecursively(); mkdirs() }
        Nucleo.init(proyecto.path, ctx.cacheDir.path)
        assertTrue(Nucleo.waitIdle(60_000))
        Nucleo.clearProject()
        assertTrue("el motor de audio abre la salida", Nucleo.start())
    }

    @After
    fun limpiar() {
        Nucleo.stopTransport()
        Nucleo.waitIdle(60_000)
        Nucleo.clearProject()
        archivos.deleteRecursively()
    }

    private fun pistas() = JSONArray(Nucleo.tracksJson())
    private fun estado() = JSONObject(Nucleo.statusJson())
    private fun cargarPrueba() {
        assertTrue(Nucleo.loadDemo())
        assertTrue(Nucleo.waitIdle(60_000))
        assertEquals(4, pistas().length())
    }
    private fun leer(f: File) = ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY).detachFd()
    private fun importar(f: File, nombre: String) {
        assertTrue(Nucleo.importFiles(intArrayOf(leer(f)), arrayOf(nombre)))
        assertTrue(Nucleo.waitIdle(60_000))
    }

    @Test
    fun pistasDePruebaSuenanYSeMiden() {
        cargarPrueba()
        assertEquals(12.0, Nucleo.durationSeconds(), 0.001)
        val salida = Nucleo.outputInfo()
        assertTrue("la salida reporta su frecuencia (${salida.joinToString()})", salida[0] > 0 && salida[2] > 0)
        Nucleo.seek(0.0)
        Nucleo.play()
        Thread.sleep(1500)
        val pos = Nucleo.positionSeconds()
        assertTrue("la posición avanza (va en $pos s)", pos > 0.5)
        val m = FloatArray(Nucleo.MEDIDORES)
        Nucleo.meters(SystemClock.uptimeMillis(), m)
        assertTrue("el master tiene señal (${m[Nucleo.medMasterL()]} dB)", m[Nucleo.medMasterL()] > -40f)
        Nucleo.seek(6.0)
        Thread.sleep(300)
        assertTrue(Nucleo.positionSeconds() >= 6.0)
        // Quitar una pista mientras suena no debe cerrar la app.
        assertTrue(Nucleo.removeTrack(pistas().getJSONObject(0).getInt("id")))
        Thread.sleep(200)
        Nucleo.pause()
        Thread.sleep(200)
        assertFalse(Nucleo.isPlaying())
        assertEquals(3, pistas().length())
    }

    @Test
    fun exportaYSeVuelveAImportar() {
        cargarPrueba()
        Nucleo.setExportFormat(24, 44100)
        val salida = File(archivos, "mezcla.wav")
        val modo = ParcelFileDescriptor.MODE_WRITE_ONLY or ParcelFileDescriptor.MODE_CREATE or
            ParcelFileDescriptor.MODE_TRUNCATE
        val esperado = Nucleo.exportBytes()
        assertTrue(Nucleo.exportMix(ParcelFileDescriptor.open(salida, modo).detachFd(), "mezcla.wav"))
        assertTrue(Nucleo.waitIdle(120_000))
        val st = estado()
        assertTrue(st.toString(), st.getBoolean("ultimaOk"))
        assertTrue(st.getString("mensaje"), st.getString("mensaje").startsWith("Mezcla exportada: mezcla.wav (24 bits · 44100 Hz · 0:12"))
        assertEquals(esperado, salida.length())
        // El archivo exportado se puede volver a abrir como cualquier WAV.
        importar(salida, "mezcla.wav")
        val nueva = pistas().getJSONObject(4)
        assertEquals("mezcla", nueva.getString("nombre"))
        assertEquals("44.1 kHz → 48 kHz · 24 bits · estéreo", nueva.getString("info"))
        assertEquals(12L * 48000, nueva.getLong("cuadros"))
    }

    @Test
    fun analisisEnVivo() {
        cargarPrueba()
        Nucleo.setAnalysisSource(-1)
        Nucleo.seek(0.0)
        Nucleo.play()
        Thread.sleep(1000)
        val st = FloatArray(Nucleo.DATOS_ANALISIS)
        var llego = false
        repeat(20) {
            llego = Nucleo.analysis(SystemClock.uptimeMillis(), false, st) || llego
            Thread.sleep(40)
        }
        assertTrue("llega audio al analizador", llego)
        assertEquals(1f, st[0], 0f)
        val cols = FloatArray(200)
        Nucleo.spectrumColumns(cols)
        assertTrue("el espectro tiene señal (${cols.max()})", cols.max() > -60f)
        assertTrue(Nucleo.goniometer(FloatArray(4096)) > 0)
        val id = pistas().getJSONObject(0).getInt("id")
        assertNull(Nucleo.spectrogram(id))  // se calcula aparte
        assertTrue(Nucleo.waitIdle(60_000))
        assertEquals(Nucleo.SG_COLUMNAS * Nucleo.SG_FILAS, Nucleo.spectrogram(id)!!.size)
        Nucleo.setNotch(60)
        Thread.sleep(200)
        Nucleo.setNotch(0)
        Nucleo.pause()
    }

    @Test
    fun eqPorPista() {
        cargarPrueba()
        val id = pistas().getJSONObject(0).getInt("id")
        assertEquals(0f, Nucleo.eqTrimDb(id), 0f)  // sin bandas
        assertEquals(0, Nucleo.addEqBand(id))
        Nucleo.setEqBand(id, 0, 0, 1000f, 6f, 1.4f, true)
        val f = floatArrayOf(100f, 1000f, 10000f)
        val db = FloatArray(3)
        Nucleo.eqResponse(id, -1, f, db)
        assertEquals(6f, db[1], 0.05f)
        assertEquals(-6f, Nucleo.eqTrimDb(id), 0.1f)
        Nucleo.setEqBypass(id, true)
        assertEquals(0f, Nucleo.eqTrimDb(id), 0f)
        Nucleo.setEqBypass(id, false)
        Nucleo.removeEqBand(id, 0)
        Nucleo.eqResponse(id, -1, f, db)
        assertEquals(0f, db[1], 0f)
        // La segunda banda va en el hueco de frecuencias más grande.
        Nucleo.addEqBand(id)
        Nucleo.addEqBand(id)
        assertEquals(140f, Nucleo.eqState(id)[3 + 5 + 1], 0.01f)
    }

    @Test
    fun importaWavDelCelular() {
        // Estéreo 16 bits a 44.1 kHz con el encabezado mal escrito (bloque = 2), como el del celular.
        val src = File(archivos, "celular.wav")
        escribirWav(src, cuadros = 44100)
        importar(src, "celular.wav")
        val p = pistas().getJSONObject(0)
        assertEquals("celular", p.getString("nombre"))
        assertEquals(2, p.getInt("canales"))
        assertEquals(48000L, p.getLong("cuadros"))  // 1 s a 48 kHz
        assertEquals("44.1 kHz → 48 kHz · 16 bits · estéreo", p.getString("info"))
        val onda = Nucleo.regionWave(p.getInt("id"), 0, 0, 48000, 100)
        assertNotNull(onda)
        assertEquals(100 * 2 * 2, onda!!.size)
    }

    @Test
    fun importaAacDecodificado() {
        val m4a = File(archivos, "tono.m4a")
        codificarAac(m4a, segundos = 2, frecuencia = 44100)
        importar(m4a, "tono.m4a")
        val arr = pistas()
        assertEquals(estado().toString(), 1, arr.length())
        val p = arr.getJSONObject(0)
        assertEquals("tono", p.getString("nombre"))
        assertTrue(p.getString("info"), p.getString("info") == "AAC · 44.1 kHz → 48 kHz · estéreo")
        assertEquals(2, p.getInt("canales"))
        // ~2 s a 48 kHz (el códec agrega un poco de relleno al inicio y al final)
        val cuadros = p.getLong("cuadros")
        assertTrue("duración $cuadros cuadros", cuadros in 95_000L..110_000L)
    }

    @Test
    fun rechazaArchivoQueNoEsAudio() {
        val src = File(archivos, "texto.wav").apply { writeText("hola") }
        importar(src, "texto.wav")
        assertEquals(0, pistas().length())
        val msg = estado().getString("mensaje")
        assertTrue(msg, msg.startsWith("problemas: texto.wav: "))
    }

    @Test
    fun pedazosDeEdicion() {
        cargarPrueba()
        val id = pistas().getJSONObject(0).getInt("id")
        val original = longArrayOf(0, 0, 12L * 48000, 0, 0)
        assertArrayEquals(original, Nucleo.regions(id))
        Nucleo.seek(6.0)
        assertEquals(1, Nucleo.cut(id, 0))
        assertTrue(Nucleo.editBegin(id, 1, Nucleo.MOVER))
        Nucleo.editDrag(2L * 48000, 0)
        Nucleo.editEnd()
        assertEquals(14.0, Nucleo.durationSeconds(), 0.001)
        Nucleo.seek(12.6)
        Nucleo.play()
        Thread.sleep(500)
        assertTrue(Nucleo.positionSeconds() > 12.6)
        Nucleo.pause()
        assertTrue(Nucleo.undo())
        assertTrue(Nucleo.undo())
        assertArrayEquals(original, Nucleo.regions(id))
        assertEquals(2, Nucleo.history())  // solo queda rehacer
    }

    @Test
    fun sesionSeGuardaYSeAbre() {
        cargarPrueba()
        val id = pistas().getJSONObject(0).getInt("id")
        Nucleo.setVolume(id, -7.3f)
        Nucleo.addEqBand(id)
        Nucleo.saveNow()
        // Otro proyecto y de regreso: todo como estaba.
        val otro = File(archivos, "otro-proyecto").apply { mkdirs() }
        Nucleo.init(otro.path, ctx.cacheDir.path)
        assertTrue(Nucleo.waitIdle(60_000))
        Nucleo.init(proyecto.path, ctx.cacheDir.path)
        assertTrue(Nucleo.waitIdle(60_000))
        val v = FloatArray(5)
        assertTrue(Nucleo.trackParams(id, v))
        assertEquals(-7.5f, v[0], 0f)
        assertEquals(1f, Nucleo.eqState(id)[2], 0f)
        assertTrue(estado().getString("mensaje").startsWith("Mezcla recuperada"))
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
