# WavApp — reglas del proyecto

## Arquitectura (regla fija, la pidió el dueño)
- **Kotlin + Jetpack Compose = solo interfaz**: dibujar, layout, gestos, textos en español y
  lo que obliga Android (Activity, selector de archivos/SAF para obtener descriptores,
  `keepScreenOn`). Nada de cálculo técnico en Kotlin.
- **C++ = el corazón, todo lo técnico**: audio, mezcla, DSP, análisis (incluye suavizados,
  veredictos y datos ya listos para dibujar), medidores (caídas/picos), edición e historial,
  modelo del proyecto, sesión/JSON, importar/decodificar, exportar, archivos internos.
- `app/src/main/cpp/core/` no usa nada de Android (se compila y prueba en Linux).
  `app/src/main/cpp/android/` es la plataforma: Oboe, MediaCodec del NDK y el puente JNI.
- La pantalla habla solo con `Studio` a través de `Nucleo.kt` ↔ `android/jni_bridge.cpp`.
  `Mesa.kt` es un espejo: compara `versions()` y vuelve a leer solo lo que cambió.
- Regla práctica: si algo toca muestras de audio, el modelo o reglas de edición/EQ, va en
  `core/`. Kotlin solo convierte valores ya calculados a píxeles.
- Sin plugins VST en Android.

## Pantalla (estilo Apple Music + toques de estudio)
- Solo `androidx.compose.foundation` (sin Material): componentes propios en `ui/Componentes.kt`
  y `ui/Superpuestos.kt`, colores y tipografía de iOS en `ui/Tema.kt`, íconos con rutas en
  `ui/Iconos.kt`. Claro u oscuro según el celular.
- Pestañas abajo (Mezcla, Editar, Análisis), mini reproductor que abre el reproductor a
  pantalla completa, hojas que suben desde abajo, menús de acciones y alertas de iOS.
- Lo que cambia en cada cuadro (posición, medidores) se lee al dibujar, no al componer.
- Lo que obliga Android en la pantalla va solo en `ui/Plataforma.kt`.

## Cómo se trabaja
- El dueño no tiene computadora: usa Android + GitHub. Todo se compila en GitHub Actions; el
  APK se instala desde Releases → `ultimo` (abrir con Chrome).
- Etapas pequeñas: diseño, código, pruebas, corregir y volver a probar.
- Comunicarse en español. Informar solo cuando algo esté listo o cuando haga falta que el
  dueño intervenga.
- Antes de subir: `bash tests/native/run.sh` y `python3 tests/check_jni.py`. Si cambia la
  pantalla: `bash tests/previa/previa.sh --compilar` (compila contra Compose 1.7) y
  `bash tests/previa/previa.sh` (capturas en `tests/previa/capturas`, revisarlas). El CI corre
  además la compilación del APK y las pruebas en emulador (con capturas en Releases →
  `capturas`), y solo publica si todo pasa.
- Subir la versión (`versionCode`/`versionName` en `app/build.gradle.kts`) con cada entrega.
- El APK que se publica es `release` (R8, solo ARM, misma llave que debug: se instala encima);
  las pruebas en emulador usan `debug` (con x86_64).
- El prototipo web "Mesa de Audio" (artifact de claude.ai) es la referencia de comportamiento.
