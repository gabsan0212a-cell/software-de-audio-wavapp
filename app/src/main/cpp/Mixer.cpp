#include "Mixer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace {

constexpr float kHalfPi = 1.57079632679489661923f;

// Constante de tiempo como setTargetAtTime: v += (objetivo - v) * coef en cada muestra.
float smoothCoef(float tauSec, int sampleRate) { return 1.0f - std::exp(-1.0f / (tauSec * sampleRate)); }

void atomicMax(std::atomic<float> &a, float v) {
    float old = a.load(std::memory_order_relaxed);
    while (v > old && !a.compare_exchange_weak(old, v, std::memory_order_relaxed)) {}
}

// Matriz de paneo de StereoPannerNode (especificación de Web Audio).
// Estéreo: L' = a·L + b·R, R' = c·L + d·R.  Mono: L' = a·x, R' = c·x.
struct PanMatrix {
    float a, b, c, d;
};

PanMatrix panMatrix(float pan, bool mono) {
    pan = std::clamp(pan, -1.0f, 1.0f);
    if (mono) {
        const float x = (pan + 1.0f) * 0.5f;
        return {std::cos(x * kHalfPi), 0.0f, std::sin(x * kHalfPi), 0.0f};
    }
    if (pan <= 0.0f) {
        const float x = pan + 1.0f;
        return {1.0f, std::cos(x * kHalfPi), 0.0f, std::sin(x * kHalfPi)};
    }
    return {std::cos(pan * kHalfPi), 0.0f, std::sin(pan * kHalfPi), 1.0f};
}

}  // namespace

Mixer::Mixer(int sampleRate) : mSampleRate(sampleRate) {
    // Mismos tiempos que el prototipo: volumen y paneo 15 ms, mute/solo 10 ms,
    // polaridad 5 ms, master 20 ms.
    mCoefGain = smoothCoef(0.015f, sampleRate);
    mCoefPan = smoothCoef(0.015f, sampleRate);
    mCoefOn = smoothCoef(0.010f, sampleRate);
    mCoefInv = smoothCoef(0.005f, sampleRate);
    mCoefMaster = smoothCoef(0.020f, sampleRate);
    mTauPanFrames = 0.015f * sampleRate;
    mLimiter.setup(sampleRate);
}

Mixer::~Mixer() = default;

int Mixer::freeSlot() const {
    std::lock_guard<std::mutex> lk(mMutex);
    for (int i = 0; i < kMaxTracks; ++i)
        if (!mSlots[i].owned) return i;
    return -1;
}

bool Mixer::hasTrack(int slot) const {
    if (slot < 0 || slot >= kMaxTracks) return false;
    std::lock_guard<std::mutex> lk(mMutex);
    return mSlots[slot].owned != nullptr;
}

bool Mixer::setTrack(int slot, std::unique_ptr<MappedAudio> audio) {
    if (slot < 0 || slot >= kMaxTracks || !audio) return false;
    std::lock_guard<std::mutex> lk(mMutex);
    Slot &s = mSlots[slot];
    if (s.owned) return false;
    s.owned = std::move(audio);
    s.gain.store(1.0f);
    s.pan.store(0.0f);
    s.mute.store(false);
    s.solo.store(false);
    s.invert.store(false);
    s.removing.store(false);
    s.peakL.store(0.0f);
    s.peakR.store(0.0f);
    s.gen.fetch_add(1, std::memory_order_relaxed);
    s.audio.store(s.owned.get(), std::memory_order_release);
    updateLength();
    return true;
}

std::unique_ptr<MappedAudio> Mixer::takeTrack(int slot) {
    if (slot < 0 || slot >= kMaxTracks) return nullptr;
    std::lock_guard<std::mutex> lk(mMutex);
    Slot &s = mSlots[slot];
    if (!s.owned) return nullptr;
    // Fundido de salida (~10 ms de constante) antes de quitarla, para que no truene.
    s.removing.store(true);
    if (mRolling.load()) std::this_thread::sleep_for(std::chrono::milliseconds(40));
    s.audio.store(nullptr);
    std::unique_ptr<MappedAudio> a = std::move(s.owned);
    if (!waitForAudioThread()) {
        // El hilo de audio no respondió: no liberamos para no arriesgar un cierre.
        a.release();
    }
    s.removing.store(false);
    updateLength();
    return a;
}

