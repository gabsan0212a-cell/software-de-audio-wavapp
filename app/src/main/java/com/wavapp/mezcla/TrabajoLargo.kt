package com.wavapp.mezcla

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import com.wavapp.mezcla.ui.textoTiempo

/**
 * Mantiene viva la app mientras el núcleo hace un trabajo largo (quitar la voz de una canción):
 * servicio en primer plano con su aviso y el procesador despierto aunque se apague la pantalla.
 * Es lo que obliga Android; el trabajo en sí lo hace el núcleo. Se va solo cuando el trabajo termina.
 */
class TrabajoLargo : Service() {
    private var despierto: PowerManager.WakeLock? = null
    private val manejador = Handler(Looper.getMainLooper())
    private val estado = FloatArray(Nucleo.DATOS_SEPARAR)

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        val gestor = getSystemService(NotificationManager::class.java)
        gestor.createNotificationChannel(
            NotificationChannel(CANAL, "Trabajos largos", NotificationManager.IMPORTANCE_LOW).apply {
                description = "Avisa mientras la app prepara una canción (quitar la voz)"
            },
        )
        val aviso = aviso("Preparando la canción…", -1f)
        when {
            Build.VERSION.SDK_INT >= 35 -> startForeground(ID, aviso, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING)
            Build.VERSION.SDK_INT >= 29 -> startForeground(ID, aviso, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
            else -> startForeground(ID, aviso)
        }
        despierto = (getSystemService(POWER_SERVICE) as PowerManager)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "WavApp:preparar")
            .apply { acquire(6 * 60 * 60 * 1000L) }  // tope de seguridad: 6 horas
        manejador.post(revisar)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int = START_NOT_STICKY

    // Cada 2 s: el avance en el aviso; si ya no hay trabajo, se va.
    private val revisar = object : Runnable {
        override fun run() {
            Nucleo.separationState(estado)
            val fase = estado[0].toInt()
            if (fase == 0) {
                stopSelf()
                return
            }
            val texto = when (fase) {
                1 -> "Preparando la canción…"
                2 -> buildString {
                    append("Quitando la voz · trozo ${estado[2].toInt()} de ${estado[3].toInt()}")
                    val falta = estado[4]
                    if (falta >= 0f) append(" · faltan ${textoTiempo(falta)}")
                }
                3 -> "Armando las pistas…"
                else -> "Analizando la canción…"
            }
            getSystemService(NotificationManager::class.java).notify(ID, aviso(texto, estado[1]))
            manejador.postDelayed(this, 2000)
        }
    }

    private fun aviso(texto: String, progreso: Float): Notification {
        val abrir = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        return Notification.Builder(this, CANAL)
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle("WavApp · preparando para cantar")
            .setContentText(texto)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setContentIntent(abrir)
            .setProgress(1000, (progreso.coerceIn(0f, 1f) * 1000).toInt(), progreso < 0f)
            .build()
    }

    // Android 15 corta estos servicios a las 6 horas y pide detenerlos enseguida (lo hecho queda
    // guardado y al volver a empezar sigue donde quedó).
    override fun onTimeout(startId: Int, fgsType: Int) = stopSelf()

    override fun onDestroy() {
        manejador.removeCallbacks(revisar)
        despierto?.let { if (it.isHeld) it.release() }
        super.onDestroy()
    }

    companion object {
        private const val ID = 7
        private const val CANAL = "trabajos"

        fun empezar(context: Context) {
            try {
                context.startForegroundService(Intent(context, TrabajoLargo::class.java))
            } catch (e: Exception) {
                // Sin servicio el trabajo sigue igual mientras la app esté abierta.
            }
        }
    }
}
