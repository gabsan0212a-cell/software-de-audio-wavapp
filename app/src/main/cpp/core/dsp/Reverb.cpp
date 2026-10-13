#include "dsp/Reverb.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kTwoPi = 6.28318530717958647692;
// Largos a 48 kHz: primos entre sí, de 32 a 72 ms (una sala mediana a grande).
constexpr int kLineLen[8] = {1559, 1789, 2063, 2333, 2593, 2879, 3137, 3433};
// Difusores de entrada (como los de Dattorro) y cuánto difunde cada uno.
constexpr int kDiffLen[4] = {229, 173, 613, 449};
constexpr float kDiffGain[4] = {0.75f, 0.75f, 0.625f, 0.625f};
// Signos de la entrada y de las dos salidas (ortogonales entre sí y con el vector de unos).
constexpr float kIn[8] = {1, -1, -1, 1, 1, 1, -1, -1};
constexpr float kOutL[8] = {1, -1, 1, -1, 1, -1, 1, -1};
constexpr float kOutR[8] = {1, 1, -1, -1, 1, 1, -1, -1};
// Modulación: qué líneas, a qué velocidad (Hz) y cuánto (muestras).
constexpr int kModLine[4] = {0, 2, 5, 7};
constexpr double kModHz[4] = {0.61, 0.73, 0.89, 1.03};
constexpr float kModDepth = 7.0f;
constexpr float kMaxPredelayMs = 200.0f;

}  // namespace

void Reverb::allocate(int sampleRate) {
    mRate = sampleRate;
    const double k = sampleRate / 48000.0;
    mPre.allocate(static_cast<int>(kMaxPredelayMs * 1e-3 * sampleRate) + 2);
    for (int i = 0; i < kDiff; ++i) {
        mDiffLen[i] = std::max(2, static_cast<int>(kDiffLen[i] * k));
        mDiff[i].allocate(mDiffLen[i] + 1);
    }
    for (int i = 0; i < kLines; ++i) {
        mLen[i] = std::max(32, static_cast<int>(kLineLen[i] * k));
        mLine[i].allocate(mLen[i] + static_cast<int>(kModDepth) + 4);
    }
    restart();
}

void Reverb::restart() {
    mPre.restart();
    for (auto &d : mDiff) d.restart();
    for (auto &d : mLine) d.restart();
    std::fill(mDamp, mDamp + kLines, 0.0f);
}

void Reverb::process(const float *l, const float *r, int n, float *outL, float *outR, const ReverbSettings &s) {
    const float decay = std::clamp(s.decayS, 0.1f, 20.0f);
    float g[kLines];
    double g2 = 0.0;
    for (int i = 0; i < kLines; ++i) {
        g[i] = static_cast<float>(std::pow(10.0, -3.0 * mLen[i] / (decay * mRate)));
        g2 += static_cast<double>(g[i]) * g[i];
    }
    // La energía de la cola crece con el tiempo: se compensa para que la cantidad mande.
    const float outGain = static_cast<float>(std::sqrt(std::max(0.0, 1.0 - g2 / kLines) / kLines));
    const float damp = static_cast<float>(std::exp(-kTwoPi * std::clamp(s.toneHz, 500.0f, 0.45f * mRate) / mRate));
    const int pre = std::clamp(static_cast<int>(s.predelayMs * 1e-3f * mRate), 1, mPre.maxDelay());
    // Modulación: el valor al principio y al final de este pedazo (es corto), en línea recta.
    float mod0[4], mod1[4];
    const double dt = static_cast<double>(n) / mRate;
    for (int m = 0; m < 4; ++m) {
        mod0[m] = kModDepth * static_cast<float>(std::sin(kTwoPi * (mLfo * kModHz[m] + m * 0.25)));
        mod1[m] = kModDepth * static_cast<float>(std::sin(kTwoPi * ((mLfo + dt) * kModHz[m] + m * 0.25)));
    }
    mLfo += dt;
    if (mLfo > 1000.0) mLfo -= 1000.0;  // todas las velocidades dan vueltas enteras en 1000 s

    const float inv = 1.0f / static_cast<float>(std::max(1, n));
    for (int i = 0; i < n; ++i) {
        const float in = r ? 0.5f * (l[i] + r[i]) : l[i];
        float x = mPre.read(pre);
        mPre.write(in);
        for (int d = 0; d < kDiff; ++d) {  // pasa-todo de Schroeder
            const float z = mDiff[d].read(mDiffLen[d]);
            float w = x + kDiffGain[d] * z;
            if (std::fabs(w) < 1e-20f) w = 0.0f;  // la cola se apaga en ceros, no en números diminutos
            x = z - kDiffGain[d] * w;
            mDiff[d].write(w);
        }
        float o[kLines];
        for (int k = 0; k < kLines; ++k) o[k] = mLine[k].read(mLen[k]);
        const float f = static_cast<float>(i) * inv;
        for (int m = 0; m < 4; ++m) {
            const int k = kModLine[m];
            o[k] = mLine[k].readFrac(static_cast<float>(mLen[k]) + mod0[m] + (mod1[m] - mod0[m]) * f);
        }
        float v[kLines], sum = 0.0f;
        for (int k = 0; k < kLines; ++k) {
            float dk = o[k] + (mDamp[k] - o[k]) * damp;
            if (std::fabs(dk) < 1e-20f) dk = 0.0f;
            mDamp[k] = dk;
            v[k] = dk * g[k];
            sum += v[k];
        }
        const float h = sum * (2.0f / kLines);  // Householder: v − (2/N)·Σv
        float yl = 0.0f, yr = 0.0f;
        for (int k = 0; k < kLines; ++k) {
            mLine[k].write(v[k] - h + x * kIn[k]);
            yl += o[k] * kOutL[k];
            yr += o[k] * kOutR[k];
        }
        outL[i] = yl * outGain;
        outR[i] = yr * outGain;
    }
}
