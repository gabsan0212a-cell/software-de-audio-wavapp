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
        suspend fun arrastrar(x0: Float, y0: Float, x1: Float, y1: Float) {
            val a = Offset(x0 * DENS, y0 * DENS)
            scene.sendPointerEvent(PointerEventType.Press, a)
            for (k in 1..12) {
                val f = k / 12f
                scene.sendPointerEvent(PointerEventType.Move, Offset((x0 + (x1 - x0) * f) * DENS, (y0 + (y1 - y0) * f) * DENS))
                cuadro()
                delay(16)
            }
            scene.sendPointerEvent(PointerEventType.Release, Offset(x1 * DENS, y1 * DENS))
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
        if (quiere("automatizacion")) {
            if (Mesa.reproduciendo) Mesa.reproducirPausar()
            val p0 = Mesa.pistas.first()
            Vista.pestana = Vista.EDITAR
            Edicion.ajustarVista()
            Edicion.verAuto(Nucleo.AUTO_VOLUMEN)
            esperar(400)
            // Toques de verdad en el carril de la pista 1: poner un punto y arrastrarlo.
            val anchoDp = ANCHO / DENS
            tocar(anchoDp * 0.25f, 228f)
            esperar(500)
            println("toque en el carril -> puntos de volumen: ${p0.autoVol.n}")
            val x0 = anchoDp * 0.25f
            arrastrar(x0, 228f, x0 + 40f, 210f)
            esperar(300)
            println("arrastre -> punto en ${p0.autoVol.cuadros.firstOrNull()} (${p0.autoVol.valores.firstOrNull()} dB)")
            Mesa.agregarPunto(p0, Nucleo.AUTO_VOLUMEN, 48000L * 6, 0.35f)
            Mesa.agregarPunto(p0, Nucleo.AUTO_VOLUMEN, 48000L * 9, 0.75f)
            val p1 = Mesa.pistas[1]
            Mesa.agregarPunto(p1, Nucleo.AUTO_VOLUMEN, 48000L * 2, 0.5f)
            Mesa.agregarPunto(p1, Nucleo.AUTO_VOLUMEN, 48000L * 4, 0.69f)
            Mesa.saltar(5.0)
            esperar(400)
            foto("20-auto-volumen")
            Edicion.verAuto(Nucleo.AUTO_PANEO)
            val p2 = Mesa.pistas[2]
            for ((t, pos) in listOf(0.0 to 0.5f, 3.0 to 0.9f, 6.0 to 0.1f, 9.0 to 0.9f, 12.0 to 0.5f))
                Mesa.agregarPunto(p2, Nucleo.AUTO_PANEO, (t * 48000).toLong(), pos)
            esperar(400)
            foto("21-auto-paneo", ambos = false)
            // Grabar movimientos: el volumen de la pista 2 mientras suena.
            Vista.pestana = Vista.MEZCLA
            Mesa.ponerModoAuto(p1, Nucleo.AUTO_GRABAR)
            Mesa.saltar(0.0)
            Mesa.reproducirPausar()
            esperar(500)
            Mesa.tocarDeslizador(p1, Nucleo.AUTO_VOLUMEN, true)
            // Como un dedo: un valor cada 16 ms (dibujar aquí es lento: solo de vez en cuando).
            val fin = System.currentTimeMillis() + 1500
            var k = 0
            while (System.currentTimeMillis() < fin) {
                val x = 1f - (fin - System.currentTimeMillis()) / 1500f
                Mesa.ponerVolumen(p1, -18f * x)
                if (++k % 8 == 0) cuadro() else Mesa.sincronizar()
                delay(16)
            }
            foto("22-grabando-movimientos", ambos = false)
            Mesa.tocarDeslizador(p1, Nucleo.AUTO_VOLUMEN, false)
            println("grabado -> puntos de volumen de la pista 2: ${p1.autoVol.n} (vueltas: $k)")
            esperar(500)
            foto("23-siguiendo", ambos = false)
            if (Mesa.reproduciendo) Mesa.reproducirPausar()
            Vista.automatizacion = p1
            esperar(600)
            foto("24-hoja-auto")
            Vista.automatizacion = null
            esperar(400)
            Vista.pestana = Vista.EDITAR
            Edicion.verAuto(Nucleo.AUTO_VOLUMEN)
            esperar(400)
            foto("25-auto-grabada", ambos = false)
            Edicion.vista = Edicion.PEDAZOS
            Vista.pestana = Vista.MEZCLA
            esperar(300)
        }
        if (quiere("efectos")) {
            Vista.pestana = Vista.MEZCLA
            val voz = Mesa.pistas[1]
            Mesa.aplicarPresetFx(voz, 1)  // voz con eco
            Mesa.aplicarPresetFx(Mesa.pistas[0], 5)  // bajo
            Mesa.saltar(0.0)
            if (!Mesa.reproduciendo) Mesa.reproducirPausar()
            esperar(1500)
            foto("26-mezcla-fx", ambos = false)
            Vista.efectos = voz
            esperar(800)
            foto("27-efectos")
            rueda(500f, 40)
            esperar(500)
            foto("28-efectos-abajo", ambos = false)
            Vista.efectos = null
            esperar(500)
        }
        if (quiere("lufs")) {
            Vista.pestana = Vista.MEZCLA
            Mesa.saltar(0.0)
            Mesa.reiniciarLufs()
            if (!Mesa.reproduciendo) Mesa.reproducirPausar()
            esperar(6000)
            Vista.pestana = Vista.ANALISIS
            esperar(800)
            foto("29-lufs")
            Mesa.medirCancion()
            hasta(30_000, "canción medida") { Mesa.volumen.cancion == 2 }
            rueda(400f, 30)
            esperar(600)
            foto("30-lufs-cancion")
            rueda(400f, -40)
            Vista.pestana = Vista.MEZCLA
            esperar(300)
            rueda(400f, 120)
            esperar(600)
            foto("31-master-lufs", ambos = false)
            rueda(400f, -120)
            if (Mesa.reproduciendo) Mesa.reproducirPausar()
            Mesa.ponerAjusteAlExportar(true)
            Vista.exportar = true
            esperar(700)
            foto("32-exportar-lufs")
            Vista.exportar = false
            Mesa.ponerAjusteAlExportar(false)
            esperar(400)
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
        if (quiere("grabar")) {
            Vista.pestana = Vista.GRABAR
            hasta(5000, "entrada abierta") { Mesa.entrada.abierta }
            esperar(1200)
            foto("14-grabar")
            Mesa.ponerEscucha(true)
            val e = Mesa.entrada.pista
            Mesa.agregarBanda(e)
            Mesa.cambiarBanda(e, e.bandas[0], tipo = 3, f = 120f, q = 0.71f)
            Mesa.agregarBanda(e)
            Mesa.cambiarBanda(e, e.bandas[1], f = 3500f, g = 4f, q = 1.2f)
            Mesa.saltar(1.0)
            esperar(300)
            Mesa.grabarOParar()
            esperar(2500)
            foto("15-grabando")
            val id = Mesa.grabarOParar()
            if (id > 0) Vista.ultimaGrabacion = id
            esperar(500)
            foto("16-grabado", ambos = false)
            Mesa.eqAbierta = Mesa.entrada.pista
            esperar(800)
            foto("17-eq-entrada")
            Mesa.eqAbierta = null
            esperar(400)
            Analisis.fuenteId = com.wavapp.mezcla.Nucleo.ID_ENTRADA
            Vista.pestana = Vista.ANALISIS
            esperar(1500)
            foto("18-analisis-micro")
            Analisis.fuenteId = -1
            Mesa.ponerEscucha(false)
            Vista.pestana = Vista.MEZCLA
            esperar(500)
            foto("19-con-grabacion", ambos = false)
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
