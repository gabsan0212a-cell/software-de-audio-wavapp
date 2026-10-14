#include "dsp/Doubler.h"

#include <cmath>

namespace {

// Cada copia: retraso base, cuánto se mueve y qué tan rápido (dos distintas: no suenan iguales).
constexpr double kBaseMs[2] = {13.0, 21.0};
constexpr double kDepthMs[2] = {1.5, 1.8};
constexpr double kRateHz[2] = {0.31, 0.43};

}  // namespace

void Doubler::allocate(int sampleRate) {
    mRate = sampleRate;
    mLine.allocate(static_cast<int>(0.030 * sampleRate) + 8);
    mLpCoef = 1.0f - std::exp(-2.0f * 3.14159265f * 7000.0f / static_cast<float>(sampleRate));
    restart();
}

void Doubler::restart() {
    mLine.restart();
    mLp[0] = mLp[1] = 0.0f;
}

void Doubler::process(const float *in, int n, float *outL, float *outR, float mix) {
    float *out[2] = {outL, outR};
    for (int i = 0; i < n; ++i) {
        mLine.write(in[i]);
        for (int c = 0; c < 2; ++c) {
            const double d = (kBaseMs[c] + kDepthMs[c] * std::sin(2.0 * M_PI * mPhase[c])) * 0.001 * mRate;
            const float v = mLine.readFrac(static_cast<float>(d));
            mLp[c] += (v - mLp[c]) * mLpCoef;
            out[c][i] += mix * mLp[c];
        }
        for (int c = 0; c < 2; ++c) {
            mPhase[c] += kRateHz[c] / mRate;
            if (mPhase[c] >= 1.0) mPhase[c] -= 1.0;
        }
    }
    for (float &v : mLp)
        if (std::fabs(v) < 1e-20f) v = 0.0f;
}
