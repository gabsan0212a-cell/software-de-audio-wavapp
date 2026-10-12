package com.wavapp.mezcla

import android.app.Activity
import android.os.Bundle
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var button: Button
    private var toneOn = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        status = TextView(this).apply { textSize = 18f }
        button = Button(this).apply {
            text = "Tono 440 Hz"
            setOnClickListener {
                toneOn = !toneOn
                NativeAudio.setToneOn(toneOn)
                text = if (toneOn) "Detener tono" else "Tono 440 Hz"
            }
        }
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(48, 48, 48, 48)
            addView(status)
            addView(button)
        })
    }

    override fun onStart() {
        super.onStart()
        val ok = NativeAudio.start()
        status.text = if (ok) "Motor listo: ${NativeAudio.sampleRate()} Hz" else "Error al abrir el audio"
    }

    override fun onStop() {
        toneOn = false
        NativeAudio.setToneOn(false)
        NativeAudio.stop()
        button.text = "Tono 440 Hz"
        super.onStop()
    }
}
