#!/usr/bin/env bash
# Vista previa de la pantalla en Linux: compila el núcleo (C++) con una salida de audio de
# escritorio, corre la pantalla real con Compose de escritorio y guarda capturas en claro y
# oscuro en tests/previa/capturas. Sirve para revisar el diseño sin esperar al emulador.
# Uso: tests/previa/previa.sh [escena,escena...]   (inicio, reproduciendo, master, reproductor,
#      eq, analisis, edicion, automatizacion, efectos, lufs, exportar, menus, tocar, grabar, voz, preparar,
#      escenario, error, crear, estilos, vacio; por omisión todas). Con WAVAPP_MODELO=ruta/voz-kim.wvm, "preparar" usa el modelo real.
#      tests/previa/previa.sh --compilar   (solo revisa que compile contra Compose 1.7.3)
set -euo pipefail
cd "$(dirname "$0")"
GRADLE=$(command -v gradle || echo ../../gradlew)
if [ "${1:-}" = "--compilar" ]; then
    exec "$GRADLE" --no-daemon -q -Pcompose=1.7.3 compileKotlin
fi
APP=../../app/src/main/cpp
mkdir -p build/nativo
# El puente JNI real; AudioOutput.h y MediaDecoder.h se toman de aquí (versión de escritorio).
cp "$APP/android/jni_bridge.cpp" "$APP/android/MediaDecoder.h" "$APP/android/Fallas.h" build/nativo/
cp nativo/AudioOutput.h nativo/AudioInput.h build/nativo/
JAVA=${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")}
g++ -std=c++17 -O2 -fPIC -shared -w -I"$APP/core" -isystem "$APP/third_party/eigen" -Ibuild/nativo -I"$JAVA/include" -I"$JAVA/include/linux" \
    $(find "$APP/core" -name '*.cpp') build/nativo/jni_bridge.cpp "$APP/android/Fallas.cpp" nativo/AudioOutput.cpp nativo/AudioInput.cpp nativo/MediaDecoder.cpp \
    -o build/nativo/libwavapp.so -lpthread
rm -rf capturas
"$GRADLE" --no-daemon -q previa -Pescenas="${1:-todas}"
ls capturas
