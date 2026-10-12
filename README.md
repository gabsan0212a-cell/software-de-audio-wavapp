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
- [ ] 2. EQ de 12 bandas
- [ ] 3. Exportación WAV 24 y 32 bits
- [ ] 4. Análisis (espectro, espectrograma, fase, zumbido)
- [ ] 5. Edición
- [ ] 6. Grabación
- [ ] 7. Guardar/abrir proyectos (por ahora la mezcla actual se guarda sola)

Sin plugins VST en Android.

## Cómo está hecho
- `app/src/main/cpp/`: motor en C++.
  - `WavReader` / `WavWriter`: leer cualquier WAV por partes; escribir el formato interno.
  - `Resampler`: convierte a 48 kHz al importar (filtro sinc, ~120 dB de precisión).
  - `MappedAudio`: las pistas se guardan como WAV float a 48 kHz y se mapean en memoria
    (no llenan la RAM; el sistema lee del disco lo que va sonando).
  - `Mixer` + `Limiter`: la mezcla en tiempo real, igual que la cadena del prototipo.
  - `AudioEngine`: salida con Oboe a 48 kHz; `jni_bridge.cpp`: puente con Kotlin.
- `app/src/main/java/.../`: `Mesa.kt` (estado, importación, sesión), `Decodificador.kt`
  (formatos comprimidos → WAV con MediaCodec) y `ui/` (pantalla en Compose).

## Pruebas
- `tests/native/run.sh`: pruebas del motor en Linux con ASan, UBSan y TSan.
- `tests/check_jni.py`: que Kotlin y C++ coincidan en el puente.
- `app/src/androidTest/`: pruebas en un emulador Android (audio real, importación, pantalla).
