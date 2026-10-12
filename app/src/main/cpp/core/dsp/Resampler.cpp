#include "dsp/Resampler.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

constexpr int kZeroCrossings = 64;  // cruces por cero a cada lado del filtro
constexpr double kRolloff = 0.95;   // corte relativo al Nyquist más bajo
constexpr double kBeta = 10.0;      // Kaiser: ~100 dB de rechazo
constexpr int64_t kMaxExactPhases = 4096;
constexpr int kInterpPhases = 1024;
constexpr double kPi = 3.14159265358979323846;

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

inline float dot(const float *a, const float *b, int n) {
    float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        s0 += a[i] * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    for (; i < n; ++i) s0 += a[i] * b[i];
    return (s0 + s1) + (s2 + s3);
}

}  // namespace

Resampler::Resampler(int channels, int inRate, int outRate) : mCh(channels) {
    const int64_t g = std::gcd<int64_t>(inRate, outRate);
    mNum = inRate / g;
    mDen = outRate / g;
    const double fc = kRolloff * std::min(1.0, static_cast<double>(outRate) / inRate);
    mHalf = static_cast<int>(std::ceil(kZeroCrossings / fc));
    mTaps = 2 * mHalf;
    mExact = mDen <= kMaxExactPhases;
    mPhases = mExact ? static_cast<int>(mDen) : kInterpPhases;

    // Fila p: filtro para una salida que cae p/mPhases cuadros después de la entrada i0.
    // Toma i0 + k con k = t - mHalf + 1 (t = 0..mTaps-1).
    const double i0beta = besselI0(kBeta);
    mTable.assign(static_cast<size_t>(mPhases + 1) * mTaps, 0.0f);
    for (int p = 0; p <= mPhases; ++p) {
        const double frac = static_cast<double>(p) / mPhases;
        float *row = &mTable[static_cast<size_t>(p) * mTaps];
        double sum = 0.0;
        std::vector<double> h(mTaps);
        for (int t = 0; t < mTaps; ++t) {
            const double x = frac - (t - mHalf + 1);
            const double r = x / mHalf;
            if (r <= -1.0 || r >= 1.0) { h[t] = 0.0; continue; }
            const double arg = kPi * fc * x;
            const double sinc = std::fabs(arg) < 1e-12 ? 1.0 : std::sin(arg) / arg;
            const double win = besselI0(kBeta * std::sqrt(1.0 - r * r)) / i0beta;
            h[t] = fc * sinc * win;
            sum += h[t];
        }
        for (int t = 0; t < mTaps; ++t) row[t] = static_cast<float>(h[t] / sum);  // ganancia 1 en DC
    }

    // Antes del inicio la entrada vale 0.
    mHist.assign(mCh, std::vector<float>(mHalf - 1, 0.0f));
    mHistStart = -(mHalf - 1);
}

int64_t Resampler::outputFrames(int64_t inFrames, int inRate, int outRate) {
    const int64_t g = std::gcd<int64_t>(inRate, outRate);
    const int64_t num = inRate / g, den = outRate / g;
    return (inFrames * den + num - 1) / num;
}

void Resampler::process(const float *in, size_t frames, std::vector<float> &out) {
    for (int c = 0; c < mCh; ++c) {
        auto &h = mHist[c];
        const size_t old = h.size();
        h.resize(old + frames);
        for (size_t i = 0; i < frames; ++i) h[old + i] = in[i * mCh + c];
    }
    mInFrames += static_cast<int64_t>(frames);
    produce(out, INT64_MAX);
}

void Resampler::finish(std::vector<float> &out) {
    for (auto &h : mHist) h.resize(h.size() + mHalf + 1, 0.0f);
    produce(out, outputFrames(mInFrames, static_cast<int>(mNum), static_cast<int>(mDen)));
}

void Resampler::produce(std::vector<float> &out, int64_t limit) {
    const int64_t histEnd = mHistStart + static_cast<int64_t>(mHist[0].size());
    const int64_t stepInt = mNum / mDen, stepRem = mNum % mDen;
    while (mOutFrames < limit && mPosInt + mHalf < histEnd) {
        const size_t base = static_cast<size_t>(mPosInt - mHalf + 1 - mHistStart);
        if (mExact) {
            const float *row = &mTable[static_cast<size_t>(mPosRem) * mTaps];
            for (int c = 0; c < mCh; ++c) out.push_back(dot(&mHist[c][base], row, mTaps));
        } else {
            const double fp = static_cast<double>(mPosRem) / mDen * mPhases;
            const int p = static_cast<int>(fp);
            const float a = static_cast<float>(fp - p);
            const float *r0 = &mTable[static_cast<size_t>(p) * mTaps];
            const float *r1 = r0 + mTaps;
            for (int c = 0; c < mCh; ++c) {
                const float d0 = dot(&mHist[c][base], r0, mTaps);
                const float d1 = dot(&mHist[c][base], r1, mTaps);
                out.push_back(d0 + a * (d1 - d0));
            }
        }
        ++mOutFrames;
        mPosInt += stepInt;
        mPosRem += stepRem;
        if (mPosRem >= mDen) { mPosRem -= mDen; ++mPosInt; }
    }
    // Descarta historial que ya no se usará.
    const int64_t keepFrom = mPosInt - mHalf + 1;
    if (keepFrom - mHistStart > 16384) {
        const size_t drop = static_cast<size_t>(keepFrom - mHistStart);
        for (auto &h : mHist) h.erase(h.begin(), h.begin() + static_cast<ptrdiff_t>(drop));
        mHistStart = keepFrom;
    }
}
