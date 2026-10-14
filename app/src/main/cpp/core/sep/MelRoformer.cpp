#include "sep/MelRoformer.h"

#include <Eigen/Core>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace {

using RowMat = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
using MatMap = Eigen::Map<RowMat, Eigen::Unaligned, Eigen::OuterStride<>>;
using ConstMatMap = Eigen::Map<const RowMat, Eigen::Unaligned, Eigen::OuterStride<>>;

constexpr int kFreqGroup = 16;  // cuadros por tarea en el transformer de frecuencia
constexpr int kMaskGroup = 16;  // cuadros por tarea al promediar y aplicar las máscaras

// y = x · Wᵀ (+ b), como nn.Linear. x: [filas][in] con paso xs; W: [out][in]; y: [filas][out]
// con paso ys. Con `add` se suma a lo que ya tiene y.
void linear(const float *x, int rows, int in, int xs, const float *w, const float *b, int out, float *y, int ys,
            bool add) {
    ConstMatMap X(x, rows, in, Eigen::OuterStride<>(xs));
    ConstMatMap W(w, out, in, Eigen::OuterStride<>(in));
    MatMap Y(y, rows, out, Eigen::OuterStride<>(ys));
    if (add) Y.noalias() += X * W.transpose();
    else Y.noalias() = X * W.transpose();
    if (b) Y.rowwise() += Eigen::Map<const Eigen::RowVectorXf>(b, out);
}

// RMSNorm del original: x / max(‖x‖, 1e-12) · √n · gamma, fila por fila (puede ser en el lugar).
void rmsNorm(const float *x, float *y, int rows, int n, int xs, int ys, const float *gamma) {
    const float scale = std::sqrt(static_cast<float>(n));
    Eigen::Map<const Eigen::ArrayXf> g(gamma, n);
    for (int r = 0; r < rows; ++r) {
        Eigen::Map<const Eigen::ArrayXf> a(x + static_cast<size_t>(r) * xs, n);
        Eigen::Map<Eigen::ArrayXf> o(y + static_cast<size_t>(r) * ys, n);
        const float k = scale / std::max(std::sqrt(a.square().sum()), 1e-12f);
        o = a * k * g;
    }
}

inline float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

// erf y tanh rápidos (aproximaciones racionales de Eigen, error del orden de 1e-7): el
// compilador las hace vectoriales. Con std::erf / std::tanh una pasada tardaba varios segundos más.
inline float fastErf(float x) {
    x = std::min(4.0f, std::max(-4.0f, x));
    const float x2 = x * x;
    float p = -2.72614225801306e-10f;
    p = p * x2 + 2.77068142495902e-08f;
    p = p * x2 + -2.10102402082508e-06f;
    p = p * x2 + -5.69250639462346e-05f;
    p = p * x2 + -7.34990630326855e-04f;
    p = p * x2 + -2.95459980854025e-03f;
    p = p * x2 + -1.60960333262415e-02f;
    p = p * x;
    float q = -1.45660718464996e-05f;
    q = q * x2 + -2.13374055278905e-04f;
    q = q * x2 + -1.68282697438203e-03f;
    q = q * x2 + -7.37332916720468e-03f;
    q = q * x2 + -1.42647390514189e-02f;
    return p / q;
}

inline float fastTanh(float x) {
    // Fuera de ±7.9 tanh ya es ±1 en float; cerca de 0 la aproximación pierde precisión relativa.
    const float y = std::min(7.90531110763549805f, std::max(-7.90531110763549805f, x));
    const float y2 = y * y;
    float p = -2.76076847742355e-16f;
    p = p * y2 + 2.00018790482477e-13f;
    p = p * y2 + -8.60467152213735e-11f;
    p = p * y2 + 5.12229709037114e-08f;
    p = p * y2 + 1.48572235717979e-05f;
    p = p * y2 + 6.37261928875436e-04f;
    p = p * y2 + 4.89352455891786e-03f;
    p = p * y;
    float q = 1.19825839466702e-06f;
    q = q * y2 + 1.18534705686654e-04f;
    q = q * y2 + 2.26843463243900e-03f;
    q = q * y2 + 4.89352518554385e-03f;
    const float r = p / q;
    return std::fabs(x) < 0.0004f ? x : r;  // sin rama: se puede vectorizar
}

void gelu(float *v, size_t n) {
    for (size_t i = 0; i < n; ++i) v[i] = 0.5f * v[i] * (1.0f + fastErf(v[i] * 0.70710678118654752f));
}

