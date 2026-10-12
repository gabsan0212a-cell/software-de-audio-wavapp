#include "dsp/Analyzer.h"

#include <algorithm>
#include <cmath>

#include "io/MappedAudio.h"
#include "engine/Mixer.h"

namespace {
constexpr double kPi = 3.14159265358979323846;

// Ventana Blackman de Web Audio (alfa 0.16), sobre N puntos.
std::vector<double> blackman(int n) {
    std::vector<double> w(n);
    for (int i = 0; i < n; ++i) {
        const double x = static_cast<double>(i) / n;
        w[i] = 0.42 - 0.5 * std::cos(2 * kPi * x) + 0.08 * std::cos(4 * kPi * x);
    }
    return w;
}
}  // namespace

FFT::FFT(int n) : mN(n), mRev(n), mCos(n / 2), mSin(n / 2) {
    int bits = 0;
    while ((1 << bits) < n) ++bits;
    for (int i = 0; i < n; ++i) {
        int r = 0, x = i;
        for (int b = 0; b < bits; ++b) {
            r = (r << 1) | (x & 1);
            x >>= 1;
        }
        mRev[i] = r;
    }
    for (int i = 0; i < n / 2; ++i) {
        mCos[i] = std::cos(2 * kPi * i / n);
        mSin[i] = -std::sin(2 * kPi * i / n);
    }
}

