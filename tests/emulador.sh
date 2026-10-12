#!/usr/bin/env bash
# Corre las pruebas instrumentadas en el emulador, guarda el logcat y junta las capturas
# (una vuelta en modo claro con todas las pruebas y otra de la pantalla en modo oscuro).
set -uo pipefail
mkdir -p capturas
juntar() { find app/build/outputs -name '*.png' -exec cp {} capturas/ \; 2> /dev/null || true; }
adb logcat -c || true
adb shell cmd uimode night no || true
./gradlew connectedDebugAndroidTest
claro=$?
juntar
adb shell cmd uimode night yes || true
./gradlew connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.wavapp.mezcla.PantallaTest
oscuro=$?
juntar
adb shell cmd uimode night no || true
adb logcat -d > logcat.txt || true
ls -la capturas || true
[ "$claro" -eq 0 ] && [ "$oscuro" -eq 0 ]