void tanhAll(float *v, size_t n) {
    for (size_t i = 0; i < n; ++i) v[i] = fastTanh(v[i]);
}

// Softmax de cada fila (en el lugar), con exp vectorial de Eigen.
void softmaxRows(float *s, int rows, int cols) {
    for (int r = 0; r < rows; ++r) {
        Eigen::Map<Eigen::ArrayXf> a(s + static_cast<size_t>(r) * cols, cols);
        a = (a - a.maxCoeff()).exp();
        a *= 1.0f / a.sum();
    }
}

}  // namespace

bool MelRoformer::loadLinear(const ModelFile &f, const std::string &name, int in, int out, bool bias, Linear &l,
                             std::string &error) {
    l.in = in;
    l.out = out;
    if (!f.floats(name + ".weight", l.w, static_cast<int64_t>(in) * out) ||
        (bias && !f.floats(name + ".bias", l.b, out))) {
        error = "Al modelo le falta " + name;
        return false;
    }
    if (!bias) l.b.clear();
    return true;
}

std::unique_ptr<MelRoformer> MelRoformer::load(const ModelFile &f, std::string &error) {
    const Json &h = f.header();
    const Json &c = h["config"];
    if (h["modelo"].asString() != "mel_band_roformer" || !c["stereo"].asBool() || c["num_stems"].asInt() != 1) {
        error = "Este modelo no es compatible";
        return nullptr;
    }
    std::unique_ptr<MelRoformer> m(new MelRoformer());
    Config &cfg = m->mCfg;
    cfg.dim = static_cast<int>(c["dim"].asInt());
    cfg.depth = static_cast<int>(c["depth"].asInt());
    cfg.bands = static_cast<int>(c["num_bands"].asInt());
    cfg.heads = static_cast<int>(c["heads"].asInt());
    cfg.dimHead = static_cast<int>(c["dim_head"].asInt());
    cfg.timeDepth = static_cast<int>(c["time_transformer_depth"].asInt(1));
    cfg.freqDepth = static_cast<int>(c["freq_transformer_depth"].asInt(1));
    cfg.maskDepth = static_cast<int>(c["mask_estimator_depth"].asInt(1));
    cfg.mlpFactor = static_cast<int>(c["mlp_expansion_factor"].asInt(4));
    cfg.sampleRate = static_cast<int>(c["sample_rate"].asInt(44100));
    cfg.nfft = static_cast<int>(c["stft_n_fft"].asInt(2048));
    cfg.hop = static_cast<int>(c["stft_hop_length"].asInt(441));
    cfg.chunk = static_cast<int>(c["chunk_size"].asInt());
    cfg.overlap = static_cast<int>(c["num_overlap"].asInt(2));
    const bool sane = cfg.dim > 0 && cfg.depth > 0 && cfg.bands > 0 && cfg.heads > 0 && cfg.dimHead > 0 &&
                      cfg.dimHead % 2 == 0 && cfg.timeDepth > 0 && cfg.freqDepth > 0 && cfg.maskDepth > 0 &&
                      cfg.nfft >= 16 && (cfg.nfft & (cfg.nfft - 1)) == 0 && cfg.hop > 0 &&
                      c["stft_win_length"].asInt(cfg.nfft) == cfg.nfft && cfg.chunk > cfg.nfft && cfg.overlap >= 1;
    if (!sane) {
        error = "La configuración del modelo no es válida";
        return nullptr;
    }
    const int D = cfg.dim, H = cfg.heads, Dh = cfg.dimHead, Di = H * Dh;
    m->mInner = Di;
    m->mBins = cfg.nfft / 2 + 1;
    m->mFrames = 1 + cfg.chunk / cfg.hop;
    m->mStft = std::make_unique<StereoStft>(cfg.nfft, cfg.hop);

    // Bandas: qué frecuencias entran en cada una (calculado por el original con librosa).
    const ModelFile::Tensor *fpb = f.find("freqs_per_band");
    if (!fpb || fpb->type != ModelFile::kU8 || fpb->count != static_cast<int64_t>(cfg.bands) * m->mBins) {
        error = "Al modelo le faltan las bandas";
        return nullptr;
    }
    m->mBandsPerBin.assign(static_cast<size_t>(m->mBins) * 2, 0.0f);
    m->mBands.resize(static_cast<size_t>(cfg.bands));
    size_t offset = 0;
    for (int b = 0; b < cfg.bands; ++b) {
        Band &band = m->mBands[static_cast<size_t>(b)];
        for (int fr = 0; fr < m->mBins; ++fr) {
            if (!fpb->data[static_cast<size_t>(b) * m->mBins + fr]) continue;
            band.bins.push_back(fr * 2);
            band.bins.push_back(fr * 2 + 1);
            m->mBandsPerBin[static_cast<size_t>(fr) * 2] += 1.0f;
            m->mBandsPerBin[static_cast<size_t>(fr) * 2 + 1] += 1.0f;
        }
        band.dimIn = static_cast<int>(band.bins.size()) * 2;
        if (band.dimIn == 0) {
            error = "Una banda del modelo está vacía";
            return nullptr;
        }
        const std::string p = "band_split.to_features." + std::to_string(b);
        if (!f.floats(p + ".0.gamma", band.gamma, band.dimIn) ||
            !m->loadLinear(f, p + ".1", band.dimIn, D, true, band.proj, error)) {
            if (error.empty()) error = "Al modelo le falta " + p;
            return nullptr;
        }
        // Máscara de la banda: MLP (D → oculta → ... → 2·entrada) y GLU.
        const int hidden = D * cfg.mlpFactor;
        const std::string q = "mask_estimators.0.to_freqs." + std::to_string(b) + ".0.";
        for (int l = 0; l <= cfg.maskDepth; ++l) {
            const int in = l == 0 ? D : hidden;
            const int out = l == cfg.maskDepth ? 2 * band.dimIn : hidden;
            const ModelFile::Tensor *w = f.find(q + std::to_string(2 * l) + ".weight");
            std::vector<float> bias;
            if (!w || w->count != static_cast<int64_t>(in) * out ||
                !f.floats(q + std::to_string(2 * l) + ".bias", bias, out)) {
                error = "Al modelo le falta " + q + std::to_string(2 * l);
                return nullptr;
            }
            band.maskW.push_back(w);
            band.maskB.push_back(std::move(bias));
        }
        band.maskOffset = offset;
        offset += static_cast<size_t>(band.dimIn);
    }
    m->mMaskTotal = static_cast<int>(offset);

    // Transformers de tiempo y de frecuencia de cada capa.
    auto loadTransformer = [&](const std::string &p, int depth, int positions, Transformer &tr) -> bool {
        for (int j = 0; j < depth; ++j) {
            Layer ly;
            const std::string a = p + "layers." + std::to_string(j) + ".0.";
            const std::string ff = p + "layers." + std::to_string(j) + ".1.net.";
            const ModelFile::Tensor *w1 = f.find(ff + "1.weight");
            if (!w1 || w1->shape.size() != 2 || w1->shape[1] != D) {
                error = "Al modelo le falta " + ff + "1";
                return false;
            }
            const int inner = static_cast<int>(w1->shape[0]);
            if (!f.floats(a + "norm.gamma", ly.attnGamma, D) || !f.floats(ff + "0.gamma", ly.ffGamma, D) ||
                !m->loadLinear(f, a + "to_qkv", D, 3 * Di, false, ly.qkv, error) ||
                !m->loadLinear(f, a + "to_gates", D, H, true, ly.gates, error) ||
                !m->loadLinear(f, a + "to_out.0", Di, D, false, ly.proj, error) ||
                !m->loadLinear(f, ff + "1", D, inner, true, ly.ff1, error) ||
                !m->loadLinear(f, ff + "4", inner, D, true, ly.ff2, error)) {
                if (error.empty()) error = "Al modelo le falta " + a;
                return false;
            }
            if (j == 0) {
                std::vector<float> freqs;
                if (!f.floats(a + "rotary_embed.freqs", freqs, Dh / 2)) {
                    error = "Al modelo le falta " + a + "rotary_embed";
                    return false;
                }
                // RoPE: ángulo = posición · frecuencia (en float, como el original).
                tr.cosT.resize(static_cast<size_t>(positions) * (Dh / 2));
                tr.sinT.resize(tr.cosT.size());
                for (int pos = 0; pos < positions; ++pos)
                    for (int i = 0; i < Dh / 2; ++i) {
                        const float ang = static_cast<float>(pos) * freqs[static_cast<size_t>(i)];
                        tr.cosT[static_cast<size_t>(pos) * (Dh / 2) + i] = static_cast<float>(std::cos(static_cast<double>(ang)));
                        tr.sinT[static_cast<size_t>(pos) * (Dh / 2) + i] = static_cast<float>(std::sin(static_cast<double>(ang)));
                    }
            }
            tr.layers.push_back(std::move(ly));
        }
        if (!f.floats(p + "norm.gamma", tr.outGamma, D)) {
            error = "Al modelo le falta " + p + "norm";
            return false;
        }
        return true;
    };
    m->mTime.resize(static_cast<size_t>(cfg.depth));
    m->mFreq.resize(static_cast<size_t>(cfg.depth));
    for (int i = 0; i < cfg.depth; ++i) {
        const std::string p = "layers." + std::to_string(i) + ".";
        if (!loadTransformer(p + "0.", cfg.timeDepth, m->mFrames, m->mTime[static_cast<size_t>(i)]) ||
            !loadTransformer(p + "1.", cfg.freqDepth, cfg.bands, m->mFreq[static_cast<size_t>(i)]))
            return nullptr;
    }
    error.clear();
    return m;
}

