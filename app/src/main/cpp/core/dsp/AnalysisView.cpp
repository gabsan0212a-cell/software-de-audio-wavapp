#include "dsp/AnalysisView.h"

#include <algorithm>
#include <cmath>

void AnalysisView::reset(Analyzer &a, const Mixer &m) {
    a.reset(m);
    mActive = mLoud = false;
    mCorrelation = 1.0f;
    mPan = 0.0f;
    mHum = HumInfo();
    mLastMs = mLastHumMs = -1;
    mPeakVector = 0.05f;
    mPeakSources = -60.0f;
    mGonioCount = 0;
}

bool AnalysisView::update(Analyzer &a, const Mixer &m, bool playing, int64_t nowMs, bool classic) {
    if (!playing) {
        if (!mActive) return false;
        reset(a, m);
        return true;
    }
    if (mLastMs >= 0 && nowMs - mLastMs < 30) return false;  // ~30 veces por segundo
    if (!a.update(m)) return false;
    mLastMs = nowMs;
    const float rl = a.rmsL, rr = a.rmsR;
    mLoud = rl + rr > 1e-4f;
    mCorrelation = mCorrelation * 0.85f + a.correlation * 0.15f;
    mPan = mLoud ? (rr - rl) / (rr + rl) : 0.0f;
    if (mLastHumMs < 0 || nowMs - mLastHumMs >= 250) {
        mHum = a.hum;
        mLastHumMs = nowMs;
    }
    mActive = true;
    if (classic != mClassic) {  // al cambiar de modo, la escala automática empieza de nuevo
        mClassic = classic;
        mPeakVector = 0.05f;
        mPeakSources = -60.0f;
    }
    if (classic) classicPoints(a);
    else sourceRays(a);
    return true;
}

AnalysisView::Verdict AnalysisView::verdict() const {
    if (!mActive || !mLoud) return kNone;
    const float c = mCorrelation;
    if (c < 0.0f) return kOutOfPhase;
    if (c < 0.3f) return kVeryWide;
    if (c < 0.9f) return kStereo;
    return kMono;
}

int AnalysisView::imbalance() const {
    if (verdict() == kNone || std::fabs(mPan) <= 0.25f) return 0;
    return mPan < 0 ? -1 : 1;
}

bool AnalysisView::bad() const {
    if (verdict() == kNone) return false;
    return mCorrelation < 0.0f || std::fabs(mPan) > 0.5f;
}

void AnalysisView::stats(float *out) const {
    out[0] = mActive ? 1.0f : 0.0f;
    out[1] = mLoud ? 1.0f : 0.0f;
    out[2] = mCorrelation;
    out[3] = mPan;
    out[4] = static_cast<float>(verdict());
    out[5] = static_cast<float>(imbalance());
    out[6] = bad() ? 1.0f : 0.0f;
    out[7] = mActive ? static_cast<float>(mHum.freq) : 0.0f;
    out[8] = mActive ? static_cast<float>(mHum.count) : 0.0f;
    out[9] = mActive ? mHum.db : 0.0f;
    out[10] = std::max(0.0f, 20.0f * std::log10(1.0f / mPeakVector));
}

int AnalysisView::goniometer(float *out, int max) const {
    if (!mActive) return 0;
    const int per = mClassic ? 2 : 3;
    const int n = std::min(mGonioCount, max / per);
    std::copy(mGonio.begin(), mGonio.begin() + n * per, out);
    return n;
}

// Nube de puntos M/S: x = lado (R - L), y = centro (L + R), con ganancia automática.
void AnalysisView::classicPoints(const Analyzer &a) {
    const int n = Analyzer::kTime;
    const float *L = a.timeL(), *R = a.timeR();
    const float k = std::sqrt(0.5f);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max(peak, std::max(std::fabs(R[i] - L[i]) * k, std::fabs(L[i] + R[i]) * k));
    mPeakVector = std::max(peak, std::max(mPeakVector * 0.985f, 0.003f));
    mGonio.resize(static_cast<size_t>(2 * n));
    for (int i = 0; i < n; ++i) {
        mGonio[2 * i] = (R[i] - L[i]) * k / mPeakVector;
        mGonio[2 * i + 1] = (L[i] + R[i]) * k / mPeakVector;
    }
    mGonioCount = n;
}

// "Por fuentes": cada pico del espectro es un rayo en su ángulo de paneo (exagerado hasta ±75°)
// y con largo según su nivel; lo centrado sube vertical.
void AnalysisView::sourceRays(const Analyzer &a) {
    const float *en = a.fanDb(), *pn = a.fanPan();
    const float hz = 48000.0f / Analyzer::kFanSize;
    const int a0 = std::max(1, static_cast<int>(std::ceil(40.0f / hz)));
    const int a1 = std::min(Analyzer::kFanBins - 1, static_cast<int>(16000.0f / hz));
    float pk = -200.0f;
    for (int i = a0; i <= a1; ++i) pk = std::max(pk, en[i]);
    mPeakSources = pk > mPeakSources ? pk : mPeakSources * 0.97f + pk * 0.03f;
    mGonioCount = 0;
    mGonio.resize(static_cast<size_t>(3 * (a1 - a0 + 1)));
    if (pk < -85.0f) return;
    const float span = 36.0f, top = mPeakSources;
    for (int i = a0; i <= a1; ++i) {
        if (i > a0 && i < a1 && !(en[i] >= en[i - 1] && en[i] > en[i + 1])) continue;  // solo picos
        const float t = (en[i] - (top - span)) / span;
        if (t < 0.04f) continue;
        const float p = pn[i];
        const float ang = (p < 0 ? -1.0f : p > 0 ? 1.0f : 0.0f) * std::pow(std::fabs(p), 0.65f) * kMaxAngle;
        const float u = std::min(1.0f, t);
        mGonio[3 * mGonioCount] = ang;
        mGonio[3 * mGonioCount + 1] = 0.08f + 0.92f * u;
        mGonio[3 * mGonioCount + 2] = 0.25f + 0.5f * u;
        ++mGonioCount;
    }
}

void AnalysisView::spectrumColumns(const Analyzer &a, float *out, int cols) {
    const float hz = a.binHz();
    const float *sp = a.spectrumDb();
    for (int c = 0; c < cols; ++c) {
        const float f0 = 20.0f * std::pow(1000.0f, static_cast<float>(c) / cols);
        const float f1 = 20.0f * std::pow(1000.0f, static_cast<float>(c + 1) / cols);
        const int i0 = std::max(0, static_cast<int>(f0 / hz));
        const int i1 = std::min(Analyzer::kSpecBins - 1, std::max(i0, static_cast<int>(f1 / hz)));
        float m = -200.0f;
        for (int i = i0; i <= i1; ++i) m = std::max(m, sp[i]);
        out[c] = m;
    }
}
