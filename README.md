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

El APK solo se publica si pasan todas las pruebas. Desde la 0.11.0 pesa unos 470 MB: trae
adentro el modelo de inteligencia artificial que le quita la voz a las canciones (funciona sin
internet).

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
- [x] 11. Preparar una canción para cantarla en vivo (menú de la pista → **Preparar para
      cantar…**): le quita la voz con inteligencia artificial (Mel-Band RoFormer de Kim, el mismo
      modelo que usan los separadores de escritorio, corriendo en C++ dentro del celular). Quedan
      el instrumental y la voz original como guía; después saca la tonalidad, el tempo, los pulsos
      y dónde canta la voz. Tarda (de 20 minutos a una hora en un celular): sigue con la pantalla
      apagada y, si se interrumpe, continúa donde quedó
- [x] 12. Voz en vivo para cantar sobre la pista (Grabar → **Voz en vivo**): auto-tune en la escala
      de la canción preparada (o la que elijas), con velocidad de "natural" a "robot"; filtro de
      graves, compuerta, de-esser, compresor, doblaje, delay a tiempo con la canción y reverb, con
      presets ("Paraíso", "Natural", "Trap (robot)"...). Afinador en pantalla. Audio de baja
      latencia: la entrada se lee a la par de la salida (full duplex), búfer mínimo que crece solo
      si hay cortes y el procesador avisado (ADPF). La voz pasa por el limitador del master
- [x] 13. Pantalla **En vivo** (el escenario) y grabar la presentación (1.0.0): cuenta regresiva
      para entrar a cantar, el mapa de dónde canta la voz, el afinador grande, el nivel del
      micrófono, presets y botones grandes; graba lo que sale hacia la consola (la pista con tu voz,
      después del master) y lo guarda en el teléfono como WAV
- [x] 14. Crear música (pestaña **Crear**, 1.2.0): batería (Trap, Boom bap, Reggaetón, Pop / rock,
      Electrónica, Lo-fi), 808, bajos, guitarras (acústica, nylon, eléctrica limpia y
      distorsionada, muteada) y sintetizador (pluck, pad, lead, supersaw, piano eléctrico, órgano,
      campanas, cuerdas), todo sintetizado en el celular. Cada instrumento llega con una idea en la
      tonalidad y el tempo elegidos; se edita en una cuadrícula (notas de la escala, acordes,
      redobles de hi-hat, swing), se toca en vivo con pads (y lo tocado se graba al patrón) y se
      le cambia el sonido. Es una pista más: volumen, EQ, efectos, edición y exportar
- [x] 15. Los efectos de la voz en vivo también en las pistas grabadas (botón FX → **Voz**): auto-tune
      (sin retraso: la voz queda en su lugar), quitar graves, compuerta, de-esser y doblaje, con
      presets ("Voz con auto-tune", "Voz trap (robot)"...)

Sin plugins VST en Android.

## Cómo cantar en vivo (por ejemplo "Paraíso (daño)")
1. **Conecta todo**: el celular a la consola (salida) y el micrófono por la interfaz USB (o los
   audífonos con micrófono). Con la bocina del celular no te vas a escuchar (se acopla).
2. **Prepara la canción** una sola vez: Mezcla → **Agregar archivos…** → la canción → **⋯** de la
   pista → **Preparar para cantar…** → **Quitar la voz**. Tarda (conecta el cargador); quedan el
   instrumental y la voz original como guía, con la tonalidad, el tempo y dónde canta la voz.
3. Toca **Cantar en vivo** (en la hoja de la canción preparada, arriba en Mezcla o en Grabar).
   Se abre el escenario, la voz se prende y te escuchas por la consola.
4. Elige el preset (**Paraíso** suena como la canción) y revisa el medidor: si se pone rojo,
   baja la ganancia en **Ajustes**. **Guía** prende la voz original por si quieres apoyarte.
