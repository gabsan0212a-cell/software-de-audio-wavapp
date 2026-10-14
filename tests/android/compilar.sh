#!/usr/bin/env bash
# Revisa que compile la parte de Android del núcleo (app/src/main/cpp/android) en Linux, con
# encabezados mínimos de Oboe y Android (tests/android/stubs): así un error de escritura se ve
# antes de que el CI compile el APK. MediaDecoder (MediaCodec del NDK) no se revisa aquí.
set -euo pipefail
cd "$(dirname "$0")/../.."
APP=app/src/main/cpp
JAVA=${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")}
OUT="${TMPDIR:-/tmp}/wavapp-android-check"
mkdir -p "$OUT"
for f in AudioInput AudioOutput Fallas jni_bridge; do
    g++ -std=c++17 -fsyntax-only -Wall -Wextra -Werror -Wno-unused-parameter \
        -Itests/android/stubs -I"$APP/core" -I"$APP/android" -isystem "$APP/third_party/eigen" \
        -I"$JAVA/include" -I"$JAVA/include/linux" "$APP/android/$f.cpp"
    echo "  $f.cpp bien"
done
