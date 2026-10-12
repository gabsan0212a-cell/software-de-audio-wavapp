#include "dsp/Equalizer.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace {

constexpr double kPi = 3.14159265358979323846;

double clampFreq(double f, double rate) { return std::clamp(f, 10.0, rate / 2 * 0.95); }
double clampQ(double q) { return std::clamp(q, 0.05, 50.0); }

}  // namespace

bool filterHasGain(int type) { return type == kPeaking || type == kLowShelf || type == kHighShelf; }

// Fórmulas del "Audio EQ Cookbook" tal como las usa Web Audio (shelves con pendiente S = 1).
Biquad designBiquad(int type, double freq, double gainDb, double q, double rate) {
    const double w0 = 2 * kPi * clampFreq(freq, rate) / rate;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double A = std::pow(10.0, gainDb / 40);
    const double alpha = sw / (2 * clampQ(q));
    double b0, b1, b2, a0, a1, a2;
    switch (type) {
        case kLowShelf: {
            const double al = sw / 2 * std::sqrt(2.0), k = 2 * std::sqrt(A) * al;
            b0 = A * ((A + 1) - (A - 1) * cw + k);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - k);
            a0 = (A + 1) + (A - 1) * cw + k;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - k;
            break;
        }
        case kHighShelf: {
            const double al = sw / 2 * std::sqrt(2.0), k = 2 * std::sqrt(A) * al;
            b0 = A * ((A + 1) + (A - 1) * cw + k);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - k);
            a0 = (A + 1) - (A - 1) * cw + k;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - k;
            break;
        }
        case kHighPass:
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case kLowPass:
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case kNotch:
            b0 = 1; b1 = -2 * cw; b2 = 1;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        default:  // campana
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
            break;
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

double biquadMagnitudeDb(const Biquad &c, double freq, double rate) {
    const double w = 2 * kPi * freq / rate;
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
    const std::complex<double> h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
    return 20 * std::log10(std::max(std::abs(h), 1e-12));
}

// ---------------------------------------------------------------- interfaz

void Equalizer::setBand(int band, bool present, bool on, int type, float freq, float gainDb, float q) {
    if (band < 0 || band >= kMaxBands) return;
    Params &p = mParams[band];
    p.type.store(std::clamp(type, 0, kFilterTypes - 1));
    p.freq.store(freq);
    p.gain.store(std::clamp(gainDb, -30.0f, 30.0f));
    p.q.store(q);
    p.on.store(on);
    p.present.store(present);
    recomputeTrim();
}

void Equalizer::setBypass(bool on) {
    mBypass.store(on);
    recomputeTrim();
}

void Equalizer::setAutoGain(bool on) {
    mAuto.store(on);
    recomputeTrim();
}

void Equalizer::clear() {
    for (Params &p : mParams) {
        p.present.store(false);
        p.on.store(true);
    }
    mBypass.store(false);
    mAuto.store(true);
    mTrimDb.store(0.0f);
}

void Equalizer::copyFrom(const Equalizer &o) {
    for (int b = 0; b < kMaxBands; ++b) {
        const Params &src = o.mParams[b];
        Params &dst = mParams[b];
        dst.type.store(src.type.load());
        dst.freq.store(src.freq.load());
        dst.gain.store(src.gain.load());
        dst.q.store(src.q.load());
        dst.on.store(src.on.load());
        dst.present.store(src.present.load());
    }
    mBypass.store(o.mBypass.load());
    mAuto.store(o.mAuto.load());
    recomputeTrim();
}

Biquad Equalizer::targetBiquad(int band) const {
    const Params &p = mParams[band];
    return designBiquad(p.type.load(), p.freq.load(), p.gain.load(), p.q.load(), mRate);
}

void Equalizer::response(int band, const float *freqs, float *outDb, int n) const {
    for (int i = 0; i < n; ++i) outDb[i] = 0.0f;
    if (band >= 0) {
        if (band >= kMaxBands || !mParams[band].present.load()) return;
        const Biquad c = targetBiquad(band);
        for (int i = 0; i < n; ++i) outDb[i] = static_cast<float>(biquadMagnitudeDb(c, freqs[i], mRate));
        return;
    }
    if (mBypass.load()) return;
    for (int b = 0; b < kMaxBands; ++b) {
        if (!mParams[b].present.load() || !mParams[b].on.load()) continue;
        const Biquad c = targetBiquad(b);
        for (int i = 0; i < n; ++i) outDb[i] += static_cast<float>(biquadMagnitudeDb(c, freqs[i], mRate));
    }
}

