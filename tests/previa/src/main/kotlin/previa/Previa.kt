package previa

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.ImageComposeScene
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.unit.Density
import com.wavapp.mezcla.Analisis
import com.wavapp.mezcla.Edicion
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.ui.PantallaMesa
import com.wavapp.mezcla.ui.Tema
import com.wavapp.mezcla.ui.Vista
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import org.jetbrains.skia.EncodedImageFormat
import java.io.File

var oscuro by mutableStateOf(false)

const val ANCHO = 1080
const val ALTO = 2280
const val DENS = 2.625f

fun main(args: Array<String>) {
    val salida = File(args[0]).apply { mkdirs() }
    val escenas = (args.getOrNull(1) ?: "todas").split(",").toSet()
    val dir = File(System.getProperty("java.io.tmpdir"), "wavapp-previa").apply { deleteRecursively(); mkdirs() }
    runBlocking(Dispatchers.Main) {
        Mesa.iniciar(Context(dir))
        Nucleo.start()
        val scene = ImageComposeScene(ANCHO, ALTO, Density(DENS), coroutineContext = Dispatchers.Main) {
            Tema(oscuro) { PantallaMesa() }
        }
        fun cuadro() = scene.render(System.nanoTime())
        suspend fun esperar(ms: Long) {
            val fin = System.currentTimeMillis() + ms
            while (System.currentTimeMillis() < fin) {
                cuadro()
                delay(16)
            }
        }
        suspend fun hasta(ms: Long, que: String, cond: () -> Boolean) {
            val fin = System.currentTimeMillis() + ms
            while (System.currentTimeMillis() < fin && !cond()) {
                cuadro()
                delay(16)
            }
            if (!cond()) println("AVISO: no se cumplió: $que")
        }
        suspend fun foto(nombre: String, ambos: Boolean = true) {
            val modos = if (ambos) listOf(false, true) else listOf(false)
            for (m in modos) {
                oscuro = m
                esperar(400)
                val img = cuadro()
                val f = File(salida, (if (m) "oscuro-" else "claro-") + nombre + ".png")
                f.writeBytes(img.encodeToData(EncodedImageFormat.PNG)!!.bytes)
                println("captura ${f.name}")
            }
            oscuro = false
        }
        fun tocar(xDp: Float, yDp: Float) {
            val o = Offset(xDp * DENS, yDp * DENS)
            scene.sendPointerEvent(PointerEventType.Press, o)
            scene.sendPointerEvent(PointerEventType.Release, o)
        }
        fun rueda(yDp: Float, pasos: Int) {
            repeat(kotlin.math.abs(pasos)) {
                scene.sendPointerEvent(
                    PointerEventType.Scroll, Offset(200 * DENS, yDp * DENS),
                    scrollDelta = Offset(0f, if (pasos > 0) 1f else -1f),
                )
            }
        }
        fun quiere(e: String) = "todas" in escenas || e in escenas

        hasta(20_000, "pistas de prueba") { Mesa.pistas.size >= 4 && !Mesa.ocupada }
        esperar(300)
        if (quiere("inicio")) foto("1-inicio")
        Mesa.reproducirPausar()
        esperar(1500)
        if (quiere("reproduciendo")) foto("2-reproduciendo")
        if (quiere("master")) {
            rueda(500f, 60)
            esperar(300)
            foto("3-master")
            rueda(500f, -40)
            esperar(300)
        }
        if (quiere("reproductor")) {
            Vista.reproductor = true
            esperar(600)
            foto("4-reproductor")
            Vista.reproductor = false
            esperar(500)
        }
        if (quiere("eq")) {
            val p = Mesa.pistas.first()
            Mesa.eqAbierta = p
            Mesa.agregarBanda(p)
            Mesa.agregarBanda(p)
            Mesa.cambiarBanda(p, p.bandas[0], tipo = 1, f = 120f, g = 5f)
            Mesa.cambiarBanda(p, p.bandas[1], f = 3000f, g = -4f, q = 2f)
            esperar(600)
            foto("5-eq")
            Mesa.eqAbierta = null
            esperar(500)
        }
        if (quiere("analisis")) {
            Vista.pestana = Vista.ANALISIS
            Nucleo.seek(0.0)
            if (!Mesa.reproduciendo) Mesa.reproducirPausar()
            hasta(8000, "análisis activo") { Analisis.activo && Analisis.fuerte }
            esperar(800)
            foto("6-analisis")
            Analisis.fuenteId = Mesa.pistas[1].id
            hasta(20_000, "espectrograma") { Analisis.espectrogramas.containsKey(Mesa.pistas[1].id) }
            rueda(500f, 60)
            esperar(500)
            foto("7-espectrograma")
            rueda(500f, -40)
        }
        if (quiere("edicion")) {
            if (Mesa.reproduciendo) Mesa.reproducirPausar()
            Vista.pestana = Vista.EDITAR
            Mesa.saltar(6.0)
            esperar(300)
            val p0 = Mesa.pistas.first()
            Edicion.seleccion = Edicion.Seleccion(p0.id, 0)
            Edicion.cortarEnCursor()
            esperar(300)
            foto("8-edicion")
            Edicion.deshacer()
            Edicion.seleccion = null
        }
        if (quiere("exportar")) {
            Vista.pestana = Vista.MEZCLA
            Vista.exportar = true
            esperar(600)
            foto("9-exportar")
            Vista.exportar = false
            esperar(500)
        }
        if (quiere("menus")) {
            Vista.pestana = Vista.MEZCLA
            Vista.opciones = Mesa.pistas.first()
            esperar(500)
            foto("10-opciones")
            Vista.opciones = null
            Vista.aQuitar = Mesa.pistas.first()
            esperar(500)
            foto("11-quitar")
            Vista.aQuitar = null
            Vista.agregar = true
            esperar(500)
            foto("12-agregar")
            Vista.agregar = false
            esperar(400)
        }
        if (quiere("tocar")) {
            // Toques de verdad sobre la pantalla (como un dedo).
            Vista.pestana = Vista.MEZCLA
            esperar(300)
            val altoDp = ALTO / DENS
            tocar(ANCHO / DENS / 2, altoDp - 25)
            esperar(300)
            println("toque pestaña Editar -> pestana=${Vista.pestana}")
            tocar(ANCHO / DENS / 6, altoDp - 25)
            esperar(300)
            println("toque pestaña Mezcla -> pestana=${Vista.pestana}")
        }
        if (quiere("vacio")) {
            if (Mesa.reproduciendo) Mesa.reproducirPausar()
            Nucleo.clearProject()
            Mesa.sincronizar()
            esperar(500)
            foto("13-vacio")
        }
        scene.close()
        Nucleo.stop()
    }
    System.exit(0)
}
