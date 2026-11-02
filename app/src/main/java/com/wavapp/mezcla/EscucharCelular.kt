package com.wavapp.mezcla

import android.annotation.SuppressLint
import android.annotation.TargetApi
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Process

/**
 * Escuchar lo que suena en el celular (la música o los videos de otras apps) para verlo en
 * Análisis. Es lo que obliga Android: el permiso de "grabar o transmitir" (MediaProjection), un
 * servicio en primer plano con su aviso y AudioRecord con la captura de lo que se reproduce
 * (Android 10 o más nuevo). El audio pasa tal cual al núcleo, que lo analiza: no suena, no se
 * graba ni se guarda. Se va al tocar "Dejar de escuchar", desde el aviso o desde Android.
 */
@TargetApi(29)
class EscucharCelular : Service() {
    private var proyeccion: MediaProjection? = null
    private var grabador: AudioRecord? = null
    private var hilo: Thread? = null
    @Volatile private var seguir = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == DETENER) {
            stopSelf()
            return START_NOT_STICKY
        }
        if (proyeccion != null) return START_NOT_STICKY  // ya está escuchando
        val gestor = getSystemService(NotificationManager::class.java)
        gestor.createNotificationChannel(
            NotificationChannel(CANAL, "Escuchar el celular", NotificationManager.IMPORTANCE_LOW).apply {
                description = "Avisa mientras la app escucha lo que suena en el celular para analizarlo"
            },
        )
        // Android pide el aviso antes de usar el permiso.
        try {
            startForeground(ID, aviso(), ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
        } catch (e: Exception) {
            stopSelf()
            return START_NOT_STICKY
        }
        val codigo = intent?.getIntExtra(EXTRA_CODIGO, 0) ?: 0
        val datos: Intent? = if (Build.VERSION.SDK_INT >= 33) {
            intent?.getParcelableExtra(EXTRA_DATOS, Intent::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent?.getParcelableExtra(EXTRA_DATOS)
        }
        val p = try {
            datos?.let { getSystemService(MediaProjectionManager::class.java).getMediaProjection(codigo, it) }
        } catch (e: Exception) {
            null
        }
        if (p == null || !empezar(p)) {
            p?.stop()
            stopSelf()
        }
        return START_NOT_STICKY
    }

    // El micrófono ya se pidió antes de abrir el permiso de grabar o transmitir.
    @SuppressLint("MissingPermission")
    private fun empezar(p: MediaProjection): Boolean {
        p.registerCallback(
            object : MediaProjection.Callback() {
                override fun onStop() = stopSelf()  // se detuvo desde Android
            },
            Handler(Looper.getMainLooper()),
        )
        // Lo que suena en las otras apps (no lo de WavApp).
        val captura = AudioPlaybackCaptureConfiguration.Builder(p)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
            .addMatchingUsage(AudioAttributes.USAGE_GAME)
            .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
            .excludeUid(Process.myUid())
            .build()
        val formato = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .setSampleRate(Nucleo.FRECUENCIA)
            .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
            .build()
        val minimo = AudioRecord.getMinBufferSize(Nucleo.FRECUENCIA, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_16BIT)
        val rec = try {
            AudioRecord.Builder()
                .setAudioFormat(formato)
                .setBufferSizeInBytes(maxOf(minimo, Nucleo.FRECUENCIA / 5 * 4))  // 200 ms
                .setAudioPlaybackCaptureConfig(captura)
                .build()
        } catch (e: Exception) {
            return false
        }
        if (rec.state != AudioRecord.STATE_INITIALIZED) {
            rec.release()
            return false
        }
        try {
            rec.startRecording()
        } catch (e: Exception) {
            rec.release()
            return false
        }
        proyeccion = p
        grabador = rec
        Nucleo.mediaOpened(2)
        seguir = true
        hilo = Thread({ leer(rec) }, "WavApp-celular").apply { start() }
        escuchando = true
        return true
    }

    // Pedazos de 20 ms, tal como llegan, al núcleo.
    private fun leer(rec: AudioRecord) {
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
        val pedazo = ShortArray(2 * Nucleo.FRECUENCIA / 50)
        while (seguir) {
            val n = rec.read(pedazo, 0, pedazo.size, AudioRecord.READ_BLOCKING)
            if (n > 0) Nucleo.mediaPush(pedazo, n / 2) else if (n < 0) break
        }
    }

    private fun aviso(): Notification {
        val abrir = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val detener = PendingIntent.getService(
            this, 1, Intent(this, EscucharCelular::class.java).setAction(DETENER),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        return Notification.Builder(this, CANAL)
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setContentTitle("WavApp · escuchando el celular")
            .setContentText("Analiza lo que suena en otras apps (no graba ni guarda nada)")
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setContentIntent(abrir)
            .addAction(Notification.Action.Builder(null, "Dejar de escuchar", detener).build())
            .build()
    }

    override fun onDestroy() {
        seguir = false
        escuchando = false
        grabador?.let {
            try {
                it.stop()  // destraba la lectura
            } catch (e: Exception) {
            }
        }
        try {
            hilo?.join(500)
        } catch (e: InterruptedException) {
        }
        grabador?.release()
        grabador = null
        Nucleo.mediaClosed()
        proyeccion?.stop()
        proyeccion = null
        super.onDestroy()
    }

    companion object {
        private const val ID = 8
        private const val CANAL = "escuchar"
        private const val DETENER = "detener"
        private const val EXTRA_CODIGO = "codigo"
        private const val EXTRA_DATOS = "datos"

        /** El servicio está escuchando (lo lee la pantalla junto con el estado del núcleo). */
        @Volatile var escuchando = false
            private set

        /** Ya se tiene el permiso de grabar o transmitir (`codigo` y `datos` de su respuesta). */
        fun empezar(context: Context, codigo: Int, datos: Intent) {
            try {
                context.startForegroundService(
                    Intent(context, EscucharCelular::class.java).putExtra(EXTRA_CODIGO, codigo).putExtra(EXTRA_DATOS, datos),
                )
            } catch (e: Exception) {
                // Sin servicio no se puede escuchar: la pantalla sigue diciendo que no escucha.
            }
        }

        fun detener(context: Context) {
            if (!escuchando) return
            try {
                context.startService(Intent(context, EscucharCelular::class.java).setAction(DETENER))
            } catch (e: Exception) {
            }
        }
    }
}
