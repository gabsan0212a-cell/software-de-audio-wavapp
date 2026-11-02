#include "engine/MediaListen.h"

#include <algorithm>
#include <cmath>

#include "engine/Mixer.h"

namespace {

constexpr float kSoundFloor = 1e-4f;  // -80 dBFS: por debajo es silencio

void raiseTo(std::atomic<float> &a, float v) {
    float cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}

}  // namespace

MediaListen::MediaListen(Mixer &mixer, int sampleRate) : mMixer(mixer), mRate(sampleRate), mFront(sampleRate) {}

void MediaListen::setActive(bool on, int channels) {
    if (on) {
        mChannels.store(std::clamp(channels, 1, 2));
        mFrames.store(0);
        mLastSound.store(0);
        mPeakL.store(0.0f);
        mPeakR.store(0.0f);
    }
    mActive.store(on);
}

void MediaListen::readState(float *out) {
    const bool on = mActive.load();
    out[0] = on ? 1.0f : 0.0f;
    out[1] = mPeakL.exchange(0.0f, std::memory_order_relaxed);
    out[2] = mPeakR.exchange(0.0f, std::memory_order_relaxed);
    const int64_t silent = on ? mFrames.load() - mLastSound.load() : 0;
    out[3] = static_cast<float>(static_cast<double>(std::max<int64_t>(0, silent)) / mRate);
}

int MediaListen::readLoudness(LoudnessBlock *out, int max) {
    const uint64_t r = mLoudRead.load(std::memory_order_relaxed);
    const uint64_t w = mLoudWrite.load(std::memory_order_acquire);
    const int n = static_cast<int>(std::min<uint64_t>(w - r, static_cast<uint64_t>(max)));
    for (int i = 0; i < n; ++i) out[i] = mLoudBuf[(r + i) % kLoudRing];
    mLoudRead.store(r + n, std::memory_order_release);
    return n;
}

void MediaListen::push(const int16_t *pcm, int frames) {
    if (!mActive.load(std::memory_order_relaxed) || !pcm || frames <= 0) return;
    const int ch = mChannels.load(std::memory_order_relaxed);
    const uint32_t gen = mLoudGenReq.load(std::memory_order_relaxed);
    if (gen != mLoudGen) {  // medición nueva: los filtros empiezan de cero
        mLoudGen = gen;
        mFront.reset();
    }
    constexpr float kScale = 1.0f / 32768.0f;
    for (int done = 0; done < frames;) {
        const int n = std::min(kChunk, frames - done);
        const int16_t *p = pcm + static_cast<size_t>(done) * ch;
        float pl = 0.0f, pr = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float l = p[i * ch] * kScale;
            const float r = ch == 2 ? p[i * ch + 1] * kScale : l;
            mL[i] = l;
            mR[i] = r;
            pl = std::max(pl, std::fabs(l));
            pr = std::max(pr, std::fabs(r));
        }
        raiseTo(mPeakL, pl);
        raiseTo(mPeakR, pr);
        const int64_t f = mFrames.load(std::memory_order_relaxed) + n;
        mFrames.store(f, std::memory_order_relaxed);
        if (std::max(pl, pr) > kSoundFloor) mLastSound.store(f, std::memory_order_relaxed);
        mMixer.writeMediaTap(mL, mR, n);
        LoudnessBlock b[2];
        const int k = mFront.process(mL, mR, n, b, 2);
        for (int i = 0; i < k; ++i) {
            const uint64_t w = mLoudWrite.load(std::memory_order_relaxed);
            if (w - mLoudRead.load(std::memory_order_acquire) >= kLoudRing) break;  // nadie lee: se pierden
            b[i].gen = gen;
            mLoudBuf[w % kLoudRing] = b[i];
            mLoudWrite.store(w + 1, std::memory_order_release);
        }
        done += n;
    }
}
