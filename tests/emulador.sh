#!/usr/bin/env bash
# Corre las pruebas instrumentadas en el emulador, guarda el logcat y junta las capturas
# (una vuelta en modo claro con todas las pruebas y otra de la pantalla en modo oscuro).
# Al final instala el APK optimizado (release, como el que se publica) y revisa que arranque.
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

# El APK optimizado (R8) arranca y no se cierra.
release=1
if ./gradlew assembleRelease -PconX86=true; then
    adb uninstall com.wavapp.mezcla > /dev/null 2>&1 || true
    adb logcat -b crash -c || true
    if adb install -r app/build/outputs/apk/release/app-release.apk; then
        adb shell am start -W -n com.wavapp.mezcla/.MainActivity
        sleep 10
        adb exec-out screencap -p > capturas/release-inicio.png || true
        if adb shell pidof com.wavapp.mezcla > /dev/null && ! adb logcat -d -b crash | grep -q com.wavapp.mezcla; then
            release=0
            echo "El APK optimizado arranca bien"
        else
            echo "El APK optimizado se cerró:"
            adb logcat -d -b crash || true
        fi
    fi
fi

adb logcat -d > logcat.txt || true
ls -la capturas || true
[ "$claro" -eq 0 ] && [ "$oscuro" -eq 0 ] && [ "$release" -eq 0 ]
