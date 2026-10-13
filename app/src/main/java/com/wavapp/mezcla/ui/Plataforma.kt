package com.wavapp.mezcla.ui

import android.graphics.Bitmap
import android.os.Build
import android.view.View
import android.view.WindowInsetsController
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import com.wavapp.mezcla.Mesa

// Lo que obliga Android (y nada más): botón "atrás", pantalla encendida, selector de archivos,
// color de los íconos de la barra de estado e imágenes desde píxeles.

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

/** Abre el selector de audio del sistema; lo elegido se importa en el núcleo. */
@Composable
internal fun recordarAbrirArchivos(): () -> Unit {
    val context = LocalContext.current
    val elegir = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        Mesa.agregarArchivos(context, uris)
    }
    return remember(elegir) { { elegir.launch(arrayOf("audio/*")) } }
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

/** Imagen a partir de píxeles ARGB (el espectrograma). */
internal fun imagenDePixeles(px: IntArray, ancho: Int, alto: Int): ImageBitmap =
    Bitmap.createBitmap(px, ancho, alto, Bitmap.Config.ARGB_8888).asImageBitmap()
