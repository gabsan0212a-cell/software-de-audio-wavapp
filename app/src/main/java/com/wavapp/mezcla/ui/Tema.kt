package com.wavapp.mezcla.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/** Colores al estilo de Apple (los del sistema de iOS), en claro y oscuro. */
@Immutable
data class Paleta(
    val oscuro: Boolean,
    val fondo: Color,          // pantalla lisa
    val fondoAgrupado: Color,  // detrás de las tarjetas
    val tarjeta: Color,
    val elevado: Color,        // mini reproductor, menús y alertas
    val relleno: Color,        // botones grises y riel de los deslizadores
    val texto: Color,
    val texto2: Color,
    val texto3: Color,
    val separador: Color,
    val acento: Color,
    val verde: Color,
    val amarillo: Color,
    val naranja: Color,
    val rojo: Color,
    val azul: Color,
    val barra: Color,          // barra de pestañas y barra de arriba
    val velo: Color,           // oscurece lo de atrás de una hoja
    val pistas: List<Color>,
) {
    /** Dentro de una hoja (como en iOS, en oscuro todo sube un tono). */
    fun elevada(): Paleta = if (!oscuro) this else copy(
        fondoAgrupado = Color(0xFF1C1C1E), tarjeta = Color(0xFF2C2C2E), relleno = Color(0xFF3A3A3C),
        separador = Color(0xFF48484A), elevado = Color(0xFF3A3A3C),
    )
}

val PaletaClara = Paleta(
    oscuro = false,
    fondo = Color(0xFFFFFFFF), fondoAgrupado = Color(0xFFF2F2F7), tarjeta = Color(0xFFFFFFFF),
    elevado = Color(0xFFFAFAFC), relleno = Color(0xFFE9E9EB),
    texto = Color(0xFF000000), texto2 = Color(0xFF8A8A8E), texto3 = Color(0xFFC4C4C6),
    separador = Color(0xFFD1D1D6),
    acento = Color(0xFFFA2D48), verde = Color(0xFF34C759), amarillo = Color(0xFFFFCC00),
    naranja = Color(0xFFFF9500), rojo = Color(0xFFFF3B30), azul = Color(0xFF007AFF),
    barra = Color(0xFFF9F9F9), velo = Color(0x66000000),
    pistas = listOf(
        0xFFFF9500, 0xFF30B0C7, 0xFFFFCC00, 0xFFAF52DE, 0xFF007AFF, 0xFFFF2D55,
        0xFF34C759, 0xFFA2845E, 0xFF32ADE6, 0xFFFF3B30, 0xFF00C7BE, 0xFF5856D6,
    ).map { Color(it) },
)

val PaletaOscura = Paleta(
    oscuro = true,
    fondo = Color(0xFF000000), fondoAgrupado = Color(0xFF000000), tarjeta = Color(0xFF1C1C1E),
    elevado = Color(0xFF2C2C2E), relleno = Color(0xFF38383A),
    texto = Color(0xFFFFFFFF), texto2 = Color(0xFF98989F), texto3 = Color(0xFF5A5A5F),
    separador = Color(0xFF38383A),
    acento = Color(0xFFFF375F), verde = Color(0xFF30D158), amarillo = Color(0xFFFFD60A),
    naranja = Color(0xFFFF9F0A), rojo = Color(0xFFFF453A), azul = Color(0xFF0A84FF),
    barra = Color(0xFF161618), velo = Color(0x8C000000),
    pistas = listOf(
        0xFFFF9F0A, 0xFF40C8E0, 0xFFFFD60A, 0xFFBF5AF2, 0xFF0A84FF, 0xFFFF375F,
        0xFF30D158, 0xFFAC8E68, 0xFF64D2FF, 0xFFFF453A, 0xFF63E6E2, 0xFF5E5CE6,
    ).map { Color(it) },
)

val LocalPaleta = staticCompositionLocalOf { PaletaClara }

/** Tipografía del sistema con los tamaños de iOS. */
object Tipo {
    val tituloGrande = TextStyle(fontSize = 34.sp, fontWeight = FontWeight.Bold, lineHeight = 41.sp)
    val titulo2 = TextStyle(fontSize = 22.sp, fontWeight = FontWeight.Bold, lineHeight = 28.sp)
    val titulo3 = TextStyle(fontSize = 20.sp, fontWeight = FontWeight.SemiBold, lineHeight = 25.sp)
    val encabezado = TextStyle(fontSize = 17.sp, fontWeight = FontWeight.SemiBold, lineHeight = 22.sp)
    val cuerpo = TextStyle(fontSize = 17.sp, lineHeight = 22.sp)
    val llamada = TextStyle(fontSize = 16.sp, lineHeight = 21.sp)
    val subtitulo = TextStyle(fontSize = 15.sp, lineHeight = 20.sp)
    val nota = TextStyle(fontSize = 13.sp, lineHeight = 18.sp)
    val leyenda = TextStyle(fontSize = 12.sp, lineHeight = 16.sp)
    val leyenda2 = TextStyle(fontSize = 11.sp, lineHeight = 13.sp)
    val pestana = TextStyle(fontSize = 10.sp, fontWeight = FontWeight.Medium, lineHeight = 12.sp)
}

/** Números de ancho fijo (los tiempos y valores no bailan al cambiar). */
val TextStyle.numeros: TextStyle get() = copy(fontFeatureSettings = "tnum")
val TextStyle.seminegrita: TextStyle get() = copy(fontWeight = FontWeight.SemiBold)

@Composable
fun Tema(oscuro: Boolean = isSystemInDarkTheme(), content: @Composable () -> Unit) {
    CompositionLocalProvider(LocalPaleta provides if (oscuro) PaletaOscura else PaletaClara, content = content)
}
