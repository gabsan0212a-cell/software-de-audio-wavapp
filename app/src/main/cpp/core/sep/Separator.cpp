#include "sep/Separator.h"

#include <algorithm>
#include <cstdio>
#include <new>
#include <vector>

namespace {

// Cómo se trocea (igual que demix_track).
struct Plan {
    int chunk = 0, overlap = 1, step = 0, fade = 0, border = 0;
    bool padded = false;  // con relleno por reflejo de `border` a cada lado
    int64_t total = 0;    // largo ya rellenado
    int chunks = 0;
};

Plan makePlan(int64_t length, int chunk, int overlap) {
    Plan p;
    p.chunk = chunk;
    p.overlap = std::max(1, overlap);
    p.step = chunk / p.overlap;
    p.fade = chunk / 10;
    p.border = chunk - p.step;
    p.padded = length > 2 * static_cast<int64_t>(p.border) && p.border > 0;
    p.total = length + (p.padded ? 2 * static_cast<int64_t>(p.border) : 0);
    p.chunks = static_cast<int>((p.total + p.step - 1) / p.step);
    return p;
}

inline int64_t reflect(int64_t j, int64_t length) {
    if (j < 0) j = -j;
    if (j >= length) j = 2 * (length - 1) - j;
    return j;
}

bool readFile(const std::string &path, std::vector<float> &a, std::vector<float> &b) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    const bool ok = std::fread(a.data(), sizeof(float), a.size(), f) == a.size() &&
                    std::fread(b.data(), sizeof(float), b.size(), f) == b.size() && std::fgetc(f) == EOF;
    std::fclose(f);
    return ok;
}

bool writeFile(const std::string &path, const std::vector<float> &a, const std::vector<float> &b) {
    const std::string tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(a.data(), sizeof(float), a.size(), f) == a.size() &&
              std::fwrite(b.data(), sizeof(float), b.size(), f) == b.size();
    ok = std::fclose(f) == 0 && ok;
    if (ok) ok = std::rename(tmp.c_str(), path.c_str()) == 0;
    if (!ok) std::remove(tmp.c_str());
    return ok;
}

std::string readText(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return "";
    std::string s;
    char buf[256];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

std::string fullKey(const std::string &key, int64_t length, int chunk, int overlap) {
    return key + "|" + std::to_string(chunk) + "|" + std::to_string(overlap) + "|" + std::to_string(length);
}

}  // namespace

int Separator::chunkCount(int64_t length, int chunk, int overlap) {
    return length <= 0 || chunk <= 0 ? 0 : makePlan(length, chunk, overlap).chunks;
}

int Separator::savedChunks(const std::string &cacheDir, const std::string &cacheKey, int64_t length, int chunk,
                           int overlap) {
    if (cacheDir.empty() || length <= 0 || chunk <= 0) return 0;
    const Plan p = makePlan(length, chunk, overlap);
    if (readText(cacheDir + "/clave") != fullKey(cacheKey, length, chunk, p.overlap)) return 0;
    const long bytes = static_cast<long>(2 * sizeof(float)) * chunk;
    int n = 0;
    for (int k = 0; k < p.chunks; ++k) {
        FILE *f = std::fopen((cacheDir + "/" + std::to_string(k) + ".f32").c_str(), "rb");
        if (!f) continue;
        const bool ok = std::fseek(f, 0, SEEK_END) == 0 && std::ftell(f) == bytes;
        std::fclose(f);
        if (ok) ++n;
    }
    return n;
}

