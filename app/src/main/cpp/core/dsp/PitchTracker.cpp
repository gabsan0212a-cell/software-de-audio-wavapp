#include "dsp/PitchTracker.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float kThreshold = 0.18f;   // YIN: primera bajada debajo de esto
constexpr float kMinClarity = 0.55f;
constexpr int kOnFrames = 2;           // análisis seguidos con nota para prender
constexpr int kOffFrames = 5;          // ... y sin nota para apagar (análisis cada ~2 ms)

}  // namespace

PitchTracker::PitchTracker(int sampleRate) : mRate(sampleRate) {
    mFactor = std::max(1, (sampleRate + 8000) / 16000);
    mDecRate = sampleRate / mFactor;
    mHop = std::max(8, static_cast<int>(std::lround(0.002 * mDecRate)));
    // Pasa-bajos Butterworth de 4.º orden antes de quedarse con 1 de cada mFactor muestras.
    const double fc = std::min(3500.0, 0.42 * mDecRate);
    const double qs[2] = {0.54119610, 1.30656296};
    for (int k = 0; k < 2; ++k) {
        const double w0 = 2.0 * M_PI * fc / sampleRate, cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * qs[k]), a0 = 1.0 + alpha;
        Biquad &b = mLp[k];
        b.b0 = static_cast<float>((1.0 - cw) / 2.0 / a0);
        b.b1 = static_cast<float>((1.0 - cw) / a0);
        b.b2 = b.b0;
        b.a1 = static_cast<float>(-2.0 * cw / a0);
        b.a2 = static_cast<float>((1.0 - alpha) / a0);
    }
    const int maxLag = static_cast<int>(std::ceil(mDecRate / kLowestHz));
    const int window = std::max(static_cast<int>(std::lround(0.016 * mDecRate)), maxLag);
    mHist.assign(static_cast<size_t>(window + maxLag + 4 * mHop + 2), 0.0f);
    mDiff.assign(static_cast<size_t>(maxLag + 2), 0.0f);
    setRange(70.0f, 1000.0f);
}

void PitchTracker::setRange(float minHz, float maxHz) {
    minHz = std::clamp(minHz, kLowestHz, 400.0f);
    maxHz = std::clamp(maxHz, minHz * 2.0f, 0.4f * mDecRate);
    mMinLag = std::max(2, static_cast<int>(std::floor(mDecRate / maxHz)));
    mMaxLag = std::max(mMinLag + 2, static_cast<int>(std::ceil(mDecRate / minHz)));
    // ~16 ms de ventana (al menos un período de la nota más grave).
    mWindow = std::max(static_cast<int>(std::lround(0.016 * mDecRate)), mMaxLag);
    mMinWindow = std::min(mWindow, static_cast<int>(std::lround(0.005 * mDecRate)));
    // La memoria se reservó para el rango más amplio: aquí no se reserva nada (se puede
    // llamar desde el hilo de audio).
    mHistLen = std::min(mWindow + mMaxLag + 4 * mHop + 2, static_cast<int>(mHist.size()));
    reset();
}

void PitchTracker::reset() {
    for (Biquad &b : mLp) b.z1 = b.z2 = 0.0f;
    mPhase = 0;
    mFill = 0;
    mSinceHop = 0;
    mVoiced = false;
    mHz = mClarity = 0.0f;
    mLevelDb = -120.0f;
    mOnCount = mOffCount = 0;
    mPendingHz = 0.0f;
}

bool PitchTracker::process(const float *x, int n) {
    bool fresh = false;
    int at = 0;  // muestra del bloque en la que se hizo la última estimación
    const int need = mWindow + mMaxLag + 1;
    for (int i = 0; i < n; ++i) {
        const float y = mLp[1].run(mLp[0].run(x[i]));
        if (++mPhase < mFactor) continue;
        mPhase = 0;
        if (mFill == mHistLen) {  // lleno: se queda con lo que hace falta
            std::memmove(mHist.data(), mHist.data() + (mFill - need), sizeof(float) * static_cast<size_t>(need));
            mFill = need;
        }
        mHist[static_cast<size_t>(mFill++)] = y;
        if (++mSinceHop >= mHop && mFill >= need) {
            mSinceHop = 0;
            analyze();
            fresh = true;
            at = i;
        }
    }
    if (fresh) {
        // El centro de la ventana: la mitad de la ventana (a ~16 kHz) más el retraso del
        // pasa-bajos (~8 muestras), contado desde el final del bloque.
        mLastAge = static_cast<float>(n - 1 - at) + 0.5f * static_cast<float>(mLastW * mFactor) + 8.0f;
    }
    // Números diminutos en los filtros vuelven lento el cálculo en algunos procesadores.
    for (Biquad &b : mLp) {
        if (std::fabs(b.z1) < 1e-20f) b.z1 = 0.0f;
        if (std::fabs(b.z2) < 1e-20f) b.z2 = 0.0f;
    }
    return fresh;
}

