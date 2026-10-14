#include "dsp/DriftFifo.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int64_t kWindow = 4800;  // el control mira el peor momento de cada ~100 ms
constexpr double kGain = 2e-5;     // velocidad extra por cuadro de reserva de más
constexpr int kSkipFrames = 960;   // más de ~20 ms de más: se salta (con fundido cruzado)
constexpr int kFade = 96;          // ~2 ms de fundido

}  // namespace

DriftFifo::DriftFifo(int channels, int capacityFrames)
    : mCh(std::max(1, channels)), mCap(std::max(256, capacityFrames)),
      mBuf(static_cast<size_t>(mCap) * static_cast<size_t>(mCh), 0.0f) {
    reset();
}

void DriftFifo::reset() {
    std::fill(mBuf.begin(), mBuf.begin() + mCh, 0.0f);  // un cuadro de historia (para interpolar)
    mFill = 1;
    mPos = 1.0;
    mRatio = mRatioTarget = 1.0;
    mRunning = false;
    mGain = 0.0f;
    mXfade = 0;
    mWindow = 0;
    mMinLevel = 1e18;
    mMaxPush = 0;
}

void DriftFifo::compact() {
    double first = mPos;
    if (mXfade > 0) first = std::min(first, mOldPos);
    const int64_t keepFrom = std::max<int64_t>(0, static_cast<int64_t>(std::floor(first)) - 1);
    if (keepFrom <= 0) return;
    std::memmove(mBuf.data(), mBuf.data() + keepFrom * mCh,
                 sizeof(float) * static_cast<size_t>((mFill - keepFrom) * mCh));
    mFill -= keepFrom;
    mPos -= static_cast<double>(keepFrom);
    mOldPos -= static_cast<double>(keepFrom);
}

void DriftFifo::push(const float *x, int frames) {
    if (frames <= 0) return;
    mMaxPush = std::max(mMaxPush, std::min(frames, mCap / 4));
    if (frames >= mCap - 4) {  // más de lo que cabe: solo lo más nuevo
        x += static_cast<size_t>(frames - (mCap - 4)) * mCh;
        frames = mCap - 4;
    }
    if (mFill + frames > mCap) compact();
    if (mFill + frames > mCap) {
        // Ni así cabe (la salida no está leyendo): se tira lo más viejo.
        const int64_t drop = mFill + frames - mCap;
        std::memmove(mBuf.data(), mBuf.data() + drop * mCh, sizeof(float) * static_cast<size_t>((mFill - drop) * mCh));
        mFill -= drop;
        mPos = std::max(1.0, mPos - static_cast<double>(drop));
        mXfade = 0;
        ++mSkips;
    }
    std::memcpy(mBuf.data() + mFill * mCh, x, sizeof(float) * static_cast<size_t>(frames) * mCh);
    mFill += frames;
}

void DriftFifo::interp(double pos, float *dst) const {
    const int64_t i = static_cast<int64_t>(pos);
    const float t = static_cast<float>(pos - static_cast<double>(i));
    for (int c = 0; c < mCh; ++c) {
        const float p0 = sample(i - 1, c), p1 = sample(i, c), p2 = sample(i + 1, c), p3 = sample(i + 2, c);
        dst[c] = p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
    }
}

int DriftFifo::pull(float *out, int frames) {
    std::fill(out, out + static_cast<size_t>(frames) * mCh, 0.0f);
    if (frames <= 0) return 0;
    const double ahead = static_cast<double>(mFill) - 2.0 - mPos;  // lo que se puede interpolar
    if (!mRunning) {
        // Juntando: arranca cuando alcanza para esta lectura, la reserva y una ráfaga completa de
        // la entrada (con fundido de entrada). Lo que sobre lo quita el control en unos segundos.
        if (ahead < frames * mRatio + mSafety + mMaxPush) return 0;
        mRunning = true;
        mGain = 0.0f;
        mWindow = 0;
        mMinLevel = 1e18;
    }
    // ¿Alcanza? Si no, lo que hay con fundido de salida y a juntar de nuevo.
    int n = frames;
    bool falta = false;
    const double needed = (frames - 1) * mRatio;
    if (ahead < needed) {
        n = ahead > 0.0 ? static_cast<int>(ahead / mRatio) : 0;
        falta = true;
    }
    const float step = 1.0f / kFade;
    float tmp[8];
    for (int i = 0; i < n; ++i) {
        float g;
        if (falta) {
            g = std::min(mGain, static_cast<float>(n - i) / static_cast<float>(std::min(n, kFade)));
        } else {
            mGain = std::min(1.0f, mGain + step);
            g = mGain;
        }
        float *o = out + static_cast<size_t>(i) * mCh;
        interp(mPos, o);
        if (mXfade > 0) {
            const float a = 1.0f - static_cast<float>(mXfade) / kFade;  // de lo viejo a lo nuevo
            interp(mOldPos, tmp);
            for (int c = 0; c < mCh; ++c) o[c] = o[c] * a + tmp[c] * (1.0f - a);
            mOldPos += mRatio;
            --mXfade;
        }
        for (int c = 0; c < mCh; ++c) o[c] *= g;
        mPos += mRatio;
    }
    if (falta) {
        ++mUnderruns;
        mSafety = std::min(mSafety + 32, kMaxSafety);  // faltó: un poco más de reserva de aquí en adelante
        mRunning = false;
        mGain = 0.0f;
        mXfade = 0;
        return n;
    }

    // Control: el peor momento (lo que queda justo después de leer) de cada ventana.
    const double level = static_cast<double>(mFill) - mPos;
    mMinLevel = std::min(mMinLevel, level);
    mWindow += frames;
    if (mWindow >= kWindow) {
        const double e = mMinLevel - mSafety;
        if (e > kSkipFrames && mXfade == 0) {
            // Sobra mucho (la app se trabó un momento): salta hasta la reserva, con fundido cruzado.
            mOldPos = mPos;
            mPos += e;
            mXfade = kFade;
            ++mSkips;
        } else {
            mRatioTarget = 1.0 + std::clamp(e * kGain, -kMaxAdjust, kMaxAdjust);
        }
        mWindow = 0;
        mMinLevel = 1e18;
    }
    mRatio += (mRatioTarget - mRatio) * 0.25;
    return n;
}
