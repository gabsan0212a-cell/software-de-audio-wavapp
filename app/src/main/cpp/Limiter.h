#pragma once

#include <cmath>

// Limitador del master como el del prototipo: DynamicsCompressorNode con umbral -3 dB,
// rodilla 4 dB, ratio 12, ataque 2 ms y liberación 250 ms. La curva estática y la ganancia
// de compensación son las mismas que calcula Chrome; el detector de nivel es más simple.
class Limiter {
public:
    void setup(int sampleRate, float thresholdDb = -3.0f, float kneeDb = 4.0f, float ratio = 12.0f,
               float attackSec = 0.002f, float releaseSec = 0.25f);
    void reset() { mGainDb = 0.0f; }

    // Procesa un cuadro estéreo en el lugar.
    inline void process(float &l, float &r) {
        const float x = std::fmax(std::fabs(l), std::fabs(r));
        float target = 0.0f;
        if (x > mLinearThreshold) target = 20.0f * std::log10(saturate(x) / x);
        const float coef = target < mGainDb ? mAttackCoef : mReleaseCoef;
        mGainDb = target + (mGainDb - target) * coef;
        if (mGainDb > -1e-5f) mGainDb = 0.0f;
        const float g = mMakeup * (mGainDb == 0.0f ? 1.0f : std::exp(mGainDb * 0.11512925f));
        l *= g;
        r *= g;
    }

    float reductionDb() const { return -mGainDb; }  // ≥ 0
    float makeupDb() const { return 20.0f * std::log10(mMakeup); }
    float saturate(float x) const;  // curva estática (entrada y salida lineales)

private:
    float kneeCurve(float x, float k) const;
    float saturate(float x, float k) const;
    float slopeAt(float x, float k) const;
    float kAtSlope(float desiredSlope) const;

    float mThresholdDb = -3.0f, mKneeDb = 4.0f, mRatio = 12.0f;
    float mLinearThreshold = 0.0f, mKneeThreshold = 0.0f, mKneeThresholdDb = 0.0f;
    float mYKneeThresholdDb = 0.0f, mSlope = 0.0f, mK = 1.0f;
    float mMakeup = 1.0f;
    float mAttackCoef = 0.0f, mReleaseCoef = 0.0f;
    float mGainDb = 0.0f;  // reducción suavizada (≤ 0)
};

// Recorte suave después del limitador: igual a la curva del WaveShaper del prototipo.
// Lineal hasta 0.85 y nunca pasa de 0.98 (-0.18 dB).
inline float softClip(float x) {
    constexpr float a = 0.85f, top = 0.98f, k = 0.69f;
    const float m = std::fabs(x);
    if (m <= a) return x;
    const float mc = m < 1.0f ? m : 1.0f;  // el WaveShaper satura fuera de [-1, 1]
    const float y = a + (top - a) * std::tanh(k * (mc - a) / (1.0f - a)) / std::tanh(k);
    return x < 0.0f ? -y : y;
}