bool Mixer::waitForAudioThread() {
    // Un callback que empezó antes de quitar la pista podría seguir leyéndola: esperamos
    // a que termine. Los callbacks siguientes ya ven el puntero vacío.
    const uint64_t c = mCallbacks.load();
    if ((c & 1) == 0) return true;
    for (int i = 0; i < 500; ++i) {
        if (mCallbacks.load() != c) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

void Mixer::updateLength() {
    int64_t len = 0;
    for (const Slot &s : mSlots)
        if (s.owned) len = std::max(len, s.owned->frames());
    mLength.store(len);
}

void Mixer::setTrackGain(int slot, float gain) {
    if (slot >= 0 && slot < kMaxTracks) mSlots[slot].gain.store(std::max(0.0f, gain));
}
void Mixer::setTrackPan(int slot, float pan) {
    if (slot >= 0 && slot < kMaxTracks) mSlots[slot].pan.store(std::clamp(pan, -1.0f, 1.0f));
}
void Mixer::setTrackMute(int slot, bool on) {
    if (slot >= 0 && slot < kMaxTracks) mSlots[slot].mute.store(on);
}
void Mixer::setTrackSolo(int slot, bool on) {
    if (slot >= 0 && slot < kMaxTracks) mSlots[slot].solo.store(on);
}
void Mixer::setTrackInvert(int slot, bool on) {
    if (slot >= 0 && slot < kMaxTracks) mSlots[slot].invert.store(on);
}
void Mixer::setMasterGain(float gain) { mMasterGain.store(std::max(0.0f, gain)); }
void Mixer::setLimiter(bool on) { mLimiterOn.store(on); }

void Mixer::play() {
    if (mLength.load() > 0) mPlaying.store(true);
}
void Mixer::pause() { mPlaying.store(false); }
void Mixer::stop() {
    mPlaying.store(false);
    mSeekTarget.store(0);
}
void Mixer::seek(int64_t frame) { mSeekTarget.store(std::clamp<int64_t>(frame, 0, mLength.load())); }

int64_t Mixer::position() const {
    const int64_t s = mSeekTarget.load();
    return s >= 0 ? s : mPosition.load();
}

void Mixer::readMeters(float *out) {
    out[0] = mPeakL.exchange(0.0f);
    out[1] = mPeakR.exchange(0.0f);
    out[2] = mReduction.load();
    for (int i = 0; i < kMaxTracks; ++i) {
        out[3 + 2 * i] = mSlots[i].peakL.exchange(0.0f);
        out[4 + 2 * i] = mSlots[i].peakR.exchange(0.0f);
    }
}

void Mixer::prefetch() {
    std::lock_guard<std::mutex> lk(mMutex);
    const int64_t pos = position();
    for (const Slot &s : mSlots)
        if (s.owned) s.owned->prefetch(pos, mSampleRate * 3);
}

void Mixer::process(float *out, int frames, int channels) {
    mCallbacks.fetch_add(1);  // impar: en curso
    int done = 0;
    while (done < frames) {
        const int n = std::min(kBlock, frames - done);
        renderBlock(n);
        float *o = out + static_cast<size_t>(done) * channels;
        if (channels == 2) {
            for (int i = 0; i < n; ++i) {
                o[2 * i] = mMixL[i];
                o[2 * i + 1] = mMixR[i];
            }
        } else {
            for (int i = 0; i < n; ++i) {
                float *f = o + static_cast<size_t>(i) * channels;
                if (channels == 1) {
                    f[0] = 0.5f * (mMixL[i] + mMixR[i]);
                } else {
                    f[0] = mMixL[i];
                    f[1] = mMixR[i];
                    for (int c = 2; c < channels; ++c) f[c] = 0.0f;
                }
            }
        }
        done += n;
    }
    mPosition.store(mPos);
    mCallbacks.fetch_add(1);  // par: libre
}

void Mixer::renderBlock(int n) {
    const int64_t length = mLength.load(std::memory_order_relaxed);
    bool wantPlay = mPlaying.load(std::memory_order_relaxed);
    int64_t seek = mSeekTarget.load();

    // Un salto se aplica cuando la rampa anti-clic llegó a 0.
    if (seek >= 0 && mTransport == 0.0f) {
        mPos = std::clamp<int64_t>(seek, 0, length);
        if (mSeekTarget.compare_exchange_strong(seek, -1)) seek = -1;
    }
    if (wantPlay && length == 0) {
        bool expected = true;
        mPlaying.compare_exchange_strong(expected, false);
        wantPlay = false;
    }

    const float target = (wantPlay && seek < 0) ? 1.0f : 0.0f;
    const bool rolling = mTransport > 0.0f || target > 0.0f;
    mRolling.store(rolling, std::memory_order_relaxed);
    if (!rolling) {
        std::fill(mMixL, mMixL + n, 0.0f);
        std::fill(mMixR, mMixR + n, 0.0f);
        mLimiter.reset();
        mReduction.store(0.0f, std::memory_order_relaxed);
        return;
    }

    // Rampa del transporte: ~5 ms de 0 a 1.
    constexpr float kStep = 1.0f / 256.0f;
    for (int i = 0; i < n; ++i) {
        if (mTransport < target) mTransport = std::min(mTransport + kStep, target);
        else if (mTransport > target) mTransport = std::max(mTransport - kStep, target);
        mRamp[i] = mTransport;
    }

    std::fill(mMixL, mMixL + n, 0.0f);
    std::fill(mMixR, mMixR + n, 0.0f);

    bool anySolo = false;
    for (const Slot &s : mSlots)
        if (s.audio.load(std::memory_order_relaxed) && s.solo.load(std::memory_order_relaxed)) anySolo = true;

    for (Slot &s : mSlots) {
        const MappedAudio *a = s.audio.load(std::memory_order_acquire);
        if (!a) continue;
        const float gainT = s.gain.load(std::memory_order_relaxed);
        const float panT = s.pan.load(std::memory_order_relaxed);
        const float invT = s.invert.load(std::memory_order_relaxed) ? -1.0f : 1.0f;
        const bool on = !s.mute.load(std::memory_order_relaxed) &&
                        (!anySolo || s.solo.load(std::memory_order_relaxed)) &&
                        !s.removing.load(std::memory_order_relaxed);
        const float onT = on ? 1.0f : 0.0f;
        const uint32_t gen = s.gen.load(std::memory_order_relaxed);
        if (gen != s.seenGen) {  // pista nueva en este espacio: entra con fundido
            s.seenGen = gen;
            s.gainS = gainT;
            s.panS = panT;
            s.invS = invT;
            s.onS = 0.0f;
        }

        // El paneo se suaviza por bloque y la matriz se interpola dentro del bloque.
        const bool mono = a->channels() == 1;
        const float panEnd = panT + (s.panS - panT) * std::exp(-n / mTauPanFrames);
        const PanMatrix m0 = panMatrix(s.panS, mono), m1 = panMatrix(panEnd, mono);
        const float inv = 1.0f / n;
        const PanMatrix dm = {(m1.a - m0.a) * inv, (m1.b - m0.b) * inv, (m1.c - m0.c) * inv, (m1.d - m0.d) * inv};
        s.panS = panEnd;

        const float *data = a->data();
        const int64_t frames = a->frames();
        float pkL = 0.0f, pkR = 0.0f;
        float g = s.gainS, onS = s.onS, invS = s.invS;
        for (int i = 0; i < n; ++i) {
            g += (gainT - g) * mCoefGain;
            onS += (onT - onS) * mCoefOn;
            invS += (invT - invS) * mCoefInv;
            const int64_t f = mPos + i;
            if (f >= frames) continue;
            const float k = g * onS * invS * mRamp[i];
            const float fi = static_cast<float>(i);
            float l, r;
            if (mono) {
                const float x = data[f];
                l = x * (m0.a + dm.a * fi);
                r = x * (m0.c + dm.c * fi);
            } else {
                const float xl = data[2 * f], xr = data[2 * f + 1];
                l = xl * (m0.a + dm.a * fi) + xr * (m0.b + dm.b * fi);
                r = xl * (m0.c + dm.c * fi) + xr * (m0.d + dm.d * fi);
            }
            l *= k;
            r *= k;
            mMixL[i] += l;
            mMixR[i] += r;
            pkL = std::max(pkL, std::fabs(l));
            pkR = std::max(pkR, std::fabs(r));
        }
        // Al llegar (casi) al objetivo se iguala: evita números diminutos (denormales) que
        // en algunos procesadores vuelven lento el cálculo.
        s.gainS = std::fabs(gainT - g) < 1e-7f ? gainT : g;
        s.onS = std::fabs(onT - onS) < 1e-7f ? onT : onS;
        s.invS = std::fabs(invT - invS) < 1e-7f ? invT : invS;
        if (std::fabs(panT - s.panS) < 1e-7f) s.panS = panT;
        atomicMax(s.peakL, pkL);
        atomicMax(s.peakR, pkR);
    }

    // Master: volumen → limitador → recorte suave.
    const float masterT = mMasterGain.load(std::memory_order_relaxed);
    const bool limiterOn = mLimiterOn.load(std::memory_order_relaxed);
    if (limiterOn && !mLimiterWasOn) mLimiter.reset();
    mLimiterWasOn = limiterOn;
    float pkL = 0.0f, pkR = 0.0f;
    for (int i = 0; i < n; ++i) {
        mMasterS += (masterT - mMasterS) * mCoefMaster;
        float l = mMixL[i] * mMasterS, r = mMixR[i] * mMasterS;
        if (limiterOn) {
            mLimiter.process(l, r);
            l = softClip(l);
            r = softClip(r);
        }
        mMixL[i] = l;
        mMixR[i] = r;
        pkL = std::max(pkL, std::fabs(l));
        pkR = std::max(pkR, std::fabs(r));
    }
    if (std::fabs(masterT - mMasterS) < 1e-7f) mMasterS = masterT;
    atomicMax(mPeakL, pkL);
    atomicMax(mPeakR, pkR);
    mReduction.store(limiterOn ? mLimiter.reductionDb() : 0.0f, std::memory_order_relaxed);

    mPos += n;
    if (mPos >= length) {  // fin de la canción: se detiene y vuelve al inicio
        bool expected = true;
        mPlaying.compare_exchange_strong(expected, false);
        mPos = 0;
        mTransport = 0.0f;
    }
}