5. Toca el **botón rojo**: empieza la canción y se graba la presentación. El aro grande dice
   cuánto falta para entrar (los últimos 3 segundos en rojo) y **¡Canta!** mientras dura cada
   parte. Al terminar toca el cuadro rojo.
6. **Guardar** deja la presentación en el teléfono como WAV (con el formato de exportar).

## Cómo crear un beat
1. Pestaña **Crear**: elige el **tempo** y la **tonalidad** (por omisión 90 BPM y La menor; con una
   canción preparada se usan los suyos).
2. Toca un instrumento en **Agregar instrumento** (por ejemplo la batería): llega con una idea que
   ya suena. **Otra idea** da otra; **Borrar** lo deja vacío.
3. **Notas**: toca un cuadro para poner o quitar una nota; en la batería deja el dedo para un
   redoble (hi-hats del trap). En los melódicos elige el largo (1/16 a 1 compás) y **Acordes**.
4. **Tocar**: pads para probar; con **Grabar notas** lo que toques entra al patrón mientras suena.
5. **Sonido**: el preset y los ajustes (brillo, duración, distorsión, deslizar, afinación).
6. Suma 808, bajo, guitarra o sintetizador igual; después graba tu voz en **Grabar** y exporta.

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
    son envíos que se suman después del volumen y el paneo), `LiveInput` (la entrada en vivo: ganancia, EQ, escucha, medidores y el grabador), `ShowRecorder` (graba la presentación: lo que sale, con un hilo propio que escribe al disco), `Ring` (cola entre hilos sin bloqueos), `Importer` (WAV o decodificado → formato interno), `Exporter`, `Meters`
    (caídas y picos de los medidores) y `Decoder.h` (lo que debe dar la plataforma).
  - `project/`: `Model` (el proyecto), `Session` (sesion.json) y `Edit` (mover, recortar,
    fundidos, cortar, imán).
  - `inst/`: crear música. `Instruments` (los presets) y `Engine` (la síntesis: batería con tonos y
    ruido filtrado, 808 con golpe y saturación, cuerdas Karplus-Strong para guitarras y bajo,
    sintetizador de resta, FM y órgano), `Pattern` (notas en pulsos, ideas para empezar, filas de
    la escala; una vuelta se renderiza una vez y las repeticiones suman su cola) y `Live` (tocar en
    vivo: cola sin bloqueos hacia el hilo de audio). `StudioCrear.cpp`: las pistas de instrumento se
    renderizan en otro hilo a un WAV y `Mixer::replaceAudio` cambia el archivo sin cortes; tempo,
    tonalidad, metrónomo y repetir el patrón.
  - `sep/`: quitar la voz. `ModelFile` (el modelo `.wvm`, mapeado en memoria directo del APK),
    `MelRoformer` (la red, igual a la original en PyTorch: Eigen y todos los núcleos con
    `ThreadPool`), `Stft` y `Separator` (la canción en trozos de 8 s que se enciman y se unen
    con fundidos; cada trozo se guarda para seguir si se interrumpe).
  - `dsp/Pitch` (tono con YIN) y `dsp/SongAnalysis` (tonalidad con perfiles de Krumhansl,
    tempo y pulsos, dónde canta la voz).
  - La voz en vivo: `dsp/PitchTracker` (tono en vivo cada 4 ms con YIN anclado a lo más
    reciente), `dsp/PitchShifter` (cambia el tono leyendo una línea de retardo a otra velocidad y
    saltando períodos completos con fundido: 1 a 6 ms de retraso), `dsp/AutoTune` (la nota de la
    escala más cercana y la velocidad de corrección), `dsp/VoiceDynamics` (filtro de graves,
    compuerta y de-esser), `dsp/Doubler` y `engine/VoiceFx` (la cadena completa, sus ajustes y
    presets). `dsp/DriftFifo` une el reloj de la entrada (p. ej. un micrófono USB) con el de la
    salida sin cortes.
