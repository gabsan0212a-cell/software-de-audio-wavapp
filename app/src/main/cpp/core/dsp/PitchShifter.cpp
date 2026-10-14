#include "dsp/PitchShifter.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMargin = 16;          // histéresis entre saltar atrás y saltar adelante
constexpr double kNoNoteMs = 10.0;   // salto cuando no hay nota (ruido, consonantes)

}  // namespace

PitchShifter::PitchShifter(int sampleRate) : mRate(sampleRate) {
    int64_t size = 1;
    while (size < sampleRate / 4) size <<= 1;  // ≥ 250 ms
    mBuf.assign(static_cast<size_t>(size), 0.0f);
    mMask = size - 1;
    reset();
}

void PitchShifter::reset() {
    std::fill(mBuf.begin(), mBuf.end(), 0.0f);
    mWrite = 0;
    mRead = -static_cast<double>(kMinDelay + kMargin);
    mRatio = 1.0f;
    mFade = mFadeLen = 0;
}

float PitchShifter::at(double pos) const {
    // Interpolación cúbica (Catmull-Rom) entre las 4 muestras de alrededor.
    const double fl = std::floor(pos);
    const int64_t i = static_cast<int64_t>(fl);
    const float t = static_cast<float>(pos - fl);
    const float xm1 = sample(i - 1), x0 = sample(i), x1 = sample(i + 1), x2 = sample(i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

double PitchShifter::bestJump(double jump, bool back) const {
    // Compara lo último que sonó con lo que sonaría después del salto, para saltos alrededor
    // del período, y se queda con el que más se parece (con decimales).
    const int64_t p = static_cast<int64_t>(std::floor(mRead));
    const int len = std::clamp(static_cast<int>(jump), 64, 384);
    const int span = std::max(2, static_cast<int>(jump / 10.0));
    int lo = static_cast<int>(std::floor(jump)) - span, hi = static_cast<int>(std::ceil(jump)) + span;
    lo = std::max(lo, 8);
    if (!back) hi = std::min<int64_t>(hi, mWrite - 4 - p);  // lo que ya entró
    if (hi - lo < 2) return jump;
    double ea = 0.0;
    for (int k = 0; k < len; ++k) {
        const float a = sample(p - k);
        ea += static_cast<double>(a) * a;
    }
    if (ea < 1e-10) return jump;  // casi silencio: da igual
    double best = -2.0, cm = 0.0, cp = 0.0;
    int bestJ = -1;
    double prev = -2.0;
    for (int j = lo; j <= hi; ++j) {
        const int64_t q = back ? p - j : p + j;
        double ab = 0.0, eb = 0.0;
        for (int k = 0; k < len; ++k) {
            const float a = sample(p - k), b = sample(q - k);
            ab += static_cast<double>(a) * b;
            eb += static_cast<double>(b) * b;
        }
        const double c = eb > 1e-12 ? ab / std::sqrt(ea * eb) : -1.0;
        if (c > best) {
            best = c;
            bestJ = j;
            cm = prev;
            cp = -2.0;
        } else if (j == bestJ + 1) {
            cp = c;
        }
        prev = c;
    }
    if (bestJ < 0) return jump;
    double shift = 0.0;
    if (bestJ > lo && bestJ < hi && cm > -2.0 && cp > -2.0) {
        const double den = cm - 2.0 * best + cp;
        if (den < -1e-12) shift = std::clamp(0.5 * (cm - cp) / den, -0.5, 0.5);
    }
    return bestJ + shift;
}

void PitchShifter::startSplice(double jump, bool back, bool voiced, float ratio) {
    const double d = static_cast<double>(mWrite - 1) - mRead;
    if (voiced) jump = bestJump(jump, back);
    int len = voiced ? std::clamp(static_cast<int>(jump * 0.5), 32, 256) : static_cast<int>(0.005 * mRate);
    // Mientras dura el fundido, la lectura vieja no puede alcanzar a lo que entra.
    const double from = back ? d : d - jump;  // retraso de la lectura que más rápido se acerca
    if (ratio > 1.0f) len = std::min(len, static_cast<int>((std::min(d, from) - 3.0) / (ratio - 1.0f)));
    len = std::max(len, 8);
    mRead2 = back ? mRead - jump : mRead + jump;
    mFade = mFadeLen = len;
    mFadePower = !voiced;
}

void PitchShifter::process(const float *in, float *out, int n, float ratio, float period, bool quiet) {
    if (n <= 0) return;
    const float r0 = mRatio, dr = (ratio - r0) / static_cast<float>(n);
    const bool voiced = period > 0.0f;
    const double jump = voiced ? static_cast<double>(period) : kNoNoteMs * 0.001 * mRate;
    const double maxJump = static_cast<double>(mBuf.size()) / 4.0;
    for (int i = 0; i < n; ++i) {
        mBuf[static_cast<size_t>(mWrite & mMask)] = in[i];
        ++mWrite;
        const float rt = r0 + dr * static_cast<float>(i + 1);
        if (mFade == 0) {
            const double d = static_cast<double>(mWrite - 1) - mRead;
            const double j = std::min(jump, maxJump);
            if (d < kMinDelay) {
                startSplice(j, true, voiced, rt);
            } else if (quiet && d > kMinDelay + 24) {
                // Silencio: se recupera el retraso mínimo (el salto no se oye).
                mRead2 = mRead + (d - (kMinDelay + 8));
                mFade = mFadeLen = 64;
                mFadePower = false;
            } else if (d > kMinDelay + j + kMargin) {
                startSplice(j, false, voiced, rt);
            }
        }
        float y = at(mRead);
        mRead += rt;
        if (mFade > 0) {
            const float t = static_cast<float>(mFadeLen - mFade + 1) / static_cast<float>(mFadeLen + 1);
            float wa, wb;
            if (mFadePower) {
                wa = std::cos(t * 1.5707963f);
                wb = std::sin(t * 1.5707963f);
            } else {
                wb = 0.5f - 0.5f * std::cos(t * 3.14159265f);
                wa = 1.0f - wb;
            }
            y = wa * y + wb * at(mRead2);
            mRead2 += rt;
            if (--mFade == 0) mRead = mRead2;
        }
        out[i] = y;
    }
    mRatio = ratio;
}
