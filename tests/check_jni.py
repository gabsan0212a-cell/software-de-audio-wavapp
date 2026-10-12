#!/usr/bin/env python3
"""Comprueba que cada `external fun` de NativeAudio.kt tenga su función JNI en C++ y viceversa.
Un nombre que no coincide compila bien pero cierra la app al llamarlo."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
kt = (root / "app/src/main/java/com/wavapp/mezcla/NativeAudio.kt").read_text(encoding="utf-8")
cpp = (root / "app/src/main/cpp/jni_bridge.cpp").read_text(encoding="utf-8")

kotlin = set(re.findall(r"external fun (\w+)\(", kt))
native = set(re.findall(r"^JNI\(\w+, (\w+)\)", cpp, re.MULTILINE))

missing = sorted(kotlin - native)
extra = sorted(native - kotlin)
if missing:
    print("Sin implementación en C++:", ", ".join(missing))
if extra:
    print("En C++ pero no declaradas en Kotlin:", ", ".join(extra))
if missing or extra:
    sys.exit(1)
print(f"JNI correcto: {len(kotlin)} funciones")