void Equalizer::recomputeTrim() {
    // Como el prototipo: el máximo de la curva entre 20 Hz y 20 kHz (192 puntos) se compensa.
    if (!mAuto.load() || mBypass.load()) {
        mTrimDb.store(0.0f);
        return;
    }
    constexpr int N = 192;
    float f[N], db[N];
    for (int i = 0; i < N; ++i) f[i] = static_cast<float>(20.0 * std::pow(1000.0, i / (N - 1.0)));
    response(-1, f, db, N);
    const float mx = *std::max_element(db, db + N);
    mTrimDb.store(-std::max(0.0f, mx));
}

// ---------------------------------------------------------------- audio

void Equalizer::resetState() {
    for (State &s : mState) s = State();
    mTrimS = std::pow(10.0, mTrimDb.load() / 20);
}

void Equalizer::process(float *l, float *r, int n) {
    const bool bypass = mBypass.load(std::memory_order_relaxed);
    // Los parámetros se suavizan como setTargetAtTime(…, 0.01) del prototipo, por bloque.
    const double c = 1.0 - std::exp(-n / (0.01 * mRate));
    bool any = false;
    for (int b = 0; b < kMaxBands; ++b) {
        const Params &p = mParams[b];
        State &s = mState[b];
        const bool want = !bypass && p.present.load(std::memory_order_relaxed) && p.on.load(std::memory_order_relaxed);
        if (!want) {
            s.active = false;
            continue;
        }
        const int type = p.type.load(std::memory_order_relaxed);
        const double tf = clampFreq(p.freq.load(std::memory_order_relaxed), mRate);
        const double tg = p.gain.load(std::memory_order_relaxed);
        const double tq = clampQ(p.q.load(std::memory_order_relaxed));
        bool dirty = false;
        if (!s.active) {  // banda nueva o recién encendida: empieza limpia
            s = State();
            s.active = true;
            s.type = type;
            s.f = tf;
            s.g = tg;
            s.q = tq;
            dirty = true;
        } else {
            if (s.type != type) {
                s.type = type;
                dirty = true;
            }
            // Frecuencia y Q se suavizan en escala logarítmica (como se oyen).
            if (std::fabs(std::log(tf / s.f)) > 1e-5) { s.f *= std::pow(tf / s.f, c); dirty = true; }
            else if (s.f != tf) { s.f = tf; dirty = true; }
            if (std::fabs(tg - s.g) > 1e-4) { s.g += (tg - s.g) * c; dirty = true; }
            else if (s.g != tg) { s.g = tg; dirty = true; }
            if (std::fabs(std::log(tq / s.q)) > 1e-5) { s.q *= std::pow(tq / s.q, c); dirty = true; }
            else if (s.q != tq) { s.q = tq; dirty = true; }
        }
        if (dirty) s.c = designBiquad(s.type, s.f, s.g, s.q, mRate);
        any = true;

        // Forma directa II transpuesta, en doble precisión (estable también en graves).
        float *ch[2] = {l, r};
        for (int k = 0; k < 2; ++k) {
            float *x = ch[k];
            if (!x) continue;
            double z1 = s.z1[k], z2 = s.z2[k];
            const Biquad &q = s.c;
            for (int i = 0; i < n; ++i) {
                const double in = x[i];
                const double y = q.b0 * in + z1;
                z1 = q.b1 * in - q.a1 * y + z2;
                z2 = q.b2 * in - q.a2 * y;
                x[i] = static_cast<float>(y);
            }
            if (std::fabs(z1) < 1e-25) z1 = 0;
            if (std::fabs(z2) < 1e-25) z2 = 0;
            s.z1[k] = z1;
            s.z2[k] = z2;
        }
    }

    // Auto-ganancia (constante de tiempo de 20 ms, como el prototipo).
    const double trimT = std::pow(10.0, mTrimDb.load(std::memory_order_relaxed) / 20);
    if (!any && std::fabs(trimT - mTrimS) < 1e-9) return;
    const double ct = 1.0 - std::exp(-1.0 / (0.02 * mRate));
    for (int i = 0; i < n; ++i) {
        mTrimS += (trimT - mTrimS) * ct;
        l[i] = static_cast<float>(l[i] * mTrimS);
        if (r) r[i] = static_cast<float>(r[i] * mTrimS);
    }
    if (std::fabs(trimT - mTrimS) < 1e-9) mTrimS = trimT;
}
