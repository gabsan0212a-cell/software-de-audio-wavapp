#!/usr/bin/env bash
# Corre las pruebas instrumentadas en el emulador y guarda el logcat para revisar fallas.
set -uo pipefail
adb logcat -c || true
./gradlew connectedDebugAndroidTest
estado=$?
adb logcat -d > logcat.txt || true
exit $estado
