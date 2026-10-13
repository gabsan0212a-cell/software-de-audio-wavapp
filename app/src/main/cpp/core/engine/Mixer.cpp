#include "engine/Mixer.h"

#include "engine/LiveInput.h"

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

// Curva de los fundidos: sin² (un fundido de salida y uno de entrada encimados suman 1).
float fadeCurve(double x) {
    const double s = std::sin(x * 1.57079632679489661923);
    return static_cast<float>(s * s);
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
    mLookAhead = static_cast<int64_t>(std::lround(0.015 * sampleRate));
    mLimiter.setup(sampleRate);
    for (Slot &s : mSlots) s.eq.setSampleRate(sampleRate);
    mTapL.reset(new std::atomic<float>[kTapSize]());
    mTapR.reset(new std::atomic<float>[kTapSize]());
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
    s.eq.clear();
    s.removing.store(false);
    for (auto &h : s.hold) h.store(false);
    s.autos.store(nullptr);
    s.ownedAutos.reset();
    s.peakL.store(0.0f);
    s.peakR.store(0.0f);
    // Nadie lee los pedazos de un espacio vacío: se cambian sin esperar.
    Region whole;
    whole.length = s.owned->frames();
    s.ownedPieces = makePieces({whole}, s.owned->frames());
    s.pieces.store(s.ownedPieces.get(), std::memory_order_release);
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
    s.pieces.store(nullptr);
    s.autos.store(nullptr);
    std::unique_ptr<MappedAudio> a = std::move(s.owned);
    std::unique_ptr<PieceSet> ps = std::move(s.ownedPieces);
    std::unique_ptr<AutoSet> as = std::move(s.ownedAutos);
    if (!waitForAudioThread()) {
        // El hilo de audio no respondió: no liberamos para no arriesgar un cierre.
        a.release();
        ps.release();
        as.release();
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
        if (s.owned && s.ownedPieces) len = std::max(len, s.ownedPieces->end);
    mLength.store(len);
}

std::unique_ptr<Mixer::PieceSet> Mixer::makePieces(std::vector<Region> in, int64_t fileFrames) {
    auto ps = std::make_unique<PieceSet>();
    for (Region r : in) {
        if (static_cast<int>(ps->regions.size()) >= kMaxRegions) break;
        r.src = std::clamp<int64_t>(r.src, 0, fileFrames);
        r.start = std::max<int64_t>(0, r.start);
        r.length = std::min(r.length, fileFrames - r.src);
        if (r.length <= 0) continue;
        r.fadeIn = std::clamp<int64_t>(r.fadeIn, 0, r.length);
        r.fadeOut = std::clamp<int64_t>(r.fadeOut, 0, r.length);
        if (r.fadeIn + r.fadeOut > r.length) {  // se reparten lo que cabe
            const double k = static_cast<double>(r.length) / (r.fadeIn + r.fadeOut);
            r.fadeIn = static_cast<int64_t>(r.fadeIn * k);
            r.fadeOut = r.length - r.fadeIn;
        }
        ps->regions.push_back(r);
    }
    // Un borde cortado lleva un fundido mínimo, salvo que el audio siga justo en otro pedazo
    // (así cortar en dos sin mover nada suena igual que antes).
    auto continues = [&](const Region &a, const Region &b) {  // b sigue a a, sin hueco
        return a.start + a.length == b.start && a.src + a.length == b.src;
    };
    for (const Region &r : ps->regions) {
        bool cutIn = r.src > 0, cutOut = r.src + r.length < fileFrames;
        for (const Region &o : ps->regions) {
            if (&o == &r) continue;
            if (cutIn && continues(o, r)) cutIn = false;
            if (cutOut && continues(r, o)) cutOut = false;
        }
        const int64_t edge = std::min<int64_t>(kEdgeFade, r.length / 2);
        ps->pieces.push_back({r.start, r.src, r.length, std::max(r.fadeIn, cutIn ? edge : 0),
                              std::max(r.fadeOut, cutOut ? edge : 0)});
        ps->end = std::max(ps->end, r.start + r.length);
    }
    return ps;
}

void Mixer::publishPieces(Slot &s, std::unique_ptr<PieceSet> ps) {
    std::unique_ptr<PieceSet> old = std::move(s.ownedPieces);
    s.ownedPieces = std::move(ps);
    s.pieces.store(s.ownedPieces.get(), std::memory_order_release);
    // Un callback en curso podría estar leyendo los anteriores.
    if (!waitForAudioThread()) old.release();
}

bool Mixer::setRegions(int slot, const std::vector<Region> &regions) {
    if (slot < 0 || slot >= kMaxTracks) return false;
    std::lock_guard<std::mutex> lk(mMutex);
    Slot &s = mSlots[slot];
    if (!s.owned) return false;
    publishPieces(s, makePieces(regions, s.owned->frames()));
    updateLength();
    return true;
}

void Mixer::publishAutos(Slot &s, std::unique_ptr<AutoSet> as) {
    std::unique_ptr<AutoSet> old = std::move(s.ownedAutos);
    s.ownedAutos = std::move(as);
    s.autos.store(s.ownedAutos.get(), std::memory_order_release);
    if (!waitForAudioThread()) old.release();
}

void Mixer::setAutomation(int slot, int param, std::vector<autom::PosPoint> points) {
    if (slot < 0 || slot >= kMaxTracks || param < 0 || param >= autom::kParamCount) return;
    std::lock_guard<std::mutex> lk(mMutex);
    Slot &s = mSlots[slot];
    if (!s.owned) return;
    auto as = std::make_unique<AutoSet>();
    if (s.ownedAutos) *as = *s.ownedAutos;
    as->lane[param] = std::move(points);
    const bool empty = std::all_of(std::begin(as->lane), std::end(as->lane), [](const auto &l) { return l.empty(); });
    publishAutos(s, empty ? nullptr : std::move(as));
}

void Mixer::setAutomationHold(int slot, int param, bool hold) {
    if (slot >= 0 && slot < kMaxTracks && param >= 0 && param < autom::kParamCount) mSlots[slot].hold[param].store(hold);
}

std::vector<autom::PosPoint> Mixer::automation(int slot, int param) const {
    if (slot < 0 || slot >= kMaxTracks || param < 0 || param >= autom::kParamCount) return {};
    std::lock_guard<std::mutex> lk(mMutex);
    const Slot &s = mSlots[slot];
    return s.owned && s.ownedAutos ? s.ownedAutos->lane[param] : std::vector<autom::PosPoint>();
}

std::vector<Region> Mixer::regions(int slot) const {
    if (slot < 0 || slot >= kMaxTracks) return {};
    std::lock_guard<std::mutex> lk(mMutex);
    const Slot &s = mSlots[slot];
    return s.owned && s.ownedPieces ? s.ownedPieces->regions : std::vector<Region>();
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
    if (mLength.load() > 0 || mFreeRun.load()) mPlaying.store(true);
}

bool Mixer::needsOutput() const {
    if (mRolling.load(std::memory_order_relaxed)) return true;
    const LiveInput *in = mLive.load(std::memory_order_acquire);
    return in && in->monitoring();
}

void Mixer::writeInputTap(const float *l, const float *r, int n) {
    if (mTapSource.load(std::memory_order_relaxed) != kTapInput) return;
    const uint64_t w = mTapWrite.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        const size_t k = static_cast<size_t>((w + i) & (kTapSize - 1));
        mTapL[k].store(l[i], std::memory_order_relaxed);
        mTapR[k].store(r[i], std::memory_order_relaxed);
    }
    mTapWrite.store(w + n, std::memory_order_release);
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
    const int64_t ahead = mSampleRate * 3;
    for (const Slot &s : mSlots) {
        if (!s.owned || !s.ownedPieces) continue;
        // Lo que va a sonar en los próximos 3 s, en el lugar del archivo donde está.
        for (const Piece &p : s.ownedPieces->pieces) {
            const int64_t from = std::max(pos, p.start), to = std::min(pos + ahead, p.start + p.length);
            if (from < to) s.owned->prefetch(p.src + (from - p.start), to - from);
        }
    }
}

