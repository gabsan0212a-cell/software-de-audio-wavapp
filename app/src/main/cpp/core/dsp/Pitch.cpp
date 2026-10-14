#include "dsp/Pitch.h"

#include <algorithm>

PitchDetector::PitchDetector(int sampleRate, int window, float minHz, float maxHz)
    : mRate(sampleRate), mW(window) {
    mMinLag = std::max(2, static_cast<int>(std::floor(sampleRate / maxHz)));
    mMaxLag = std::max(mMinLag + 2, static_cast<int>(std::ceil(sampleRate / minHz)));
    mDiff.resize(static_cast<size_t>(mMaxLag) + 2);
}

PitchDetector::Result PitchDetector::detect(const float *x, float threshold) {
    // Diferencia d(τ) = Σ (x[j] - x[j+τ])² y su versión normalizada por la media acumulada.
    float *d = mDiff.data();
    d[0] = 1.0f;
    double cum = 0.0;
    for (int tau = 1; tau <= mMaxLag; ++tau) {
        float s = 0.0f;
        const float *a = x, *b = x + tau;
        for (int j = 0; j < mW; ++j) {
            const float v = a[j] - b[j];
            s += v * v;
        }
        cum += s;
        d[tau] = cum > 0.0 ? static_cast<float>(s * tau / cum) : 1.0f;
    }
    // La primera bajada debajo del umbral (evita confundir la nota con una octava más grave).
    int tau = -1;
    for (int t = mMinLag; t < mMaxLag; ++t) {
        if (d[t] < threshold) {
            while (t + 1 < mMaxLag && d[t + 1] < d[t]) ++t;
            tau = t;
            break;
        }
    }
    Result r;
    if (tau < 0) {
        const int best = static_cast<int>(std::min_element(d + mMinLag, d + mMaxLag) - d);
        r.clarity = std::max(0.0f, 1.0f - d[best]);
        return r;  // sin tono claro
    }
    // Parábola por los tres puntos alrededor del mínimo: período con decimales.
    float shift = 0.0f;
    if (tau > 1 && tau < mMaxLag) {
        const float a = d[tau - 1], b = d[tau], c = d[tau + 1];
        const float den = a - 2.0f * b + c;
        if (den > 1e-9f) shift = std::clamp(0.5f * (a - c) / den, -0.5f, 0.5f);
    }
    r.hz = static_cast<float>(mRate) / (static_cast<float>(tau) + shift);
    r.clarity = std::max(0.0f, 1.0f - d[tau]);
    return r;
}
