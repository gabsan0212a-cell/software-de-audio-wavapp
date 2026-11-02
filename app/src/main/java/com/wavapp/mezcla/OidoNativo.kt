package com.wavapp.mezcla

import android.media.audiofx.Visualizer
import android.os.Process
import java.util.concurrent.Semaphore
import java.util.concurrent.TimeUnit

/**
 * Lo que suena en el celular "de forma nativa": el Visualizer de Android sobre lo que sale
 * (sesión 0), como los visualizadores de música. No pide grabar la pantalla (solo el permiso del
 * micrófono, que Android exige para esto) ni deja avisos. Android da pedazos sueltos de lo que
 * suena (~1024 muestras mono) y su nivel; se pide uno por cuadro de pantalla (así no se gasta nada
 * cuando no se ve) y pasa tal cual al núcleo, que lo analiza. Si nadie mira, se apaga solo.
 *
 * Todo lo de Android (crearlo puede tardar) va en su propio hilo: la pantalla nunca espera.
 */
internal object OidoNativo {
    private val cerrojo = Any()
    private var corrida = 0      // con cerrojo: la escucha vigente (cambia al empezar y al detener)
    private var abierta = 0      // con cerrojo: la escucha que abrió el núcleo (0 = ninguna)
    @Volatile private var activa = false
    private val pedidos = Semaphore(0)

    /** Las pruebas en el emulador lo apagan para mandar su propio audio (como lo daría Android). */
    @Volatile var deshabilitado = false

    /** Empieza a escuchar; si este celular no lo deja, llama a `alFallar` (desde otro hilo). */
    fun empezar(alFallar: () -> Unit) {
        if (deshabilitado) return
        val id = synchronized(cerrojo) {
            if (activa) return
            activa = true
            ++corrida
        }
        pedidos.drainPermits()
        Thread({ correr(id, alFallar) }, "WavApp-oido").start()
    }

    /** Deja de escuchar (el núcleo se entera al momento; el hilo suelta lo de Android al salir). */
    fun detener() {
        synchronized(cerrojo) {
            if (!activa) return
            activa = false
            corrida++
            if (abierta != 0) {
                Nucleo.mediaClosed()
                abierta = 0
            }
        }
        pedidos.release()
    }

    /** Un cuadro de pantalla quiere lo último que sonó (barato: solo avisa al hilo). */
    fun pedir() {
        if (activa && pedidos.availablePermits() == 0) pedidos.release()
    }

    private fun vigente(id: Int) = synchronized(cerrojo) { corrida == id }

    private fun crear(): Visualizer? {
        val v = try {
            Visualizer(0)
        } catch (e: Throwable) {
            return null
        }
        return try {
            v.captureSize = Visualizer.getCaptureSizeRange()[1].coerceAtMost(2048)
            v.scalingMode = Visualizer.SCALING_MODE_NORMALIZED
            v.measurementMode = Visualizer.MEASUREMENT_MODE_PEAK_RMS
            if (v.setEnabled(true) != Visualizer.SUCCESS) throw IllegalStateException("Visualizer")
            v
        } catch (e: Throwable) {
            v.release()
            null
        }
    }

    private fun correr(id: Int, alFallar: () -> Unit) {
        Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY)
        val v = crear()
        if (v == null) {
            if (vigente(id)) alFallar()
            return
        }
        try {
            synchronized(cerrojo) {
                if (corrida != id) return  // ya se detuvo mientras se creaba
                Nucleo.mediaOpenedNative()
                abierta = id
            }
            val onda = ByteArray(v.captureSize)
            val medida = Visualizer.MeasurementPeakRms()
            val frecuencia = v.samplingRate  // mHz
            var prendido = true
            while (vigente(id)) {
                val hay = try {
                    pedidos.tryAcquire(1, TimeUnit.SECONDS)
                } catch (e: InterruptedException) {
                    false
                }
                if (!vigente(id)) break
                try {
                    if (!hay) {  // nadie está mirando: se apaga hasta el próximo pedido
                        if (prendido) prendido = v.setEnabled(false) != Visualizer.SUCCESS
                        continue
                    }
                    if (!prendido) {
                        prendido = v.setEnabled(true) == Visualizer.SUCCESS
                        continue
                    }
                    if (v.getWaveForm(onda) != Visualizer.SUCCESS) continue
                    val nivel = v.getMeasurementPeakRms(medida) == Visualizer.SUCCESS
                    if (!vigente(id)) break
                    Nucleo.mediaSnapshot(
                        onda, onda.size, frecuencia,
                        if (nivel) medida.mPeak else Int.MIN_VALUE, if (nivel) medida.mRms else Int.MIN_VALUE,
                    )
                } catch (e: Throwable) {
                    // Android se lo quitó (otra app tomó la salida): se sigue intentando.
                }
            }
        } finally {
            try {
                v.setEnabled(false)
            } catch (e: Throwable) {
            }
            v.release()
        }
    }
}