// Un transformer completo (todas sus capas y la norma final) sobre `sequences` secuencias de
// `length` filas de mX. Fila k de la secuencia q: firstRow + q·seqStride + k·rowStride.
void MelRoformer::transformer(const Transformer &tr, int sequences, int length, int firstRow, int seqStride,
                              int rowStride, Scratch &s) {
    const int D = mCfg.dim, H = mCfg.heads, Dh = mCfg.dimHead, Di = mInner, half = Dh / 2;
    const int rows = sequences * length;
    const float scale = 1.0f / std::sqrt(static_cast<float>(Dh));
    s.x.resize(static_cast<size_t>(rows) * D);
    s.xn.resize(s.x.size());
    s.qkv.resize(static_cast<size_t>(rows) * 3 * Di);
    s.gates.resize(static_cast<size_t>(rows) * H);
    s.att.resize(static_cast<size_t>(rows) * Di);
    s.scores.resize(static_cast<size_t>(length) * length);
    auto rowOf = [&](int q, int k) {
        return static_cast<size_t>(firstRow + q * seqStride + k * rowStride) * D;
    };
    for (int q = 0; q < sequences; ++q)
        for (int k = 0; k < length; ++k)
            std::memcpy(&s.x[static_cast<size_t>(q * length + k) * D], &mX[rowOf(q, k)], sizeof(float) * D);

    for (const Layer &ly : tr.layers) {
        // Atención
        rmsNorm(s.x.data(), s.xn.data(), rows, D, D, D, ly.attnGamma.data());
        linear(s.xn.data(), rows, D, D, ly.qkv.w.data(), nullptr, 3 * Di, s.qkv.data(), 3 * Di, false);
        linear(s.xn.data(), rows, D, D, ly.gates.w.data(), ly.gates.b.data(), H, s.gates.data(), H, false);
        for (float &g : s.gates) g = sigmoid(g);
        for (int r = 0; r < rows; ++r) {  // RoPE en q y k (posición dentro de la secuencia)
            const int pos = r % length;
            const float *cs = &tr.cosT[static_cast<size_t>(pos) * half];
            const float *sn = &tr.sinT[static_cast<size_t>(pos) * half];
            for (int part = 0; part < 2; ++part)
                for (int h = 0; h < H; ++h) {
                    float *v = &s.qkv[static_cast<size_t>(r) * 3 * Di + part * Di + h * Dh];
                    for (int i = 0; i < half; ++i) {
                        const float x0 = v[2 * i], x1 = v[2 * i + 1];
                        v[2 * i] = x0 * cs[i] - x1 * sn[i];
                        v[2 * i + 1] = x1 * cs[i] + x0 * sn[i];
                    }
                }
        }
        for (int q = 0; q < sequences; ++q) {
            const float *base = &s.qkv[static_cast<size_t>(q) * length * 3 * Di];
            for (int h = 0; h < H; ++h) {
                ConstMatMap Q(base + h * Dh, length, Dh, Eigen::OuterStride<>(3 * Di));
                ConstMatMap K(base + Di + h * Dh, length, Dh, Eigen::OuterStride<>(3 * Di));
                ConstMatMap V(base + 2 * Di + h * Dh, length, Dh, Eigen::OuterStride<>(3 * Di));
                MatMap S(s.scores.data(), length, length, Eigen::OuterStride<>(length));
                S.noalias() = Q * K.transpose();
                S *= scale;
                softmaxRows(s.scores.data(), length, length);
                MatMap O(&s.att[static_cast<size_t>(q) * length * Di + h * Dh], length, Dh, Eigen::OuterStride<>(Di));
                O.noalias() = S * V;
            }
        }
        for (int r = 0; r < rows; ++r)
            for (int h = 0; h < H; ++h) {
                const float g = s.gates[static_cast<size_t>(r) * H + h];
                float *o = &s.att[static_cast<size_t>(r) * Di + h * Dh];
                for (int i = 0; i < Dh; ++i) o[i] *= g;
            }
        linear(s.att.data(), rows, Di, Di, ly.proj.w.data(), nullptr, D, s.x.data(), D, true);

        // Feed-forward (GELU exacto)
        const int inner = ly.ff1.out;
        s.hidden.resize(static_cast<size_t>(rows) * inner);
        rmsNorm(s.x.data(), s.xn.data(), rows, D, D, D, ly.ffGamma.data());
        linear(s.xn.data(), rows, D, D, ly.ff1.w.data(), ly.ff1.b.data(), inner, s.hidden.data(), inner, false);
        gelu(s.hidden.data(), s.hidden.size());
        linear(s.hidden.data(), rows, inner, inner, ly.ff2.w.data(), ly.ff2.b.data(), D, s.x.data(), D, true);
    }
    rmsNorm(s.x.data(), s.x.data(), rows, D, D, D, tr.outGamma.data());
    for (int q = 0; q < sequences; ++q)
        for (int k = 0; k < length; ++k)
            std::memcpy(&mX[rowOf(q, k)], &s.x[static_cast<size_t>(q * length + k) * D], sizeof(float) * D);
}

