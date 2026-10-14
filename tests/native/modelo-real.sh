#!/usr/bin/env bash
# Prueba el modelo de voz real contra el original en PyTorch (ver tests/native/modelo_real.cpp).
# Uso: tests/native/modelo-real.sh [carpeta]
# La carpeta debe tener voz-kim.wvm y kim-ref.wvm (del release "modelos"); si faltan y está `gh`,
# se bajan ahí.
set -euo pipefail
cd "$(dirname "$0")/../.."

DIR="${1:-${TMPDIR:-/tmp}/wavapp-modelos}"
mkdir -p "$DIR"
for f in voz-kim.wvm kim-ref.wvm; do
    if [ ! -s "$DIR/$f" ]; then
        echo "Bajando $f del release \"modelos\"..."
        gh release download modelos -p "$f" -D "$DIR" --clobber
    fi
done

CORE=app/src/main/cpp/core
OUT="${TMPDIR:-/tmp}/wavapp-tests"
mkdir -p "$OUT"
# Optimizado como en el celular (en x86 con AVX2/FMA; en ARM el compilador usa NEON).
ARCH=""
if [ "$(uname -m)" = "x86_64" ]; then ARCH="-march=x86-64-v3"; fi
# shellcheck disable=SC2086
${CXX:-g++} -std=c++17 -O2 $ARCH -DNDEBUG -Wall -Wextra -Werror -I$CORE -isystem app/src/main/cpp/third_party/eigen \
    tests/native/modelo_real.cpp $CORE/sep/*.cpp $CORE/dsp/Fft.cpp $CORE/io/Json.cpp \
    -o "$OUT/modelo-real" -lpthread
"$OUT/modelo-real" "$DIR"
