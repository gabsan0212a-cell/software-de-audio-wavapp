package com.wavapp.mezcla

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.Locale
import java.util.UUID
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.pow

/** Nivel de un medidor en dB, con caída. Se lee al dibujar, así no recompone la pantalla. */
class Medidor {
    var l by mutableFloatStateOf(-120f)
    var r by mutableFloatStateOf(-120f)
}

class Pista(
    val slot: Int,
    val id: Int,
    val nombre: String,
    val archivo: String,  // dentro de la carpeta de audio de la app
    val canales: Int,
    val cuadros: Long,
    val info: String,
) {
    var volDb by mutableFloatStateOf(0f)  // -60 (= -∞) .. +6
    var pan by mutableIntStateOf(0)       // -100 .. 100
    var mute by mutableStateOf(false)
    var solo by mutableStateOf(false)
    var fase by mutableStateOf(false)
    val medidor = Medidor()
    val segundos: Double get() = cuadros / Mesa.RATE
    val color: Int get() = (id - 1) % 12
}

class MedidorMaster {
    var l by mutableFloatStateOf(-120f)
    var r by mutableFloatStateOf(-120f)
    var tickL by mutableFloatStateOf(-120f)
    var tickR by mutableFloatStateOf(-120f)
    var picoL by mutableFloatStateOf(-120f)
    var picoR by mutableFloatStateOf(-120f)
    var clipL by mutableStateOf(false)
    var clipR by mutableStateOf(false)
    var reduccion by mutableFloatStateOf(0f)
    var tickTiempoL = 0L
    var tickTiempoR = 0L
}

/** Estado de la mesa (vive mientras viva el proceso, igual que el motor nativo). */
object Mesa {
    const val RATE = 48000.0
    const val MAX_PISTAS = 12
    const val VOL_MIN = -60f
    const val VOL_MAX = 6f
    const val MASTER_MIN = -70f

    private lateinit var carpeta: File
    private lateinit var archivoSesion: File
    private var iniciada = false
    private val scope = MainScope()
    private var guardado: Job? = null
    private var siguienteId = 1

    val pistas = mutableStateListOf<Pista>()

    var masterDb by mutableFloatStateOf(0f)
        private set
    var masterMute by mutableStateOf(false)
        private set
    var limitador by mutableStateOf(true)
        private set

    var reproduciendo by mutableStateOf(false)
        private set
    var posicion by mutableDoubleStateOf(0.0)
        private set
    var duracion by mutableDoubleStateOf(0.0)
        private set

    var estado by mutableStateOf("")
        private set
    var ocupada by mutableStateOf(false)
        private set
    var ocupadaTexto by mutableStateOf("")
        private set
    var progreso by mutableFloatStateOf(-1f)  // -1 = sin barra
        private set

    val master = MedidorMaster()
    private val bufMedidores = FloatArray(3 + 2 * MAX_PISTAS)
    private var ultimoTick = 0L

    // ------------------------------------------------------------------ inicio

    fun iniciar(context: Context) {
        if (iniciada) return
        iniciada = true
        carpeta = File(context.filesDir, "audio").apply { mkdirs() }
        archivoSesion = File(context.filesDir, "sesion.json")
        aplicarMaster()
        // Igual que el prototipo: la primera vez arranca con las pistas de prueba.
        if (archivoSesion.exists()) restaurar() else cargarPrueba()
    }

    // ------------------------------------------------------------------ pistas

