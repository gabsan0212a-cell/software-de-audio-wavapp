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
- Pestañas abajo (Mezcla, Grabar, Editar, Análisis), mini reproductor que abre el reproductor a
  pantalla completa, hojas que suben desde abajo, menús de acciones y alertas de iOS.
- Lo que cambia en cada cuadro (posición, medidores) se lee al dibujar, no al componer.
- Lo que obliga Android en la pantalla va solo en `ui/Plataforma.kt` (también el permiso del
  micrófono y la lista de entradas/salidas de audio).
- Grabación: `core/engine/LiveInput` (entrada → ganancia → se graba → EQ → escucha/análisis) y
  `android/AudioInput` (Oboe). El EQ de la entrada usa las funciones del EQ con `Studio::kInputId`.
  La entrada no tiene callback propio: `AudioOutput` la lee en el suyo (`AudioInput::pull`, full
  duplex con reserva de una ráfaga). La escucha entra al bus antes del master (pasa por el limitador).
- Voz en vivo: `core/engine/VoiceFx` (ajustes `vfx::Param`, presets) dentro de `LiveInput`:
  graves → compuerta → `dsp/AutoTune` (`PitchTracker` + `PitchShifter`) → EQ de la entrada →
  de-esser → compresor → volumen → doblaje y envíos de delay (a tiempo con el BPM de la canción) y
  reverb. La escala "de la canción" y el tempo los pone `Studio::applySongToVoiceLocked`. En modo
  en vivo `AudioOutput` usa 2 ráfagas de búfer (sube si hay cortes) y ADPF. Pantalla: `ui/VozEnVivo.kt`.
- Escenario (cantar en vivo, `ui/Escenario.kt`, siempre oscuro): la cuenta para entrar la da
  `Studio::songCue` (las partes con voz siguen los pedazos del instrumental: `songPartsLocked`);
  la presentación la graba `core/engine/ShowRecorder` (el `Mixer` le pasa lo que sale, después del
  master; un hilo propio escribe el WAV) y `Studio::saveShow` la guarda con el formato de exportar.
  La salida no se duerme mientras graba.
- Automatización: `core/engine/Automation` (curvas) + `Studio` (puntos, grabar movimientos con
  `autoTouch`, deshacer) + `Mixer` (la sigue al sonar y al exportar). La pantalla solo dibuja lo
  que da `autoLane` (posiciones 0..1 ya calculadas) y lee `automationNow` en cada cuadro.
- Efectos por pista: `core/engine/Effects` (ajustes `fx::Param`, presets, `TrackFx`) con
  `dsp/Dynamics`, `dsp/Delay` y `dsp/Reverb`. El compresor va en la señal; delay y reverb son
  envíos estéreo que se suman después del volumen y el paneo. Rangos y escalas vienen de `fxInfo`.
- LUFS: `dsp/Loudness` (BS.1770 / EBU R128). El mezclador manda bloques de 100 ms de lo que sale;
  `Studio::loudness` hace las cuentas; `measureSong` y la exportación miden fuera de tiempo real.
- Quitar la voz: `core/sep/` (`ModelFile` .wvm mapeado, `MelRoformer` con Eigen + `ThreadPool`,
  `Stft`, `Separator` con trozos guardados para seguir) + `Studio::separate` (trabajo largo: deja
  "· instrumental" y "· voz original" debajo; luego `dsp/SongAnalysis` saca tonalidad, BPM, pulsos
  y dónde canta, y queda en `ProjectModel::song`). `dsp/Pitch` (YIN) sirve también para el
  auto-tune. Android: `TrabajoLargo` (servicio en primer plano) y `ui/Preparar.kt`.
- El modelo: `tools/modelos/` lo convierte en `modelos.yml` (release `modelos`). El código de
  terceros (repo de Kim, checkpoint) corre solo en el trabajo de solo lectura, con
  `weights_only=True` y huellas SHA-256 fijas; nunca en trabajos con permisos de escritura. El CI
  baja `voz-kim.wvm` (huella fija) a `app/src/main/assets/modelos/` (no va en git, sin comprimir
  en el APK). `bash tests/native/modelo-real.sh <carpeta>` compara el C++ con PyTorch.

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
