package com.wavapp.mezcla

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Decodifica audio comprimido (MP3, AAC/M4A, FLAC, OGG, Opus... lo que soporte el celular)
 * a un WAV temporal con el decodificador de Android. Ese WAV se importa igual que cualquier otro.
 */
object Decodificador {

    class Resultado(val error: String, val codec: String = "", val frecuencia: Int = 0, val canales: Int = 0)

    fun aWav(context: Context, uri: Uri, destino: File, progreso: (Float) -> Unit): Resultado {
        val ex = MediaExtractor()
        var codec: MediaCodec? = null
        val salida = Wav(destino)
        try {
            ex.setDataSource(context, uri, null)
            val pista = (0 until ex.trackCount).firstOrNull {
                ex.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
            } ?: return Resultado("no tiene audio que se pueda leer")
            ex.selectTrack(pista)
            val formato = ex.getTrackFormat(pista)
            val mime = formato.getString(MediaFormat.KEY_MIME) ?: return Resultado("formato desconocido")
            val duracionUs = if (formato.containsKey(MediaFormat.KEY_DURATION)) formato.getLong(MediaFormat.KEY_DURATION) else 0L
            var frecuencia = formato.getInteger(MediaFormat.KEY_SAMPLE_RATE)
            var canales = formato.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
            var flotante = false

            codec = try {
                MediaCodec.createDecoderByType(mime)
            } catch (e: Exception) {
                return Resultado("este celular no sabe decodificar $mime")
            }
            codec.configure(formato, null, null, 0)
            codec.start()

            val info = MediaCodec.BufferInfo()
            var finEntrada = false
            var finSalida = false
            var copia = ByteArray(0)
            while (!finSalida) {
                if (!finEntrada) {
                    val i = codec.dequeueInputBuffer(10_000)
                    if (i >= 0) {
                        val buf = codec.getInputBuffer(i)!!
                        val n = ex.readSampleData(buf, 0)
                        if (n < 0) {
                            codec.queueInputBuffer(i, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            finEntrada = true
                        } else {
                            codec.queueInputBuffer(i, 0, n, ex.sampleTime, 0)
                            ex.advance()
                        }
                    }
                }
                val o = codec.dequeueOutputBuffer(info, 10_000)
                when {
                    o == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        val f = codec.outputFormat
                        frecuencia = f.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                        canales = f.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                        flotante = f.containsKey(MediaFormat.KEY_PCM_ENCODING) &&
                            f.getInteger(MediaFormat.KEY_PCM_ENCODING) == AudioFormat.ENCODING_PCM_FLOAT
                    }
                    o >= 0 -> {
                        if (info.size > 0) {
                            if (!salida.abierto) salida.abrir(frecuencia, canales, flotante)
                            val buf = codec.getOutputBuffer(o)!!
                            buf.position(info.offset)
                            buf.limit(info.offset + info.size)
                            if (copia.size < info.size) copia = ByteArray(info.size)
                            buf.get(copia, 0, info.size)
                            salida.escribir(copia, info.size)
                            if (duracionUs > 0) progreso((info.presentationTimeUs.toFloat() / duracionUs).coerceIn(0f, 1f))
                        }
                        codec.releaseOutputBuffer(o, false)
                        if ((info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) finSalida = true
                    }
                }
            }
            if (!salida.abierto || salida.bytes == 0L) return Resultado("no se obtuvo audio del archivo")
            salida.cerrar()
            val nombre = mime.removePrefix("audio/").uppercase()
                .replace("MPEG", "MP3").replace("MP4A-LATM", "AAC").replace("VORBIS", "OGG Vorbis")
            return Resultado("", nombre, frecuencia, canales)
        } catch (e: Exception) {
            salida.liberar()
            destino.delete()
            return Resultado("no se pudo decodificar (${e.message ?: e.javaClass.simpleName})")
        } finally {
            salida.liberar()
            try { codec?.stop() } catch (_: Exception) {}
            try { codec?.release() } catch (_: Exception) {}
            ex.release()
        }
    }

    /** WAV PCM 16 bits o float 32, con los tamaños del encabezado completados al cerrar. */
    private class Wav(val archivo: File) {
        private var raf: RandomAccessFile? = null
        var abierto = false
            private set
        var bytes = 0L
            private set

        fun abrir(frecuencia: Int, canales: Int, flotante: Boolean) {
            val bits = if (flotante) 32 else 16
            val bloque = canales * bits / 8
            val h = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN)
            h.put("RIFF".toByteArray()).putInt(36).put("WAVEfmt ".toByteArray()).putInt(16)
            h.putShort((if (flotante) 3 else 1).toShort()).putShort(canales.toShort())
            h.putInt(frecuencia).putInt(frecuencia * bloque).putShort(bloque.toShort()).putShort(bits.toShort())
            h.put("data".toByteArray()).putInt(0)
            raf = RandomAccessFile(archivo, "rw").apply {
                setLength(0)
                write(h.array())
            }
            abierto = true
        }

        fun escribir(datos: ByteArray, n: Int) {
            raf!!.write(datos, 0, n)
            bytes += n
        }

        /** Cierra sin completar el encabezado (por error o si ya se cerró). */
        fun liberar() {
            try { raf?.close() } catch (_: Exception) {}
            raf = null
        }

        fun cerrar() {
            val r = raf ?: return
            val total = bytes.coerceAtMost(0xFFFFFFFFL - 36)
            val t = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
            r.seek(4); r.write(t.putInt(0, (36 + total).toInt()).array())
            r.seek(40); r.write(t.putInt(0, total.toInt()).array())
            r.close()
            raf = null
        }
    }
}