    fun agregarArchivos(context: Context, uris: List<Uri>) {
        if (ocupada || uris.isEmpty()) return
        scope.launch {
            ocupada = true
            val errores = mutableListOf<String>()
            var agregadas = 0
            for (uri in uris) {
                val nombreArchivo = nombreDe(context, uri)
                if (pistas.size >= MAX_PISTAS) {
                    errores += "límite de $MAX_PISTAS pistas"
                    break
                }
                ocupadaTexto = "Importando $nombreArchivo…"
                progreso = 0f
                val archivo = "pista-${UUID.randomUUID()}.wav"
                val (r, error) = withContext(Dispatchers.IO) {
                    try {
                        val fd = context.contentResolver.openFileDescriptor(uri, "r")?.use { it.detachFd() }
                        if (fd == null) {
                            null to "no se pudo abrir"
                        } else {
                            val res = NativeAudio.importWav(fd, File(carpeta, archivo).path)
                            res to (if (res == null) NativeAudio.lastError() else "")
                        }
                    } catch (e: Exception) {
                        null to (e.message ?: "no se pudo abrir")
                    }
                }
                if (r == null) {
                    errores += "$nombreArchivo: $error"
                    continue
                }
                val p = Pista(
                    slot = r[0].toInt(), id = siguienteId++, nombre = sinExtension(nombreArchivo),
                    archivo = archivo, canales = r[1].toInt(), cuadros = r[2],
                    info = describir(r[3].toInt(), r[4].toInt(), r[5].toInt(), r[6] == 1L, r[7] == 1L),
                )
                pistas.add(p)
                aplicar(p)
                agregadas++
            }
            progreso = -1f
            ocupada = false
            val msg = when {
                errores.isEmpty() -> "$agregadas pista(s) agregada(s)"
                else -> (if (agregadas > 0) "$agregadas agregada(s); " else "") +
                    "problemas: " + errores.joinToString("; ") + ". Por ahora solo se aceptan WAV"
            }
            actualizarEstado(msg)
            pedirGuardar()
        }
    }

    fun cargarPrueba() {
        if (ocupada) return
        scope.launch {
            ocupada = true
            ocupadaTexto = "Creando pistas de prueba…"
            // Nombre, paneo y volumen como en el prototipo.
            val demos = listOf(Triple("Bajo", 0, 0f), Triple("Voz", 0, 0f),
                Triple("Guitarra", 60, -2f), Triple("Teclado", -60, -3f))
            var n = 0
            for ((i, d) in demos.withIndex()) {
                if (pistas.size >= MAX_PISTAS) break
                val archivo = "prueba-${UUID.randomUUID()}.wav"
                val r = withContext(Dispatchers.IO) { NativeAudio.addDemoTrack(i, File(carpeta, archivo).path) }
                    ?: continue
                val p = Pista(r[0].toInt(), siguienteId++, d.first, archivo, r[1].toInt(), r[2], "prueba · mono · 12 s")
                p.pan = d.second
                p.volDb = d.third
                pistas.add(p)
                aplicar(p)
                n++
            }
            ocupada = false
            actualizarEstado(if (n > 0) "Pistas de prueba cargadas" else "Límite de $MAX_PISTAS pistas")
            pedirGuardar()
        }
    }

    fun quitar(p: Pista) {
        if (!pistas.remove(p)) return
        if (pistas.isEmpty()) NativeAudio.stopTransport()
        scope.launch {
            withContext(Dispatchers.IO) {
                NativeAudio.removeTrack(p.slot)
                File(carpeta, p.archivo).delete()
            }
            actualizarEstado()
            pedirGuardar()
        }
    }

    fun ponerVolumen(p: Pista, db: Float) {
        p.volDb = (Math.round(db.coerceIn(VOL_MIN, VOL_MAX) * 2) / 2f)
        NativeAudio.setTrackGain(p.slot, ganancia(p.volDb, VOL_MIN + 0.1f))
        pedirGuardar()
    }

    fun ponerPan(p: Pista, pan: Float) {
        p.pan = Math.round(pan.coerceIn(-100f, 100f))
        NativeAudio.setTrackPan(p.slot, p.pan / 100f)
        pedirGuardar()
    }

    fun alternarMute(p: Pista) { p.mute = !p.mute; NativeAudio.setTrackMute(p.slot, p.mute); pedirGuardar() }
    fun alternarSolo(p: Pista) { p.solo = !p.solo; NativeAudio.setTrackSolo(p.slot, p.solo); pedirGuardar() }
    fun alternarFase(p: Pista) { p.fase = !p.fase; NativeAudio.setTrackInvert(p.slot, p.fase); pedirGuardar() }

    /** ¿Suena esta pista con el mute/solo actual? (para atenuar su carril, como el prototipo) */
    fun suena(p: Pista): Boolean = !p.mute && (pistas.none { it.solo } || p.solo)

    private fun aplicar(p: Pista) {
        NativeAudio.setTrackGain(p.slot, ganancia(p.volDb, VOL_MIN + 0.1f))
        NativeAudio.setTrackPan(p.slot, p.pan / 100f)
        NativeAudio.setTrackMute(p.slot, p.mute)
        NativeAudio.setTrackSolo(p.slot, p.solo)
        NativeAudio.setTrackInvert(p.slot, p.fase)
    }

