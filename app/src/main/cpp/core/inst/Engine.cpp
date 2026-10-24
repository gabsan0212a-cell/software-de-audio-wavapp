#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "dsp/Delay.h"
#include "dsp/Reverb.h"
#include "inst/Instruments.h"
#include "inst/Patches.h"

namespace inst {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 6.283185307179586;
constexpr int kStringBuf = 4096;  // cuadros por cuerda: alcanza para notas de ~12 Hz
constexpr int kControl = 16;      // los filtros del sintetizador se recalculan cada 16 muestras
constexpr float kSilent = 1e-4f;  // -80 dB: la nota ya terminó

inline double midiHz(double m) { return 440.0 * std::exp2((m - 69.0) / 12.0); }

// Ruido blanco repetible (xorshift): -1 .. 1.
inline float rnd(uint32_t &s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float>(s >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

// Seno por tabla (fase en vueltas, cualquier número).
struct SineTable {
    static constexpr int kSize = 2048;
    float v[kSize + 1];
    SineTable() {
        for (int i = 0; i <= kSize; ++i) v[i] = static_cast<float>(std::sin(kTwoPi * i / kSize));
    }
};
const SineTable &sineTable() {
    static const SineTable t;
    return t;
}
// Fase ya en [0, 1): sin redondear (lo más común).
inline float sine01(double phase) {
    const SineTable &t = sineTable();
    const double x = phase * SineTable::kSize;
    const int i = std::min(static_cast<int>(x), SineTable::kSize - 1);
    const float a = static_cast<float>(x - i);
    return t.v[i] + a * (t.v[i + 1] - t.v[i]);
}
inline float sine(double phase) {
    const SineTable &t = sineTable();
    double f = phase - std::floor(phase);
    const double x = f * SineTable::kSize;
    const int i = static_cast<int>(x);
    const float a = static_cast<float>(x - i);
    return t.v[i] + a * (t.v[i + 1] - t.v[i]);
}

// Corrección de los saltos de las ondas de sierra y cuadrada (sin "aliasing" audible).
inline double blep(double t, double dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

inline double osc(int wave, double ph, double dt) {
    switch (wave) {
        case wSaw: return 2.0 * ph - 1.0 - blep(ph, dt);
        case wSquare: {
            double y = ph < 0.5 ? 1.0 : -1.0;
            y += blep(ph, dt);
            double p2 = ph + 0.5;
            if (p2 >= 1.0) p2 -= 1.0;
            return y - blep(p2, dt);
        }
        case wPulse: {
            double y = ph < 0.25 ? 1.0 : -1.0;
            y += blep(ph, dt);
            double p2 = ph + 0.75;
            if (p2 >= 1.0) p2 -= 1.0;
            return y - blep(p2, dt) - 0.5;
        }
        case wTri: return 4.0 * std::fabs(ph - 0.5) - 1.0;
        default: return sine(ph);
    }
}

// Filtro de dos polos (fórmulas de RBJ), en double.
struct BQ {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    enum Type { kLP, kHP, kBP, kPeak, kHighShelf };
    void set(int type, double f, double q, double gainDb, double sr) {
        f = std::clamp(f, 10.0, sr * 0.45);
        const double w = kTwoPi * f / sr, cw = std::cos(w), sw = std::sin(w);
        const double alpha = sw / (2.0 * q);
        const double A = std::pow(10.0, gainDb / 40.0);
        double a0 = 1;
        switch (type) {
            case kLP: b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case kHP: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case kBP: b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case kPeak:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
                break;
            default: {  // high shelf (S = 1)
                const double sq = 2 * std::sqrt(A) * std::sin(w) / 2 * std::sqrt(2.0);
                b0 = A * ((A + 1) + (A - 1) * cw + sq);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - sq);
                a0 = (A + 1) - (A - 1) * cw + sq;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - sq;
            }
        }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    void reset() { z1 = z2 = 0; }
    inline float run(float x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
    void flush() {  // sin números diminutos (lentos en algunos procesadores)
        if (std::fabs(z1) < 1e-25) z1 = 0;
        if (std::fabs(z2) < 1e-25) z2 = 0;
    }
};

// Filtro de estado variable (Zavalishin), pasa-bajos con resonancia: el del sintetizador.
struct Svf {
    double ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0;
    void set(double fc, double k, double sr) {
        fc = std::clamp(fc, 20.0, sr * 0.42);
        const double g = std::tan(3.141592653589793 * fc / sr);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline double lp(double v0) {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        return v2;
    }
    void reset() { ic1 = ic2 = 0; }
};

// Envolvente ADSR (ataque en línea recta; caída y liberación exponenciales).
struct Env {
    int stage = 0;  // 0 apagada, 1 ataque, 2 caída/sostenido, 3 liberación
    float level = 0, inc = 0, sustain = 0, dCoef = 0, rCoef = 0;
    void start(float a, float d, float s, float r, float sr, bool fromZero) {
        stage = 1;
        if (fromZero) level = 0;
        inc = 1.0f / std::max(1.0f, a * sr);
        sustain = s;
        dCoef = std::exp(-4.0f / std::max(1.0f, d * sr));
        rCoef = std::exp(-4.0f / std::max(1.0f, r * sr));
    }
    void release() {
        if (stage != 0) stage = 3;
    }
    inline float next() {
        switch (stage) {
            case 1:
                level += inc;
                if (level >= 1.0f) {
                    level = 1.0f;
                    stage = 2;
                }
                break;
            case 2: level = sustain + (level - sustain) * dCoef; break;
            case 3:
                level *= rCoef;
                if (level < kSilent) {
                    level = 0;
                    stage = 0;
                }
                break;
            default: break;
        }
        return level;
    }
};

// Relaciones de los osciladores "metálicos" de los hi-hats (como la 808).
constexpr float kMetal[6] = {1.0f, 1.4471f, 1.6170f, 1.9265f, 2.5028f, 2.6637f};
// Órgano: armónicos (registros) y su nivel.
constexpr float kOrganH[9] = {0.5f, 1, 1.5f, 2, 3, 4, 5, 6, 8};
constexpr float kOrganL[9] = {0.45f, 1, 0.55f, 0.6f, 0.15f, 0.3f, 0.05f, 0.08f, 0.2f};

// tanh aproximada (error < 0.2 %), mucho más barata.
inline float fastTanh(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float balanceL(float pan) { return std::min(1.0f, 1.0f - pan); }
inline float balanceR(float pan) { return std::min(1.0f, 1.0f + pan); }

}  // namespace

struct Voice {
    bool on = false, held = false;
    int pitch = 0;
    float vel = 0;
    uint64_t age = 0;
    uint32_t rng = 1;
    int64_t t = 0;  // muestras desde que empezó
    float pan = 0;
    // Batería
    int piece = 0;
    float choke = 1.0f, chokeCoef = 1.0f;
    float envTone = 1, envNoise = 1, envSweep = 1, envClick = 1;
    float kTone = 0, kNoise = 0, kSweep = 0, kClick = 0;
    int64_t maxLen = 0;
    double ph[8] = {};
    BQ f1, f2;
    // Melódicos
    double cur = 0, target = 0;  // nota actual (con deslizamiento) y la buscada
    Env amp, fil;
    Svf svfL, svfR;
    double fmPh = 0, subPh = 0, lfo = 0;
    float idx = 1, kIdx = 1, tineEnv = 0, kTine = 0;
    // 808
    float punch = 1, kPunch = 0, decay = 1, kDecay = 0, rel = 1, kRel = 1, attack = 0, kAttack = 1;
    // Cuerda
    float *buf = nullptr;
    int64_t w = 0;
    double delay = 0;
    float S = 0.5f, rho = 0.99f, rhoMute = 0.9f, rhoNow = 0.99f, dcX = 0, dcY = 0, peak = 0;
    // Piano: cada parcial es un número complejo que gira (su tono) y se achica (se apaga solo).
    static constexpr int kPartials = 40, kSecond = 8;
    int np = 0, nq = 0;  // parciales que todavía suenan (de la primera y la segunda cuerda)
    bool damped = false;
    float thump = 0;
    float zr[kPartials], zi[kPartials], cr[kPartials], ci[kPartials];
    float qr[kSecond], qi[kSecond], dr[kSecond], di[kSecond];
};

struct Engine::Impl {
    int sr;
    int kind = kSynth;
    int voicing = vSynth;
    Kit kit{};
    MelodicPreset mp{};
    Voice v[kVoices];
    std::vector<float> strings;
    uint64_t counter = 0;
    // De los ajustes
    double tune = 0;
    float decayMul = 1, attack = 0, glideMs = 0, drive = 0, tone = 0;
    // Después de las voces (estéreo)
    BQ toneL, toneR, lpL, lpR, b1L, b1R, b2L, b2R, b3L, b3R, cab1L, cab1R, cab2L, cab2R, cabHL, cabHR, midL, midR;
    bool toneOn = false, lpOn = false, bodyOn = false, cabOn = false;
    float driveGain = 0, outGain = 1;
    // Movimiento (chorus, trémolo o vibrato) y ambiente (reverb), al final.
    int modType = modNone;
    float modDepth = 0, modHz = 1, space = 0;
    double modPh = 0;
    DelayLine chL, chR;
    Reverb reverb;
    ReverbSettings rv;
    int64_t tail = 0;  // cuadros que todavía suena la cola después de la última nota

    explicit Impl(int rate) : sr(rate), strings(static_cast<size_t>(kVoices) * kStringBuf, 0.0f) {
        for (int i = 0; i < kVoices; ++i) v[i].buf = strings.data() + static_cast<size_t>(i) * kStringBuf;
        (void)sineTable();
        chL.allocate(static_cast<int>(0.03 * rate) + 4);
        chR.allocate(static_cast<int>(0.03 * rate) + 4);
        reverb.allocate(rate);
    }

    float rate() const { return static_cast<float>(sr); }

    void configure(const Sound &s) {
        kind = s.kind;
        const Values &p = s.values;
        tune = p[kTune];
        decayMul = p[kDecay];
        attack = p[kAttack];
        drive = p[kDrive];
        tone = p[kTone];
        glideMs = 0;
        toneOn = lpOn = bodyOn = cabOn = false;
        driveGain = 0;
        outGain = 1;
        if (kind == kDrums) {
            kit = inst::kit(s.preset);
            voicing = -1;
            if (kit.lowpassHz > 0) {
                lpOn = true;
                lpL.set(BQ::kLP, kit.lowpassHz, 0.7, 0, sr);
                lpR = lpL;
            }
            driveGain = drive * 5.0f;
        } else {
            mp = melodic(kind, s.preset);
            voicing = mp.voicing;
            if (voicing == vSynth) {
                glideMs = mp.synth.glideMs + p[kGlide] * 300.0f;
                driveGain = std::min(1.0f, mp.synth.drive + drive) * 6.0f;
                outGain = mp.synth.gain;
            } else if (voicing == vBoom) {
                glideMs = mp.boom.glideMs + p[kGlide] * 300.0f;
                lpOn = true;
                lpL.set(BQ::kLP, mp.boom.toneHz, 0.7, 0, sr);
                lpR = lpL;
                outGain = mp.boom.gain;
            } else if (voicing == vPiano) {
                driveGain = drive * 6.0f;
                outGain = mp.piano.gain;
                if (mp.piano.lowpassHz > 0) {
                    lpOn = true;
                    lpL.set(BQ::kLP, mp.piano.lowpassHz, 0.7, 0, sr);
                    lpR = lpL;
                }
            } else {
                const StringPatch &st = mp.str;
                if (st.body > 0) {
                    bodyOn = true;
                    b1L.set(BQ::kPeak, 105, 2.0, 8 * st.body, sr);
                    b2L.set(BQ::kPeak, 230, 1.5, 4 * st.body, sr);
                    b3L.set(BQ::kHP, 70, 0.7, 0, sr);
                    b1R = b1L;
                    b2R = b2L;
                    b3R = b3L;
                }
                const float d = std::min(1.0f, st.drive + drive);
                driveGain = d * 28.0f;
                if (st.cabHz > 0 || d > 0.3f) {
                    cabOn = true;
                    const double hz = st.cabHz > 0 ? st.cabHz : 5000.0;
                    cab1L.set(BQ::kLP, hz, 0.7, 0, sr);
                    cab2L.set(BQ::kLP, hz * 1.2, 0.6, 0, sr);
                    cabHL.set(BQ::kHP, kind == kBass ? 35 : 90, 0.7, 0, sr);
                    midL.set(BQ::kPeak, 1600, 0.8, 3, sr);
                    cab1R = cab1L;
                    cab2R = cab2L;
                    cabHR = cabHL;
                    midR = midL;
                }
                outGain = st.gain;
            }
        }
        if (tone < -0.01f) {
            toneOn = true;
            toneL.set(BQ::kLP, 18000.0 * std::exp2(tone * 4.5), 0.7, 0, sr);
        } else if (tone > 0.01f) {
            toneOn = true;
            toneL.set(BQ::kHighShelf, 2500, 0.7, tone * 8.0, sr);
        }
        toneR = toneL;
        configureFx(kind == kDrums ? kit.fx : mp.fx, p);
    }

    void configureFx(const FxPatch &base, const Values &p) {
        const int oldMod = modType;
        const float oldSpace = space;
        const bool modOk = kind != kDrums && kind != k808;
        modDepth = modOk ? std::clamp(base.modDepth + p[kMod], 0.0f, 1.0f) : 0.0f;
        modType = modDepth < 0.005f ? modNone : base.mod != modNone ? base.mod : modChorus;
        modHz = base.modHz > 0 ? base.modHz : modType == modChorus ? 0.8f : 5.0f;
        space = kind != k808 ? std::clamp(base.space + p[kSpace], 0.0f, 1.0f) : 0.0f;
        if (space < 0.005f) space = 0;
        rv.decayS = base.spaceS > 0 ? base.spaceS : 1.8f;
        rv.toneHz = 6500.0f;
        rv.predelayMs = kind == kDrums ? 8.0f : 22.0f;
        // Lo que quedó en las memorias de antes no debe sonar al volver a prenderlas.
        if (oldMod == modNone && modType != modNone) {
            chL.restart();
            chR.restart();
        }
        if (oldSpace <= 0 && space > 0) reverb.restart();
    }

    int64_t tailFrames() const {
        int64_t t = 0;
        if (space > 0) t += static_cast<int64_t>((rv.decayS * 1.3f + rv.predelayMs * 0.001f) * rate());
        if (modType == modChorus || modType == modVibrato) t += sr / 50;
        return t;
    }

    void resetPost() {
        for (BQ *b : {&toneL, &toneR, &lpL, &lpR, &b1L, &b1R, &b2L, &b2R, &b3L, &b3R, &cab1L, &cab1R, &cab2L, &cab2R,
                      &cabHL, &cabHR, &midL, &midR})
            b->reset();
        chL.restart();
        chR.restart();
        reverb.restart();
        modPh = 0;
        tail = 0;
    }

    // ---------------------------------------------------------------- notas

    Voice *freeVoice() {
        Voice *best = nullptr;
        for (Voice &x : v)
            if (!x.on) return &x;
        // Todas ocupadas: la más baja de las que ya se soltaron, si no la más vieja.
        for (Voice &x : v)
            if (!x.held && (!best || x.amp.level < best->amp.level)) best = &x;
        if (best) return best;
        for (Voice &x : v)
            if (!best || x.age < best->age) best = &x;
        return best;
    }

    void startDrum(int piece, float vel, uint32_t seed) {
        piece = std::clamp(piece, 0, kDrumPieces - 1);
        // Cada pieza suena una vez a la vez (el golpe nuevo calla al anterior) y el hi-hat cerrado
        // calla al abierto, como en una batería de verdad.
        for (Voice &x : v)
            if (x.on && x.chokeCoef == 1.0f && (x.piece == piece || (piece == 3 && x.piece == 4)))
                x.chokeCoef = std::exp(-1.0f / ((x.piece == piece ? 0.004f : 0.008f) * rate()));
        Voice &x = *freeVoice();
        float *buf = x.buf;
        x = Voice{};
        x.buf = buf;
        x.on = true;
        x.piece = piece;
        x.vel = vel;
        x.rng = seed | 1u;
        x.age = ++counter;
        const DrumPatch &d = kit.piece[piece];
        x.pan = d.pan;
        const float dm = decayMul;
        auto coef = [&](float ms) { return std::exp(-1.0f / std::max(1.0f, ms * 0.001f * dm * rate())); };
        x.kTone = coef(d.decayMs);
        x.kNoise = coef(d.noiseDecayMs);
        x.kSweep = std::exp(-1.0f / std::max(1.0f, d.sweepMs * 0.001f * rate()));
        x.kClick = std::exp(-1.0f / (0.0015f * rate()));
        const float longest = std::max(d.decayMs, d.noiseDecayMs) * dm;
        x.maxLen = static_cast<int64_t>((longest * 0.001f * 7.0f + 0.05f) * rate());
        const double nh = d.noiseHz * std::exp2(tune / 12.0);
        switch (d.type) {
            case dSnare:
                x.f1.set(BQ::kHP, nh * 0.5, 0.7, 0, sr);
                x.f2.set(BQ::kLP, std::min(16000.0, nh * 3.0), 0.7, 0, sr);
                break;
            case dClap: x.f1.set(BQ::kBP, nh, 1.2, 0, sr); break;
            case dHat:
            case dCymbal:
                x.f1.set(BQ::kBP, nh, 0.9, 0, sr);
                x.f2.set(BQ::kHP, nh * 0.7, 0.7, 0, sr);
                break;
            case dShaker: x.f1.set(BQ::kHP, nh, 0.8, 0, sr); break;
            default: x.f1.set(BQ::kBP, std::max(200.0, nh), 1.0, 0, sr); break;
        }
    }

    void startMelodic(int pitch, float vel, uint32_t seed) {
        const bool mono = voicing == vBoom || (voicing == vSynth && mp.synth.mono) || (voicing == vString && kind == kBass);
        Voice *vp = nullptr;
        bool legato = false;
        if (mono) {
            for (Voice &x : v)
                if (x.on && (!vp || x.age > vp->age)) vp = &x;
            // Las demás (si quedaron de otro modo) se apagan.
            for (Voice &x : v)
                if (&x != vp) x.on = false;
            legato = vp && vp->held && glideMs > 0.5f;
        }
        if (!vp) vp = freeVoice();
        Voice &x = *vp;
        const bool wasOn = x.on;
        if (legato) {
            // Desliza a la nota nueva sin volver a golpear.
            x.pitch = pitch;
            x.target = pitch;
            x.held = true;
            x.age = ++counter;
            return;
        }
        if (!(mono && wasOn)) {
            float *buf = x.buf;
            x = Voice{};
            x.buf = buf;
            x.cur = pitch;
        } else {
            x.t = 0;
        }
        x.on = true;
        x.held = true;
        x.pitch = pitch;
        x.target = pitch;
        if (!(mono && wasOn)) x.cur = pitch;
        else if (glideMs <= 0.5f) x.cur = pitch;
        x.vel = vel;
        x.rng = seed | 1u;
        x.age = ++counter;
        x.t = 0;
        const float a = attack;
        if (voicing == vBoom) {
            const BoomPatch &b = mp.boom;
            // Al volver a golpear sigue desde donde iba (sin salto).
            x.attack = wasOn ? std::min(1.0f, x.attack * x.decay * x.rel) : 0.0f;
            x.punch = 1.0f;
            x.kPunch = std::exp(-1.0f / std::max(1.0f, b.punchMs * 0.001f * rate()));
            x.decay = 1.0f;
            x.kDecay = std::exp(-6.9f / std::max(1.0f, b.decayS * decayMul * rate()));
            x.rel = 1.0f;
            x.kRel = 1.0f;
            x.envClick = 1.0f;
            x.kClick = std::exp(-1.0f / (0.001f * rate()));
            x.kAttack = 1.0f / std::max(1.0f, (0.001f + a * 0.05f) * rate());
        } else if (voicing == vSynth) {
            const SynthPatch &p = mp.synth;
            const float dm = decayMul;
            x.amp.start(p.a + a * 1.5f, p.d * dm, p.s, p.r * dm, rate(), !(mono && wasOn));
            x.fil.start(p.fa + a * 0.8f, p.fd * dm, p.fs, p.fr * dm, rate(), true);
            x.idx = 1.0f;
            x.kIdx = std::exp(-1.0f / std::max(1.0f, p.fmDecay * dm * rate()));
            x.tineEnv = 1.0f;
            x.kTine = std::exp(-1.0f / (0.025f * rate()));
            if (!(mono && wasOn)) {
                x.svfL.reset();
                x.svfR.reset();
                for (int k = 0; k < 8; ++k) x.ph[k] = static_cast<double>(rnd(x.rng) * 0.5f + 0.5f);
            }
            x.pan = p.mode == mSub ? 0.0f : std::clamp((pitch - 60) / 36.0f, -1.0f, 1.0f) * p.spread;
        } else if (voicing == vPiano) {
            startPiano(x, pitch, vel);
        } else {
            startString(x, pitch, vel);
        }
    }

    void startPiano(Voice &x, int pitch, float vel) {
        const PianoPatch &pp = mp.piano;
        const double f0 = midiHz(pitch + tune);
        // Las cuerdas agudas son más rígidas: sus armónicos suben un poco más (estiramiento).
        const double B = 1.4e-4 * pp.inharm * std::exp2((pitch - 60) / 14.0);
        const double bright = std::clamp(pp.bright + tone * 0.35f, 0.0f, 1.0f);
        // Un golpe suave saca menos armónicos.
        const double tilt = std::max(0.6, 2.2 - 1.1 * bright - 0.6 * vel);
        const double fc = 1200.0 + 7000.0 * bright * (0.35 + 0.65 * vel);
        const double t60 = std::clamp(pp.t60 * std::pow(261.63 / f0, 0.55), 0.4, 30.0) * decayMul;
        const double s1 = 6.91 / t60;
        const double maxHz = std::min(0.45 * sr, 15000.0);
        const double uni = std::clamp(static_cast<double>(pp.unison), 0.0, 1.0);
        const double det = std::exp2(pp.detune / 1200.0);
        auto loss = [&](int k, double fk) { return s1 * (1.0 + 0.12 * (k - 1)) + 2.4e-7 * fk * fk; };
        double amp[Voice::kPartials];
        double sum2 = 0;
        int n = 0;
        for (int k = 1; k <= Voice::kPartials; ++k) {
            const double fk = k * f0 * std::sqrt(1.0 + B * k * k);
            if (fk >= maxHz) break;
            // Donde golpea el martillo faltan algunos armónicos (los que tienen un nodo ahí).
            const double a = (std::fabs(std::sin(kPi * k * pp.strike)) + 0.06) * std::pow(k, -tilt) * std::exp(-fk / fc);
            const double rr = std::exp(-loss(k, fk) / sr), w = kTwoPi * fk / sr;
            x.cr[n] = static_cast<float>(rr * std::cos(w));
            x.ci[n] = static_cast<float>(rr * std::sin(w));
            amp[n] = a;
            sum2 += a * a;
            ++n;
        }
        x.np = n;
        x.nq = std::min(n, Voice::kSecond);
        // La segunda cuerda, desafinada apenas: batimiento y la cola larga (se apaga más lento).
        for (int j = 0; j < x.nq; ++j) {
            const int k = j + 1;
            const double fk = k * f0 * std::sqrt(1.0 + B * k * k) * det;
            const double rr = std::exp(-0.45 * loss(k, fk) / sr), w = kTwoPi * fk / sr;
            x.dr[j] = static_cast<float>(rr * std::cos(w));
            x.di[j] = static_cast<float>(rr * std::sin(w));
        }
        const double level = (0.18 + 0.82 * std::pow(vel, 1.4)) / std::sqrt(std::max(1e-12, sum2));
        for (int j = 0; j < n; ++j) {
            x.zr[j] = static_cast<float>(amp[j] * level * (j < x.nq ? 1.0 - 0.35 * uni : 1.0));
            x.zi[j] = 0;
        }
        for (int j = 0; j < x.nq; ++j) {
            x.qr[j] = static_cast<float>(amp[j] * level * uni * 0.8);
            x.qi[j] = 0;
        }
        x.damped = false;
        x.pan = std::clamp((pitch - 60) / 42.0f, -1.0f, 1.0f) * 0.45f;
        // El martillo: un golpe corto de ruido (más brillante si se toca fuerte).
        x.thump = static_cast<float>(pp.thump * 0.3 * level * std::sqrt(sum2) * (0.3 + 0.7 * vel));
        x.envNoise = 1.0f;
        x.kNoise = std::exp(-1.0f / (0.010f * rate()));
        x.f1.set(BQ::kLP, 250.0 + 2200.0 * vel * (0.4 + bright), 0.7, 0, sr);
        x.attack = 0.0f;
        x.kAttack = 1.0f / std::max(1.0f, (0.0012f + attack * 0.4f) * rate());
        x.peak = 1;
    }

    void startString(Voice &x, int pitch, float vel) {
        const StringPatch &st = mp.str;
        const double f = midiHz(pitch + tune);
        const double N = std::clamp(static_cast<double>(sr) / f, 4.0, kStringBuf - 8.0);
        const float bright = std::clamp(st.bright + tone * 0.25f, 0.0f, 1.0f);
        x.S = 0.5f - 0.45f * bright;
        x.delay = N - x.S;
        const double t60 = st.t60 * std::pow(110.0 / f, 0.5) * decayMul;
        x.rho = static_cast<float>(std::pow(0.001, 1.0 / (std::max(0.02, t60) * f)));
        x.rhoMute = static_cast<float>(std::pow(0.001, 1.0 / (std::max(0.01, static_cast<double>(st.muteT60)) * f)));
        x.rhoNow = x.rho;
        x.pan = std::clamp((pitch - 60) / 30.0f, -1.0f, 1.0f) * 0.25f;
        // Ataque: ruido filtrado (más oscuro si se toca suave o con "ataque" alto) y peinado según
        // dónde se pulsa la cuerda.
        const int len = static_cast<int>(std::ceil(N)) + 2;
        const float a = std::clamp(st.pickTone * (0.55f + 0.45f * vel) * (1.0f - 0.7f * attack), 0.03f, 0.98f);
        const int P = std::max(1, static_cast<int>(st.pickPos * N));
        std::fill(x.buf, x.buf + kStringBuf, 0.0f);
        float y = 0, sum2 = 0;
        for (int i = 0; i < len; ++i) {
            y += a * (rnd(x.rng) - y);
            x.buf[i] = y;
        }
        for (int i = len - 1; i >= P; --i) x.buf[i] -= x.buf[i - P];
        float mean = 0;
        for (int i = 0; i < len; ++i) mean += x.buf[i];
        mean /= len;
        for (int i = 0; i < len; ++i) {
            x.buf[i] -= mean;
            sum2 += x.buf[i] * x.buf[i];
        }
        const float g = (0.35f + 0.65f * vel) * 0.9f / std::sqrt(std::max(1e-9f, sum2 / len));
        for (int i = 0; i < len; ++i) x.buf[i] *= g;
        x.w = len;
        x.dcX = x.dcY = 0;
        x.peak = 1;
    }

    // ---------------------------------------------------------------- sonido

    void renderDrum(Voice &x, float *l, float *r, int n) {
        const DrumPatch &d = kit.piece[x.piece];
        const double tm = std::exp2(tune / 12.0);
        const double invSr = 1.0 / sr;
        const float gl = balanceL(x.pan) * d.gain * x.vel, gr = balanceR(x.pan) * d.gain * x.vel;
        const float dg = 1.0f + d.drive * 6.0f, dn = 1.0f / fastTanh(dg);
        const int clap2 = sr * 105 / 10000, clap3 = 2 * clap2;  // las palmas: 3 golpes cada ~10 ms
        for (int i0 = 0; i0 < n; i0 += kControl) {
            const int m = std::min(kControl, n - i0);
            // La afinación del bombo, el tom y la caja cae rápido: basta calcularla cada 16 muestras.
            const double f = d.freq * tm * std::exp2(d.sweep / 12.0 * x.envSweep);
            const double inc = f * invSr;
            for (int i = i0; i < i0 + m; ++i) {
                float s;
                switch (d.type) {
                    case dKick:
                    case dTom: {
                        x.ph[0] += inc;
                        if (x.ph[0] >= 1.0) x.ph[0] -= 1.0;
                        s = sine01(x.ph[0]) * x.envTone + rnd(x.rng) * x.envClick * d.click * 0.6f;
                        if (d.noise > 0) s += x.f1.run(rnd(x.rng)) * x.envNoise * d.noise * 2.0f;
                        if (d.drive > 0) s = fastTanh(s * dg) * dn;
                        break;
                    }
                    case dSnare: {
                        x.ph[0] += inc;
                        if (x.ph[0] >= 1.0) x.ph[0] -= 1.0;
                        x.ph[1] += inc * 1.47;
                        if (x.ph[1] >= 1.0) x.ph[1] -= 1.0;
                        const float tonePart = (sine01(x.ph[0]) + 0.5f * sine01(x.ph[1])) * 0.7f * x.envTone;
                        const float nz = x.f2.run(x.f1.run(rnd(x.rng))) * x.envNoise;
                        s = tonePart * (1.0f - 0.5f * d.noise) + nz * d.noise * 1.6f + rnd(x.rng) * x.envClick * d.click * 0.5f;
                        if (d.drive > 0) s = fastTanh(s * dg) * dn;
                        break;
                    }
                    case dClap: {
                        if (x.t == clap2 || x.t == clap3) x.envClick = 1.0f;
                        float e = x.envClick;
                        if (x.t >= clap3) e = std::max(e, 0.75f * x.envNoise);
                        s = x.f1.run(rnd(x.rng)) * e * 2.2f;
                        break;
                    }
                    case dHat:
                    case dCymbal: {
                        float mt = 0;
                        for (int k = 0; k < 6; ++k) {
                            x.ph[k] += d.freq * tm * kMetal[k] * invSr;
                            if (x.ph[k] >= 1.0) x.ph[k] -= 1.0;
                            mt += x.ph[k] < 0.5 ? 1.0f : -1.0f;
                        }
                        mt *= 1.0f / 6.0f;
                        const float mix = mt * (1.0f - d.noise) + rnd(x.rng) * d.noise;
                        const float env = d.type == dCymbal ? 0.7f * x.envTone + 0.6f * x.envClick : x.envTone;
                        s = x.f2.run(x.f1.run(mix)) * env * 2.4f;
                        break;
                    }
                    case dRim: {
                        x.ph[0] += d.freq * tm * invSr;
                        if (x.ph[0] >= 1.0) x.ph[0] -= 1.0;
                        x.ph[1] += d.freq * 0.31 * tm * invSr;
                        if (x.ph[1] >= 1.0) x.ph[1] -= 1.0;
                        s = (0.6f * sine01(x.ph[0]) + 0.5f * sine01(x.ph[1])) * x.envTone +
                            x.f1.run(rnd(x.rng)) * x.envNoise * d.noise * 2.0f;
                        break;
                    }
                    default: {  // dShaker: entra en ~6 ms
                        const float att = std::min(1.0f, static_cast<float>(x.t) * static_cast<float>(invSr) / 0.006f);
                        s = x.f1.run(rnd(x.rng)) * att * x.envNoise * 1.6f;
                        break;
                    }
                }
                x.envTone *= x.kTone;
                x.envNoise *= x.kNoise;
                x.envSweep *= x.kSweep;
                x.envClick *= x.kClick;
                x.choke *= x.chokeCoef;
                s *= x.choke;
                l[i] += s * gl;
                r[i] += s * gr;
                ++x.t;
            }
        }
        x.f1.flush();
        x.f2.flush();
        if (x.t >= x.maxLen || x.choke < kSilent) x.on = false;
    }

    void glideStep(Voice &x, int n) {
        if (x.cur == x.target) return;
        if (glideMs <= 0.5f) {
            x.cur = x.target;
            return;
        }
        const double k = 1.0 - std::exp(-static_cast<double>(n) / (glideMs * 0.001 * sr / 3.0));
        x.cur += (x.target - x.cur) * k;
        if (std::fabs(x.target - x.cur) < 0.001) x.cur = x.target;
    }

    void renderBoom(Voice &x, float *l, float *r, int n) {
        const BoomPatch &b = mp.boom;
        const float invSr = 1.0f / rate();
        const float dg = 1.0f + std::min(1.0f, b.drive + drive) * 9.0f, dn = 1.0f / fastTanh(dg);
        const float amp = (0.35f + 0.65f * x.vel);
        float peak = 0;
        for (int i0 = 0; i0 < n; i0 += kControl) {
            const int m = std::min(kControl, n - i0);
            glideStep(x, m);
            const double base = midiHz(x.cur + tune);
            for (int i = i0; i < i0 + m; ++i) {
                const double f = base * std::exp2(b.punchSemis / 12.0 * x.punch);
                x.ph[0] += f * invSr;
                if (x.ph[0] >= 1.0) x.ph[0] -= 1.0;
                const double ph2 = x.ph[0] < 0.5 ? 2.0 * x.ph[0] : 2.0 * x.ph[0] - 1.0;
                float s = sine01(x.ph[0]) + b.harm * sine01(ph2);
                x.attack = std::min(1.0f, x.attack + x.kAttack);
                const float e = x.attack * x.decay * x.rel;
                s = s * e + rnd(x.rng) * x.envClick * b.click * 0.4f;
                s = fastTanh(s * dg) * dn * amp;
                x.punch *= x.kPunch;
                x.decay *= x.kDecay;
                x.rel *= x.kRel;
                x.envClick *= x.kClick;
                l[i] += s;
                r[i] += s;
                peak = std::max(peak, e);
                ++x.t;
            }
        }
        if (peak < kSilent && x.t > sr / 100) x.on = false;
    }

    void renderSynth(Voice &x, float *l, float *r, int n) {
        const SynthPatch &p = mp.synth;
        const float invSr = 1.0f / rate();
        const int U = std::clamp(p.unison, 1, 5);
        const float vamp = 0.3f + 0.7f * x.vel;
        float uPan[5], uDet[5];
        for (int u = 0; u < U; ++u) {
            const float k = U == 1 ? 0.0f : (2.0f * u / (U - 1) - 1.0f);
            uPan[u] = k * p.spread;
            uDet[u] = k * p.detune;
        }
        const float uNorm = 1.0f / std::sqrt(static_cast<float>(U));
        const float organPerc = std::exp(-1.0f / (0.25f * rate())), organClick = std::exp(-1.0f / (0.002f * rate()));
        const double k = 2.0 * std::pow(std::max(0.0, 1.0 - p.reso * 0.98), 1.5) + 0.04;
        for (int i0 = 0; i0 < n; i0 += kControl) {
            const int m = std::min(kControl, n - i0);
            glideStep(x, m);
            // Vibrato: entra poco a poco después de ~0.25 s.
            x.lfo += m * 5.5 * invSr;
            const float vibIn = std::clamp((static_cast<float>(x.t) * invSr - 0.25f) / 0.4f, 0.0f, 1.0f);
            const double vib = p.vibrato * 0.01 * vibIn * sine(x.lfo);
            const double note = x.cur + tune + vib;
            const double f = midiHz(note);
            if (p.mode == mSub) {
                const double cutoff = p.cutoff * std::exp2(p.envAmt * x.fil.level + (note - 60.0) / 24.0);
                x.svfL.set(cutoff, k, sr);
                x.svfR.a1 = x.svfL.a1;
                x.svfR.a2 = x.svfL.a2;
                x.svfR.a3 = x.svfL.a3;
            }
            double dts[5];
            for (int u = 0; u < U; ++u) dts[u] = f * std::exp2(uDet[u] / 1200.0) * invSr;
            const double dt2 = f * std::exp2((p.semi2 + 0.06) / 12.0) * invSr;
            const double dtSub = f * 0.5 * invSr;
            for (int i = i0; i < i0 + m; ++i) {
                const float e = x.amp.next();
                x.fil.next();
                float sl = 0, sr2 = 0;
                if (p.mode == mSub) {
                    for (int u = 0; u < U; ++u) {
                        x.ph[u] += dts[u];
                        if (x.ph[u] >= 1.0) x.ph[u] -= 1.0;
                        const float o = static_cast<float>(osc(p.wave1, x.ph[u], dts[u])) * uNorm;
                        sl += o * balanceL(uPan[u]);
                        sr2 += o * balanceR(uPan[u]);
                    }
                    float c = 0;
                    if (p.mix2 > 0) {
                        x.ph[6] += dt2;
                        if (x.ph[6] >= 1.0) x.ph[6] -= 1.0;
                        c += static_cast<float>(osc(p.wave2, x.ph[6], dt2)) * p.mix2;
                    }
                    if (p.sub > 0) {
                        x.subPh += dtSub;
                        if (x.subPh >= 1.0) x.subPh -= 1.0;
                        c += sine01(x.subPh) * p.sub;
                    }
                    if (p.noise > 0) c += rnd(x.rng) * p.noise;
                    sl = static_cast<float>(x.svfL.lp(sl + c));
                    sr2 = static_cast<float>(x.svfR.lp(sr2 + c));
                } else if (p.mode == mFm) {
                    x.fmPh += dts[0] * p.fmRatio;
                    if (x.fmPh >= 1.0) x.fmPh -= 1.0;
                    x.ph[0] += dts[0];
                    if (x.ph[0] >= 1.0) x.ph[0] -= 1.0;
                    const float index = p.fmIndex * (0.35f + 0.65f * x.vel) * (0.15f + 0.85f * x.idx);
                    float o = sine(x.ph[0] + index * sine01(x.fmPh) / kTwoPi);
                    if (p.tine > 0 && x.tineEnv > kSilent) {
                        x.ph[1] += dts[0] * 13.7;
                        if (x.ph[1] >= 1.0) x.ph[1] -= std::floor(x.ph[1]);
                        o += sine01(x.ph[1]) * p.tine * x.tineEnv * x.vel;
                    }
                    x.idx *= x.kIdx;
                    x.tineEnv *= x.kTine;
                    sl = o * balanceL(x.pan);
                    sr2 = o * balanceR(x.pan);
                } else {  // órgano
                    float o = 0;
                    for (int h = 0; h < 8; ++h) {
                        x.ph[h] += dts[0] * kOrganH[h];
                        if (x.ph[h] >= 1.0) x.ph[h] -= 1.0;
                        o += sine01(x.ph[h]) * kOrganL[h];
                    }
                    x.subPh += dts[0] * kOrganH[8];
                    if (x.subPh >= 1.0) x.subPh -= 1.0;
                    o += sine01(x.subPh) * kOrganL[8];
                    // Percusión (el "golpe" del tercer armónico) y el click de la tecla.
                    o += sine01(x.ph[4]) * 1.2f * x.tineEnv;
                    if (x.envClick > kSilent) o += rnd(x.rng) * 0.08f * x.envClick;
                    x.tineEnv *= organPerc;
                    x.envClick *= organClick;
                    o *= 0.32f;
                    sl = o * balanceL(x.pan);
                    sr2 = o * balanceR(x.pan);
                }
                l[i] += sl * e * vamp;
                r[i] += sr2 * e * vamp;
                ++x.t;
            }
        }
        if (x.amp.stage == 0) x.on = false;
    }

    void renderString(Voice &x, float *l, float *r, int n) {
        const float gl = balanceL(x.pan), gr = balanceR(x.pan);
        const int64_t mask = kStringBuf - 1;
        const float target = x.held ? x.rho : x.rhoMute;
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            x.rhoNow += (target - x.rhoNow) * 0.002f;
            const double pos = static_cast<double>(x.w) - x.delay;
            const int64_t i0 = static_cast<int64_t>(std::floor(pos));
            const float fr = static_cast<float>(pos - static_cast<double>(i0));
            const float a = x.buf[i0 & mask], b = x.buf[(i0 + 1) & mask], c = x.buf[(i0 - 1) & mask];
            const float y0 = a + fr * (b - a);          // y(n - D)
            const float y1 = c + fr * (a - c);          // y(n - D - 1)
            const float y = x.rhoNow * ((1.0f - x.S) * y0 + x.S * y1);
            x.buf[x.w & mask] = y;
            ++x.w;
            // Sin componente continua.
            const float o = y - x.dcX + 0.995f * x.dcY;
            x.dcX = y;
            x.dcY = o;
            l[i] += o * gl;
            r[i] += o * gr;
            peak = std::max(peak, std::fabs(o));
            ++x.t;
        }
        if (std::fabs(x.dcY) < 1e-25f) x.dcY = 0;
        x.peak = std::max(peak, x.peak * 0.5f);
        if (x.peak < kSilent && x.t > sr / 20) x.on = false;
    }

    void renderPiano(Voice &x, float *l, float *r, int n) {
        const PianoPatch &pp = mp.piano;
        // Al soltar, el apagador calla la cuerda (las más agudas no tienen apagador).
        if (!x.held && !x.damped && x.pitch < 89) {
            const float k = std::exp(-6.91f / std::max(1.0f, pp.releaseS * decayMul * rate()));
            for (int j = 0; j < x.np; ++j) {
                x.cr[j] *= k;
                x.ci[j] *= k;
            }
            for (int j = 0; j < x.nq; ++j) {
                x.dr[j] *= k;
                x.di[j] *= k;
            }
            x.damped = true;
        }
        const float gl = balanceL(x.pan), gr = balanceR(x.pan);
        constexpr int kChunk = 64;
        float acc[kChunk];
        float peak = 0;
        for (int i0 = 0; i0 < n; i0 += kChunk) {
            const int m = std::min(kChunk, n - i0);
            std::fill(acc, acc + m, 0.0f);
            // Cada parcial en su propio ciclo: su estado se queda en registros.
            for (int j = 0; j < x.np; ++j) {
                float a = x.zr[j], b = x.zi[j];
                const float c = x.cr[j], d = x.ci[j];
                for (int i = 0; i < m; ++i) {
                    const float na = a * c - b * d;
                    b = a * d + b * c;
                    a = na;
                    acc[i] += b;
                }
                x.zr[j] = a;
                x.zi[j] = b;
            }
            for (int j = 0; j < x.nq; ++j) {
                float a = x.qr[j], b = x.qi[j];
                const float c = x.dr[j], d = x.di[j];
                for (int i = 0; i < m; ++i) {
                    const float na = a * c - b * d;
                    b = a * d + b * c;
                    a = na;
                    acc[i] += b;
                }
                x.qr[j] = a;
                x.qi[j] = b;
            }
            for (int i = 0; i < m; ++i) {
                float s = acc[i];
                if (x.envNoise > kSilent) {
                    s += x.f1.run(rnd(x.rng)) * x.envNoise * x.thump;
                    x.envNoise *= x.kNoise;
                }
                x.attack = std::min(1.0f, x.attack + x.kAttack);
                s *= x.attack;
                l[i0 + i] += s * gl;
                r[i0 + i] += s * gr;
                peak = std::max(peak, std::fabs(s));
                ++x.t;
            }
        }
        // Los parciales de arriba que ya se apagaron dejan de calcularse.
        auto quiet = [](float a, float b) { return std::fabs(a) + std::fabs(b) < 1e-6f; };
        while (x.np > 0 && quiet(x.zr[x.np - 1], x.zi[x.np - 1])) --x.np;
        while (x.nq > 0 && quiet(x.qr[x.nq - 1], x.qi[x.nq - 1])) --x.nq;
        x.f1.flush();
        x.peak = std::max(peak, x.peak * 0.5f);
        if (x.peak < kSilent && x.t > sr / 20) x.on = false;
    }

    void render(float *l, float *r, int n) {
        std::fill(l, l + n, 0.0f);
        std::fill(r, r + n, 0.0f);
        bool any = false;
        for (Voice &x : v) {
            if (!x.on) continue;
            any = true;
            if (kind == kDrums) renderDrum(x, l, r, n);
            else if (voicing == vBoom) renderBoom(x, l, r, n);
            else if (voicing == vSynth) renderSynth(x, l, r, n);
            else if (voicing == vPiano) renderPiano(x, l, r, n);
            else renderString(x, l, r, n);
        }
        post(l, r, n);
        tail = any ? tailFrames() : std::max<int64_t>(0, tail - n);
    }

    // Movimiento: trémolo (volumen que sube y baja), chorus (copias que se mueven, más ancho) o
    // vibrato (solo la copia que se mueve: la afinación ondula).
    void modulate(float *l, float *r, int n) {
        if (modType == modNone) return;
        const double inc = modHz / sr;
        if (modType == modTremolo) {
            for (int i = 0; i < n; ++i) {
                modPh += inc;
                if (modPh >= 1.0) modPh -= 1.0;
                const float g = 1.0f - 0.85f * modDepth * (0.5f + 0.5f * sine01(modPh));
                l[i] *= g;
                r[i] *= g;
            }
            return;
        }
        const bool vib = modType == modVibrato;
        const float base = (vib ? 0.006f : 0.011f) * rate();
        // El vibrato mueve la afinación lo mismo a cualquier velocidad (±1/3 de semitono como máximo).
        const float depth = std::min(base - 2.0f, (vib ? 0.0006f * 5.0f / modHz : 0.003f) * modDepth * rate());
        const float mix = std::min(1.0f, modDepth * 1.6f);
        const float wet = vib ? 1.0f : 0.55f * mix, dry = vib ? 0.0f : 1.0f - 0.3f * mix;
        for (int i = 0; i < n; ++i) {
            modPh += inc;
            if (modPh >= 1.0) modPh -= 1.0;
            const double phR = modPh < 0.75 ? modPh + 0.25 : modPh - 0.75;
            chL.write(l[i]);
            chR.write(r[i]);
            l[i] = dry * l[i] + wet * chL.readFrac(base + depth * sine01(modPh));
            r[i] = dry * r[i] + wet * chR.readFrac(base + depth * sine01(phR));
        }
    }

    // Ambiente: la reverb se suma a lo que sale.
    void ambience(float *l, float *r, int n) {
        if (space <= 0) return;
        const float wet = space * 0.5f;
        constexpr int kChunk = 128;
        float wl[kChunk], wr[kChunk];
        for (int i0 = 0; i0 < n; i0 += kChunk) {
            const int m = std::min(kChunk, n - i0);
            reverb.process(l + i0, r + i0, m, wl, wr, rv);
            for (int i = 0; i < m; ++i) {
                l[i0 + i] += wl[i] * wet;
                r[i0 + i] += wr[i] * wet;
            }
        }
    }

    void post(float *l, float *r, int n) {
        if (voicing == vString) {
            if (bodyOn) {
                for (int i = 0; i < n; ++i) {
                    l[i] = b3L.run(b2L.run(b1L.run(l[i])));
                    r[i] = b3R.run(b2R.run(b1R.run(r[i])));
                }
            }
            if (driveGain > 0.01f) {
                // Amplificador: saturación un poco asimétrica; lo fuerte queda parejo.
                const float g = 1.0f + driveGain, bias = 0.15f;
                const float off = fastTanh(bias), norm = 1.0f / fastTanh(g * 0.5f + bias);
                for (int i = 0; i < n; ++i) {
                    l[i] = (fastTanh(l[i] * g + bias) - off) * norm * 0.7f;
                    r[i] = (fastTanh(r[i] * g + bias) - off) * norm * 0.7f;
                }
            }
            if (cabOn) {
                for (int i = 0; i < n; ++i) {
                    l[i] = midL.run(cabHL.run(cab2L.run(cab1L.run(l[i]))));
                    r[i] = midR.run(cabHR.run(cab2R.run(cab1R.run(r[i]))));
                }
            }
            for (int i = 0; i < n; ++i) {
                l[i] *= outGain;
                r[i] *= outGain;
            }
        } else {
            const float g0 = kind == kDrums ? 0.62f : outGain;
            for (int i = 0; i < n; ++i) {
                l[i] *= g0;
                r[i] *= g0;
            }
            if (driveGain > 0.01f) {
                // Saturación que deja igual un nivel normal (-6 dB) y redondea lo que pasa de ahí.
                const float g = 1.0f + driveGain, norm = 0.5f / fastTanh(g * 0.5f);
                for (int i = 0; i < n; ++i) {
                    l[i] = fastTanh(l[i] * g) * norm;
                    r[i] = fastTanh(r[i] * g) * norm;
                }
            }
        }
        if (lpOn) {
            for (int i = 0; i < n; ++i) {
                l[i] = lpL.run(l[i]);
                r[i] = lpR.run(r[i]);
            }
        }
        if (toneOn) {
            for (int i = 0; i < n; ++i) {
                l[i] = toneL.run(l[i]);
                r[i] = toneR.run(r[i]);
            }
        }
        modulate(l, r, n);
        ambience(l, r, n);
        for (BQ *b : {&toneL, &toneR, &lpL, &lpR, &b1L, &b1R, &b2L, &b2R, &b3L, &b3R, &cab1L, &cab1R, &cab2L, &cab2R,
                      &cabHL, &cabHR, &midL, &midR})
            b->flush();
    }
};

Engine::Engine(int sampleRate) : mImpl(std::make_unique<Impl>(sampleRate)) { mImpl->configure(mSound); }

Engine::~Engine() = default;

void Engine::setSound(const Sound &s) {
    const Sound c = clampSound(s);
    if (c.kind != mSound.kind || c.preset != mSound.preset) silence();
    mSound = c;
    mImpl->configure(c);
}

void Engine::noteOn(int pitch, float velocity, uint32_t seed) {
    const float vel = std::clamp(velocity, 0.0f, 1.0f);
    if (mSound.kind == kDrums) mImpl->startDrum(pitch, vel, seed);
    else mImpl->startMelodic(pitch, vel, seed);
}

void Engine::noteOff(int pitch) {
    for (Voice &x : mImpl->v) {
        if (!x.on || !x.held || x.pitch != pitch) continue;
        x.held = false;
        x.amp.release();
        x.fil.release();
        if (mImpl->voicing == vBoom) x.kRel = std::exp(-1.0f / (0.03f * mImpl->rate()));
    }
}

void Engine::releaseAll() {
    for (Voice &x : mImpl->v) {
        if (!x.on || !x.held) continue;
        noteOff(x.pitch);
    }
}

void Engine::silence() {
    for (Voice &x : mImpl->v) x.on = false;
    mImpl->resetPost();
}

void Engine::render(float *l, float *r, int n) { mImpl->render(l, r, n); }

bool Engine::active() const {
    for (const Voice &x : mImpl->v)
        if (x.on) return true;
    return mImpl->tail > 0;
}

}  // namespace inst
