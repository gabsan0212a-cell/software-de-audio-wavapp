package com.wavapp.mezcla.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.wavapp.mezcla.Edicion
import com.wavapp.mezcla.Mesa
import com.wavapp.mezcla.Nucleo
import com.wavapp.mezcla.Pista

/** Hoja del botón "A" de una pista: modo de la automatización y sus puntos. */
@Composable
fun HojaAutomatizacion(pista: Pista?, alCerrar: () -> Unit) {
    val recordada = remember { arrayOfNulls<Pista>(1) }
    if (pista != null) recordada[0] = pista
    val p = pista ?: recordada[0]
    Hoja(
        visible = pista != null,
        alCerrar = alCerrar,
        titulo = "Automatización",
        izquierda = { BotonReproducir(LocalPaleta.current.acento, 24.dp, 44.dp) },
        derecha = { BotonTexto("Listo", alCerrar, estilo = Tipo.encabezado) },
    ) {
        if (p != null) ContenidoAuto(p, alCerrar)
    }
}

@Composable
private fun ContenidoAuto(p: Pista, alCerrar: () -> Unit) {
    val pal = LocalPaleta.current
    Row(Modifier.padding(start = 4.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Portada(p.id, pal.pistas[p.color], 40.dp)
        Column(Modifier.padding(start = 12.dp)) {
            Texto(p.nombre, Tipo.encabezado, pal.texto, lineas = 1)
            Texto("Volumen y paneo que cambian solos", Tipo.nota, pal.texto2, lineas = 1)
        }
    }
    EncabezadoGrupo("Modo")
    Segmentado(
        listOf("Apagada", "Leer", "Grabar"), p.autoModo, { Mesa.ponerModoAuto(p, it) },
        Modifier.fillMaxWidth().testTag("modoAuto"),
    )
    PieGrupo(
        when (p.autoModo) {
            Nucleo.AUTO_APAGADA -> "Suenan los valores fijos de Vol y Pan. Los puntos se guardan para después."
            Nucleo.AUTO_GRABAR ->
                "Dale play y mueve Vol o Pan: lo que hagas queda grabado (en rojo) y al soltar regresa a lo que " +
                    "había. Detenido, mover un deslizador cambia el punto del cursor."
            else ->
                "Mientras suena, Vol y Pan siguen sus puntos (en verde). Si los mueves, se oye solo mientras " +
                    "tienes el dedo encima."
        },
    )
    EncabezadoGrupo("Puntos")
    Tarjeta(relleno = PaddingValues(start = 16.dp, end = 4.dp, top = 2.dp, bottom = 2.dp)) {
        FilaCarril(p, Nucleo.AUTO_VOLUMEN, "Volumen", alCerrar)
        Separador()
        FilaCarril(p, Nucleo.AUTO_PANEO, "Paneo", alCerrar)
    }
    PieGrupo(
        "En Editar, toca el carril para poner un punto, arrástralo para moverlo y tócalo dos veces para " +
            "quitarlo. Deshacer también sirve aquí.",
    )
}

@Composable
private fun FilaCarril(p: Pista, param: Int, titulo: String, alCerrar: () -> Unit) {
    val pal = LocalPaleta.current
    val n = p.curva(param).n
    Row(Modifier.fillMaxWidth().heightIn(min = 52.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Texto(titulo, Tipo.cuerpo, pal.texto, lineas = 1)
            Texto(
                when (n) {
                    0 -> "Sin puntos"
                    1 -> "1 punto"
                    else -> "$n puntos"
                },
                Tipo.nota, pal.texto2, lineas = 1,
            )
        }
        BotonTexto(
            "Ver",
            {
                Edicion.verAuto(param)
                Vista.pestana = Vista.EDITAR
                Vista.reproductor = false
                alCerrar()
            },
        )
        BotonTexto("Borrar", { Mesa.borrarAuto(p, param) }, color = pal.rojo, habilitado = n > 0)
    }
}