std::string Separator::run(MelRoformer &model, ThreadPool &pool, int64_t length, const Reader &read,
                           const Writer &write, const Options &opt) {
    if (length <= 0) return "No hay audio";
    const Plan p = makePlan(length, model.chunk(), opt.overlap > 0 ? opt.overlap : model.config().overlap);
    const int C = p.chunk;
    auto cancelled = [&] { return opt.cancel && opt.cancel->load(); };

    // Trozos guardados de antes: solo valen si son de la misma entrada y del mismo troceo.
    auto chunkPath = [&](int k) { return opt.cacheDir + "/" + std::to_string(k) + ".f32"; };
    const bool cache = !opt.cacheDir.empty();
    if (cache) {
        const std::string key = fullKey(opt.cacheKey, length, C, p.overlap);
        const std::string keyPath = opt.cacheDir + "/clave";
        if (readText(keyPath) != key) {
            for (int k = 0;; ++k) {  // los trozos van numerados desde 0
                if (std::remove(chunkPath(k).c_str()) != 0 && k >= p.chunks) break;
            }
            FILE *f = std::fopen(keyPath.c_str(), "wb");
            if (f) {
                std::fwrite(key.data(), 1, key.size(), f);
                std::fclose(f);
            }
        }
    }

    try {
        // Ventana: sube al principio y baja al final (linspace del original).
        std::vector<float> window(static_cast<size_t>(C), 1.0f);
        for (int k = 0; k < p.fade; ++k) {
            const float up = p.fade > 1 ? static_cast<float>(k) / static_cast<float>(p.fade - 1) : 1.0f;
            window[static_cast<size_t>(k)] *= up;
            window[static_cast<size_t>(C - p.fade + k)] *= 1.0f - up;
        }
        std::vector<float> partL(static_cast<size_t>(C)), partR(static_cast<size_t>(C));
        std::vector<float> outL(static_cast<size_t>(C)), outR(static_cast<size_t>(C));
        std::vector<float> srcL, srcR;
        // Lo que se va sumando para las posiciones [i, i + C) (se corre `step` con cada trozo).
        std::vector<float> accL(static_cast<size_t>(C), 0.0f), accR(static_cast<size_t>(C), 0.0f);
        std::vector<float> cnt(static_cast<size_t>(C), 0.0f);
        std::vector<float> doneL, doneR;
        const int64_t off = p.padded ? p.border : 0;
        int reused = 0;

        // Entrega las posiciones [from, to) ya terminadas (quitando el relleno).
        auto emit = [&](int64_t base, int64_t from, int64_t to) -> bool {
            const int64_t a = std::max(from, off), b = std::min(to, off + length);
            if (b <= a) return true;
            const int n = static_cast<int>(b - a);
            doneL.resize(static_cast<size_t>(n));
            doneR.resize(static_cast<size_t>(n));
            for (int k = 0; k < n; ++k) {
                const size_t q = static_cast<size_t>(a - base + k);
                // Donde no sumó nada (contador 0) el original da 0/0 → NaN → 0.
                doneL[static_cast<size_t>(k)] = cnt[q] != 0.0f ? accL[q] / cnt[q] : 0.0f;
                doneR[static_cast<size_t>(k)] = cnt[q] != 0.0f ? accR[q] / cnt[q] : 0.0f;
            }
            return write(doneL.data(), doneR.data(), n);
        };

        for (int k = 0; k < p.chunks; ++k) {
            if (cancelled()) return "Cancelado";
            const int64_t i = static_cast<int64_t>(k) * p.step;
            const int len = static_cast<int>(std::min<int64_t>(C, p.total - i));
            if (!(cache && readFile(chunkPath(k), outL, outR))) {
                // El trozo [i, i + len) de la señal rellenada, leído de una vez de la original.
                int64_t lo = length, hi = -1;
                for (int q = 0; q < len; ++q) {
                    const int64_t j = reflect(i + q - off, length);
                    lo = std::min(lo, j);
                    hi = std::max(hi, j);
                }
                srcL.resize(static_cast<size_t>(hi - lo + 1));
                srcR.resize(srcL.size());
                read(lo, static_cast<int>(hi - lo + 1), srcL.data(), srcR.data());
                for (int q = 0; q < len; ++q) {
                    const size_t j = static_cast<size_t>(reflect(i + q - off, length) - lo);
                    partL[static_cast<size_t>(q)] = srcL[j];
                    partR[static_cast<size_t>(q)] = srcR[j];
                }
                if (len < C) {
                    if (len > C / 2 + 1) {  // reflejo del mismo trozo
                        for (int q = len; q < C; ++q) {
                            partL[static_cast<size_t>(q)] = partL[static_cast<size_t>(2 * len - 2 - q)];
                            partR[static_cast<size_t>(q)] = partR[static_cast<size_t>(2 * len - 2 - q)];
                        }
                    } else {
                        std::fill(partL.begin() + len, partL.end(), 0.0f);
                        std::fill(partR.begin() + len, partR.end(), 0.0f);
                    }
                }
                if (!model.run(partL.data(), partR.data(), outL.data(), outR.data(), pool, opt.cancel))
                    return "Cancelado";
                if (cache) writeFile(chunkPath(k), outL, outR);  // si no se puede guardar, igual sigue
            } else {
                ++reused;
            }
            // Sumar con la ventana (el primero sin subida; el que llega al final, sin bajada).
            for (int q = 0; q < len; ++q) {
                float w = window[static_cast<size_t>(q)];
                if (k == 0 && q < p.fade) w = 1.0f;
                else if (k != 0 && i + C >= p.total && q >= C - p.fade) w = 1.0f;
                accL[static_cast<size_t>(q)] += outL[static_cast<size_t>(q)] * w;
                accR[static_cast<size_t>(q)] += outR[static_cast<size_t>(q)] * w;
                cnt[static_cast<size_t>(q)] += w;
            }
            // Lo que ya no cambia: [i, i + step), o todo lo que queda si es el último.
            const bool last = k == p.chunks - 1;
            const int64_t end = last ? p.total : std::min<int64_t>(i + p.step, p.total);
            if (!emit(i, i, end)) return "No se pudo guardar el resultado (¿falta espacio?)";
            if (!last) {
                std::move(accL.begin() + p.step, accL.end(), accL.begin());
                std::move(accR.begin() + p.step, accR.end(), accR.begin());
                std::move(cnt.begin() + p.step, cnt.end(), cnt.begin());
                std::fill(accL.end() - p.step, accL.end(), 0.0f);
                std::fill(accR.end() - p.step, accR.end(), 0.0f);
                std::fill(cnt.end() - p.step, cnt.end(), 0.0f);
            }
            if (opt.progress) opt.progress(k + 1, p.chunks, reused);
        }
    } catch (const std::bad_alloc &) {
        return "No hay memoria suficiente";
    }
    return "";
}