void PitchTracker::analyze() {
    // Con nota, la ventana es de ~3 períodos (responde más rápido en notas agudas); sin nota,
    // la más larga (para encontrar también las graves).
    const int W = mVoiced ? std::clamp(static_cast<int>(3.0f * mDecRate / mHz), mMinWindow, mWindow) : mWindow;
    mLastW = W;
    const float *a = mHist.data() + mFill - W;  // los W más nuevos
    // Nivel de lo analizado.
    double e2 = 0.0;
    for (int j = 0; j < W; ++j) e2 += static_cast<double>(a[j]) * a[j];
    mLevelDb = 10.0f * static_cast<float>(std::log10(e2 / W + 1e-14));

    float hz = 0.0f, clarity = 0.0f;
    if (mLevelDb > kFloorDb) {
        // d(τ) = Σ (x[j] - x[j-τ])² sobre los W más nuevos, y su versión normalizada.
        float *d = mDiff.data();
        d[0] = 1.0f;
        double cum = 0.0;
        for (int tau = 1; tau <= mMaxLag; ++tau) {
            const float *b = a - tau;
            float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
            int j = 0;
            for (; j + 4 <= W; j += 4) {
                const float v0 = a[j] - b[j], v1 = a[j + 1] - b[j + 1], v2 = a[j + 2] - b[j + 2], v3 = a[j + 3] - b[j + 3];
                s0 += v0 * v0;
                s1 += v1 * v1;
                s2 += v2 * v2;
                s3 += v3 * v3;
            }
            for (; j < W; ++j) {
                const float v = a[j] - b[j];
                s0 += v * v;
            }
            const float s = (s0 + s1) + (s2 + s3);
            cum += s;
            d[tau] = cum > 0.0 ? static_cast<float>(s * tau / cum) : 1.0f;
        }
        int tau = -1;
        for (int t = mMinLag; t < mMaxLag; ++t) {
            if (d[t] < kThreshold) {
                while (t + 1 < mMaxLag && d[t + 1] < d[t]) ++t;
                tau = t;
                break;
            }
        }
        if (tau > 0) {
            float shift = 0.0f;
            const float p = d[tau - 1], q = d[tau], r = d[tau + 1];
            const float den = p - 2.0f * q + r;
            if (den > 1e-9f) shift = std::clamp(0.5f * (p - r) / den, -0.5f, 0.5f);
            hz = static_cast<float>(mDecRate) / (static_cast<float>(tau) + shift);
            clarity = std::max(0.0f, 1.0f - q);
        }
    }
    mClarity = clarity;

    const bool raw = hz > 0.0f && clarity >= kMinClarity;
    if (!raw) {
        mOnCount = 0;
        mPendingHz = 0.0f;
        if (mVoiced && ++mOffCount >= kOffFrames) mVoiced = false;
        return;
    }
    mOffCount = 0;
    if (!mVoiced) {
        // Prende con dos análisis seguidos que coinciden (no por un golpe suelto).
        if (mOnCount > 0 && std::fabs(hz / mHz - 1.0f) < 0.06f) {
            if (++mOnCount >= kOnFrames) mVoiced = true;
        } else {
            mOnCount = 1;
        }
        mHz = hz;
        return;
    }
    // Un salto de una octava (arriba o abajo) se cree solo si se repite.
    const float ratio = hz / mHz;
    const bool octave = std::fabs(ratio - 2.0f) < 0.12f || std::fabs(ratio - 0.5f) < 0.03f;
    if (octave && !(mPendingHz > 0.0f && std::fabs(hz / mPendingHz - 1.0f) < 0.05f)) {
        mPendingHz = hz;
        return;
    }
    mPendingHz = 0.0f;
    mHz = hz;
}
