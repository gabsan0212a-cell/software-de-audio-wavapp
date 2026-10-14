"""Convierte el modelo de voz (Mel-Band RoFormer de Kim, PyTorch) al formato de WavApp (.wvm).

Uso: python convertir.py --kim <repo de Kim> --ckpt MelBandRoformer.ckpt --salida voz-kim.wvm

Los pesos grandes quedan en float16 (la mitad de tamaño); el núcleo los pasa a float32 al usarlos.
También se guarda qué frecuencias del STFT entran en cada banda mel ("freqs_per_band"), tal como
las calcula el modelo original (librosa), para no depender de redondeos al recalcularlas.
"""

import argparse
import sys

import numpy as np
import torch
import yaml

import formato


def cargar_kim(repo, ckpt=None, config=None, **cambios):
    """El modelo con el código original de Kim. Sin ckpt: pesos al azar (para la prueba mini)."""
    sys.path.insert(0, repo)
    from models.mel_band_roformer import MelBandRoformer  # noqa: E402

    if config is None:
        with open(f"{repo}/configs/config_vocals_mel_band_roformer.yaml") as f:
            config = yaml.load(f, Loader=yaml.FullLoader)
    params = dict(config["model"])
    params.update(cambios)
    modelo = MelBandRoformer(**params)
    if ckpt is not None:
        # Solo tensores: el archivo no puede ejecutar código al abrirse.
        sd = torch.load(ckpt, map_location="cpu", weights_only=True)
        if isinstance(sd, dict) and "state_dict" in sd:
            sd = sd["state_dict"]
        modelo.load_state_dict(sd)
    modelo.eval()
    return modelo, params, config


def cabecera(params, inferencia, nombre):
    claves = ["dim", "depth", "stereo", "num_stems", "time_transformer_depth", "freq_transformer_depth",
              "num_bands", "dim_head", "heads", "sample_rate", "stft_n_fft", "stft_hop_length",
              "stft_win_length", "mask_estimator_depth"]
    cfg = {k: params[k] for k in claves}
    cfg["stereo"] = bool(cfg["stereo"])
    cfg["mlp_expansion_factor"] = 4  # fijo en MaskEstimator del código de Kim
    cfg["chunk_size"] = int(inferencia["chunk_size"])
    cfg["num_overlap"] = int(inferencia["num_overlap"])
    return {"formato": 1, "modelo": "mel_band_roformer", "nombre": nombre, "config": cfg}


def tensores(modelo):
    """(nombre, arreglo, tipo) de todo lo que necesita el núcleo."""
    lista = []
    for nombre, t in modelo.state_dict().items():
        arr = t.detach().cpu().numpy().astype(np.float32)
        lista.append((nombre, arr, formato.tipo_peso(arr)))
    bandas = modelo.freqs_per_band.cpu().numpy().astype(np.uint8)
    lista.append(("freqs_per_band", bandas, "u8"))
    return lista


def redondear_como_archivo(modelo):
    """Deja en el modelo los pesos tal como quedan en el archivo (float16 donde corresponde)."""
    with torch.no_grad():
        for _, t in modelo.state_dict().items():
            if formato.tipo_peso(t.numpy()) == "f16":
                t.copy_(t.half().float())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kim", required=True)
    ap.add_argument("--ckpt", required=True)
    ap.add_argument("--salida", required=True)
    a = ap.parse_args()
    modelo, params, config = cargar_kim(a.kim, a.ckpt)
    cab = cabecera(params, config["inference"], "Mel-Band RoFormer (voz) · Kim · MIT")
    lista = tensores(modelo)
    formato.escribir(a.salida, cab, lista)
    total = sum(x[1].size for x in lista)
    print(f"{a.salida}: {len(lista)} tensores, {total / 1e6:.1f} M valores")
    nb = modelo.num_freqs_per_band.cpu().numpy()
    print(f"bandas: {len(nb)}, frecuencias por banda: min {nb.min()}, max {nb.max()}, suma {nb.sum()}")


if __name__ == "__main__":
    main()
