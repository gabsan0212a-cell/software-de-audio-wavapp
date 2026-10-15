package com.wavapp.mezcla.ui

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
import android.view.View
import android.view.WindowInsetsController
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import com.wavapp.mezcla.Errores
import com.wavapp.mezcla.Mesa

// Lo que obliga Android (y nada más): botón "atrás", pantalla encendida, selector de archivos,
// color de los íconos de la barra de estado, imágenes desde píxeles, permisos (micrófono y
// avisos) y qué entradas y salidas de audio hay conectadas.

@Composable
internal fun AlRegresar(activo: Boolean, accion: () -> Unit) = BackHandler(activo, accion)

/** Mientras suena, la pantalla no se apaga (al apagarse se detendría el audio). */
@Composable
internal fun PantallaEncendida(encendida: Boolean) {
    val vista = LocalView.current
    DisposableEffect(encendida) {
        vista.keepScreenOn = encendida
        onDispose { vista.keepScreenOn = false }
    }
}

/** Íconos claros en las barras del sistema mientras se ve algo oscuro (el reproductor). */
@Composable
internal fun BarraDeEstadoClara(activa: Boolean, temaOscuro: Boolean) {
    val vista = LocalView.current
    DisposableEffect(activa, temaOscuro) {
        iconosOscuros(vista, !activa && !temaOscuro)
        onDispose { iconosOscuros(vista, !temaOscuro) }
    }
}

@Suppress("DEPRECATION")
private fun iconosOscuros(vista: View, oscuros: Boolean) {
    if (Build.VERSION.SDK_INT >= 30) {
        val banderas = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or
            WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
        vista.windowInsetsController?.setSystemBarsAppearance(if (oscuros) banderas else 0, banderas)
    } else {
        val banderas = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
        vista.systemUiVisibility = if (oscuros) vista.systemUiVisibility or banderas else vista.systemUiVisibility and banderas.inv()
    }
}

/**
 * Tipos que muestra el selector. No solo `audio/*`: muchas carpetas anuncian archivos de audio
 * con otro tipo (.m4a como `video/mp4`, .ogg/.opus como `application/ogg`, .flac como
 * `application/x-flac`, lo descargado o de WhatsApp como `application/octet-stream`) y el sistema
 * los esconde. El núcleo mira el contenido, saca la pista de audio (también de un video) y avisa
 * si un archivo no tiene audio que se pueda leer.
 */
private val tiposDeAudio = arrayOf(
    "audio/*", "video/*", "application/ogg", "application/x-ogg", "application/flac", "application/x-flac",
    "application/x-matroska", "application/octet-stream",
)

/** Abre el selector de audio del sistema; lo elegido se importa en el núcleo. */
@Composable
internal fun recordarAbrirArchivos(): () -> Unit {
    val context = LocalContext.current
    val elegir = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        Mesa.agregarArchivos(context, uris)
    }
    return remember(elegir) { { elegir.launch(tiposDeAudio) } }
}

/** Pregunta dónde guardar el WAV y el núcleo exporta ahí. */
@Composable
internal fun recordarGuardarMezcla(): () -> Unit {
    val context = LocalContext.current
    val guardar = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("audio/wav")) { uri ->
        if (uri != null) Mesa.exportar(context, uri)
    }
    return remember(guardar) { { guardar.launch(Mesa.nombreExportacion()) } }
}

/** Pregunta dónde guardar la presentación grabada y el núcleo la guarda ahí. */
@Composable
internal fun recordarGuardarPresentacion(): () -> Unit {
    val context = LocalContext.current
    val guardar = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("audio/wav")) { uri ->
        if (uri != null) Mesa.guardarPresentacion(context, uri)
    }
    return remember(guardar) { { guardar.launch(Mesa.nombrePresentacion()) } }
}

/** El reporte de la última vez que la app se cerró por un error (lo anota [Errores]). */
@Composable
internal fun recordarUltimoError(): UltimoError {
    val context = LocalContext.current
    var texto by remember { mutableStateOf(Errores.leer(context)) }
    return UltimoError(texto) {
        Errores.borrar(context)
        texto = null
    }
}

