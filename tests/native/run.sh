#!/usr/bin/env bash
# Compila y corre las pruebas del núcleo (app/src/main/cpp/core) en Linux, con detectores de errores:
#   ASan/UBSan (memoria y comportamiento indefinido) y TSan (carreras entre hilos).
# El núcleo no usa nada de Android, por eso se puede probar aquí completo.
# Uso: tests/native/run.sh [nombreDePrueba...]
set -euo pipefail
cd "$(dirname "$0")/../.."

CORE=app/src/main/cpp/core
FILES="$(find "$CORE" -name '*.cpp' | sort | tr '\n' ' ') tests/native/tests.cpp"
OUT="${TMPDIR:-/tmp}/wavapp-tests"
mkdir -p "$OUT"
CXX="${CXX:-g++}"
FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -I$CORE -D_FILE_OFFSET_BITS=64"

echo "== ASan + UBSan"
# shellcheck disable=SC2086
$CXX $FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    $FILES -o "$OUT/tests-asan" -lpthread
"$OUT/tests-asan" "$@"

echo "== TSan (concurrencia)"
# shellcheck disable=SC2086
$CXX $FLAGS -fsanitize=thread $FILES -o "$OUT/tests-tsan" -lpthread
if [ "$#" -eq 0 ]; then
    "$OUT/tests-tsan" concurrencia concurrenciaEstudio
else
    "$OUT/tests-tsan" "$@"
fi
