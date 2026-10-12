package com.wavapp.mezcla.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily

/** Colores del prototipo "Mesa de Audio". */
@Immutable
data class Paleta(
    val bg: Color,
    val panel: Color,
    val linea: Color,
    val fg: Color,
    val tenue: Color,
    val verde: Color,
    val ambar: Color,
    val rojo: Color,
    val acento: Color,
    val acentoSuave: Color,
)

val PaletaOscura = Paleta(
    bg = Color(0xFF131415), panel = Color(0xFF1D1E20), linea = Color(0xFF36383C),
    fg = Color(0xFFEBE8E2), tenue = Color(0xFF8E8B85), verde = Color(0xFF38D46A),
    ambar = Color(0xFFFFA11F), rojo = Color(0xFFFF4D3D), acento = Color(0xFFFFA11F),
    acentoSuave = Color(0xFF4A3513),
)

val PaletaClara = Paleta(
    bg = Color(0xFFE8E6E1), panel = Color(0xFFF6F5F2), linea = Color(0xFFC8C5BD),
    fg = Color(0xFF1D1C1A), tenue = Color(0xFF66635D), verde = Color(0xFF1C9A47),
    ambar = Color(0xFFC46D00), rojo = Color(0xFFC92F22), acento = Color(0xFFC46D00),
    acentoSuave = Color(0xFFF3D9B0),
)

val LocalPaleta = staticCompositionLocalOf { PaletaOscura }

val ColoresPista = listOf(
    0xFFE8833A, 0xFF3FB6A8, 0xFFD4B13A, 0xFFB072D6, 0xFF5D9DE0, 0xFFE0607E,
    0xFF7FB84A, 0xFFC98A5E, 0xFF4FC1D6, 0xFFD66BB0, 0xFF9AA63A, 0xFF8A8FE0,
).map { Color(it) }

val Mono = FontFamily.Monospace

@Composable
fun Tema(content: @Composable () -> Unit) {
    val oscuro = isSystemInDarkTheme()
    val p = if (oscuro) PaletaOscura else PaletaClara
    val esquema = if (oscuro) {
        darkColorScheme(
            primary = p.acento, onPrimary = p.bg, background = p.bg, onBackground = p.fg,
            surface = p.panel, onSurface = p.fg, outline = p.linea, surfaceVariant = p.panel,
            onSurfaceVariant = p.tenue,
        )
    } else {
        lightColorScheme(
            primary = p.acento, onPrimary = p.bg, background = p.bg, onBackground = p.fg,
            surface = p.panel, onSurface = p.fg, outline = p.linea, surfaceVariant = p.panel,
            onSurfaceVariant = p.tenue,
        )
    }
    CompositionLocalProvider(LocalPaleta provides p) {
        MaterialTheme(colorScheme = esquema, content = content)
    }
}
