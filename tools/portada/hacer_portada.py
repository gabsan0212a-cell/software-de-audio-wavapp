"""Hace el ícono, la portada y la pantalla de arranque de la app a partir de original.jpg.

El dibujo (la onda blanca que forma la parte de arriba de un círculo) se separa del fondo: su
brillo pasa a ser transparencia sobre un fondo liso del mismo color. Así se puede poner a
cualquier tamaño y en cualquier margen sin costuras ni el ruido del JPEG.

Uso: python3 -I tools/portada/hacer_portada.py   (escribe en app/src/main/res)
"""
import os
import sys

import numpy as np
from PIL import Image

AQUI = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(AQUI, "..", "..", "app", "src", "main", "res")

FONDO = 19  # gris del fondo de la imagen (#131313)
CENTRO = (704.0, 387.0)  # centro del círculo en original.jpg
DIAMETRO = 542.0  # de borde a borde (las líneas brillantes)
RADIO_UTIL = 340.0  # más allá solo hay fondo


def transparencia():
    """Brillo del dibujo sobre el fondo, 0..1 (sin el ruido del fondo)."""
    im = Image.open(os.path.join(AQUI, "original.jpg")).convert("L")
    L = np.asarray(im).astype(np.float64)
    a = np.clip((L - (FONDO + 1.5)) / (255.0 - FONDO), 0.0, 1.0)
    yy, xx = np.mgrid[: L.shape[0], : L.shape[1]]
    d = np.hypot(xx - CENTRO[0], yy - CENTRO[1])
    a *= np.clip((RADIO_UTIL + 10 - d) / 20.0, 0.0, 1.0)  # fuera del dibujo, nada
    return a


def colocar(a, lado, diametro):
    """El dibujo centrado en un cuadro de `lado` px, con el círculo de `diametro` px."""
    escala = diametro / DIAMETRO
    medio = lado / escala / 2.0
    # Margen de fondo (transparencia 0) para recortes más grandes que la imagen.
    m = int(np.ceil(medio)) + 4
    lienzo = np.pad(a, m).astype(np.float32)
    caja = (CENTRO[0] - medio + m, CENTRO[1] - medio + m, CENTRO[0] + medio + m, CENTRO[1] + medio + m)
    img = Image.fromarray(lienzo, mode="F").resize((lado, lado), Image.LANCZOS, box=caja)
    return np.clip(np.asarray(img), 0, 1)


def guardar_gris(alfa, ruta, calidad=None):
    v = FONDO + alfa * (255 - FONDO)
    img = Image.fromarray(np.round(v).astype(np.uint8), mode="L").convert("RGB")
    os.makedirs(os.path.dirname(ruta), exist_ok=True)
    if ruta.endswith(".webp"):
        img.save(ruta, "WEBP", quality=calidad or 92, method=6)
    else:
        img.save(ruta, optimize=True)


def guardar_blanco(alfa, ruta):
    """Blanco con transparencia (capa del ícono adaptativo)."""
    rgba = np.zeros(alfa.shape + (4,), np.uint8)
    rgba[..., :3] = 255
    rgba[..., 3] = np.round(alfa * 255).astype(np.uint8)
    os.makedirs(os.path.dirname(ruta), exist_ok=True)
    Image.fromarray(rgba, mode="RGBA").save(ruta, optimize=True)


def main():
    a = transparencia()
    # Solo las líneas (sin el resplandor): para el ícono de un color de Android 13.
    lineas = np.clip((a - 0.6) / 0.3, 0, 1)
    # Portada de la app (mini reproductor, reproductor, pantalla vacía): 1024 px, el círculo al 62 %.
    guardar_gris(colocar(a, 1024, 1024 * 0.62), os.path.join(RES, "drawable-nodpi", "portada.webp"))
    # Ícono adaptativo: capa de 108 dp; el círculo mide 58 dp (se ve entero en cualquier forma).
    for carpeta, lado in (("mdpi", 108), ("hdpi", 162), ("xhdpi", 216), ("xxhdpi", 324), ("xxxhdpi", 432)):
        d = os.path.join(RES, "mipmap-" + carpeta)
        guardar_blanco(colocar(a, lado, lado * 0.54), os.path.join(d, "ic_launcher_foreground.png"))
        guardar_blanco(colocar(lineas, lado, lado * 0.54), os.path.join(d, "ic_launcher_monochrome.png"))
    print("listo")


if __name__ == "__main__":
    sys.exit(main())