void Mixer::process(float *out, int frames, int channels) {
    mCallbacks.fetch_add(1);  // impar: en curso
    int done = 0;
    while (done < frames) {
        const int n = std::min(kBlock, frames - done);
        renderBlock(n);
        if (LiveInput *in = mLive.load(std::memory_order_acquire)) {
            // La escucha de la entrada va encima de la mezcla (con su propio volumen).
            in->mixMonitor(mMixL, mMixR, n);
            for (int i = 0; i < n; ++i) {
                mMixL[i] = std::clamp(mMixL[i], -1.0f, 1.0f);
                mMixR[i] = std::clamp(mMixR[i], -1.0f, 1.0f);
            }
        }
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
    const bool freeRun = mFreeRun.load(std::memory_order_relaxed);
    if (wantPlay && length == 0 && !freeRun) {
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

    // Arranca desde silencio (play o un salto): los valores suavizados pueden ir directo a su
    // objetivo, así una automatización suena bien desde la primera muestra.
    const bool fresh = mTransport == 0.0f;

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

    const int tapSource = mTapSource.load(std::memory_order_relaxed);
    std::fill(mBlkTapL, mBlkTapL + n, 0.0f);
    std::fill(mBlkTapR, mBlkTapR + n, 0.0f);

    for (int si = 0; si < kMaxTracks; ++si) {
        Slot &s = mSlots[si];
        const MappedAudio *a = s.audio.load(std::memory_order_acquire);
        const PieceSet *ps = s.pieces.load(std::memory_order_acquire);
        if (!a || !ps) continue;
        const bool tap = si == tapSource;
        float gainT = s.gain.load(std::memory_order_relaxed);
        float panT = s.pan.load(std::memory_order_relaxed);
        const uint32_t gen = s.gen.load(std::memory_order_relaxed);
        const bool snap = fresh || gen != s.seenGen;  // los valores suavizados empiezan donde van
        float gainNow = gainT, panNow = panT;
        if (const AutoSet *as = s.autos.load(std::memory_order_acquire)) {
            // El suavizado va ~15 ms atrás de su objetivo: la automatización se lee esos 15 ms
            // adelante, así lo que suena cae justo en la curva (y un escalón queda centrado).
            const int64_t at = mPos + n / 2 + mLookAhead;
            const std::vector<autom::PosPoint> &vol = as->lane[autom::kVolume], &pan = as->lane[autom::kPan];
            if (!vol.empty() && !s.hold[autom::kVolume].load(std::memory_order_relaxed)) {
                gainT = autom::gainFromPos(autom::posAt(vol, at));
                gainNow = snap ? autom::gainFromPos(autom::posAt(vol, mPos)) : gainT;
            }
            if (!pan.empty() && !s.hold[autom::kPan].load(std::memory_order_relaxed)) {
                panT = autom::panFromPos(autom::posAt(pan, at));
                panNow = snap ? autom::panFromPos(autom::posAt(pan, mPos)) : panT;
            }
        }
        const float invT = s.invert.load(std::memory_order_relaxed) ? -1.0f : 1.0f;
        const bool on = !s.mute.load(std::memory_order_relaxed) &&
                        (!anySolo || s.solo.load(std::memory_order_relaxed)) &&
                        !s.removing.load(std::memory_order_relaxed);
        const float onT = on ? 1.0f : 0.0f;
        if (gen != s.seenGen) {  // pista nueva en este espacio: entra con fundido
            s.seenGen = gen;
            s.gainS = gainNow;
            s.panS = panNow;
            s.invS = invT;
            s.onS = mOffline || fresh ? onT : 0.0f;  // al exportar no hay fundido de entrada
            s.eq.resetState();
        } else if (fresh) {
            s.gainS = gainNow;
            s.panS = panNow;
            s.onS = onT;
        }

        // El paneo se suaviza por bloque y la matriz se interpola dentro del bloque.
        const bool mono = a->channels() == 1;
        const float panEnd = panT + (s.panS - panT) * std::exp(-n / mTauPanFrames);
        const PanMatrix m0 = panMatrix(s.panS, mono), m1 = panMatrix(panEnd, mono);
        const float inv = 1.0f / n;
        const PanMatrix dm = {(m1.a - m0.a) * inv, (m1.b - m0.b) * inv, (m1.c - m0.c) * inv, (m1.d - m0.d) * inv};
        s.panS = panEnd;

        // Entrada del bloque (ceros fuera de los pedazos; el EQ puede seguir sonando un poco).
        readInput(*a, *ps, n);
        s.eq.process(mInL, mono ? nullptr : mInR, n);

        float pkL = 0.0f, pkR = 0.0f;
        float g = s.gainS, onS = s.onS, invS = s.invS;
        for (int i = 0; i < n; ++i) {
            g += (gainT - g) * mCoefGain;
            onS += (onT - onS) * mCoefOn;
            invS += (invT - invS) * mCoefInv;
            const float k = g * invS * mRamp[i];
            const float fi = static_cast<float>(i);
            float l, r;
            if (mono) {
                const float x = mInL[i];
                l = x * (m0.a + dm.a * fi);
                r = x * (m0.c + dm.c * fi);
            } else {
                const float xl = mInL[i], xr = mInR[i];
                l = xl * (m0.a + dm.a * fi) + xr * (m0.b + dm.b * fi);
                r = xl * (m0.c + dm.c * fi) + xr * (m0.d + dm.d * fi);
            }
            l *= k;
            r *= k;
            if (tap) {  // el análisis de una pista va antes de mute/solo, como el prototipo
                mBlkTapL[i] = l;
                mBlkTapR[i] = r;
            }
            l *= onS;
            r *= onS;
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

    // Bus: anti-zumbido; el análisis de la mezcla se toma aquí (antes del volumen master).
    applyNotch(n);
    if (tapSource < 0) {
        std::copy(mMixL, mMixL + n, mBlkTapL);
        std::copy(mMixR, mMixR + n, mBlkTapR);
    }
    if (!mOffline && tapSource != kTapInput) {
        const uint64_t w = mTapWrite.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) {
            const size_t k = static_cast<size_t>((w + i) & (kTapSize - 1));
            mTapL[k].store(mBlkTapL[i], std::memory_order_relaxed);
            mTapR[k].store(mBlkTapR[i], std::memory_order_relaxed);
        }
        mTapWrite.store(w + n, std::memory_order_release);
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
    if (mPos >= length && !freeRun) {
        if (mSeekTarget.load() >= 0) {
            // Llegó al final con un salto pendiente (p. ej. "al inicio" justo antes de que
            // termine): no se detiene; espera en silencio a que el salto se aplique.
            mPos = length;
        } else {  // fin de la canción: se detiene y vuelve al inicio
            bool expected = true;
            mPlaying.compare_exchange_strong(expected, false);
            mPos = 0;
            mTransport = 0.0f;
        }
    }
}

void Mixer::readInput(const MappedAudio &a, const PieceSet &ps, int n) {
    const bool mono = a.channels() == 1;
    std::fill(mInL, mInL + n, 0.0f);
    if (!mono) std::fill(mInR, mInR + n, 0.0f);
    const float *data = a.data();
    const int64_t frames = a.frames();
    for (const Piece &p : ps.pieces) {
        const int64_t from = std::max(mPos, p.start);
        const int64_t to = std::min({mPos + n, p.start + p.length, p.start + (frames - p.src)});
        for (int64_t f = from; f < to; ++f) {
            const int64_t k = f - p.start, src = p.src + k, rest = p.length - k;
            float g = 1.0f;
            if (k < p.fadeIn) g *= fadeCurve((k + 0.5) / p.fadeIn);
            if (rest <= p.fadeOut) g *= fadeCurve((rest - 0.5) / p.fadeOut);
            const int i = static_cast<int>(f - mPos);
            if (mono) {
                mInL[i] += g * data[src];
            } else {
                mInL[i] += g * data[2 * src];
                mInR[i] += g * data[2 * src + 1];
            }
        }
    }
}

void Mixer::applyNotch(int n) {
    const int hz = mNotchHz.load(std::memory_order_relaxed);
    if (hz != mNotchSeen) {
        mNotchSeen = hz;
        for (int k = 0; k < 4; ++k) {
            const double f = static_cast<double>(hz) * (k + 1);
            const bool on = hz > 0 && f < mSampleRate / 2.0 * 0.95;
            if (on) mNotchC[k] = designBiquad(kNotch, f, 0, 30, mSampleRate);
            if (!on || !mNotchOn[k]) std::fill(&mNotchZ[k][0][0], &mNotchZ[k][0][0] + 4, 0.0);
            mNotchOn[k] = on;
        }
    }
    if (hz == 0) return;
    float *ch[2] = {mMixL, mMixR};
    for (int k = 0; k < 4; ++k) {
        if (!mNotchOn[k]) continue;
        const Biquad &c = mNotchC[k];
        for (int j = 0; j < 2; ++j) {
            double z1 = mNotchZ[k][j][0], z2 = mNotchZ[k][j][1];
            float *x = ch[j];
            for (int i = 0; i < n; ++i) {
                const double in = x[i];
                const double y = c.b0 * in + z1;
                z1 = c.b1 * in - c.a1 * y + z2;
                z2 = c.b2 * in - c.a2 * y;
                x[i] = static_cast<float>(y);
            }
            mNotchZ[k][j][0] = std::fabs(z1) < 1e-25 ? 0 : z1;
            mNotchZ[k][j][1] = std::fabs(z2) < 1e-25 ? 0 : z2;
        }
    }
}

uint64_t Mixer::readTap(float *l, float *r, int n) const {
    const uint64_t w = mTapWrite.load(std::memory_order_acquire);
    n = std::min(n, kTapSize);
    for (int i = 0; i < n; ++i) {
        const int64_t pos = static_cast<int64_t>(w) - n + i;
        if (pos < 0) {
            l[i] = r[i] = 0.0f;
            continue;
        }
        const size_t k = static_cast<size_t>(pos) & (kTapSize - 1);
        l[i] = mTapL[k].load(std::memory_order_relaxed);
        r[i] = mTapR[k].load(std::memory_order_relaxed);
    }
    return w;
}

bool Mixer::cloneInto(Mixer &dst, std::string &error) const {
    std::lock_guard<std::mutex> lk(mMutex);
    for (int i = 0; i < kMaxTracks; ++i) {
        const Slot &s = mSlots[i];
        if (!s.owned) continue;
        auto a = MappedAudio::open(s.owned->path(), error);
        if (!a || !dst.setTrack(i, std::move(a))) {
            if (error.empty()) error = "No se pudo preparar la pista " + std::to_string(i + 1);
            return false;
        }
        if (s.ownedPieces) dst.setRegions(i, s.ownedPieces->regions);
        Slot &d = dst.mSlots[i];
        if (s.ownedAutos) {
            std::lock_guard<std::mutex> dl(dst.mMutex);
            dst.publishAutos(d, std::make_unique<AutoSet>(*s.ownedAutos));
        }
        d.gain.store(s.gain.load());
        d.pan.store(s.pan.load());
        d.mute.store(s.mute.load());
        d.solo.store(s.solo.load());
        d.invert.store(s.invert.load());
        d.eq.copyFrom(s.eq);
    }
    dst.mMasterGain.store(mMasterGain.load());
    dst.mLimiterOn.store(mLimiterOn.load());
    dst.mNotchHz.store(mNotchHz.load());
    return true;
}

void Mixer::prepareOffline() {
    mOffline = true;
    mPos = 0;
    mSeekTarget.store(-1);
    mPlaying.store(mLength.load() > 0);
    mTransport = 1.0f;
    mMasterS = mMasterGain.load();
    mLimiterWasOn = mLimiterOn.load();
}

std::string Mixer::trackPath(int slot) const {
    if (slot < 0 || slot >= kMaxTracks) return "";
    std::lock_guard<std::mutex> lk(mMutex);
    return mSlots[slot].owned ? mSlots[slot].owned->path() : "";
}
