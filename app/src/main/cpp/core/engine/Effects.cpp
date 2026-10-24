#include "engine/Effects.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/VoiceFx.h"

namespace fx {

namespace {

const Info kInfo[kCount] = {
    {"compresor", 0, 1, 0, 1, false},
    {"umbral", -60, 0, -18, 0.5, false},            // dB
    {"proporcion", 1, 20, 3, 0.1, true},            // :1
    {"ataque", 0.5f, 100, 10, 0.1, true},           // ms
    {"liberacion", 20, 1000, 150, 1, true},         // ms
    {"ganancia", 0, 24, 0, 0.5, false},             // dB
    {"delay", 0, 1, 0, 1, false},
    {"tiempo", 20, 1500, 350, 1, true},             // ms
    {"repeticiones", 0, 0.9f, 0.3f, 0.01, false},   // realimentación
    {"cantidadDelay", 0, 1, 0.2f, 0.01, false},
    {"brilloDelay", 1000, 16000, 5000, 10, true},   // Hz
    {"pingpong", 0, 1, 0, 1, false},
    {"reverb", 0, 1, 0, 1, false},
    {"cantidadReverb", 0, 1, 0.2f, 0.01, false},
    {"duracion", 0.3f, 8, 1.6f, 0.05, true},        // s
    {"brilloReverb", 1500, 16000, 7000, 10, true},  // Hz
    {"predelay", 0, 150, 20, 1, false},             // ms
    {"graves", 0, 1, 0, 1, false},
    {"gravesHz", 40, 250, 90, 1, true},             // Hz: el filtro quita lo de abajo
    {"compuerta", 0, 1, 0, 1, false},
    {"umbralCompuerta", -80, -20, -52, 1, false},   // dB
    {"autotune", 0, 1, 0, 1, false},
    {"afinar", 0, 1, 1, 0.01, false},               // cuánto corrige (0..1)
    {"velocidad", 0, 400, 5, 1, false},             // ms (0 = robot)
    {"tonica", 0, 11, 9, 1, false},                 // si la escala no es la de la canción
    {"escala", 0, 3, 0, 1, false},                  // vfx::ScaleChoice
    {"registro", 0, 2, 1, 1, false},                // grave, media, aguda
    {"deesser", 0, 1, 0, 1, false},
    {"umbralEses", -50, 0, -28, 0.5, false},        // dB
    {"doblaje", 0, 1, 0, 1, false},
    {"cantidadDoblaje", 0, 1, 0.35f, 0.01, false},
};

bool isSwitch(int p) {
    return p == kCompOn || p == kDelayOn || p == kDelayPingPong || p == kReverbOn || p == kHpOn || p == kGateOn ||
           p == kTuneOn || p == kDeessOn || p == kDoublerOn;
}

// Un preset parte de los valores iniciales y cambia lo que dice la lista.
Preset make(const char *name, std::initializer_list<std::pair<int, float>> changes) {
    Preset p{name, defaults()};
    for (const auto &c : changes) p.values[static_cast<size_t>(c.first)] = c.second;
    return p;
}

const std::vector<Preset> &presets() {
    static const std::vector<Preset> list = {
        make("Voz", {{kCompOn, 1}, {kCompThreshold, -20}, {kCompRatio, 3}, {kCompAttack, 5}, {kCompRelease, 120},
                     {kCompMakeup, 5}, {kReverbOn, 1}, {kReverbMix, 0.16f}, {kReverbDecay, 1.4f}, {kReverbTone, 7000},
                     {kReverbPredelay, 25}}),
        make("Voz con eco", {{kCompOn, 1}, {kCompThreshold, -20}, {kCompRatio, 3}, {kCompAttack, 5},
                             {kCompRelease, 120}, {kCompMakeup, 5}, {kDelayOn, 1}, {kDelayTime, 320},
                             {kDelayFeedback, 0.28f}, {kDelayMix, 0.16f}, {kDelayTone, 4500}, {kDelayPingPong, 1},
                             {kReverbOn, 1}, {kReverbMix, 0.14f}, {kReverbDecay, 1.6f}, {kReverbPredelay, 20}}),
        make("Voz al frente", {{kCompOn, 1}, {kCompThreshold, -24}, {kCompRatio, 4}, {kCompAttack, 3},
                               {kCompRelease, 80}, {kCompMakeup, 8}, {kReverbOn, 1}, {kReverbMix, 0.08f},
                               {kReverbDecay, 0.8f}, {kReverbTone, 6000}, {kReverbPredelay, 10}}),
        make("Coros", {{kCompOn, 1}, {kCompThreshold, -22}, {kCompRatio, 3}, {kCompAttack, 10}, {kCompRelease, 150},
                       {kCompMakeup, 5}, {kDelayOn, 1}, {kDelayTime, 240}, {kDelayFeedback, 0.2f}, {kDelayMix, 0.08f},
                       {kDelayTone, 4000}, {kDelayPingPong, 1}, {kReverbOn, 1}, {kReverbMix, 0.3f},
                       {kReverbDecay, 2.2f}, {kReverbTone, 6500}, {kReverbPredelay, 30}}),
        make("Batería", {{kCompOn, 1}, {kCompThreshold, -16}, {kCompRatio, 4}, {kCompAttack, 10},
                         {kCompRelease, 100}, {kCompMakeup, 4}, {kReverbOn, 1}, {kReverbMix, 0.12f},
                         {kReverbDecay, 1.0f}, {kReverbTone, 8000}, {kReverbPredelay, 10}}),
        make("Bajo", {{kCompOn, 1}, {kCompThreshold, -20}, {kCompRatio, 4}, {kCompAttack, 15}, {kCompRelease, 150},
                      {kCompMakeup, 6}}),
        make("Guitarra acústica", {{kCompOn, 1}, {kCompThreshold, -18}, {kCompRatio, 2.5f}, {kCompAttack, 15},
                                   {kCompRelease, 200}, {kCompMakeup, 3}, {kReverbOn, 1}, {kReverbMix, 0.2f},
                                   {kReverbDecay, 1.8f}, {kReverbTone, 7000}, {kReverbPredelay, 20}}),
        make("Guitarra eléctrica", {{kCompOn, 1}, {kCompThreshold, -16}, {kCompRatio, 3}, {kCompAttack, 8},
                                    {kCompRelease, 150}, {kCompMakeup, 3}, {kDelayOn, 1}, {kDelayTime, 380},
                                    {kDelayFeedback, 0.3f}, {kDelayMix, 0.15f}, {kDelayTone, 3500},
                                    {kReverbOn, 1}, {kReverbMix, 0.15f}, {kReverbDecay, 1.6f}, {kReverbTone, 6000},
                                    {kReverbPredelay, 15}}),
        make("Piano y teclado", {{kCompOn, 1}, {kCompThreshold, -20}, {kCompRatio, 2}, {kCompAttack, 20},
                                 {kCompRelease, 250}, {kCompMakeup, 2}, {kReverbOn, 1}, {kReverbMix, 0.22f},
                                 {kReverbDecay, 2.0f}, {kReverbTone, 7500}, {kReverbPredelay, 20}}),
        make("Ambiente grande", {{kReverbOn, 1}, {kReverbMix, 0.4f}, {kReverbDecay, 4.5f}, {kReverbTone, 5500},
                                 {kReverbPredelay, 40}}),
        // Como la voz en vivo, para una voz grabada.
        make("Voz con auto-tune", {{kHpOn, 1}, {kHpFreq, 90}, {kGateOn, 1}, {kGateThreshold, -52}, {kTuneOn, 1},
                                   {kTuneAmount, 1}, {kTuneSpeed, 4}, {kDeessOn, 1}, {kDeessThreshold, -30},
                                   {kCompOn, 1}, {kCompThreshold, -22}, {kCompRatio, 4}, {kCompAttack, 4},
                                   {kCompRelease, 90}, {kCompMakeup, 6}, {kDoublerOn, 1}, {kDoublerMix, 0.28f},
                                   {kDelayOn, 1}, {kDelayTime, 330}, {kDelayFeedback, 0.3f}, {kDelayMix, 0.14f},
                                   {kDelayTone, 4500}, {kDelayPingPong, 1}, {kReverbOn, 1}, {kReverbMix, 0.24f},
                                   {kReverbDecay, 2.2f}, {kReverbTone, 6500}, {kReverbPredelay, 30}}),
        make("Voz trap (robot)", {{kHpOn, 1}, {kHpFreq, 100}, {kGateOn, 1}, {kTuneOn, 1}, {kTuneAmount, 1},
                                  {kTuneSpeed, 0}, {kDeessOn, 1}, {kCompOn, 1}, {kCompThreshold, -24}, {kCompRatio, 5},
                                  {kCompAttack, 3}, {kCompRelease, 80}, {kCompMakeup, 7}, {kDoublerOn, 1},
                                  {kDoublerMix, 0.22f}, {kDelayOn, 1}, {kDelayTime, 500}, {kDelayFeedback, 0.22f},
                                  {kDelayMix, 0.1f}, {kDelayTone, 4500}, {kDelayPingPong, 1}, {kReverbOn, 1},
                                  {kReverbMix, 0.18f}, {kReverbDecay, 1.4f}}),
        make("Voz afinada natural", {{kHpOn, 1}, {kGateOn, 1}, {kGateThreshold, -55}, {kTuneOn, 1}, {kTuneAmount, 0.7f},
                                     {kTuneSpeed, 70}, {kDeessOn, 1}, {kCompOn, 1}, {kCompThreshold, -20},
                                     {kCompRatio, 3}, {kCompAttack, 5}, {kCompRelease, 120}, {kCompMakeup, 4},
                                     {kReverbOn, 1}, {kReverbMix, 0.16f}, {kReverbDecay, 1.5f}}),
        make("Limpiar voz", {{kHpOn, 1}, {kHpFreq, 100}, {kGateOn, 1}, {kGateThreshold, -50}, {kDeessOn, 1},
                             {kDeessThreshold, -30}, {kCompOn, 1}, {kCompThreshold, -20}, {kCompRatio, 3},
                             {kCompAttack, 5}, {kCompRelease, 120}, {kCompMakeup, 4}}),
        make("Sin efectos", {}),
    };
    return list;
}

}  // namespace

const Info &info(int p) { return kInfo[std::clamp(p, 0, kCount - 1)]; }

Values defaults() {
    Values v{};
    for (int p = 0; p < kCount; ++p) v[static_cast<size_t>(p)] = kInfo[p].def;
    return v;
}

float clampValue(int p, float v) {
    const Info &i = info(p);
    if (!std::isfinite(v)) v = i.def;
    if (isSwitch(p)) return v > 0.5f ? 1.0f : 0.0f;
    v = std::clamp(v, i.min, i.max);
    v = static_cast<float>(std::round(static_cast<double>(v) / i.step) * i.step);
    return std::clamp(v, i.min, i.max);
}

bool anyOn(const Values &v) {
    for (int p : {kCompOn, kDelayOn, kReverbOn, kHpOn, kGateOn, kTuneOn, kDeessOn, kDoublerOn})
        if (v[static_cast<size_t>(p)] > 0.5f) return true;
    return false;
}

int presetCount() { return static_cast<int>(presets().size()); }

const Preset &preset(int i) { return presets()[static_cast<size_t>(std::clamp(i, 0, presetCount() - 1))]; }

}  // namespace fx