    // ------------------------------------------------------------------ master

    fun ponerMaster(db: Float) {
        masterDb = Math.round(db.coerceIn(MASTER_MIN, VOL_MAX) * 2) / 2f
        aplicarMaster()
        pedirGuardar()
    }

    fun alternarMasterMute() { masterMute = !masterMute; aplicarMaster(); pedirGuardar() }

    fun alternarLimitador() {
        limitador = !limitador
        NativeAudio.setLimiter(limitador)
        pedirGuardar()
    }

    fun borrarClip() {
        master.clipL = false; master.clipR = false
        master.picoL = -120f; master.picoR = -120f
    }

    private fun aplicarMaster() {
        NativeAudio.setMasterGain(if (masterMute) 0f else ganancia(masterDb, MASTER_MIN + 0.5f))
        NativeAudio.setLimiter(limitador)
    }

    // ------------------------------------------------------------------ transporte

    fun reproducirPausar() {
        if (pistas.isEmpty()) return
        if (NativeAudio.isPlaying()) NativeAudio.pause() else NativeAudio.play()
        reproduciendo = NativeAudio.isPlaying()
    }

    fun detener() {
        NativeAudio.stopTransport()
        reproduciendo = false
    }

    fun saltar(segundos: Double) {
        NativeAudio.seek(segundos.coerceIn(0.0, duracion))
        posicion = NativeAudio.positionSeconds()
    }

    /** Se llama en cada cuadro de pantalla: lee posición y medidores del motor. */
    fun tick(ahoraMs: Long) {
        val dt = if (ultimoTick == 0L) 0.016f else ((ahoraMs - ultimoTick).coerceIn(0, 200) / 1000f)
        ultimoTick = ahoraMs
        reproduciendo = NativeAudio.isPlaying()
        posicion = NativeAudio.positionSeconds()
        duracion = NativeAudio.durationSeconds()
        if (ocupada && progreso >= 0f) progreso = NativeAudio.importProgress()

        NativeAudio.readMeters(bufMedidores)
        // Caídas del prototipo (por cuadro de ~33 ms): pistas 1.5 dB, master 1.2 dB; la marca
        // de pico se queda 1.2 s y luego baja 0.5 dB.
        val caidaPista = 45f * dt
        val caidaMaster = 36f * dt
        for (p in pistas) {
            p.medidor.l = max(db(bufMedidores[3 + 2 * p.slot]), p.medidor.l - caidaPista)
            p.medidor.r = max(db(bufMedidores[4 + 2 * p.slot]), p.medidor.r - caidaPista)
        }
        val m = master
        val dl = db(bufMedidores[0])
        val dr = db(bufMedidores[1])
        m.l = max(dl, m.l - caidaMaster)
        m.r = max(dr, m.r - caidaMaster)
        if (dl >= m.tickL) { m.tickL = dl; m.tickTiempoL = ahoraMs } else if (ahoraMs - m.tickTiempoL > 1200) m.tickL = max(dl, m.tickL - 15f * dt)
        if (dr >= m.tickR) { m.tickR = dr; m.tickTiempoR = ahoraMs } else if (ahoraMs - m.tickTiempoR > 1200) m.tickR = max(dr, m.tickR - 15f * dt)
        if (dl > m.picoL) m.picoL = dl
        if (dr > m.picoR) m.picoR = dr
        if (bufMedidores[0] >= 0.999f) m.clipL = true
        if (bufMedidores[1] >= 0.999f) m.clipR = true
        m.reduccion = bufMedidores[2]
    }

    // ------------------------------------------------------------------ sesión

    fun guardarYa() {
        if (!iniciada) return
        guardado?.cancel()
        try {
            archivoSesion.writeText(sesionJson().toString())
        } catch (_: Exception) {
        }
    }

    private fun pedirGuardar() {
        guardado?.cancel()
        guardado = scope.launch {
            delay(400)
            val texto = sesionJson().toString()
            withContext(Dispatchers.IO) {
                try {
                    val tmp = File(archivoSesion.path + ".tmp")
                    tmp.writeText(texto)
                    tmp.renameTo(archivoSesion)
                } catch (_: Exception) {
                }
            }
        }
    }

