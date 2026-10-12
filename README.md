# WavApp — software de mezcla nativo para Android

Kotlin + C++ con [Oboe](https://github.com/google/oboe) para audio de baja latencia.
Sin computadora: GitHub Actions compila, prueba y publica el APK en la nube.
La versión web ("Mesa de Audio") es el prototipo de referencia: la app nativa copia su
comportamiento (volumen, paneo, limitador, medidores, colores).

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
- [ ] 6. Grabación
- [ ] 7. Guardar/abrir proyectos (por ahora la mezcla actual se guarda sola)

Sin plugins VST en Android.

## Cómo está hecho
- `app/src/main/cpp/`: motor en C++.
  - `WavReader` / `WavWriter`: leer cualquier WAV por partes; escribir el formato interno.
  - `Resampler`: convierte a 48 kHz al importar (filtro sinc, ~120 dB de precisión).
  - `MappedAudio`: las pistas se guardan como WAV float a 48 kHz y se mapean en memoria
    (no llenan la RAM; el sistema lee del disco lo que va sonando).
  - `Mixer` + `Limiter`: la mezcla en tiempo real, igual que la cadena del prototipo. Cada
    pista suena como una lista de pedazos (`Region`) de su archivo, con fundidos.
  - `Equalizer`: EQ por pista (filtros biquad como Web Audio); solo procesa las bandas que existen.
  - `Exporter`: renderiza la mezcla fuera de tiempo real con una copia del mezclador.
  - `Analyzer`: FFT, espectro como AnalyserNode, fase, zumbido y espectrograma.
  - `AudioEngine`: salida con Oboe a 48 kHz; `jni_bridge.cpp`: puente con Kotlin.
- `app/src/main/java/.../`: `Mesa.kt` (estado, importación, sesión), `Edicion.kt` (pedazos,
  deshacer/rehacer), `Decodificador.kt`
  (formatos comprimidos → WAV con MediaCodec) y `ui/` (pantalla en Compose).

## Pruebas
- `tests/native/run.sh`: pruebas del motor en Linux con ASan, UBSan y TSan.
- `tests/check_jni.py`: que Kotlin y C++ coincidan en el puente.
- `app/src/androidTest/`: pruebas en un emulador Android (audio real, importación, pantalla).