TrackFx::TrackFx(int sampleRate) : mRate(sampleRate), mTune(sampleRate) {
    const fx::Values d = fx::defaults();
    for (int p = 0; p < fx::kCount; ++p) mParams[static_cast<size_t>(p)].store(d[static_cast<size_t>(p)]);
    mGateL.setup(sampleRate);
    mGateR.setup(sampleRate);
    mDeessL.setup(sampleRate);
    mDeessR.setup(sampleRate);
    mDoubler.allocate(sampleRate);
    std::fill(mMono, mMono + kMaxBlock, 0.0f);
    mComp.setup(sampleRate);
    mDelay.allocate(sampleRate, fx::info(fx::kDelayTime).max + 50.0f);
    mReverb.allocate(sampleRate);
    mSmooth = 1.0f - std::exp(-1.0f / (0.02f * sampleRate));  // 20 ms
    std::fill(mTmpL, mTmpL + kMaxBlock, 0.0f);
    std::fill(mTmpR, mTmpR + kMaxBlock, 0.0f);
}

void TrackFx::copyParams(const TrackFx &o) {
    for (int p = 0; p < fx::kCount; ++p) set(p, o.get(p));
    setSongKey(o.mSongKey.load(), o.mSongMinor.load());
}

void TrackFx::setSongKey(int key, bool minor) {
    mSongMinor.store(minor);
    mSongKey.store(key >= 0 && key < 12 ? key : -1);
}

