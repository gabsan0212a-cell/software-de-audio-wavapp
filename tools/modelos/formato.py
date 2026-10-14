"""Archivo de modelo de WavApp (.wvm): una cabecera JSON y los tensores en binario.

    "WAVAPPM1"            8 bytes
    largo de la cabecera  uint32 little-endian
    cabecera              JSON (utf-8): datos del modelo y la tabla de tensores
    (relleno hasta múltiplo de 64)
    tensores              cada uno alineado a 64 bytes; "desplazamiento" cuenta desde aquí

Tipos: "f32" (float32), "f16" (float16) y "u8". Todo little-endian.
El núcleo (app/src/main/cpp/core/sep/ModelFile) lee exactamente esto.
"""

import json
import struct

import numpy as np

MAGIA = b"WAVAPPM1"
TIPOS = {"f32": "<f4", "f16": "<f2", "u8": "u1"}


def tipo_peso(arr):
    """Los pesos grandes (matrices) van en float16; lo pequeño (normas, sesgos), en float32."""
    return "f16" if arr.ndim == 2 and arr.size >= 4096 else "f32"


def escribir(ruta, cabecera, tensores):
    """tensores: lista de (nombre, arreglo numpy, tipo)."""
    datos = bytearray()
    tabla = {}
    for nombre, arr, tipo in tensores:
        b = np.ascontiguousarray(arr).astype(TIPOS[tipo]).tobytes()
        datos += b"\0" * ((-len(datos)) % 64)
        tabla[nombre] = {"tipo": tipo, "forma": [int(x) for x in arr.shape], "desplazamiento": len(datos),
                         "bytes": len(b)}
        datos += b
    cab = dict(cabecera)
    cab["tensores"] = tabla
    js = json.dumps(cab, ensure_ascii=False).encode("utf-8")
    inicio = MAGIA + struct.pack("<I", len(js)) + js
    inicio += b"\0" * ((-len(inicio)) % 64)
    with open(ruta, "wb") as f:
        f.write(inicio)
        f.write(datos)


def leer(ruta):
    """Devuelve (cabecera, {nombre: arreglo float32/uint8})."""
    with open(ruta, "rb") as f:
        todo = f.read()
    assert todo[:8] == MAGIA, "no es un archivo de modelo de WavApp"
    (largo,) = struct.unpack("<I", todo[8:12])
    cab = json.loads(todo[12:12 + largo].decode("utf-8"))
    base = 12 + largo
    base += (-base) % 64
    salida = {}
    for nombre, t in cab["tensores"].items():
        a = np.frombuffer(todo, dtype=TIPOS[t["tipo"]], count=int(np.prod(t["forma"])) if t["forma"] else 1,
                          offset=base + t["desplazamiento"])
        salida[nombre] = a.reshape(t["forma"]).astype(np.float32 if t["tipo"] != "u8" else np.uint8)
    return cab, salida
