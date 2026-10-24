#include "inst/Live.h"

#include <algorithm>

namespace inst {

Live::Live(int sampleRate) : mEngine(sampleRate) {
    // Los presets se arman aquí (no la primera vez que suenan en el hilo de audio).
    for (int k = 0; k < kKindCount; ++k) (void)presetName(k, 0);
    std::fill(mL, mL + kBlock, 0.0f);
    std::fill(mR, mR + kBlock, 0.0f);
}

void Live::push(const Event &e) {
    std::lock_guard<std::mutex> lk(mPush);
    const uint32_t w = mWrite.load(std::memory_order_relaxed);
    if (w - mRead.load(std::memory_order_acquire) >= kQueue) return;  // llena: se pierde la nota
    mQueue[w % kQueue] = e;
    mWrite.store(w + 1, std::memory_order_release);
}

void Live::play(const Sound &s, int pitch, float velocity, float gain) {
    Event e;
    e.type = kOn;
    e.pitch = pitch;
    e.vel = std::clamp(velocity, 0.0f, 1.0f);
    e.gain = std::clamp(gain, 0.0f, 4.0f);
    e.sound = s;
    {
        std::lock_guard<std::mutex> lk(mPush);
        mSeed = mSeed * 1664525u + 1013904223u;
        e.seed = mSeed;
    }
    push(e);
}

void Live::release(int pitch) {
    Event e;
    e.type = kOff;
    e.pitch = pitch;
    push(e);
}

void Live::releaseAll() {
    Event e;
    e.type = kOffAll;
    push(e);
}

void Live::stop() {
    Event e;
    e.type = kStop;
    push(e);
}

bool Live::busy() const {
    return mActive.load(std::memory_order_relaxed) ||
           mWrite.load(std::memory_order_acquire) != mRead.load(std::memory_order_relaxed);
}

bool Live::mix(float *l, float *r, int n) {
    uint32_t rd = mRead.load(std::memory_order_relaxed);
    const uint32_t w = mWrite.load(std::memory_order_acquire);
    while (rd != w) {
        const Event &e = mQueue[rd % kQueue];
        switch (e.type) {
            case kOn:
                if (e.sound != mEngine.sound()) mEngine.setSound(e.sound);
                mGain = e.gain;
                mEngine.noteOn(e.pitch, e.vel, e.seed);
                break;
            case kOff: mEngine.noteOff(e.pitch); break;
            case kOffAll: mEngine.releaseAll(); break;
            default: mEngine.silence(); break;
        }
        ++rd;
    }
    mRead.store(rd, std::memory_order_release);
    if (!mEngine.active()) {
        mActive.store(false, std::memory_order_relaxed);
        return false;
    }
    mActive.store(true, std::memory_order_relaxed);
    for (int done = 0; done < n;) {
        const int m = std::min(kBlock, n - done);
        mEngine.render(mL, mR, m);
        for (int i = 0; i < m; ++i) {
            mGainS += (mGain - mGainS) * 0.002f;
            l[done + i] += mL[i] * mGainS;
            r[done + i] += mR[i] * mGainS;
        }
        done += m;
    }
    return true;
}

}  // namespace inst
