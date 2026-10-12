#!/usr/bin/env bash
# Corre las pruebas instrumentadas en el emulador, guarda el logcat y junta las capturas.
set -uo pipefail
adb logcat -c || true
./gradlew connectedDebugAndroidTest
estado=$?
adb logcat -d > logcat.txt || true
mkdir -p capturas
find app/build/outputs -name '*.png' -exec cp {} capturas/ \; 2> /dev/null || true
ls -la capturas || true
exit $estado