void FFT::forward(double *re, double *im) const {
    const int n = mN;
    for (int i = 0; i < n; ++i) {
        const int j = mRev[i];
        if (j > i) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int size = 2; size <= n; size <<= 1) {
        const int half = size >> 1, step = n / size;
        for (int i = 0; i < n; i += size) {
            for (int j = 0, k = 0; j < half; ++j, k += step) {
                const int a = i + j, b = a + half;
                const double wr = mCos[k], wi = mSin[k];
                const double tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

// ---------------------------------------------------------------- en vivo

Analyzer::Analyzer(int sampleRate)
    : mRate(sampleRate), mSpecFft(kSpecSize), mFanFft(kFanSize), mSpecWin(blackman(kSpecSize)),
      mFanWin(blackman(kFanSize)), mRe(kSpecSize), mIm(kSpecSize), mL(kSpecSize), mR(kSpecSize),
      mSpecMag(kSpecBins), mFanMagL(kFanBins), mFanMagR(kFanBins), mSpecDb(kSpecBins, -200.0f),
      mFanE(kFanBins, -200.0f), mFanP(kFanBins, 0.0f) {}

void Analyzer::reset() {
    std::fill(mSpecMag.begin(), mSpecMag.end(), 0.0);
    std::fill(mFanMagL.begin(), mFanMagL.end(), 0.0);
    std::fill(mFanMagR.begin(), mFanMagR.end(), 0.0);
    std::fill(mSpecDb.begin(), mSpecDb.end(), -200.0f);
    std::fill(mFanE.begin(), mFanE.end(), -200.0f);
    std::fill(mFanP.begin(), mFanP.end(), 0.0f);
    correlation = 1;
    peakL = peakR = rmsL = rmsR = 0;
    hum = HumInfo();
    mLastWrite = ~0ull;
}

void Analyzer::reset(const Mixer &m) {
    reset();
    float l, r;
    mIgnoreBefore = m.readTap(&l, &r, 0);
}

bool Analyzer::update(const Mixer &m) {
    const uint64_t w = m.readTap(mL.data(), mR.data(), kSpecSize);
    if (w == mLastWrite) return false;
    mLastWrite = w;
    // Lo que llegó antes del último cambio de fuente no cuenta.
    for (int i = 0; i < kSpecSize; ++i) {
        if (static_cast<int64_t>(w) - kSpecSize + i >= static_cast<int64_t>(mIgnoreBefore)) break;
        mL[i] = mR[i] = 0.0f;
    }
    analyze();
    return true;
}

void Analyzer::updateFrom(const float *l, const float *r) {
    std::copy(l, l + kSpecSize, mL.begin());
    std::copy(r, r + kSpecSize, mR.begin());
    analyze();
}

void Analyzer::analyze() {
    // Espectro mono (Web Audio mezcla a mono: (L + R) / 2), magnitud / N, suavizado 0.85.
    for (int i = 0; i < kSpecSize; ++i) {
        mRe[i] = 0.5 * (mL[i] + mR[i]) * mSpecWin[i];
        mIm[i] = 0.0;
    }
    mSpecFft.forward(mRe.data(), mIm.data());
    for (int k = 0; k < kSpecBins; ++k) {
        const double mag = std::hypot(mRe[k], mIm[k]) / kSpecSize;
        mSpecMag[k] = 0.85 * mSpecMag[k] + 0.15 * mag;
        mSpecDb[k] = static_cast<float>(20 * std::log10(mSpecMag[k] + 1e-30));
    }

    // L y R por separado (2048 puntos, suavizado 0.55) para el paneo por frecuencia.
    const int off = kSpecSize - kFanSize;
    for (int c = 0; c < 2; ++c) {
        const std::vector<float> &x = c == 0 ? mL : mR;
        std::vector<double> &sm = c == 0 ? mFanMagL : mFanMagR;
        for (int i = 0; i < kFanSize; ++i) {
            mRe[i] = x[off + i] * mFanWin[i];
            mIm[i] = 0.0;
        }
        mFanFft.forward(mRe.data(), mIm.data());
        for (int k = 0; k < kFanBins; ++k) sm[k] = 0.55 * sm[k] + 0.45 * std::hypot(mRe[k], mIm[k]) / kFanSize;
    }
    for (int k = 0; k < kFanBins; ++k) {
        const double l = mFanMagL[k], r = mFanMagR[k], s = l + r;
        mFanE[k] = static_cast<float>(10 * std::log10(l * l + r * r + 1e-20));
        mFanP[k] = s > 1e-12 ? static_cast<float>((r - l) / s) : 0.0f;
    }

    // Tiempo: picos, RMS y correlación de los últimos 2048 cuadros.
    double sl = 0, sr = 0, slr = 0;
    float pl = 0, pr = 0;
    const float *L = timeL(), *R = timeR();
    for (int i = 0; i < kTime; ++i) {
        sl += static_cast<double>(L[i]) * L[i];
        sr += static_cast<double>(R[i]) * R[i];
        slr += static_cast<double>(L[i]) * R[i];
        pl = std::max(pl, std::fabs(L[i]));
        pr = std::max(pr, std::fabs(R[i]));
    }
    peakL = pl;
    peakR = pr;
    rmsL = static_cast<float>(std::sqrt(sl / kTime));
    rmsR = static_cast<float>(std::sqrt(sr / kTime));
    const double den = std::sqrt(sl * sr);
    if (den > 1e-9) correlation = static_cast<float>(slr / den);

    detectHum();
}

// Igual que detectHum() del prototipo: prominencia de 50/60 Hz y 3 armónicos sobre el
// piso cercano (mediana entre ±12 y ±30 Hz).
void Analyzer::detectHum() {
    const double hz = binHz();
    auto prom = [&](double f, double &lvl) {
        const int c = static_cast<int>(std::lround(f / hz));
        const int w = std::max(1, static_cast<int>(std::lround(3 / hz)));
        double pk = -200;
        for (int i = std::max(0, c - w); i <= std::min(kSpecBins - 1, c + w); ++i) pk = std::max(pk, (double)mSpecDb[i]);
        // Un zumbido real cae justo en 50/60 Hz: si el pico más cercano está corrido (p. ej. un
        // bajo tocando La de 55 Hz), no cuenta. (Mejora sobre el prototipo.)
        int arg = c;
        const int ancho = static_cast<int>(std::lround(9 / hz));
        for (int i = std::max(0, c - ancho); i <= std::min(kSpecBins - 1, c + ancho); ++i)
            if (mSpecDb[i] > mSpecDb[arg]) arg = i;
        if (std::fabs(arg * hz - f) > 4.5) {
            lvl = pk;
            return -100.0;
        }
        std::vector<float> nb;
        const double rangos[2][2] = {{f - 30, f - 12}, {f + 12, f + 30}};
        for (const auto &rg : rangos)
            for (int i = std::max(0, (int)std::lround(rg[0] / hz)); i <= std::min(kSpecBins - 1, (int)std::lround(rg[1] / hz)); ++i)
                nb.push_back(mSpecDb[i]);
        std::sort(nb.begin(), nb.end());
        lvl = pk;
        return pk - (nb.empty() ? -120.0 : nb[nb.size() >> 1]);
    };
    struct Mejor { int f = 0, cnt = -1; double sum = 0; bool first = false; } best;
    for (int f : {50, 60}) {
        int cnt = 0;
        double sum = 0;
        bool first = false;
        for (int k = 1; k <= 4; ++k) {
            if (f * k >= mRate / 2.0 * 0.9) break;
            double lvl;
            const double p = prom(f * k, lvl);
            if (p >= 10 && lvl > -85) {
                ++cnt;
                sum += p;
                if (k == 1) first = true;
            }
        }
        if (cnt > best.cnt || (cnt == best.cnt && sum > best.sum)) best = {f, cnt, sum, first};
    }
    const bool ok = best.cnt >= 3 || (best.cnt >= 2 && best.first);
    hum = ok ? HumInfo{best.f, best.cnt, static_cast<float>(std::lround(best.sum / best.cnt))} : HumInfo();
}

// ---------------------------------------------------------------- espectrograma

std::vector<uint8_t> spectrogram(const MappedAudio &a, int cols, int rows, float *hiHz) {
    constexpr int SN = 4096;
    const double sr = a.sampleRate();
    const int64_t len = a.frames();
    const int ch = a.channels();
    const float *d = a.data();
    const double binHz = sr / SN, hi = std::min(20000.0, sr / 2 * 0.98);
    if (hiHz) *hiHz = static_cast<float>(hi);
    std::vector<uint8_t> out(static_cast<size_t>(cols) * rows, 0);
    std::vector<double> b0(rows), b1(rows), win(SN), re(SN), im(SN), db(SN / 2);
    for (int r = 0; r < rows; ++r) {
        b0[r] = 20 * std::pow(hi / 20, static_cast<double>(r) / rows) / binHz;
        b1[r] = 20 * std::pow(hi / 20, static_cast<double>(r + 1) / rows) / binHz;
    }
    for (int i = 0; i < SN; ++i) win[i] = 0.5 - 0.5 * std::cos(2 * kPi * i / (SN - 1));
    FFT fft(SN);
    for (int col = 0; col < cols; ++col) {
        const int64_t s0 = static_cast<int64_t>(std::floor((col + 0.5) * len / cols)) - SN / 2;
        for (int i = 0; i < SN; ++i) {
            const int64_t k = s0 + i;
            double v = 0;
            if (k >= 0 && k < len) v = ch == 2 ? 0.5 * (d[2 * k] + d[2 * k + 1]) : d[k];
            re[i] = v * win[i];
            im[i] = 0;
        }
        fft.forward(re.data(), im.data());
        for (int i = 0; i < SN / 2; ++i) db[i] = 20 * std::log10(std::hypot(re[i], im[i]) / (SN / 4) + 1e-9);
        for (int r = 0; r < rows; ++r) {
            const double lo = b0[r], up = b1[r];
            double m;
            if (up - lo < 1) {
                const double x = (lo + up) / 2;
                const int i0 = static_cast<int>(std::floor(x));
                const double f = x - i0;
                m = db[std::min(SN / 2 - 1, i0)] * (1 - f) + db[std::min(SN / 2 - 1, i0 + 1)] * f;
            } else {
                m = -200;
                for (int i = static_cast<int>(std::floor(lo)); i <= std::min(SN / 2 - 1, static_cast<int>(std::floor(up))); ++i)
                    m = std::max(m, db[i]);
            }
            out[static_cast<size_t>(col) * rows + r] =
                static_cast<uint8_t>(std::clamp(std::lround((m + 90) / 80 * 255), 0L, 255L));
        }
    }
    return out;
}
