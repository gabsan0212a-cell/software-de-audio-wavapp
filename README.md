# WavApp — software de mezcla nativo para Android

Kotlin + C++ con [Oboe](https://github.com/google/oboe) para audio de baja latencia.
Sin computadora: GitHub Actions compila el APK en la nube.

## Cómo instalar en el celular
1. Cada push compila solo (pestaña **Actions**).
2. Abre **Releases → ultimo** y descarga `wavapp.apk`.
3. Instálalo (permitir "orígenes desconocidos" la primera vez). Las nuevas versiones se instalan encima.

## Etapas
- [x] 0. App mínima: tono de prueba 440 Hz + compilación en la nube
- [ ] 1. Motor de audio nativo (reproducir WAV, mezclar pistas)
- [ ] 2. EQ de 12 bandas
- [ ] 3. Exportación WAV 24 y 32 bits
- [ ] 4. Análisis (medidores, espectro)
- [ ] 5. Edición
- [ ] 6. Grabación
- [ ] 7. Guardar/abrir proyectos

La versión web es el prototipo de referencia (salvo exportación). Sin plugins VST en Android.
