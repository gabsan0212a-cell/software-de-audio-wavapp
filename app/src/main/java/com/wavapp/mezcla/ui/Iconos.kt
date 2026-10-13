package com.wavapp.mezcla.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.addPathNodes
import androidx.compose.ui.unit.dp

/** Arma un ícono de 24×24 con rutas rellenas o de trazo redondeado (al estilo de SF Symbols). */
private class Dibujo(nombre: String) {
    val b = ImageVector.Builder(nombre, 24.dp, 24.dp, 24f, 24f)

    fun relleno(vararg rutas: String) {
        for (r in rutas) b.addPath(addPathNodes(r), fill = SolidColor(Color.Black))
    }

    fun trazo(grosor: Float, vararg rutas: String) {
        for (r in rutas) {
            b.addPath(
                addPathNodes(r), stroke = SolidColor(Color.Black), strokeLineWidth = grosor,
                strokeLineCap = StrokeCap.Round, strokeLineJoin = StrokeJoin.Round,
            )
        }
    }
}

private fun icono(nombre: String, f: Dibujo.() -> Unit): ImageVector = Dibujo(nombre).apply(f).b.build()

/** Íconos propios, dibujados con rutas (sin librerías de íconos). */
object Iconos {
    val reproducir by lazy {
        icono("reproducir") {
            relleno("M8 5.4 C8 4.6 8.9 4.1 9.6 4.6 L19.7 11.2 C20.3 11.6 20.3 12.4 19.7 12.8 L9.6 19.4 C8.9 19.9 8 19.4 8 18.6 Z")
        }
    }
    val pausa by lazy {
        icono("pausa") {
            relleno(
                "M6.5 4.5 H9.5 C10.05 4.5 10.5 4.95 10.5 5.5 V18.5 C10.5 19.05 10.05 19.5 9.5 19.5 H6.5 " +
                    "C5.95 19.5 5.5 19.05 5.5 18.5 V5.5 C5.5 4.95 5.95 4.5 6.5 4.5 Z",
                "M14.5 4.5 H17.5 C18.05 4.5 18.5 4.95 18.5 5.5 V18.5 C18.5 19.05 18.05 19.5 17.5 19.5 H14.5 " +
                    "C13.95 19.5 13.5 19.05 13.5 18.5 V5.5 C13.5 4.95 13.95 4.5 14.5 4.5 Z",
            )
        }
    }
    val detener by lazy {
        icono("detener") {
            relleno(
                "M7 5.5 H17 C17.83 5.5 18.5 6.17 18.5 7 V17 C18.5 17.83 17.83 18.5 17 18.5 H7 " +
                    "C6.17 18.5 5.5 17.83 5.5 17 V7 C5.5 6.17 6.17 5.5 7 5.5 Z",
            )
        }
    }
    val alInicio by lazy {
        icono("alInicio") {
            relleno(
                "M5 5.5 H6.8 C7.19 5.5 7.5 5.81 7.5 6.2 V17.8 C7.5 18.19 7.19 18.5 6.8 18.5 H5 " +
                    "C4.61 18.5 4.3 18.19 4.3 17.8 V6.2 C4.3 5.81 4.61 5.5 5 5.5 Z",
                "M19.5 6.3 V17.7 C19.5 18.5 18.6 19 17.9 18.5 L9.3 12.8 C8.7 12.4 8.7 11.6 9.3 11.2 " +
                    "L17.9 5.5 C18.6 5 19.5 5.5 19.5 6.3 Z",
            )
        }
    }
    val masOpciones by lazy {
        icono("masOpciones") {
            relleno(
                "M5 10.2 A1.8 1.8 0 1 1 5 13.8 A1.8 1.8 0 1 1 5 10.2 Z",
                "M12 10.2 A1.8 1.8 0 1 1 12 13.8 A1.8 1.8 0 1 1 12 10.2 Z",
                "M19 10.2 A1.8 1.8 0 1 1 19 13.8 A1.8 1.8 0 1 1 19 10.2 Z",
            )
        }
    }
    val bocina by lazy {
        icono("bocina") {
            relleno(
                "M3.5 9.2 H6.3 L10.7 5.5 C11.2 5.1 12 5.4 12 6.1 V17.9 C12 18.6 11.2 18.9 10.7 18.5 " +
                    "L6.3 14.8 H3.5 C2.95 14.8 2.5 14.35 2.5 13.8 V10.2 C2.5 9.65 2.95 9.2 3.5 9.2 Z",
            )
        }
    }
    val bocinaFuerte by lazy {
        icono("bocinaFuerte") {
            relleno(
                "M2.5 9.2 H5.3 L9.7 5.5 C10.2 5.1 11 5.4 11 6.1 V17.9 C11 18.6 10.2 18.9 9.7 18.5 " +
                    "L5.3 14.8 H2.5 C1.95 14.8 1.5 14.35 1.5 13.8 V10.2 C1.5 9.65 1.95 9.2 2.5 9.2 Z",
            )
            trazo(
                1.7f,
                "M14 9.3 C14.8 10 15.2 11 15.2 12 C15.2 13 14.8 14 14 14.7",
                "M16.7 6.6 C18.1 7.9 19 9.9 19 12 C19 14.1 18.1 16.1 16.7 17.4",
                "M19.4 3.9 C21.4 5.9 22.6 8.8 22.6 12 C22.6 15.2 21.4 18.1 19.4 20.1",
            )
        }
    }
    val mas by lazy { icono("mas") { trazo(2f, "M12 5 V19", "M5 12 H19") } }
    val compartir by lazy {
        icono("compartir") {
            trazo(
                1.8f,
                "M12 3.5 V14.5",
                "M8.5 7 L12 3.5 L15.5 7",
                "M8.5 10 H7 A1.5 1.5 0 0 0 5.5 11.5 V19.5 A1.5 1.5 0 0 0 7 21 H17 A1.5 1.5 0 0 0 18.5 19.5 " +
                    "V11.5 A1.5 1.5 0 0 0 17 10 H15.5",
            )
        }
    }
    val mezclador by lazy {
        icono("mezclador") {
            trazo(1.8f, "M6 4 V20", "M12 4 V20", "M18 4 V20")
            trazo(4.2f, "M5.3 14 H6.7", "M11.3 8 H12.7", "M17.3 15.5 H18.7")
        }
    }
    val tijeras by lazy {
        icono("tijeras") {
            trazo(
                1.8f,
                "M6.5 4.5 A2.5 2.5 0 1 1 6.5 9.5 A2.5 2.5 0 1 1 6.5 4.5 Z",
                "M6.5 14.5 A2.5 2.5 0 1 1 6.5 19.5 A2.5 2.5 0 1 1 6.5 14.5 Z",
                "M8.6 8.4 L20 16.5",
                "M8.6 15.6 L20 7.5",
            )
        }
    }
    val onda by lazy {
        icono("onda") {
            trazo(2f, "M3 10.5 V13.5", "M6.5 8 V16", "M10 4.5 V19.5", "M13.5 7.5 V16.5", "M17 10 V14", "M20.5 11 V13")
        }
    }
    val deshacer by lazy { icono("deshacer") { trazo(2f, "M9 14.5 L4 9.5 L9 4.5", "M4 9.5 H14 A5.5 5.5 0 0 1 14 20.5 H11") } }
    val rehacer by lazy { icono("rehacer") { trazo(2f, "M15 14.5 L20 9.5 L15 4.5", "M20 9.5 H10 A5.5 5.5 0 0 0 10 20.5 H13") } }
    val basura by lazy {
        icono("basura") {
            trazo(
                1.8f,
                "M4 6.5 H20",
                "M9.5 6.5 V4.8 C9.5 4.1 10 3.5 10.7 3.5 H13.3 C14 3.5 14.5 4.1 14.5 4.8 V6.5",
                "M6 6.5 L6.9 19.1 C7 20.1 7.8 21 8.9 21 H15.1 C16.2 21 17 20.1 17.1 19.1 L18 6.5",
                "M10 10.5 V17",
                "M14 10.5 V17",
            )
        }
    }
    val iman by lazy {
        icono("iman") {
            trazo(
                1.8f,
                "M5.5 3.5 H9.5 V12 A2.5 2.5 0 0 0 14.5 12 V3.5 H18.5 V12 A6.5 6.5 0 0 1 5.5 12 Z",
                "M5.5 7 H9.5",
                "M14.5 7 H18.5",
            )
        }
    }
    val alejar by lazy {
        icono("alejar") {
            trazo(1.8f, "M10.5 3.5 A7 7 0 1 1 10.5 17.5 A7 7 0 1 1 10.5 3.5 Z", "M15.5 15.5 L20.5 20.5", "M7.5 10.5 H13.5")
        }
    }
    val acercar by lazy {
        icono("acercar") {
            trazo(
                1.8f, "M10.5 3.5 A7 7 0 1 1 10.5 17.5 A7 7 0 1 1 10.5 3.5 Z", "M15.5 15.5 L20.5 20.5",
                "M7.5 10.5 H13.5", "M10.5 7.5 V13.5",
            )
        }
    }
    val ajustar by lazy {
        icono("ajustar") { trazo(1.8f, "M3.5 12 H20.5", "M7 8.5 L3.5 12 L7 15.5", "M17 8.5 L20.5 12 L17 15.5") }
    }
    val chevronAbajo by lazy { icono("chevronAbajo") { trazo(2.2f, "M5.5 9 L12 15.5 L18.5 9") } }
    val chevronDerecha by lazy { icono("chevronDerecha") { trazo(2.2f, "M9 5.5 L15.5 12 L9 18.5") } }
    val chevronIzquierda by lazy { icono("chevronIzquierda") { trazo(2.2f, "M15 5.5 L8.5 12 L15 18.5") } }
    val cerrar by lazy { icono("cerrar") { trazo(2f, "M6.5 6.5 L17.5 17.5", "M17.5 6.5 L6.5 17.5") } }
    val microfono by lazy {
        icono("microfono") {
            trazo(
                1.8f,
                "M12 3.5 A3 3 0 0 0 9 6.5 V11.5 A3 3 0 0 0 15 11.5 V6.5 A3 3 0 0 0 12 3.5 Z",
                "M6 11 A6 6 0 0 0 18 11",
                "M12 17 V20.5",
                "M9 20.5 H15",
            )
        }
    }
    val audifonos by lazy {
        icono("audifonos") {
            trazo(1.8f, "M4.5 15 V12 A7.5 7.5 0 0 1 19.5 12 V15")
            relleno(
                "M4 14 H6.5 C7.05 14 7.5 14.45 7.5 15 V19 C7.5 19.55 7.05 20 6.5 20 H5 C4.17 20 3.5 19.33 3.5 18.5 V14.5 " +
                    "C3.5 14.22 3.72 14 4 14 Z",
                "M17.5 14 H20 C20.28 14 20.5 14.22 20.5 14.5 V18.5 C20.5 19.33 19.83 20 19 20 H17.5 " +
                    "C16.95 20 16.5 19.55 16.5 19 V15 C16.5 14.45 16.95 14 17.5 14 Z",
            )
        }
    }
    val nota by lazy {
        icono("nota") {
            trazo(1.8f, "M9 18.5 V6.2 L19 3.8 V16.2")
            relleno(
                "M6.6 16 A2.5 2.5 0 1 1 6.6 21 A2.5 2.5 0 1 1 6.6 16 Z",
                "M16.6 13.7 A2.5 2.5 0 1 1 16.6 18.7 A2.5 2.5 0 1 1 16.6 13.7 Z",
            )
        }
    }
}
