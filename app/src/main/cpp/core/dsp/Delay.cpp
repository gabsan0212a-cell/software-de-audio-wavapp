#include "dsp/Delay.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
}

void DelayLine::allocate(int maxDelay) {
    int size = 16;
    while (size < maxDelay + 4) size <<= 1;
    mBuf.assign(static_cast<size_t>(size), 0.0f);
    mSize = size;
    mMask = size - 1;
    mPos = 0;
    mValid = 0;
}

void StereoDelay::allocate(int sampleRate, float maxMs) {
    mRate = sampleRate;
    const int max = static_cast<int>(maxMs * 1e-3f * sampleRate) + 4;
    mLine[0].allocate(max);
    mLine[1].allocate(max);
    restart();
}

void StereoDelay::restart() {
    mLine[0].restart();
    mLine[1].restart();
    mDelay = -1.0f;
    mLp[0] = mLp[1] = mLow[0] = mLow[1] = 0.0f;
}

void StereoDelay::process(const float *l, const float *r, int n, float *outL, float *outR, const DelaySettings &s) {
    const float target =
        std::clamp(s.timeMs * 1e-3f * mRate, 2.0f, static_cast<float>(mLine[0].maxDelay() - 2));
    if (mDelay < 0.0f) mDelay = target;
    const float glide = 1.0f - std::exp(-1.0f / (0.08f * mRate));  // ~80 ms
    const float lpA = std::exp(-kTwoPi * std::clamp(s.toneHz, 200.0f, 0.45f * mRate) / mRate);
    const float lowA = std::exp(-kTwoPi * 90.0f / mRate);
    const float fb = std::clamp(s.feedback, 0.0f, 0.95f);
    for (int i = 0; i < n; ++i) {
        mDelay += (target - mDelay) * glide;
        float y[2];
        for (int c = 0; c < 2; ++c) {
            const float x = mLine[c].readFrac(mDelay);
            mLp[c] = x + (mLp[c] - x) * lpA;           // brillo
            mLow[c] = mLp[c] + (mLow[c] - mLp[c]) * lowA;
            y[c] = mLp[c] - mLow[c];                     // sin graves que se acumulen
            if (std::fabs(y[c]) < 1e-20f) y[c] = 0.0f;  // los ecos se apagan en ceros
        }
        const float xl = l[i], xr = r ? r[i] : l[i];
        if (s.pingPong) {
            mLine[0].write(0.5f * (xl + xr) + fb * y[1]);
            mLine[1].write(fb * y[0]);
        } else {
            mLine[0].write(xl + fb * y[0]);
            mLine[1].write(xr + fb * y[1]);
        }
        outL[i] = y[0];
        outR[i] = y[1];
    }
    for (int c = 0; c < 2; ++c) {
        if (std::fabs(mLp[c]) < 1e-20f) mLp[c] = 0.0f;
        if (std::fabs(mLow[c]) < 1e-20f) mLow[c] = 0.0f;
    }
}
