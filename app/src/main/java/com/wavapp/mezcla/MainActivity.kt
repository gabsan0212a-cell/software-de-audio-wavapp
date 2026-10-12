package com.wavapp.mezcla

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import com.wavapp.mezcla.ui.PantallaMesa
import com.wavapp.mezcla.ui.Tema

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        Mesa.iniciar(applicationContext)
        setContent { Tema { PantallaMesa() } }
    }

    override fun onStart() {
        super.onStart()
        if (!NativeAudio.start()) Mesa.avisar("No se pudo abrir la salida de audio")
    }

    override fun onStop() {
        Mesa.guardarYa()
        NativeAudio.pause()
        NativeAudio.stop()
        super.onStop()
    }
}
