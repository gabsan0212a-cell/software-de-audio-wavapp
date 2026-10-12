#!/usr/bin/env python3
"""Comprueba que cada `external fun` de Nucleo.kt tenga su función JNI en C++ con los mismos
tipos (y viceversa). Un nombre o un tipo que no coincide compila bien pero cierra la app (o
corrompe memoria) al llamarlo."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
kt = (root / "app/src/main/java/com/wavapp/mezcla/Nucleo.kt").read_text(encoding="utf-8")
cpp = (root / "app/src/main/cpp/android/jni_bridge.cpp").read_text(encoding="utf-8")

TIPOS = {
    "Int": "jint", "Long": "jlong", "Float": "jfloat", "Double": "jdouble", "Boolean": "jboolean",
    "String": "jstring", "IntArray": "jintArray", "LongArray": "jlongArray", "FloatArray": "jfloatArray",
    "ByteArray": "jbyteArray", "Array<String>": "jobjectArray", "Unit": "void",
}

kotlin = {}
for m in re.finditer(r"external fun (\w+)\(([^)]*)\)\s*(?::\s*([\w<>?]+))?", kt):
    nombre, params, ret = m.group(1), m.group(2).strip(), (m.group(3) or "Unit").rstrip("?")
    tipos = [p.split(":")[1].strip().rstrip("?") for p in params.split(",") if p.strip()] if params else []
    kotlin[nombre] = ([TIPOS.get(t, "?" + t) for t in tipos], TIPOS.get(ret, "?" + ret))

nativo = {}
for m in re.finditer(r"^JNI\((\w+), (\w+)\)\(([^)]*)\)", cpp, re.MULTILINE | re.DOTALL):
    ret, nombre, params = m.group(1), m.group(2), m.group(3)
    partes = [p.strip() for p in params.split(",")][2:]  # sin JNIEnv* ni jobject
    nativo[nombre] = ([p.split()[0] for p in partes if p], ret)

errores = []
for n in sorted(set(kotlin) - set(nativo)):
    errores.append(f"Sin implementación en C++: {n}")
for n in sorted(set(nativo) - set(kotlin)):
    errores.append(f"En C++ pero no declarada en Kotlin: {n}")
for n in sorted(set(kotlin) & set(nativo)):
    if kotlin[n] != nativo[n]:
        errores.append(f"{n}: Kotlin {kotlin[n]} ≠ C++ {nativo[n]}")
if errores:
    print("\n".join(errores))
    sys.exit(1)
print(f"JNI correcto: {len(kotlin)} funciones, mismos tipos")
