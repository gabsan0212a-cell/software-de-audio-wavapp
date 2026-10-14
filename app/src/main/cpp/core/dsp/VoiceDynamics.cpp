#include "dsp/VoiceDynamics.h"

#include <algorithm>
#include <cmath>

namespace {

inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
inline float coef(float seconds, int rate) { return 1.0f - std::exp(-1.0f / (seconds * static_cast<float>(rate))); }

}  // namespace

void MonoBiquad::highPass(double freq, double q, int sampleRate) {
    const double w0 = 2.0 * M_PI * std::clamp(freq, 10.0, 0.45 * sampleRate) / sampleRate;
    const double cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q), a0 = 1.0 + alpha;
    mB0 = (1.0 + cw) / 2.0 / a0;
    mB1 = -(1.0 + cw) / a0;
    mB2 = mB0;
    mA1 = -2.0 * cw / a0;
    mA2 = (1.0 - alpha) / a0;
}

void MonoBiquad::bandPass(double freq, double q, int sampleRate) {
    const double w0 = 2.0 * M_PI * std::clamp(freq, 10.0, 0.45 * sampleRate) / sampleRate;
    const double cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q), a0 = 1.0 + alpha;
    mB0 = alpha / a0;
    mB1 = 0.0;
    mB2 = -alpha / a0;
    mA1 = -2.0 * cw / a0;
    mA2 = (1.0 - alpha) / a0;
}

// ---------------------------------------------------------------- compuerta

void NoiseGate::setup(int sampleRate) {
    mRate = sampleRate;
    reset();
}

void NoiseGate::reset() {
    mEnv = 0.0f;
    mGain = 1.0f;
    mOpen = false;
    mHold = 0;
}

float NoiseGate::gainDb() const { return 20.0f * std::log10(std::max(mGain, 1e-6f)); }

void NoiseGate::process(float *x, int n, const GateSettings &s) {
    const float thr = dbToLin(s.thresholdDb), thrClose = thr * 0.5f;  // cierra 6 dB más abajo
    const float floorGain = dbToLin(std::min(0.0f, s.rangeDb));
    const float envKeep = std::exp(-1.0f / (0.050f * mRate));  // el detector cae en ~50 ms
    const float att = coef(0.0005f, mRate);
    const float rel = coef(std::max(1.0f, s.releaseMs) * 1e-3f, mRate);
    const int hold = static_cast<int>(std::max(0.0f, s.holdMs) * 1e-3f * mRate);
    for (int i = 0; i < n; ++i) {
        mEnv = std::max(std::fabs(x[i]), mEnv * envKeep);
        if (mEnv > thr) {
            mOpen = true;
            mHold = hold;
        } else if (mOpen && mEnv < thrClose && --mHold <= 0) {
            mOpen = false;
        }
        const float target = mOpen ? 1.0f : floorGain;
        mGain += (target - mGain) * (target > mGain ? att : rel);
        x[i] *= mGain;
    }
    if (mEnv < 1e-20f) mEnv = 0.0f;
    if (std::fabs(mGain - 1.0f) < 1e-7f) mGain = 1.0f;
}

// ---------------------------------------------------------------- de-esser

void DeEsser::setup(int sampleRate) {
    mRate = sampleRate;
    mFreq = -1.0f;
    reset();
}

void DeEsser::reset() {
    mBand.reset();
    mEnv = mCutDb = 0.0f;
}

float DeEsser::process(float *x, int n, const DeEssSettings &s) {
    if (s.freqHz != mFreq) {
        mFreq = s.freqHz;
        mBand.bandPass(mFreq, 0.9, mRate);  // ~1.5 octavas de ancho
    }
    const float envAtt = coef(0.0003f, mRate), envRel = coef(0.040f, mRate);
    const float cutAtt = coef(0.001f, mRate), cutRel = coef(0.060f, mRate);
    float maxCut = 0.0f;
    for (int i = 0; i < n; ++i) {
        // La banda de las "eses": la salida es la voz con esa banda bajada (campana de ganancia g).
        const float h = mBand.run(x[i]);
        const float a = std::fabs(h);
        mEnv += (a - mEnv) * (a > mEnv ? envAtt : envRel);
        float want = 0.0f;
        if (mEnv > 1e-6f) {
            const float over = 20.0f * std::log10(mEnv) - s.thresholdDb;
            if (over > 0.0f) want = std::min(s.maxCutDb, over * 0.75f);  // 4:1
        }
        mCutDb += (want - mCutDb) * (want > mCutDb ? cutAtt : cutRel);
        const float g = std::exp(-mCutDb * 0.11512925f);
        x[i] += (g - 1.0f) * h;
        maxCut = std::max(maxCut, mCutDb);
    }
    if (mEnv < 1e-20f) mEnv = 0.0f;
    if (mCutDb < 1e-6f) mCutDb = 0.0f;
    return maxCut;
}
