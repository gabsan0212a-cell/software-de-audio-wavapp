#include "dsp/Loudness.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k) {
        const double t = x / (2.0 * k);
        term *= t * t;
        sum += term;
        if (term < 1e-12 * sum) break;
    }
    return sum;
}

}  // namespace

// ---------------------------------------------------------------- front-end

LoudnessFrontEnd::LoudnessFrontEnd(int sampleRate) : mRate(sampleRate), mBlockFrames(sampleRate / 10) {
    // Ponderación K para cualquier frecuencia (las mismas fórmulas que libebur128): un
    // realce de agudos (+4 dB, como la cabeza) y un pasa-altos RLB.
    {
        const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan(kPi * f0 / mRate);
        const double vh = std::pow(10.0, g / 20.0), vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        mPre = {(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan(kPi * f0 / mRate);
        const double a0 = 1.0 + k / q + k * k;
        mHp = {1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
    // Interpolador ×4 para el pico real: sinc con ventana de Kaiser, 12 coeficientes por fase
    // (como el de la norma); cada fase suma 1.
    constexpr int total = kTaps * kPhases;
    double h[total];
    const double beta = 6.0, i0b = besselI0(beta);
    for (int k = 0; k < total; ++k) {
        const double t = (k - (total - 1) / 2.0) / kPhases;
        const double sinc = std::fabs(t) < 1e-12 ? 1.0 : std::sin(kPi * t) / (kPi * t);
        const double u = 2.0 * k / (total - 1) - 1.0;
        h[k] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / i0b;
    }
    for (int p = 0; p < kPhases; ++p) {
        double s = 0.0;
        for (int j = 0; j < kTaps; ++j) s += h[p + kPhases * j];
        for (int j = 0; j < kTaps; ++j) mCoef[p][j] = static_cast<float>(h[p + kPhases * j] / s);
    }
    reset();
}

void LoudnessFrontEnd::reset() {
    for (auto &z : mZ) std::fill(z, z + 4, 0.0);
    for (auto &h : mHist) std::fill(h, h + 2 * kTaps, 0.0f);
    mHistPos = 0;
    mSum = 0.0;
    mCount = 0;
    mPeak = 0.0f;
}

int LoudnessFrontEnd::process(const float *l, const float *r, int n, LoudnessBlock *out, int max) {
    int made = 0;
    for (int i = 0; i < n; ++i) {
        const float xs[2] = {l[i], r[i]};
        for (int c = 0; c < 2; ++c) {
            const double x = std::isfinite(xs[c]) ? xs[c] : 0.0;
            double *z = mZ[c];
            const double y = mPre.b0 * x + z[0];
            z[0] = mPre.b1 * x - mPre.a1 * y + z[1];
            z[1] = mPre.b2 * x - mPre.a2 * y;
            const double y2 = mHp.b0 * y + z[2];
            z[2] = mHp.b1 * y - mHp.a1 * y2 + z[3];
            z[3] = mHp.b2 * y - mHp.a2 * y2;
            mSum += y2 * y2;
            // Historia doble: las últimas 12 muestras quedan seguidas (de la más nueva hacia atrás).
            float *h = mHist[c];
            h[mHistPos] = h[mHistPos + kTaps] = static_cast<float>(x);
            const float *w = h + mHistPos + kTaps;  // w[-j] = muestra de hace j
            float pk = std::fabs(static_cast<float>(x));
            for (int p = 0; p < kPhases; ++p) {
                float acc = 0.0f;
                for (int j = 0; j < kTaps; ++j) acc += mCoef[p][j] * w[-j];
                pk = std::max(pk, std::fabs(acc));
            }
            mPeak = std::max(mPeak, pk);
        }
        mHistPos = mHistPos + 1 == kTaps ? 0 : mHistPos + 1;
        if (++mCount == mBlockFrames) {
            if (made < max) {
                out[made].energy = mSum / mBlockFrames;
                out[made].peak = mPeak;
                out[made].gen = 0;
                ++made;
            }
            mSum = 0.0;
            mCount = 0;
            mPeak = 0.0f;
            // Silencio largo: que los filtros no se queden con números diminutos (lentos).
            for (auto &z : mZ)
                for (double &v : z)
                    if (std::fabs(v) < 1e-30) v = 0.0;
        }
    }
    return made;
}

// ---------------------------------------------------------------- medidor

float LoudnessMeter::toLufs(double energy) {
    return energy > 1e-20 ? static_cast<float>(-0.691 + 10.0 * std::log10(energy)) : kLoudnessNone;
}

void LoudnessMeter::Histogram::clear() {
    sum.assign(kBins, 0.0);
    count.assign(kBins, 0);
}

void LoudnessMeter::Histogram::add(double energy) {
    const float l = toLufs(energy);
    if (l <= -70.0f) return;  // compuerta absoluta
    const int i = std::clamp(static_cast<int>(std::floor((l + 70.0f) * 10.0f)), 0, kBins - 1);
    sum[i] += energy;
    ++count[i];
}

double LoudnessMeter::Histogram::meanAbove(float aboveLufs, int64_t *n) const {
    double s = 0.0;
    int64_t c = 0;
    for (int i = 0; i < kBins; ++i) {
        if (!count[i] || -70.0f + (i + 0.5f) * 0.1f <= aboveLufs) continue;
        s += sum[i];
        c += count[i];
    }
    *n = c;
    return c ? s / static_cast<double>(c) : 0.0;
}

void LoudnessMeter::reset() {
    mRecent.assign(30, 0.0);
    mBlocks = 0;
    mGating.clear();
    mShort.clear();
    mPeak = 0.0f;
    mMaxMomentary = mMaxShortTerm = kLoudnessNone;
    mHistM.assign(kHistory, kLoudnessNone);
    mHistS.assign(kHistory, kLoudnessNone);
    mHistCount = 0;
}

double LoudnessMeter::meanOfLast(int n) const {
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += mRecent[static_cast<size_t>((mBlocks - 1 - i) % 30)];
    return s / n;
}

void LoudnessMeter::add(const LoudnessBlock &b) {
    mRecent[static_cast<size_t>(mBlocks % 30)] = std::isfinite(b.energy) ? std::max(0.0, b.energy) : 0.0;
    ++mBlocks;
    if (std::isfinite(b.peak)) mPeak = std::max(mPeak, b.peak);
    // Bloques de 400 ms que se enciman 75 %: uno nuevo cada 100 ms (para el integrado).
    if (mBlocks >= 4) {
        const double e = meanOfLast(4);
        mGating.add(e);
        mMaxMomentary = std::max(mMaxMomentary, toLufs(e));
    }
    // Corto plazo (3 s); para el rango, uno cada segundo (como libebur128).
    if (mBlocks >= 30) {
        const double e = meanOfLast(30);
        mMaxShortTerm = std::max(mMaxShortTerm, toLufs(e));
        if ((mBlocks - 30) % 10 == 0) mShort.add(e);
    }
    const size_t h = static_cast<size_t>(mHistCount % kHistory);
    mHistM[h] = momentary();
    mHistS[h] = shortTerm();
    ++mHistCount;
}

float LoudnessMeter::momentary() const {
    return mBlocks ? toLufs(meanOfLast(static_cast<int>(std::min<int64_t>(4, mBlocks)))) : kLoudnessNone;
}

float LoudnessMeter::shortTerm() const {
    return mBlocks ? toLufs(meanOfLast(static_cast<int>(std::min<int64_t>(30, mBlocks)))) : kLoudnessNone;
}

float LoudnessMeter::integrated() const {
    int64_t n = 0;
    const double all = mGating.meanAbove(-70.0f, &n);
    if (n == 0) return kLoudnessNone;
    const double gated = mGating.meanAbove(toLufs(all) - 10.0f, &n);  // compuerta relativa
    return n ? toLufs(gated) : kLoudnessNone;
}

float LoudnessMeter::range() const {
    int64_t n = 0;
    const double all = mShort.meanAbove(-70.0f, &n);
    if (n == 0) return kLoudnessNone;
    const float gate = toLufs(all) - 20.0f;
    std::vector<std::pair<float, int64_t>> bins;  // (centro, cuántos) sobre la compuerta
    int64_t total = 0;
    for (int i = 0; i < kBins; ++i) {
        const float c = -70.0f + (i + 0.5f) * 0.1f;
        if (!mShort.count[static_cast<size_t>(i)] || c <= gate) continue;
        bins.emplace_back(c, mShort.count[static_cast<size_t>(i)]);
        total += mShort.count[static_cast<size_t>(i)];
    }
    if (total == 0) return kLoudnessNone;
    auto at = [&](double q) {
        const int64_t k = static_cast<int64_t>((total - 1) * q + 0.5);
        int64_t seen = 0;
        for (const auto &b : bins) {
            seen += b.second;
            if (seen > k) return b.first;
        }
        return bins.back().first;
    };
    return std::max(0.0f, at(0.95) - at(0.10));
}

float LoudnessMeter::truePeakDb() const {
    return mPeak > 1e-10f ? 20.0f * std::log10(mPeak) : kLoudnessNone;
}

void LoudnessMeter::history(float *momentary, float *shortTerm, int n) const {
    for (int i = 0; i < n; ++i) {
        const int64_t k = mHistCount - n + i;
        const bool ok = k >= 0 && mHistCount - k <= kHistory;
        const size_t h = ok ? static_cast<size_t>(k % kHistory) : 0;
        if (momentary) momentary[i] = ok ? mHistM[h] : kLoudnessNone;
        if (shortTerm) shortTerm[i] = ok ? mHistS[h] : kLoudnessNone;
    }
}
