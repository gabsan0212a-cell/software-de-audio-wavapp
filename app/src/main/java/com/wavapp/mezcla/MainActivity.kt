package com.wavapp.mezcla

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var fileInfo: TextView
    private lateinit var position: TextView
    private lateinit var playButton: Button
    private val handler = Handler(Looper.getMainLooper())
    private var duration = 0.0

    private val ticker = object : Runnable {
        override fun run() {
            refresh()
            handler.postDelayed(this, 100)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        status = TextView(this).apply { textSize = 16f }
        fileInfo = TextView(this).apply { textSize = 16f; text = "Ningún archivo cargado" }
        position = TextView(this).apply { textSize = 32f; text = "0:00.0 / 0:00.0" }
        val openButton = Button(this).apply {
            text = "Abrir WAV"
            setOnClickListener { pickWav() }
        }
        playButton = Button(this).apply {
            text = "▶ Reproducir"
            isEnabled = false
            setOnClickListener {
                if (NativeAudio.isPlaying()) NativeAudio.pause() else NativeAudio.play()
                refresh()
            }
        }
        val stopButton = Button(this).apply {
            text = "⏹ Detener"
            setOnClickListener { NativeAudio.stopPlayback(); refresh() }
        }
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(48, 48, 48, 48)
            addView(status)
            addView(openButton)
            addView(fileInfo)
            addView(position)
            addView(playButton)
            addView(stopButton)
        })
    }

    override fun onStart() {
        super.onStart()
        val ok = NativeAudio.start()
        status.text = if (ok) "Motor listo: ${NativeAudio.deviceRate()} Hz" else "Error al abrir el audio"
        handler.post(ticker)
    }

    override fun onStop() {
        handler.removeCallbacks(ticker)
        NativeAudio.pause()
        NativeAudio.stop()
        super.onStop()
    }

    private fun pickWav() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "audio/*"
        }
        @Suppress("DEPRECATION")
        startActivityForResult(intent, PICK_WAV)
    }

    @Deprecated("Activity básica, sin AndroidX")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if (requestCode != PICK_WAV || resultCode != RESULT_OK || uri == null) return
        loadWav(uri)
    }

    private fun loadWav(uri: Uri) {
        val name = displayName(uri)
        fileInfo.text = "Cargando $name…"
        playButton.isEnabled = false
        // Leer un archivo grande puede tardar: lo hacemos fuera del hilo de la pantalla.
        Thread {
            val error = try {
                val fd = contentResolver.openFileDescriptor(uri, "r")!!.detachFd()
                NativeAudio.loadWav(fd)
            } catch (e: Exception) {
                "No se pudo abrir: ${e.message}"
            }
            runOnUiThread { showLoaded(name, error) }
        }.start()
    }

    private fun showLoaded(name: String, error: String) {
        val info = NativeAudio.clipInfo()
        if (error.isNotEmpty() || info == null) {
            fileInfo.text = "Error: $error"
            return
        }
        val (rate, channels, bits, isFloat, frames) = info
        duration = frames.toDouble() / rate
        val type = if (isFloat == 1L) "$bits bits float" else "$bits bits"
        val chText = if (channels == 1L) "mono" else if (channels == 2L) "estéreo" else "$channels canales"
        fileInfo.text = "$name\n$rate Hz · $type · $chText"
        playButton.isEnabled = true
        refresh()
    }

    private fun refresh() {
        position.text = "${formatTime(NativeAudio.positionSeconds())} / ${formatTime(duration)}"
        playButton.text = if (NativeAudio.isPlaying()) "⏸ Pausa" else "▶ Reproducir"
    }

    private fun displayName(uri: Uri): String {
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) return it.getString(0)
        }
        return uri.lastPathSegment ?: "archivo"
    }

    private fun formatTime(seconds: Double): String {
        val tenths = (seconds * 10).toLong()
        return "%d:%02d.%d".format(tenths / 600, (tenths / 10) % 60, tenths % 10)
    }

    companion object {
        private const val PICK_WAV = 1
    }
}