void TrackFx::restart() {
    mHpL.reset();
    mHpR.reset();
    mGateL.reset();
    mGateR.reset();
    mTune.reset();
    mDeessL.reset();
    mDeessR.reset();
    mDoubler.restart();
    mDoublerGain = on(fx::kDoublerOn) ? get(fx::kDoublerMix) : 0.0f;
    mDoublerIdle = mDoublerGain == 0.0f;
    mComp.reset();
    mDelay.restart();
    mReverb.restart();
    // Lo prendido arranca ya con su cantidad (el transporte hace su propio fundido).
    mDelayGain = on(fx::kDelayOn) ? get(fx::kDelayMix) : 0.0f;
    mReverbGain = on(fx::kReverbOn) ? get(fx::kReverbMix) : 0.0f;
    mDelayIdle = mDelayGain == 0.0f;
    mReverbIdle = mReverbGain == 0.0f;
}

void TrackFx::processPre(float *l, float *r, int n) {
    n = std::min(n, kMaxBlock);
    if (on(fx::kHpOn)) {
        const float hz = get(fx::kHpFreq);
        if (hz != mHpFreq) {
            mHpFreq = hz;
            mHpL.highPass(hz, 0.7071, mRate);
            mHpR.highPass(hz, 0.7071, mRate);
        }
        mHpL.process(l, n);
        if (r) mHpR.process(r, n);
    }
    bool quiet = false;
    if (on(fx::kGateOn)) {
        GateSettings g;
        g.thresholdDb = get(fx::kGateThreshold);
        mGateL.process(l, n, g);
        if (r) mGateR.process(r, n, g);
        quiet = !mGateL.open() && (!r || !mGateR.open());
    }
    const bool tune = on(fx::kTuneOn);
    if (!tune) {
        mTuneWasOn = false;
        return;
    }
    if (!mTuneWasOn) {
        mTune.reset();
        mTuneWasOn = true;
    }
    const int range = static_cast<int>(std::lround(get(fx::kTuneRange)));
    if (range != mRange) {
        mRange = range;
        float lo, hi;
        vfx::rangeHz(range, lo, hi);
        mTune.setRange(lo, hi);
    }
    TuneSettings t;
    const int choice = static_cast<int>(std::lround(get(fx::kTuneScale)));
    if (choice == vfx::kScaleSong) {
        const int sk = mSongKey.load(std::memory_order_relaxed);
        t.key = std::max(0, sk);
        t.scale = sk < 0 ? kScaleChromatic : (mSongMinor.load(std::memory_order_relaxed) ? kScaleMinor : kScaleMajor);
    } else {
        t.key = static_cast<int>(std::lround(get(fx::kTuneKey)));
        t.scale = choice == vfx::kScaleMajorChoice ? kScaleMajor
                  : choice == vfx::kScaleMinorChoice ? kScaleMinor : kScaleChromatic;
    }
    t.amount = get(fx::kTuneAmount);
    t.speedMs = get(fx::kTuneSpeed);
    // El auto-tune trabaja en mono (una voz): una pista estéreo queda igual en los dos lados.
    float *x = l;
    if (r) {
        for (int i = 0; i < n; ++i) mMono[i] = 0.5f * (l[i] + r[i]);
        x = mMono;
    }
    mTune.process(x, n, t, quiet);
    if (r) {
        std::copy(x, x + n, l);
        std::copy(x, x + n, r);
    }
}

