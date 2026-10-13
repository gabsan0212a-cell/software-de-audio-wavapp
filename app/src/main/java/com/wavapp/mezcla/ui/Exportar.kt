package com.wavapp.mezcla.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.wavapp.mezcla.Mesa
import java.util.Locale

/** Hoja para exportar la mezcla a WAV: formato, frecuencia y dónde guardarla. */
@Composable
fun HojaExportar(visible: Boolean, alCerrar: () -> Unit, alGuardar: () -> Unit) {
    Hoja(
        visible = visible,
        alCerrar = alCerrar,
        titulo = "Exportar la mezcla",
        izquierda = { BotonTexto("Cancelar", alCerrar) },
    ) {
        val p = LocalPaleta.current
        Row(Modifier.padding(start = 4.dp, top = 4.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Mosaico(Mesa.pistas, 56.dp, radio = 8.dp)
            Column(Modifier.padding(start = 14.dp)) {
                Texto("Mezcla", Tipo.encabezado, p.texto, lineas = 1)
                val n = Mesa.pistas.size
                Texto(
                    "${tiempoCorto(Mesa.duracion)} · $n ${if (n == 1) "pista" else "pistas"} · 48 kHz", Tipo.nota.numeros, p.texto2,
                    lineas = 1,
                )
            }
        }
        EncabezadoGrupo("Formato WAV")
        Segmentado(
            listOf("16 bits", "24 bits", "32 bits float"), listOf(16, 24, 32).indexOf(Mesa.exportBits),
            { Mesa.ponerExportacion(listOf(16, 24, 32)[it], Mesa.exportRate) }, Modifier.fillMaxWidth(),
        )
        EncabezadoGrupo("Frecuencia")
        Segmentado(
            listOf("48000 Hz", "44100 Hz"), listOf(48000, 44100).indexOf(Mesa.exportRate),
            { Mesa.ponerExportacion(Mesa.exportBits, listOf(48000, 44100)[it]) }, Modifier.fillMaxWidth(),
        )
        val mb = Mesa.tamanoExportacion() / 1048576.0
        PieGrupo(
            "Se exporta la canción completa tal como suena: volumen, paneo, mute/solo, EQ, notch y master con " +
                "limitador. Tamaño ≈ " + String.format(Locale.US, "%.1f", mb) + " MB.",
        )
        BotonRelleno(
            "Elegir dónde guardar…", alGuardar, Modifier.padding(top = 22.dp).fillMaxWidth(), icono = Iconos.compartir,
            habilitado = !Mesa.ocupada && Mesa.pistas.isNotEmpty(),
        )
    }
}
