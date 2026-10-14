package com.wavapp.mezcla

import android.content.Context
import android.os.Build
import android.util.Log
import java.io.File

/**
 * Si la app se cierra por un error de la pantalla (Kotlin), queda anotado en un archivo; los del
 * núcleo (C++) los anota el núcleo mismo en el mismo archivo. La próxima vez que se abre, la
 * pantalla ofrece copiarlo para mandarlo y arreglarlo (el dueño no tiene computadora para ver
 * los registros del sistema).
 */
object Errores {
    private const val ARCHIVO = "ultimo-error.txt"

    /** Se llama una vez al abrir la app (antes que nada). */
    fun instalar(context: Context) {
        val archivo = File(context.filesDir, ARCHIVO)
        val anterior = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { hilo, e ->
            try {
                archivo.writeText("La pantalla (Kotlin) se cerró por un error en el hilo \"${hilo.name}\":\n" + Log.getStackTraceString(e))
            } catch (_: Throwable) {
            }
            anterior?.uncaughtException(hilo, e)  // el sistema la cierra como siempre
        }
    }

    /** El reporte de la vez anterior (con la versión y el celular), o null si no hubo. */
    fun leer(context: Context): String? {
        val archivo = File(context.filesDir, ARCHIVO)
        val texto = try {
            if (archivo.exists()) archivo.readText() else null
        } catch (_: Exception) {
            null
        } ?: return null
        if (texto.isBlank()) return null
        val version = try {
            val i = context.packageManager.getPackageInfo(context.packageName, 0)
            "${i.versionName} (${if (Build.VERSION.SDK_INT >= 28) i.longVersionCode else 0})"
        } catch (_: Exception) {
            "?"
        }
        return "WavApp $version · Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT}) · " +
            "${Build.MANUFACTURER} ${Build.MODEL} · ${Build.SUPPORTED_ABIS.firstOrNull() ?: "?"}\n" + texto.trimEnd()
    }

    fun borrar(context: Context) {
        try {
            File(context.filesDir, ARCHIVO).delete()
        } catch (_: Exception) {
        }
    }
}
