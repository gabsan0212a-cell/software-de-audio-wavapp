#!/usr/bin/env bash
# Compila y corre las pruebas del núcleo nativo en Linux, con detectores de errores:
#   ASan/UBSan (memoria y comportamiento indefinido) y TSan (carreras entre hilos).
# Uso: tests/native/run.sh [nombreDePrueba]
set -euo pipefail
cd "$(dirname "$0")/../.."

SRC=app/src/main/cpp
FILES="$SRC/WavReader.cpp $SRC/WavWriter.cpp $SRC/Resampler.cpp $SRC/MappedAudio.cpp \
       $SRC/Limiter.cpp $SRC/Mixer.cpp $SRC/Importer.cpp tests/native/tests.cpp"
OUT="${TMPDIR:-/tmp}/wavapp-tests"
mkdir -p "$OUT"
CXX="${CXX:-g++}"
FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -I$SRC -D_FILE_OFFSET_BITS=64"

echo "== ASan + UBSan"
# shellcheck disable=SC2086
$CXX $FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    $FILES -o "$OUT/tests-asan" -lpthread
"$OUT/tests-asan" "$@"

echo "== TSan (concurrencia)"
# shellcheck disable=SC2086
$CXX $FLAGS -fsanitize=thread $FILES -o "$OUT/tests-tsan" -lpthread
"$OUT/tests-tsan" concurrencia
