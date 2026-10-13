#include "engine/Effects.h"

#include <algorithm>
#include <cmath>
#include <vector>

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
};

bool isSwitch(int p) { return p == kCompOn || p == kDelayOn || p == kDelayPingPong || p == kReverbOn; }

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

bool anyOn(const Values &v) { return v[kCompOn] > 0.5f || v[kDelayOn] > 0.5f || v[kReverbOn] > 0.5f; }

int presetCount() { return static_cast<int>(presets().size()); }

const Preset &preset(int i) { return presets()[static_cast<size_t>(std::clamp(i, 0, presetCount() - 1))]; }

}  // namespace fx

TrackFx::TrackFx(int sampleRate) : mRate(sampleRate) {
    const fx::Values d = fx::defaults();
    for (int p = 0; p < fx::kCount; ++p) mParams[static_cast<size_t>(p)].store(d[static_cast<size_t>(p)]);
    mComp.setup(sampleRate);
    mDelay.allocate(sampleRate, fx::info(fx::kDelayTime).max + 50.0f);
    mReverb.allocate(sampleRate);
    mSmooth = 1.0f - std::exp(-1.0f / (0.02f * sampleRate));  // 20 ms
    std::fill(mTmpL, mTmpL + kMaxBlock, 0.0f);
    std::fill(mTmpR, mTmpR + kMaxBlock, 0.0f);
}

void TrackFx::copyParams(const TrackFx &o) {
    for (int p = 0; p < fx::kCount; ++p) set(p, o.get(p));
}

void TrackFx::restart() {
    mComp.reset();
    mDelay.restart();
    mReverb.restart();
    // Lo prendido arranca ya con su cantidad (el transporte hace su propio fundido).
    mDelayGain = on(fx::kDelayOn) ? get(fx::kDelayMix) : 0.0f;
    mReverbGain = on(fx::kReverbOn) ? get(fx::kReverbMix) : 0.0f;
    mDelayIdle = mDelayGain == 0.0f;
    mReverbIdle = mReverbGain == 0.0f;
}

void TrackFx::processInsert(float *l, float *r, int n) {
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