    private fun sesionJson(): JSONObject {
        val arr = JSONArray()
        for (p in pistas) {
            arr.put(JSONObject().apply {
                put("id", p.id); put("nombre", p.nombre); put("archivo", p.archivo); put("info", p.info)
                put("volDb", p.volDb.toDouble()); put("pan", p.pan)
                put("mute", p.mute); put("solo", p.solo); put("fase", p.fase)
            })
        }
        return JSONObject().apply {
            put("version", 1)
            put("siguienteId", siguienteId)
            put("master", JSONObject().apply {
                put("volDb", masterDb.toDouble()); put("mute", masterMute); put("limitador", limitador)
            })
            put("pistas", arr)
        }
    }

    private fun restaurar() {
        scope.launch {
            ocupada = true
            ocupadaTexto = "Abriendo la mezcla anterior…"
            val json = withContext(Dispatchers.IO) {
                try { JSONObject(archivoSesion.readText()) } catch (_: Exception) { null }
            }
            if (json == null) {
                ocupada = false
                cargarPrueba()
                return@launch
            }
            json.optJSONObject("master")?.let {
                masterDb = it.optDouble("volDb", 0.0).toFloat()
                masterMute = it.optBoolean("mute", false)
                limitador = it.optBoolean("limitador", true)
            }
            aplicarMaster()
            siguienteId = json.optInt("siguienteId", 1)
            val arr = json.optJSONArray("pistas") ?: JSONArray()
            var faltan = 0
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                val archivo = o.optString("archivo")
                val r = withContext(Dispatchers.IO) { NativeAudio.addTrackFile(File(carpeta, archivo).path) }
                if (r == null) {
                    faltan++
                    continue
                }
                val p = Pista(r[0].toInt(), o.optInt("id", siguienteId), o.optString("nombre", "pista"),
                    archivo, r[1].toInt(), r[2], o.optString("info"))
                p.volDb = o.optDouble("volDb", 0.0).toFloat()
                p.pan = o.optInt("pan", 0)
                p.mute = o.optBoolean("mute", false)
                p.solo = o.optBoolean("solo", false)
                p.fase = o.optBoolean("fase", false)
                siguienteId = max(siguienteId, p.id + 1)
                pistas.add(p)
                aplicar(p)
            }
            limpiarHuerfanos()
            ocupada = false
            actualizarEstado(if (faltan > 0) "$faltan pista(s) no se encontraron" else "Mezcla recuperada")
        }
    }

    private suspend fun limpiarHuerfanos() {
        val usados = pistas.map { it.archivo }.toSet()
        withContext(Dispatchers.IO) {
            carpeta.listFiles()?.forEach { if (it.name !in usados) it.delete() }
        }
    }

    // ------------------------------------------------------------------ utilidades

    fun avisar(msg: String) {
        estado = msg
    }

    private fun actualizarEstado(msg: String = "") {
        val base = if (pistas.isEmpty()) {
            "Sin pistas. Agrega archivos WAV o carga las pistas de prueba."
        } else {
            val mb = pistas.sumOf { File(carpeta, it.archivo).length() } / 1048576.0
            "${pistas.size} pista(s) · 48000 Hz · ≈ ${Math.round(mb)} MB en disco"
        }
        estado = if (msg.isEmpty()) base else "$msg · $base"
    }

    private fun ganancia(db: Float, apagadoBajoDe: Float): Float =
        if (db <= apagadoBajoDe) 0f else 10f.pow(db / 20f)

    private fun db(x: Float): Float = if (x > 1e-6f) 20f * log10(x) else -120f

    private fun nombreDe(context: Context, uri: Uri): String {
        try {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) return it.getString(0)
            }
        } catch (_: Exception) {
        }
        return uri.lastPathSegment ?: "archivo"
    }

    private fun sinExtension(n: String): String = n.replace(Regex("\\.[^.]+$"), "").ifEmpty { n }

    private fun describir(rate: Int, canales: Int, bits: Int, esFloat: Boolean, dual: Boolean): String {
        val khz = if (rate % 1000 == 0) "${rate / 1000}" else String.format(Locale.US, "%.1f", rate / 1000.0)
        val tipo = if (esFloat) "$bits bits float" else "$bits bits"
        val ch = when {
            dual -> "estéreo idéntico → mono"
            canales == 1 -> "mono"
            canales == 2 -> "estéreo"
            else -> "$canales canales → estéreo"
        }
        val conv = if (rate != RATE.toInt()) " → 48 kHz" else ""
        return "$khz kHz$conv · $tipo · $ch"
    }
}