// Máscara de la banda b para todos los cuadros: MLP con tanh y GLU al final.
void MelRoformer::maskBand(int b, Scratch &s) {
    const Band &band = mBands[static_cast<size_t>(b)];
    const int D = mCfg.dim, T = mFrames, F = mCfg.bands;
    const int layers = static_cast<int>(band.maskW.size());
    const float *in = &mX[static_cast<size_t>(b) * D];
    int inDim = D, inStride = F * D;
    std::vector<float> *bufs[2] = {&s.h1, &s.h2};
    for (int l = 0; l < layers; ++l) {
        const ModelFile::Tensor &wt = *band.maskW[static_cast<size_t>(l)];
        const int out = static_cast<int>(band.maskB[static_cast<size_t>(l)].size());
        std::vector<float> &w = s.w[l % 3];
        w.resize(static_cast<size_t>(wt.count));
        ModelFile::toFloat(wt, w.data());
        std::vector<float> &y = l == layers - 1 ? s.y : *bufs[l % 2];
        y.resize(static_cast<size_t>(T) * out);
        linear(in, T, inDim, inStride, w.data(), band.maskB[static_cast<size_t>(l)].data(), out, y.data(), out, false);
        if (l < layers - 1) tanhAll(y.data(), y.size());
        in = y.data();
        inDim = out;
        inStride = out;
    }
    // GLU: primera mitad · sigmoide(segunda mitad).
    const int n = band.dimIn;
    float *dst = &mMasks[band.maskOffset * T];
    for (int t = 0; t < T; ++t) {
        const float *y = &s.y[static_cast<size_t>(t) * 2 * n];
        for (int j = 0; j < n; ++j) dst[static_cast<size_t>(t) * n + j] = y[j] * sigmoid(y[n + j]);
    }
}

