// El modelo de voz real (voz-kim.wvm) contra el original en PyTorch (kim-ref.wvm, de
// tools/modelos/referencias.py): una pasada con lo de adentro y el troceo de 12 s. También mide
// cuánto tarda. Uso: tests/native/modelo-real.sh [carpeta con los dos archivos]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sep/Separator.h"

namespace {
int gFallas = 0;

double snrDb(const std::vector<float> &ref, const std::vector<float> &x, size_t n) {
    double s = 0, e = 0;
    for (size_t i = 0; i < n; ++i) {
        s += static_cast<double>(ref[i]) * ref[i];
        const double d = static_cast<double>(ref[i]) - x[i];
        e += d * d;
    }
    return 10 * std::log10(s / std::max(e, 1e-30));
}

void exigir(const char *que, double snr, double minimo) {
    const bool ok = snr >= minimo;
    if (!ok) ++gFallas;
    std::printf("  %-22s %6.1f dB %s\n", que, snr, ok ? "" : "  ← FALLA");
}

double segundos(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

int main(int argc, char **argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::string e;
    auto t0 = std::chrono::steady_clock::now();
    auto mf = ModelFile::open(dir + "/voz-kim.wvm", e);
    auto ref = ModelFile::open(dir + "/kim-ref.wvm", e);
    if (!mf || !ref) {
        std::printf("No se abrieron los archivos: %s\n", e.c_str());
        return 1;
    }
    auto m = MelRoformer::load(*mf, e);
    if (!m) {
        std::printf("No se cargó el modelo: %s\n", e.c_str());
        return 1;
    }
    std::printf("Modelo cargado en %.2f s (%d bandas, %d capas, trozo de %d)\n", segundos(t0), m->config().bands,
                m->config().depth, m->chunk());
    ThreadPool pool;

    // Una pasada: lo de adentro solo se compara en los primeros 8 cuadros (así viene en la referencia).
    std::vector<float> in, out, x;
    ref->floats("entrada", in);
    ref->floats("salida", out);
    const int C = m->chunk();
    std::vector<float> l(static_cast<size_t>(C)), r(static_cast<size_t>(C));
    MelRoformer::Inside ins;
    t0 = std::chrono::steady_clock::now();
    m->run(in.data(), in.data() + C, l.data(), r.data(), pool, nullptr, &ins);
    std::printf("Una pasada (con lo de adentro) en %.2f s con %d hilos\n", segundos(t0), pool.size());
    for (auto &t : ins.times) std::printf("    %-11s %.2f s\n", t.first, t.second);
    const size_t porCuadro = static_cast<size_t>(m->config().bands) * m->config().dim;
    ref->floats("bandas", x);
    exigir("bandas", snrDb(x, ins.bands, x.size()), 60);
    for (int i = 0; i < m->config().depth; ++i) {
        ref->floats("capa" + std::to_string(i) + ".tiempo", x);
        exigir(("capa " + std::to_string(i) + " tiempo").c_str(), snrDb(x, ins.time[static_cast<size_t>(i)], 8 * porCuadro), 60);
        ref->floats("capa" + std::to_string(i) + ".frecuencia", x);
        exigir(("capa " + std::to_string(i) + " frecuencia").c_str(), snrDb(x, ins.freq[static_cast<size_t>(i)], 8 * porCuadro), 60);
    }
    ref->floats("mascaras", x);
    exigir("máscaras", snrDb(x, ins.masks, x.size()), 70);
    std::vector<float> mia(l);
    mia.insert(mia.end(), r.begin(), r.end());
    exigir("voz (una pasada)", snrDb(out, mia, out.size()), 90);

    // La canción de 12 s troceada como demix_track.
    std::vector<float> ent, sal, gl, gr;
    ref->floats("demix.entrada", ent);
    ref->floats("demix.salida", sal);
    const int64_t n = static_cast<int64_t>(ent.size() / 2);
    Separator::Options opt;
    t0 = std::chrono::steady_clock::now();
    const std::string err = Separator::run(*m, pool, n,
        [&](int64_t from, int k, float *a, float *b) {
            std::copy(ent.begin() + from, ent.begin() + from + k, a);
            std::copy(ent.begin() + n + from, ent.begin() + n + from + k, b);
        },
        [&](const float *a, const float *b, int k) {
            gl.insert(gl.end(), a, a + k);
            gr.insert(gr.end(), b, b + k);
            return true;
        }, opt);
    const double s = segundos(t0);
    const int trozos = Separator::chunkCount(n, C, m->config().overlap);
    std::printf("12 s de canción: %d trozos en %.1f s (%.1f s por trozo)\n", trozos, s, s / trozos);
    if (!err.empty() || gl.size() != static_cast<size_t>(n)) {
        std::printf("  FALLA: %s\n", err.c_str());
        ++gFallas;
    } else {
        gl.insert(gl.end(), gr.begin(), gr.end());
        exigir("voz (troceada)", snrDb(sal, gl, sal.size()), 90);
    }
    std::printf("%s\n", gFallas ? "FALLAS en el modelo real" : "Modelo real igual al original");
    return gFallas ? 1 : 0;
}