- `app/src/main/cpp/third_party/eigen/`: Eigen 3.4 (álgebra lineal, licencia MPL2).
- `tools/modelos/` y `.github/workflows/modelos.yml`: convierten el modelo original (PyTorch) al
  formato `.wvm` y generan las referencias con las que se compara el C++; se publican en
  Releases → `modelos`. El código de terceros corre en un trabajo sin permisos de escritura, con
  huellas fijas. El CI baja el modelo a `app/src/main/assets/modelos/` (no va en git) y lo mete
  sin comprimir en el APK.
- `app/src/main/cpp/android/`: lo de la plataforma: `AudioOutput` y `AudioInput` (Oboe; la
  entrada se lee dentro del callback de la salida, full duplex), `MediaDecoder`
  (MP3, AAC, FLAC, OGG, Opus... con MediaCodec desde C++) y `jni_bridge.cpp` (el puente).
- `app/src/main/java/.../`: **solo pantalla**. `Nucleo.kt` (el puente), `Mesa.kt` (espejo del
  estado del núcleo para Compose), `Analisis.kt` y `Edicion.kt` (lo que recuerda cada panel),
  `TrabajoLargo.kt` (lo que obliga Android para que quitar la voz siga con la pantalla apagada:
  servicio en primer plano con su aviso) y `ui/` (Compose, solo `foundation`: sin Material).
  - `Tema.kt` (colores y tipografía de iOS), `Iconos.kt` (íconos dibujados con rutas),
    `Componentes.kt` (botones, píldoras, interruptor, segmentado, deslizadores, tarjetas) y
    `Superpuestos.kt` (hojas, menús de acciones, alertas, barra de arriba).
  - `Pantalla.kt` (pestañas y mini reproductor), `MezclaTab.kt`, `Reproductor.kt`,
    `Ecualizador.kt`, `Efectos.kt` (la hoja del botón FX), `AnalisisPanel.kt`, `Volumen.kt`
    (LUFS), `EdicionPanel.kt` (pedazos y carriles de automatización), `Automatizacion.kt` (la hoja
    del botón A), `GrabarTab.kt`, `VozEnVivo.kt` (afinador, presets y la hoja de ajustes de la
    voz), `Exportar.kt`, `Preparar.kt` (preparar para cantar), `Escenario.kt` (la pantalla para
    cantar en vivo), `CrearTab.kt` (la pestaña Crear y la tonalidad) y `EditorInstrumento.kt`
    (cuadrícula de notas, pads y sonido).
  - `Plataforma.kt`: lo único que obliga Android en la pantalla (atrás, pantalla encendida,
    selector de archivos, barra de estado).
  - `Errores.kt` y `android/Fallas.cpp`: si la app se cierra por un error (de la pantalla o del
    núcleo), queda anotado y la próxima vez la app ofrece copiarlo para mandarlo.
  - `src/main/baseline-prof.txt` + `profileinstaller`: Android precompila la pantalla al instalar.

## Pruebas
- `tests/native/run.sh`: pruebas del núcleo en Linux con ASan, UBSan y TSan (incluye el
  Studio completo: sesión, importar, editar, deshacer, exportar, varios hilos a la vez).
- `tests/check_jni.py`: que Kotlin y C++ coincidan en el puente (nombres y tipos).
- `tests/android/compilar.sh`: que compile la parte de Android del núcleo (con encabezados mínimos
  de Oboe y Android) sin esperar al CI.
- `tests/native/modelo-real.sh`: el modelo de voz de verdad en C++ comparado con PyTorch
  (cada capa y la canción completa); lo corre el CI.
- `tests/previa/previa.sh`: vista previa de la pantalla en Linux (Compose de escritorio con
  el núcleo real): capturas en claro y oscuro sin esperar al emulador. Solo para desarrollar.
- `app/src/androidTest/`: pruebas en un emulador Android (audio real, importación, pantalla en
  claro y oscuro, con capturas en Releases → `capturas`). Al final se instala el APK
  optimizado (el que se publica) y se revisa que arranque.
