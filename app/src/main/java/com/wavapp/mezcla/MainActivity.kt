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