void TrackFx::processInsert(float *l, float *r, int n) {
    if (on(fx::kDeessOn)) {
        DeEssSettings d;
        d.thresholdDb = get(fx::kDeessThreshold);
        mDeessL.process(l, std::min(n, kMaxBlock), d);
        if (r) mDeessR.process(r, std::min(n, kMaxBlock), d);
    }
    if (!on(fx::kCompOn)) return;
    CompSettings s;
    s.thresholdDb = get(fx::kCompThreshold);
    s.ratio = get(fx::kCompRatio);
    s.attackMs = get(fx::kCompAttack);
    s.releaseMs = get(fx::kCompRelease);
    s.makeupDb = get(fx::kCompMakeup);
    const float gr = mComp.process(l, r, std::min(n, kMaxBlock), s);
    float old = mReduction.load(std::memory_order_relaxed);
    while (gr > old && !mReduction.compare_exchange_weak(old, gr, std::memory_order_relaxed)) {}
}

bool TrackFx::processSends(const float *l, const float *r, int n, float *wetL, float *wetR) {
    n = std::min(n, kMaxBlock);
    const float delayT = on(fx::kDelayOn) ? get(fx::kDelayMix) : 0.0f;
    const float reverbT = on(fx::kReverbOn) ? get(fx::kReverbMix) : 0.0f;
    const float doublerT = on(fx::kDoublerOn) ? get(fx::kDoublerMix) : 0.0f;
    bool any = false;
    // Un envío que se apaga baja en 20 ms y luego deja de calcularse; al volver arranca vacío.
    auto send = [&](float target, float &gain, bool &idle, auto &&run) {
        if (target <= 0.0f && gain < 1e-4f) {
            gain = 0.0f;
            idle = true;
            return;
        }
        if (idle) {
            run(true);
            idle = false;
        }
        run(false);
        float g = gain;
        for (int i = 0; i < n; ++i) {
            g += (target - g) * mSmooth;
            const float a = mTmpL[i] * g, b = mTmpR[i] * g;
            wetL[i] = any ? wetL[i] + a : a;
            wetR[i] = any ? wetR[i] + b : b;
        }
        gain = std::fabs(target - g) < 1e-6f ? target : g;
        any = true;
    };
    send(doublerT, mDoublerGain, mDoublerIdle, [&](bool restart) {
        if (restart) {
            mDoubler.restart();
            return;
        }
        // Dos copias de la voz (mono), una a cada lado.
        float *in = mMono;
        for (int i = 0; i < n; ++i) in[i] = r ? 0.5f * (l[i] + r[i]) : l[i];
        std::fill(mTmpL, mTmpL + n, 0.0f);
        std::fill(mTmpR, mTmpR + n, 0.0f);
        mDoubler.process(in, n, mTmpL, mTmpR, 1.0f);
    });
    send(delayT, mDelayGain, mDelayIdle, [&](bool restart) {
        if (restart) {
            mDelay.restart();
            return;
        }
        DelaySettings s;
        s.timeMs = get(fx::kDelayTime);
        s.feedback = get(fx::kDelayFeedback);
        s.toneHz = get(fx::kDelayTone);
        s.pingPong = on(fx::kDelayPingPong);
        mDelay.process(l, r, n, mTmpL, mTmpR, s);
    });
    send(reverbT, mReverbGain, mReverbIdle, [&](bool restart) {
        if (restart) {
            mReverb.restart();
            return;
        }
        ReverbSettings s;
        s.decayS = get(fx::kReverbDecay);
        s.toneHz = get(fx::kReverbTone);
        s.predelayMs = get(fx::kReverbPredelay);
        mReverb.process(l, r, n, mTmpL, mTmpR, s);
    });
    return any;
}
