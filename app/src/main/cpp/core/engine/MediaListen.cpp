#include "engine/MediaListen.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "engine/Mixer.h"

namespace {

constexpr float kSoundFloor = 1e-4f;  // -80 dBFS: por debajo es silencio

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void raiseTo(std::atomic<float> &a, float v) {
    float cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}

}  // namespace

MediaListen::MediaListen(Mixer &mixer, int sampleRate)
    : mMixer(mixer), mRate(sampleRate), mFront(sampleRate), mSnap(kMaxSnapshot, 0.0f) {}

void MediaListen::setActive(bool on, int channels, bool native) {
    if (on) {
        mChannels.store(native ? 1 : std::clamp(channels, 1, 2));
        mFrames.store(0);
        mLastSound.store(0);
        mPeakL.store(0.0f);
        mPeakR.store(0.0f);
        mRms.store(0.0f);
        const int64_t t = nowMs();
        mOpenMs.store(t);
        mLastSoundMs.store(t);
        std::lock_guard<std::mutex> lk(mSnapMutex);
        mSnapTaken = mSnapSeq;  // lo de antes no cuenta
    }
    mNative.store(on && native);
    mActive.store(on);
}

void MediaListen::readState(float *out) {
    const bool on = mActive.load();
    const bool native = on && mNative.load();
    out[0] = on ? 1.0f : 0.0f;
    // Medidores: el pico nuevo sube al instante y lo mostrado cae poco a poco.
    const int64_t t = nowMs();
    const float dt = mShowMs < 0 ? 0.0f : std::min(1.0f, static_cast<float>(t - mShowMs) / 1000.0f);
    mShowMs = t;
    auto db = [](float lin) { return lin > 1e-6f ? std::max(kFloorDb, 20.0f * std::log10(lin)) : kFloorDb; };
    auto show = [&](float &shown, float now, float fall) {
        shown = std::max(now, std::max(kFloorDb, shown - fall * dt));
        return on ? shown : kFloorDb;
    };
    out[1] = show(mShowL, db(mPeakL.exchange(0.0f, std::memory_order_relaxed)), 24.0f);
    out[2] = show(mShowR, db(mPeakR.exchange(0.0f, std::memory_order_relaxed)), 24.0f);
    if (native) {
        out[3] = static_cast<float>(std::max<int64_t>(0, nowMs() - mLastSoundMs.load())) / 1000.0f;
    } else {
        const int64_t silent = on ? mFrames.load() - mLastSound.load() : 0;
        out[3] = static_cast<float>(static_cast<double>(std::max<int64_t>(0, silent)) / mRate);
    }
    out[4] = native ? 1.0f : 0.0f;
    out[5] = show(mShowRms, on ? db(mRms.load(std::memory_order_relaxed)) : kFloorDb, 12.0f);
    if (!on) mShowL = mShowR = mShowRms = kFloorDb;
}

void MediaListen::pushSnapshot(const uint8_t *wave, int n, int sampleRate, float peakDb, float rmsDb) {
    if (!mActive.load(std::memory_order_relaxed) || !mNative.load(std::memory_order_relaxed) || !wave || n <= 0) return;
    n = std::min(n, kMaxSnapshot);
    float peak, rms;
    {
        std::lock_guard<std::mutex> lk(mSnapMutex);
        double s2 = 0;
        float pk = 0;
        for (int i = 0; i < n; ++i) {
            const float x = (static_cast<int>(wave[i]) - 128) * (1.0f / 128.0f);
            mSnap[static_cast<size_t>(i)] = x;
            s2 += static_cast<double>(x) * x;
            pk = std::max(pk, std::fabs(x));
        }
        mSnapN = n;
        mSnapRate = sampleRate > 0 ? sampleRate : mRate;
        ++mSnapSeq;
        // Sin el nivel de Android, el del pedazo (que Android ya escala: sirve para saber si suena).
        peak = peakDb > kNoLevel ? std::pow(10.0f, peakDb / 20.0f) : pk;
        rms = rmsDb > kNoLevel ? std::pow(10.0f, rmsDb / 20.0f) : static_cast<float>(std::sqrt(s2 / n));
    }
    raiseTo(mPeakL, peak);
    raiseTo(mPeakR, peak);
    mRms.store(rms, std::memory_order_relaxed);
    if (rms > 3e-4f) mLastSoundMs.store(nowMs(), std::memory_order_relaxed);  // -70 dB
}

bool MediaListen::takeSnapshot(float *mono, int &n, int &sampleRate, float &rms) {
    std::lock_guard<std::mutex> lk(mSnapMutex);
    if (mSnapSeq == mSnapTaken || mSnapN <= 0) return false;
    mSnapTaken = mSnapSeq;
    n = mSnapN;
    std::copy(mSnap.begin(), mSnap.begin() + n, mono);
    sampleRate = mSnapRate;
    rms = mRms.load(std::memory_order_relaxed);
    return true;
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
    if (!mActive.load(std::memory_order_relaxed) || mNative.load(std::memory_order_relaxed) || !pcm || frames <= 0) return;
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
