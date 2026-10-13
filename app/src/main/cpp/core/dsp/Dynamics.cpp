#include "dsp/Dynamics.h"

#include <algorithm>
#include <cmath>

float Compressor::gainComputer(float inDb, float thresholdDb, float ratio, float kneeDb) {
    const float d = inDb - thresholdDb;
    const float slope = 1.0f / std::max(1.0f, ratio) - 1.0f;  // ≤ 0
    if (2.0f * d <= -kneeDb) return 0.0f;
    if (2.0f * d >= kneeDb) return slope * d;
    const float t = d + kneeDb / 2.0f;
    return slope * t * t / (2.0f * kneeDb);
}

float Compressor::process(float *l, float *r, int n, const CompSettings &s) {
    constexpr int kStep = 8;
    const float att = std::max(0.05f, s.attackMs) * 1e-3f * mRate;
    const float rel = std::max(1.0f, s.releaseMs) * 1e-3f * mRate;
    const float aA = std::exp(-kStep / att), aR = std::exp(-kStep / rel);
    float maxGr = 0.0f;
    for (int i0 = 0; i0 < n; i0 += kStep) {
        const int k = std::min(kStep, n - i0);
        float pk = 0.0f;
        for (int i = i0; i < i0 + k; ++i) {
            pk = std::max(pk, std::fabs(l[i]));
            if (r) pk = std::max(pk, std::fabs(r[i]));
        }
        const float inDb = pk > 1e-6f ? 20.0f * std::log10(pk) : -120.0f;
        const float gc = gainComputer(inDb, s.thresholdDb, s.ratio);
        // Ataque cuando hay que bajar más; liberación cuando hay que soltar.
        const float a = gc < mEnvDb ? aA : aR;
        mEnvDb = gc + (mEnvDb - gc) * a;
        if (mEnvDb > -1e-5f) mEnvDb = 0.0f;
        maxGr = std::max(maxGr, -mEnvDb);
        const float target = std::pow(10.0f, (mEnvDb + s.makeupDb) / 20.0f);
        if (mGain < 0.0f) mGain = target;
        const float step = (target - mGain) / static_cast<float>(k);
        float g = mGain;
        for (int i = i0; i < i0 + k; ++i) {
            g += step;
            l[i] *= g;
            if (r) r[i] *= g;
        }
        mGain = target;
    }
    return maxGr;
}
