#include "Limiter.h"

#include <algorithm>

namespace {
float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
float linToDb(float x) { return 20.0f * std::log10(x); }
}  // namespace

// Las funciones siguientes siguen DynamicsCompressorKernel de Chromium (Web Audio), que es lo
// que usaba el prototipo: rodilla exponencial que empata la pendiente en ambos extremos.
float Limiter::kneeCurve(float x, float k) const {
    if (x < mLinearThreshold) return x;
    return mLinearThreshold + (1.0f - std::exp(-k * (x - mLinearThreshold))) / k;
}

float Limiter::saturate(float x, float k) const {
    if (x < mKneeThreshold) return kneeCurve(x, k);
    const float yDb = mYKneeThresholdDb + mSlope * (linToDb(x) - mKneeThresholdDb);
    return dbToLin(yDb);
}

float Limiter::saturate(float x) const { return saturate(x, mK); }

float Limiter::slopeAt(float x, float k) const {
    if (x < mLinearThreshold) return 1.0f;
    const float x2 = x * 1.001f;
    const float xDb = linToDb(x), x2Db = linToDb(x2);
    const float yDb = linToDb(kneeCurve(x, k)), y2Db = linToDb(kneeCurve(x2, k));
    return (y2Db - yDb) / (x2Db - xDb);
}

float Limiter::kAtSlope(float desiredSlope) const {
    const float x = dbToLin(mThresholdDb + mKneeDb);
    float minK = 0.1f, maxK = 10000.0f, k = 5.0f;
    for (int i = 0; i < 15; ++i) {
        if (slopeAt(x, k) < desiredSlope) maxK = k;  // k muy alto
        else minK = k;                                // k muy bajo
        k = std::sqrt(minK * maxK);
    }
    return k;
}

void Limiter::setup(int sampleRate, float thresholdDb, float kneeDb, float ratio, float attackSec,
                    float releaseSec) {
    mThresholdDb = thresholdDb;
    mKneeDb = kneeDb;
    mRatio = ratio;
    mLinearThreshold = dbToLin(thresholdDb);
    mSlope = 1.0f / ratio;
    mK = kAtSlope(1.0f / ratio);
    mKneeThresholdDb = thresholdDb + kneeDb;
    mKneeThreshold = dbToLin(mKneeThresholdDb);
    mYKneeThresholdDb = linToDb(kneeCurve(mKneeThreshold, mK));
    // Compensación como Chrome: (1 / salida a 0 dBFS) ^ 0.6
    mMakeup = std::pow(1.0f / saturate(1.0f, mK), 0.6f);
    mAttackCoef = std::exp(-1.0f / (attackSec * sampleRate));
    mReleaseCoef = std::exp(-1.0f / (releaseSec * sampleRate));
    mGainDb = 0.0f;
}
