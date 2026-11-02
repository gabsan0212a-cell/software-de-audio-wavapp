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
 */
internal object OidoNativo {
    private var vis: Visualizer? = null
    private var hilo: Thread? = null
    private val pedidos = Semaphore(0)
    @Volatile private var seguir = false

    /** Empieza a escuchar. false si este celular no lo deja. */
    @Synchronized
    fun empezar(): Boolean {
        if (vis != null) return true
        val v = try {
            Visualizer(0)
        } catch (e: Throwable) {
            return false
        }
        try {
            v.captureSize = Visualizer.getCaptureSizeRange()[1].coerceAtMost(2048)
            v.scalingMode = Visualizer.SCALING_MODE_NORMALIZED
            v.measurementMode = Visualizer.MEASUREMENT_MODE_PEAK_RMS
            if (v.setEnabled(true) != Visualizer.SUCCESS) throw IllegalStateException("Visualizer")
        } catch (e: Throwable) {
            v.release()
            return false
        }
        vis = v
        pedidos.drainPermits()
        Nucleo.mediaOpenedNative()
        seguir = true
        hilo = Thread({ leer(v) }, "WavApp-oido").apply { start() }
        return true
    }

    /** Un cuadro de pantalla quiere lo último que sonó (barato: solo avisa al hilo). */
    fun pedir() {
        if (vis != null && pedidos.availablePermits() == 0) pedidos.release()
    }

    private fun leer(v: Visualizer) {
        Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY)
        val onda = ByteArray(v.captureSize)
        val medida = Visualizer.MeasurementPeakRms()
        val frecuencia = v.samplingRate  // mHz
        var prendido = true
        while (seguir) {
            val hay = try {
                pedidos.tryAcquire(1, TimeUnit.SECONDS)
            } catch (e: InterruptedException) {
                false
            }
            if (!seguir) break
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
                Nucleo.mediaSnapshot(
                    onda, onda.size, frecuencia,
                    if (nivel) medida.mPeak else Int.MIN_VALUE, if (nivel) medida.mRms else Int.MIN_VALUE,
                )
            } catch (e: Throwable) {
                // Android se lo quitó (otra app tomó la salida): se sigue intentando.
            }
        }
    }

    @Synchronized
    fun detener() {
        val v = vis ?: return
        seguir = false
        pedidos.release()
        try {
            hilo?.join(500)
        } catch (e: InterruptedException) {
        }
        hilo = null
        try {
            v.setEnabled(false)
        } catch (e: Throwable) {
        }
        v.release()
        vis = null
        Nucleo.mediaClosed()
    }
}
