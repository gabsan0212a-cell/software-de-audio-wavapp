package com.wavapp.mezcla

import android.graphics.Color
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import com.wavapp.mezcla.ui.PantallaMesa
import com.wavapp.mezcla.ui.Tema

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        // Barras del sistema transparentes: la pantalla se dibuja detrás (la barra de pestañas
        // llega hasta abajo, como en iOS).
        enableEdgeToEdge(navigationBarStyle = SystemBarStyle.auto(Color.TRANSPARENT, Color.TRANSPARENT))
        if (Build.VERSION.SDK_INT >= 29) window.isNavigationBarContrastEnforced = false
        super.onCreate(savedInstanceState)
        Mesa.iniciar(applicationContext)
        setContent { Tema { PantallaMesa() } }
    }

    override fun onStart() {
        super.onStart()
        Mesa.enPantalla = true
        Nucleo.start()  // si la salida de audio no abre, el núcleo lo avisa en el estado
        Mesa.sincronizar()
    }

    override fun onStop() {
        Mesa.enPantalla = false
        Mesa.guardarYa()
        Nucleo.pause()
        Nucleo.stop()
        super.onStop()
    }
}