bool MelRoformer::run(const float *inL, const float *inR, float *outL, float *outR, ThreadPool &pool,
                      const std::atomic<bool> *cancel, Inside *inside) {
    const int D = mCfg.dim, F = mCfg.bands, T = mFrames, nb = mBins;
    auto stopped = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    auto clock = std::chrono::steady_clock::now();
    auto lap = [&](const char *what) {
        if (!inside) return;
        const auto now = std::chrono::steady_clock::now();
        inside->times.emplace_back(what, std::chrono::duration<double>(now - clock).count());
        clock = now;
    };
    if (inside) inside->times.clear();
    mSpec.resize(static_cast<size_t>(T) * nb * 4);
    mX.resize(static_cast<size_t>(T) * F * D);
    mMasks.resize(static_cast<size_t>(T) * mMaskTotal);
    if (mScratch.size() != static_cast<size_t>(pool.size())) mScratch.resize(static_cast<size_t>(pool.size()));

    mStft->forward(inL, inR, mCfg.chunk, mSpec.data());
    lap("stft");

    // Entrada de bandas: RMSNorm + lineal de cada banda → mX[cuadro][banda].
    pool.parallelFor(F, [&](int b, int w) {
        const Band &band = mBands[static_cast<size_t>(b)];
        Scratch &s = mScratch[static_cast<size_t>(w)];
        s.xn.resize(static_cast<size_t>(T) * band.dimIn);
        for (int t = 0; t < T; ++t) {
            const float *spec = &mSpec[static_cast<size_t>(t) * nb * 4];
            float *row = &s.xn[static_cast<size_t>(t) * band.dimIn];
            for (size_t j = 0; j < band.bins.size(); ++j) {
                row[2 * j] = spec[2 * band.bins[j]];
                row[2 * j + 1] = spec[2 * band.bins[j] + 1];
            }
        }
        rmsNorm(s.xn.data(), s.xn.data(), T, band.dimIn, band.dimIn, band.dimIn, band.gamma.data());
        linear(s.xn.data(), T, band.dimIn, band.dimIn, band.proj.w.data(), band.proj.b.data(), D,
               &mX[static_cast<size_t>(b) * D], F * D, false);
    }, cancel);
    if (stopped()) return false;
    lap("bandas");
    if (inside) {
        inside->bands = mX;
        inside->time.clear();
        inside->freq.clear();
    }

    for (int i = 0; i < mCfg.depth; ++i) {
        // Tiempo: una secuencia por banda (los cuadros de esa banda).
        pool.parallelFor(F, [&](int b, int w) {
            transformer(mTime[static_cast<size_t>(i)], 1, T, b, 0, F, mScratch[static_cast<size_t>(w)]);
        }, cancel);
        if (stopped()) return false;
        lap("tiempo");
        if (inside) inside->time.push_back(mX);
        // Frecuencia: una secuencia por cuadro (sus bandas), de a kFreqGroup cuadros por tarea.
        const int groups = (T + kFreqGroup - 1) / kFreqGroup;
        pool.parallelFor(groups, [&](int g, int w) {
            const int t0 = g * kFreqGroup, n = std::min(kFreqGroup, T - t0);
            transformer(mFreq[static_cast<size_t>(i)], n, F, t0 * F, F, 1, mScratch[static_cast<size_t>(w)]);
        }, cancel);
        if (stopped()) return false;
        lap("frecuencia");
        if (inside) inside->freq.push_back(mX);
    }

    pool.parallelFor(F, [&](int b, int w) { maskBand(b, mScratch[static_cast<size_t>(w)]); }, cancel);
    if (stopped()) return false;
    lap("mascaras");
    if (inside) {  // como el original: [cuadro][bandas una tras otra]
        inside->masks.assign(static_cast<size_t>(T) * mMaskTotal, 0.0f);
        for (const Band &band : mBands)
            for (int t = 0; t < T; ++t)
                std::memcpy(&inside->masks[static_cast<size_t>(t) * mMaskTotal + band.maskOffset],
                            &mMasks[band.maskOffset * T + static_cast<size_t>(t) * band.dimIn],
                            sizeof(float) * static_cast<size_t>(band.dimIn));
    }

    // Promedio de las máscaras donde se enciman las bandas, y espectro × máscara (complejos).
    const int groups = (T + kMaskGroup - 1) / kMaskGroup;
    pool.parallelFor(groups, [&](int g, int w) {
        std::vector<float> &sum = mScratch[static_cast<size_t>(w)].xn;
        sum.resize(static_cast<size_t>(nb) * 4);
        for (int t = g * kMaskGroup; t < std::min(T, (g + 1) * kMaskGroup); ++t) {
            std::fill(sum.begin(), sum.end(), 0.0f);
            for (const Band &band : mBands) {
                const float *mk = &mMasks[band.maskOffset * T + static_cast<size_t>(t) * band.dimIn];
                for (size_t j = 0; j < band.bins.size(); ++j) {
                    sum[2 * band.bins[j]] += mk[2 * j];
                    sum[2 * band.bins[j] + 1] += mk[2 * j + 1];
                }
            }
            float *spec = &mSpec[static_cast<size_t>(t) * nb * 4];
            for (int q = 0; q < nb * 2; ++q) {
                const float k = 1.0f / std::max(mBandsPerBin[static_cast<size_t>(q)], 1e-8f);
                const float mr = sum[2 * q] * k, mi = sum[2 * q + 1] * k;
                const float sr = spec[2 * q], si = spec[2 * q + 1];
                spec[2 * q] = sr * mr - si * mi;
                spec[2 * q + 1] = sr * mi + si * mr;
            }
        }
    }, cancel);
    if (stopped()) return false;

    lap("aplicar");
    mStft->inverse(mSpec.data(), T, outL, outR);
    lap("istft");
    return true;
}
