"""Referencias para probar el motor de separación en C++ contra el código original (PyTorch).

Uso:
  python referencias.py --kim <repo de Kim> --salida <carpeta> [--ckpt MelBandRoformer.ckpt]

Siempre genera un modelo "mini" con pesos al azar (mini.wvm) y sus respuestas (mini-ref.wvm):
una pasada del modelo con lo de adentro (bandas, cada transformer, máscaras) y el troceo con
traslape (demix_track) en varios largos. Con --ckpt, hace lo mismo con el modelo real
(kim-ref.wvm), con los pesos redondeados a float16 como quedan en el archivo, y mide cuánto
cambia la salida por ese redondeo.
"""

import argparse
import os
import sys
import time
from types import SimpleNamespace

import numpy as np
import torch
from scipy import signal

import convertir
import formato


def espacio(d):
    return SimpleNamespace(**{k: espacio(v) if isinstance(v, dict) else v for k, v in d.items()})


def musica_sintetica(segundos, sr, semilla=1):
    """Algo parecido a una canción: voz (pulsos glotales por formantes, con vibrato y sílabas),
    acordes, bajo y batería, en estéreo. Solo para comparar números, no para medir calidad."""
    rng = np.random.default_rng(semilla)
    n = int(segundos * sr)
    t = np.arange(n) / sr
    # Voz
    notas = np.array([57, 60, 62, 64, 62, 60, 59, 57, 64, 65, 64, 62])
    dur = 0.42
    k = (t / dur).astype(int)
    f0 = 440.0 * 2.0 ** ((notas[k % len(notas)] - 69) / 12.0) * (1 + 0.012 * np.sin(2 * np.pi * 5.3 * t))
    fase = np.cumsum(f0) / sr
    voz = np.zeros(n)
    for h in range(1, 40):
        voz += np.where(h * f0 < sr * 0.45, np.sin(2 * np.pi * h * fase) / h, 0.0)
    formantes = [((800, 1150, 2900), (270, 2300, 3000)), ((500, 1000, 2600), (300, 870, 2250))]
    salida_voz = np.zeros(n)
    for i, (a, b) in enumerate(formantes):
        parte = np.zeros(n)
        for f in (a if i == 0 else b):
            bb, aa = signal.iirpeak(f, 6.0, fs=sr)
            parte += signal.lfilter(bb, aa, voz)
        mascara = ((k + i) % 2 == 0).astype(float)
        salida_voz += parte * mascara
    pos = (t % dur) / dur
    silaba = np.clip(pos / 0.08, 0, 1) * np.clip((0.9 - pos) / 0.15, 0, 1)
    salida_voz *= silaba * (0.55 + 0.45 * ((k // 8) % 2))
    # Acordes (sierra filtrada), bajo y batería
    acordes = [(57, 60, 64), (53, 57, 60), (48, 52, 55), (55, 59, 62)]
    compas = (t / (dur * 4)).astype(int) % 4
    pad_l = np.zeros(n)
    pad_r = np.zeros(n)
    bajo = np.zeros(n)
    for c, notas_c in enumerate(acordes):
        m = (compas == c).astype(float)
        for j, nota in enumerate(notas_c):
            f = 440.0 * 2.0 ** ((nota - 69) / 12.0)
            pad_l += m * signal.sawtooth(2 * np.pi * f * 1.002 * t + j)
            pad_r += m * signal.sawtooth(2 * np.pi * f * 0.998 * t + 2 * j)
        f = 440.0 * 2.0 ** ((notas_c[0] - 24 - 69) / 12.0)
        bajo += m * np.sin(2 * np.pi * f * t)
    bb, aa = signal.butter(2, 1800, fs=sr)
    pad_l = signal.lfilter(bb, aa, pad_l) * 0.12
    pad_r = signal.lfilter(bb, aa, pad_r) * 0.12
    golpe = t % (dur * 2)
    bombo = np.sin(2 * np.pi * (50 + 70 * np.exp(-golpe * 30)) * golpe) * np.exp(-golpe * 9)
    ruido = rng.standard_normal(n)
    bb, aa = signal.butter(2, 6000, btype="high", fs=sr)
    platillo = signal.lfilter(bb, aa, ruido) * np.exp(-((t % (dur / 2)) * 40)) * 0.15
    izq = 0.5 * salida_voz / np.max(np.abs(salida_voz)) + pad_l + 0.5 * bajo + 0.6 * bombo + 0.3 * platillo
    der = 0.5 * salida_voz / np.max(np.abs(salida_voz)) + pad_r + 0.5 * bajo + 0.6 * bombo + 0.7 * platillo
    mezcla = np.stack([izq, der])
    return (0.8 * mezcla / np.max(np.abs(mezcla))).astype(np.float32)


def capturar(modelo):
    capt = {}

    def gancho(nombre):
        def f(_m, _i, salida):
            capt[nombre] = salida.detach().clone()
        return f

    modelo.band_split.register_forward_hook(gancho("bandas"))
    for i, (tt, ft) in enumerate(modelo.layers):
        tt.register_forward_hook(gancho(f"capa{i}.tiempo"))
        ft.register_forward_hook(gancho(f"capa{i}.frecuencia"))
    modelo.mask_estimators[0].register_forward_hook(gancho("mascaras"))
    return capt


def adentro(capt, modelo, cuadros):
    """Lo de adentro en el orden del núcleo: [tiempo][banda][dim] (y las máscaras [tiempo][...])."""
    lista = []
    b = capt["bandas"][0]  # [t, f, d]
    lista.append(("bandas", b[:cuadros].numpy(), "f32"))
    num_bandas = b.shape[1]
    for i in range(len(modelo.layers)):
        x = capt[f"capa{i}.tiempo"]  # [(f), t, d]
        x = x.reshape(num_bandas, -1, x.shape[-1]).permute(1, 0, 2)
        lista.append((f"capa{i}.tiempo", x[:cuadros].numpy(), "f32"))
        x = capt[f"capa{i}.frecuencia"]  # [(t), f, d]
        lista.append((f"capa{i}.frecuencia", x[:cuadros].numpy(), "f32"))
    m = capt["mascaras"][0]  # [t, (f c)]
    lista.append(("mascaras", m[:cuadros].numpy(), "f32"))
    return lista


def demix(utils, config, modelo, mezcla):
    res, _ = utils.demix_track(espacio(config), modelo, torch.from_numpy(mezcla), "cpu")
    return np.asarray(res[config["training"]["target_instrument"]], dtype=np.float32)


def snr(a, b):
    return 10 * np.log10(np.sum(a.astype(np.float64) ** 2) / max(np.sum((a - b).astype(np.float64) ** 2), 1e-30))


def mini(repo, carpeta, utils):
    torch.manual_seed(1234)
    params = dict(dim=32, depth=2, stereo=True, num_stems=1, time_transformer_depth=1, freq_transformer_depth=1,
                  num_bands=12, dim_head=8, heads=4, attn_dropout=0.0, ff_dropout=0.0, flash_attn=True,
                  dim_freqs_in=129, sample_rate=16000, stft_n_fft=256, stft_hop_length=64, stft_win_length=256,
                  stft_normalized=False, mask_estimator_depth=2)
    config = {"model": params, "inference": {"chunk_size": 4096, "num_overlap": 2},
              "training": {"target_instrument": "vocals", "instruments": ["vocals", "other"]}}
    modelo, params, _ = convertir.cargar_kim(repo, None, config=config)
    with torch.no_grad():
        for nombre, p in modelo.named_parameters():
            if nombre.endswith("rotary_embed.freqs"):
                continue
            if nombre.endswith("gamma"):
                p.copy_(1 + 0.3 * torch.randn_like(p))
            elif nombre.endswith("bias"):
                p.copy_(0.2 * torch.randn_like(p))
            else:
                p.copy_(p * 1.5)
    convertir.redondear_como_archivo(modelo)
    formato.escribir(os.path.join(carpeta, "mini.wvm"), convertir.cabecera(params, config["inference"], "mini"),
                     convertir.tensores(modelo))

    rng = np.random.default_rng(7)
    t = np.arange(20000) / 16000.0
    largo = (0.3 * np.sin(2 * np.pi * 220 * t) + 0.2 * np.sin(2 * np.pi * 1330 * t + 1)
             + 0.05 * rng.standard_normal(len(t)))
    largo = np.stack([largo, 0.7 * largo + 0.1 * rng.standard_normal(len(t))]).astype(np.float32)

    capt = capturar(modelo)
    entrada = largo[:, :4096].copy()
    with torch.no_grad():
        salida = modelo(torch.from_numpy(entrada)[None])[0].numpy()
    lista = [("entrada", entrada, "f32"), ("salida", salida, "f32")]
    lista += adentro(capt, modelo, 10 ** 9)
    for n in (15000, 3000, 1500):
        x = largo[:, :n].copy()
        with torch.no_grad():
            lista.append((f"demix{n}.entrada", x, "f32"))
            lista.append((f"demix{n}.salida", demix(utils, config, modelo, x), "f32"))
    formato.escribir(os.path.join(carpeta, "mini-ref.wvm"), {"formato": 1, "referencia": "mini"}, lista)
    print("mini: listo")


def real(repo, ckpt, carpeta, utils):
    modelo, params, config = convertir.cargar_kim(repo, ckpt)
    config = dict(config)
    config["training"] = {"target_instrument": "vocals", "instruments": ["vocals", "other"]}
    sr = params["sample_rate"]
    chunk = config["inference"]["chunk_size"]
    musica = musica_sintetica(20.0, sr)
    entrada = musica[:, sr * 2: sr * 2 + chunk].copy()

    t0 = time.time()
    with torch.no_grad():
        exacta = modelo(torch.from_numpy(entrada)[None])[0].numpy()
    print(f"real: una pasada (float32) en {time.time() - t0:.1f} s")

    convertir.redondear_como_archivo(modelo)
    capt = capturar(modelo)
    with torch.no_grad():
        salida = modelo(torch.from_numpy(entrada)[None])[0].numpy()
    print(f"real: SNR por guardar los pesos en float16: {snr(exacta, salida):.1f} dB")
    lista = [("entrada", entrada, "f32"), ("salida", salida, "f32")]
    lista += adentro(capt, modelo, 8)

    largo = musica[:, : sr * 12].copy()
    t0 = time.time()
    with torch.no_grad():
        lista.append(("demix.entrada", largo, "f32"))
        lista.append(("demix.salida", demix(utils, config, modelo, largo), "f32"))
    print(f"real: troceo de 12 s en {time.time() - t0:.1f} s")
    formato.escribir(os.path.join(carpeta, "kim-ref.wvm"), {"formato": 1, "referencia": "kim"}, lista)
    print("real: listo")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kim", required=True)
    ap.add_argument("--salida", required=True)
    ap.add_argument("--ckpt")
    a = ap.parse_args()
    os.makedirs(a.salida, exist_ok=True)
    torch.set_num_threads(os.cpu_count() or 1)
    sys.path.insert(0, a.kim)
    import utils  # noqa: E402  (demix_track del repo de Kim)

    mini(a.kim, a.salida, utils)
    if a.ckpt:
        real(a.kim, a.ckpt, a.salida, utils)


if __name__ == "__main__":
    main()