/** Imagen a partir de píxeles ARGB (el espectrograma). */
internal fun imagenDePixeles(px: IntArray, ancho: Int, alto: Int): ImageBitmap =
    Bitmap.createBitmap(px, ancho, alto, Bitmap.Config.ARGB_8888).asImageBitmap()

/** Pide (una vez que se toca el botón) el permiso del micrófono. */
@Composable
internal fun recordarPermisoMicrofono(): PermisoMicrofono {
    val context = LocalContext.current
    var respuesta by remember { mutableStateOf<Boolean?>(null) }
    val pedir = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { respuesta = it }
    // Se revisa cada vez (por si se dio en Ajustes y se volvió a la app).
    val concedido = context.checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
    return PermisoMicrofono(concedido, respuesta == false && !concedido) { pedir.launch(Manifest.permission.RECORD_AUDIO) }
}

/**
 * Android 13+: pide permiso para mostrar avisos (el de un trabajo largo, como quitar la voz). Si
 * no se da, el trabajo sigue igual, solo que sin aviso visible.
 */
@Composable
internal fun recordarPermisoAvisos(): () -> Unit {
    val context = LocalContext.current
    val pedir = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { }
    return {
        if (Build.VERSION.SDK_INT >= 33 &&
            context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            pedir.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
    }
}

/** Entradas de audio conectadas y por dónde sale el sonido; se actualiza al conectar o quitar algo. */
@Composable
internal fun recordarAudio(): EstadoAudio {
    val context = LocalContext.current
    val am = remember { context.getSystemService(Context.AUDIO_SERVICE) as AudioManager }
    var estado by remember { mutableStateOf(leerAudio(am)) }
    DisposableEffect(am) {
        val aviso = object : AudioDeviceCallback() {
            override fun onAudioDevicesAdded(added: Array<out AudioDeviceInfo>?) { estado = leerAudio(am) }
            override fun onAudioDevicesRemoved(removed: Array<out AudioDeviceInfo>?) { estado = leerAudio(am) }
        }
        am.registerAudioDeviceCallback(aviso, null)
        onDispose { am.unregisterAudioDeviceCallback(aviso) }
    }
    return estado
}

private fun leerAudio(am: AudioManager): EstadoAudio {
    val entradas = ArrayList<EntradaAudio>()
    var celular = false
    for (d in am.getDevices(AudioManager.GET_DEVICES_INPUTS)) {
        val nombre = when (d.type) {
            AudioDeviceInfo.TYPE_BUILTIN_MIC -> if (celular) continue else "Celular".also { celular = true }
            AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_HEADSET, AudioDeviceInfo.TYPE_USB_ACCESSORY ->
                "USB" + (d.productName?.toString()?.trim()?.takeIf { it.isNotEmpty() }?.let { " · $it" } ?: "")
            AudioDeviceInfo.TYPE_WIRED_HEADSET -> "Audífonos"
            else -> continue
        }
        val canales = d.channelCounts
        entradas += EntradaAudio(d.id, nombre, canales.isEmpty() || canales.any { it >= 2 })
    }
    val tipos = am.getDevices(AudioManager.GET_DEVICES_OUTPUTS).map { it.type }.toSet()
    val bluetooth = setOf(
        AudioDeviceInfo.TYPE_BLUETOOTH_A2DP, AudioDeviceInfo.TYPE_BLUETOOTH_SCO, AudioDeviceInfo.TYPE_HEARING_AID,
        AudioDeviceInfo.TYPE_BLE_HEADSET, AudioDeviceInfo.TYPE_BLE_SPEAKER,
    )
    val cable = setOf(
        AudioDeviceInfo.TYPE_WIRED_HEADSET, AudioDeviceInfo.TYPE_WIRED_HEADPHONES, AudioDeviceInfo.TYPE_USB_HEADSET,
        AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_LINE_ANALOG, AudioDeviceInfo.TYPE_LINE_DIGITAL,
        AudioDeviceInfo.TYPE_AUX_LINE,
    )
    val salida = when {
        tipos.any { it in bluetooth } -> Salida.BLUETOOTH
        tipos.any { it in cable } -> Salida.CABLE
        else -> Salida.BOCINA
    }
    return EstadoAudio(entradas, salida)
}
