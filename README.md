# WavApp — software de mezcla nativo para Android

Dos capas, con una regla fija:
- **Kotlin + Jetpack Compose: solo la pantalla** (dibujar, botones, gestos y textos).
- **C++: el corazón, todo lo técnico** (audio, mezcla, efectos, análisis, edición, importar,
  exportar, la sesión). Salida de audio con [Oboe](https://github.com/google/oboe), baja latencia.

Sin computadora: GitHub Actions compila, prueba y publica el APK en la nube.
La versión web ("Mesa de Audio") es el prototipo de referencia: la app nativa copia su
comportamiento (volumen, paneo, limitador, medidores, análisis, edición).

La pantalla tiene el estilo de Apple Music con toques de estudio (como GarageBand): pestañas
abajo (Mezcla, Grabar, Editar, Análisis), mini reproductor que se abre a pantalla completa, títulos
grandes, acento rojo, colores por pista y regiones de color. Claro u oscuro según el celular.

## Cómo instalar en el celular
1. Cada push compila y prueba solo (pestaña **Actions**).
2. Abre **Releases → ultimo** desde **Chrome** y descarga `wavapp.apk`
   (desde el navegador interno de otras apps la descarga se queda trabada).
3. Instálalo. Las versiones nuevas se instalan encima, sin desinstalar.
4. ¿Algo salió mal? Cada versión queda también en Releases como `vX.Y.Z` para regresar a una anterior.

El APK solo se publica si pasan todas las pruebas.

## Etapas
- [x] 0. App mínima: tono de prueba + compilación en la nube
- [x] 1. Motor de audio nativo
  - [x] 1a. Abrir y reproducir WAV (8/16/24/32 bits, float; encabezados mal escritos)
        y también MP3, AAC/M4A, FLAC, OGG y Opus (los decodifica Android)
  - [x] 1b. Volumen, paneo, mute, solo y polaridad por pista
  - [x] 1c. Hasta 12 pistas mezcladas + master (fader, limitador, medidores, clip)
- [x] 2. EQ paramétrico por pista: hasta 12 bandas que se agregan y quitan (6 tipos de filtro,
      curva con nodos, auto-ganancia, bypass)
- [x] 3. Exportar la mezcla a WAV 16/24 bits (con dither) o 32 bits float, a 48 o 44.1 kHz
- [x] 4. Análisis como el prototipo: goniómetro, fase y paneo, espectro con detección de zumbido
      y notch 50/60 Hz, espectrograma; espectro de fondo en el EQ
- [x] 5. Edición: mover pistas en el tiempo, recortar, fundidos de entrada/salida, cortar en
      pedazos, borrar, imán, zoom y deshacer/rehacer
- [x] 5b. Interfaz estilo Apple Music y optimización (APK release con R8 solo para ARM, sin
      Material; la salida de audio se duerme si no suena)
- [x] 6. Grabación: micrófono del celular, de los audífonos o interfaz USB (mono o estéreo),
      con ganancia, medidor, escucha en vivo con EQ en tiempo real y análisis del micrófono.
      Graba desde el cursor con la mezcla sonando; la toma queda alineada (se compensa el
      retraso del audio) como pista nueva, sin efecto y con el EQ que se escuchó
- [x] 7. Automatización de volumen y paneo por pista: puntos dibujados en Editar (tocar pone,
      arrastrar mueve, dos toques quitan, con deshacer) y movimientos de los deslizadores
      grabados mientras suena (botón **A** → Grabar; al soltar regresa a lo que había). Se oye
      igual al exportar
- [x] 8. Medidor de volumen percibido (LUFS, como Spotify o YouTube): momentáneo, corto plazo,
      integrado, rango y pico real, en vivo y de la canción completa; al exportar se puede ajustar
      a −14, −16 o −23 LUFS
- [x] 9. Efectos por pista: compresor, delay (eco, con ping-pong) y reverb, con presets ("Voz",
      "Voz con eco", "Batería", "Bajo", "Guitarra"...). Suenan igual al exportar
- [ ] 10. Guardar/abrir proyectos (por ahora la mezcla actual se guarda sola)

Sin plugins VST en Android.

## Cómo está hecho
- `app/src/main/cpp/core/`: **el núcleo**, C++17 sin nada de Android (se prueba en Linux).
  - `Studio`: la fachada de todo. Es dueño del proyecto (pistas, EQ, pedazos, automatización,
    master), hace
    los trabajos largos en su propio hilo (importar, exportar, abrir la sesión), guarda la
    sesión poco después de cada cambio, lleva el historial de deshacer/rehacer y da a la
    pantalla lo que dibuja (versiones de cada cosa, medidores, análisis, onda).
  - `io/`: `WavReader` / `WavWriter` (cualquier WAV por partes; el formato interno),
    `MappedAudio` (las pistas, mapeadas en memoria: no llenan la RAM), `Json` (la sesión).
  - `dsp/`: `Resampler` (a 48 kHz al importar, ~120 dB de precisión), `Equalizer` (biquads
    como Web Audio), `Limiter`, `Analyzer` (FFT, espectro, fase, zumbido, espectrograma),
    `AnalysisView` (lo que muestra el panel: veredicto, goniómetro, espectro por columnas),
    `Loudness` (LUFS según ITU-R BS.1770 / EBU R128: ponderación K, compuertas, rango y pico
    real ×4), `Dynamics` (compresor), `Delay` y `Reverb` (red de retardos de 8 líneas).
  - `engine/`: `Mixer` (la mezcla en tiempo real; cada pista es una lista de pedazos con
    fundidos y su volumen y paneo pueden seguir una automatización), `Automation` (curvas de
    automatización: escala del carril, valor en cada momento, simplificar lo grabado), `Effects`
    (los efectos de una pista: ajustes, presets; el compresor va en la señal y el delay y la reverb
    son envíos que se suman después del volumen y el paneo), `LiveInput` (la entrada en vivo: ganancia, EQ, escucha, medidores y el grabador), `Importer` (WAV o decodificado → formato interno), `Exporter`, `Meters`
    (caídas y picos de los medidores) y `Decoder.h` (lo que debe dar la plataforma).
  - `project/`: `Model` (el proyecto), `Session` (sesion.json) y `Edit` (mover, recortar,
    fundidos, cortar, imán).
- `app/src/main/cpp/android/`: lo de la plataforma: `AudioOutput` y `AudioInput` (Oboe), `MediaDecoder`
  (MP3, AAC, FLAC, OGG, Opus... con MediaCodec desde C++) y `jni_bridge.cpp` (el puente).
- `app/src/main/java/.../`: **solo pantalla**. `Nucleo.kt` (el puente), `Mesa.kt` (espejo del
  estado del núcleo para Compose), `Analisis.kt` y `Edicion.kt` (lo que recuerda cada panel)
  y `ui/` (Compose, solo `foundation`: sin Material).
  - `Tema.kt` (colores y tipografía de iOS), `Iconos.kt` (íconos dibujados con rutas),
    `Componentes.kt` (botones, píldoras, interruptor, segmentado, deslizadores, tarjetas) y
    `Superpuestos.kt` (hojas, menús de acciones, alertas, barra de arriba).
  - `Pantalla.kt` (pestañas y mini reproductor), `MezclaTab.kt`, `Reproductor.kt`,
    `Ecualizador.kt`, `Efectos.kt` (la hoja del botón FX), `AnalisisPanel.kt`, `Volumen.kt`
    (LUFS), `EdicionPanel.kt` (pedazos y carriles de automatización), `Automatizacion.kt` (la hoja
    del botón A), `GrabarTab.kt` y `Exportar.kt`.
  - `Plataforma.kt`: lo único que obliga Android en la pantalla (atrás, pantalla encendida,
    selector de archivos, barra de estado).

## Pruebas
- `tests/native/run.sh`: pruebas del núcleo en Linux con ASan, UBSan y TSan (incluye el
  Studio completo: sesión, importar, editar, deshacer, exportar, varios hilos a la vez).
- `tests/check_jni.py`: que Kotlin y C++ coincidan en el puente (nombres y tipos).
- `tests/previa/previa.sh`: vista previa de la pantalla en Linux (Compose de escritorio con
  el núcleo real): capturas en claro y oscuro sin esperar al emulador. Solo para desarrollar.
- `app/src/androidTest/`: pruebas en un emulador Android (audio real, importación, pantalla en
  claro y oscuro, con capturas en Releases → `capturas`). Al final se instala el APK
  optimizado (el que se publica) y se revisa que arranque.
