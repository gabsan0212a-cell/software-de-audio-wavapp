#include "engine/VoiceFx.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vfx {

namespace {

const Info kInfo[kCount] = {
    {"encendida", 0, 1, 0, 1, false},
    {"autotune", 0, 1, 1, 1, false},
    {"afinar", 0, 1, 1, 0.01, false},               // cuánto corrige (0..1)
    {"velocidad", 0, 400, 5, 1, false},             // ms (0 = robot); al empezar ya se nota
    {"tonica", 0, 11, 9, 1, false},                 // si la escala no es la de la canción
    {"escala", 0, 3, 0, 1, false},                  // ScaleChoice
    {"registro", 0, 2, 1, 1, false},                // grave, media, aguda
    {"graves", 40, 250, 90, 1, true},               // Hz: el filtro quita lo de abajo
    {"compuerta", 0, 1, 1, 1, false},
    {"umbralCompuerta", -80, -20, -52, 1, false},   // dB
    {"deesser", 0, 1, 1, 1, false},
    {"umbralEses", -50, 0, -28, 0.5, false},        // dB
    {"compresor", 0, 1, 1, 1, false},
    {"umbral", -40, 0, -20, 0.5, false},            // dB
    {"proporcion", 1, 10, 3.5f, 0.1, true},         // :1
    {"ganancia", 0, 18, 5, 0.5, false},             // dB
    {"doblaje", 0, 1, 0, 1, false},
    {"cantidadDoblaje", 0, 1, 0.35f, 0.01, false},
    {"delay", 0, 1, 0, 1, false},
    {"pulsosDelay", 0, 3, 1, 1, false},             // 1/4, 1/8 con punto, 1/8, 1/16
    {"repeticiones", 0, 0.8f, 0.25f, 0.01, false},
    {"cantidadDelay", 0, 1, 0.15f, 0.01, false},
    {"reverb", 0, 1, 1, 1, false},
    {"cantidadReverb", 0, 1, 0.18f, 0.01, false},
    {"duracion", 0.3f, 6, 1.8f, 0.05, true},        // s
    {"volumen", -24, 12, 0, 0.5, false},            // dB
};

bool isSwitch(int p) {
    return p == kOn || p == kTuneOn || p == kGateOn || p == kDeessOn || p == kCompOn || p == kDoublerOn ||
           p == kDelayOn || p == kReverbOn;
}

Preset make(const char *name, std::initializer_list<std::pair<int, float>> changes) {
    Preset p{name, defaults()};
    p.values[kOn] = 1.0f;
    for (const auto &c : changes) p.values[static_cast<size_t>(c.first)] = c.second;
    return p;
}

const std::vector<Preset> &presets() {
    static const std::vector<Preset> list = {
        // Como la canción: afinación rápida pero no robot, doblaje, eco de 1/8 con punto y reverb.
        make("Paraíso", {{kTuneAmount, 1}, {kTuneSpeed, 4}, {kGateThreshold, -52}, {kDeessThreshold, -30},
                         {kCompThreshold, -22}, {kCompRatio, 4}, {kCompMakeup, 6}, {kDoublerOn, 1},
                         {kDoublerMix, 0.28f}, {kDelayOn, 1}, {kDelayBeats, 1}, {kDelayFeedback, 0.3f},
                         {kDelayMix, 0.14f}, {kReverbOn, 1}, {kReverbMix, 0.24f}, {kReverbDecay, 2.2f}}),
        make("Natural", {{kTuneAmount, 0.7f}, {kTuneSpeed, 70}, {kGateThreshold, -55}, {kCompThreshold, -20},
                         {kCompRatio, 3}, {kCompMakeup, 4}, {kReverbOn, 1}, {kReverbMix, 0.16f}, {kReverbDecay, 1.5f}}),
        make("Trap (robot)", {{kTuneAmount, 1}, {kTuneSpeed, 0}, {kCompThreshold, -24}, {kCompRatio, 5},
                              {kCompMakeup, 7}, {kDoublerOn, 1}, {kDoublerMix, 0.22f}, {kDelayOn, 1},
                              {kDelayBeats, 0}, {kDelayFeedback, 0.22f}, {kDelayMix, 0.1f}, {kReverbOn, 1},
                              {kReverbMix, 0.18f}, {kReverbDecay, 1.4f}}),
        make("Balada", {{kTuneAmount, 0.55f}, {kTuneSpeed, 120}, {kCompThreshold, -18}, {kCompRatio, 2.5f},
                        {kCompMakeup, 3}, {kDelayOn, 1}, {kDelayBeats, 0}, {kDelayFeedback, 0.18f},
                        {kDelayMix, 0.08f}, {kReverbOn, 1}, {kReverbMix, 0.3f}, {kReverbDecay, 2.8f}}),
        make("Solo afinar", {{kTuneAmount, 1}, {kTuneSpeed, 25}, {kDeessOn, 0}, {kCompOn, 0}, {kReverbOn, 0}}),
        make("Sin efectos", {{kTuneOn, 0}, {kGateOn, 0}, {kDeessOn, 0}, {kCompOn, 0}, {kReverbOn, 0}}),
    };
    return list;
}

}  // namespace

void rangeHz(int choice, float &minHz, float &maxHz) {
    switch (choice) {
        case 0: minHz = 55.0f; maxHz = 500.0f; break;    // grave
        case 2: minHz = 130.0f; maxHz = 1100.0f; break;  // aguda
        default: minHz = 75.0f; maxHz = 800.0f; break;   // media
    }
}

float delayBeats(int choice) {
    static const float beats[4] = {1.0f, 0.75f, 0.5f, 0.25f};
    return beats[std::clamp(choice, 0, 3)];
}

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

int presetCount() { return static_cast<int>(presets().size()); }

const Preset &preset(int i) { return presets()[static_cast<size_t>(std::clamp(i, 0, presetCount() - 1))]; }

}  // namespace vfx

namespace {

inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }

void atomicMax(std::atomic<float> &a, float v) {
    float old = a.load(std::memory_order_relaxed);
    while (v > old && !a.compare_exchange_weak(old, v, std::memory_order_relaxed)) {}
}

constexpr float kMaxDelayMs = 1600.0f;  // 1/4 a 40 BPM

}  // namespace

VoiceFx::VoiceFx(int sampleRate) : mRate(sampleRate), mTune(sampleRate) {
    const vfx::Values d = vfx::defaults();
    for (int p = 0; p < vfx::kCount; ++p) mParams[static_cast<size_t>(p)].store(d[static_cast<size_t>(p)]);
    mGate.setup(sampleRate);
    mDeess.setup(sampleRate);
    mComp.setup(sampleRate);
    mDoubler.allocate(sampleRate);
    mDelay.allocate(sampleRate, kMaxDelayMs + 50.0f);
    mReverb.allocate(sampleRate);
    mSmooth = 1.0f - std::exp(-1.0f / (0.02f * sampleRate));  // 20 ms
    std::fill(mWetL, mWetL + kMaxBlock, 0.0f);
    std::fill(mWetR, mWetR + kMaxBlock, 0.0f);
}

void VoiceFx::set(int p, float v) {
    if (p >= 0 && p < vfx::kCount) mParams[static_cast<size_t>(p)].store(vfx::clampValue(p, v), std::memory_order_relaxed);
}

void VoiceFx::setSongKey(int key, bool minor) {
    mSongMinor.store(minor);
    mSongKey.store(key >= 0 && key < 12 ? key : -1);
}

void VoiceFx::setTempo(float bpm) { mBpm.store(std::isfinite(bpm) && bpm >= 40.0f && bpm <= 240.0f ? bpm : 0.0f); }

void VoiceFx::resolveScale(int &key, int &scale, bool &fromSong) const {
    const int choice = static_cast<int>(std::lround(get(vfx::kTuneScale)));
    fromSong = choice == vfx::kScaleSong;
    if (fromSong) {
        const int sk = mSongKey.load(std::memory_order_relaxed);
        key = std::max(0, sk);
        // Sin canción preparada (o sin tonalidad clara): todas las notas.
        scale = sk < 0 ? kScaleChromatic : (mSongMinor.load(std::memory_order_relaxed) ? kScaleMinor : kScaleMajor);
        return;
    }
    key = static_cast<int>(std::lround(get(vfx::kTuneKey)));
    scale = choice == vfx::kScaleMajorChoice ? kScaleMajor : choice == vfx::kScaleMinorChoice ? kScaleMinor : kScaleChromatic;
}

void VoiceFx::meters(float *out) {
    const AutoTune::Info t = mTune.info();
    out[0] = t.inputMidi;
    out[1] = t.outputMidi;
    out[2] = static_cast<float>(t.note);
    out[3] = mGateOpen.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    out[4] = mDeessPeak.exchange(0.0f, std::memory_order_relaxed);
    out[5] = mCompPeak.exchange(0.0f, std::memory_order_relaxed);
    out[6] = mTune.latencySamples() * 1000.0f / static_cast<float>(mRate);
    int key, scale;
    bool fromSong;
    resolveScale(key, scale, fromSong);
    out[7] = scale == kScaleChromatic ? -1.0f : static_cast<float>(key);
    out[8] = static_cast<float>(scale);
    out[9] = fromSong ? 1.0f : 0.0f;
}

void VoiceFx::restart() {
    mHp.reset();
    mGate.reset();
    mTune.reset();
    mDeess.reset();
    mComp.reset();
    mDoubler.restart();
    mDelay.restart();
    mReverb.restart();
    mLevelS = dbToLin(get(vfx::kLevel));
    mDoublerS = mDelayS = mReverbS = 0.0f;
    mDelayIdle = mReverbIdle = true;
}

void VoiceFx::processPre(float *x, int n) {
    n = std::min(n, kMaxBlock);
    const float hp = get(vfx::kHighPass);
    if (hp != mHpFreq) {
        mHpFreq = hp;
        mHp.highPass(hp, 0.7071, mRate);
    }
    mHp.process(x, n);
    const int range = static_cast<int>(std::lround(get(vfx::kRange)));
    if (range != mRange) {
        mRange = range;
        float lo, hi;
        vfx::rangeHz(range, lo, hi);
        mTune.setRange(lo, hi);
    }
    bool quiet = false;
    if (flag(vfx::kGateOn)) {
        GateSettings g;
        g.thresholdDb = get(vfx::kGateThreshold);
        mGate.process(x, n, g);
        quiet = !mGate.open();
        mGateOpen.store(mGate.open(), std::memory_order_relaxed);
    } else {
        mGateOpen.store(true, std::memory_order_relaxed);
    }
    TuneSettings t;
    bool fromSong;
    resolveScale(t.key, t.scale, fromSong);
    t.amount = get(vfx::kTuneAmount);
    t.speedMs = get(vfx::kTuneSpeed);
    t.correct = flag(vfx::kTuneOn);
    mTune.process(x, n, t, quiet);
}

void VoiceFx::processPost(float *x, int n, float *outL, float *outR) {
    n = std::min(n, kMaxBlock);
    if (flag(vfx::kDeessOn)) {
        DeEssSettings d;
        d.thresholdDb = get(vfx::kDeessThreshold);
        atomicMax(mDeessPeak, mDeess.process(x, n, d));
    }
    if (flag(vfx::kCompOn)) {
        CompSettings c;
        c.thresholdDb = get(vfx::kCompThreshold);
        c.ratio = get(vfx::kCompRatio);
        c.attackMs = 4.0f;
        c.releaseMs = 90.0f;
        c.makeupDb = get(vfx::kCompMakeup);
        atomicMax(mCompPeak, mComp.process(x, nullptr, n, c));
    }
    const float levelT = dbToLin(get(vfx::kLevel));
    for (int i = 0; i < n; ++i) {
        mLevelS += (levelT - mLevelS) * mSmooth;
        x[i] *= mLevelS;
        outL[i] = outR[i] = x[i];
    }
    if (std::fabs(levelT - mLevelS) < 1e-7f) mLevelS = levelT;

    // Doblaje y envíos: se calculan aparte y se suman con su cantidad suavizada; apagados (y ya
    // en silencio) no cuestan nada, y al volver arrancan sin ecos viejos.
    auto add = [&](float target, float &gain, auto &&run) {
        if (target <= 0.0f && gain < 1e-4f) {
            gain = 0.0f;
            return false;
        }
        std::fill(mWetL, mWetL + n, 0.0f);
        std::fill(mWetR, mWetR + n, 0.0f);
        run();
        float g = gain;
        for (int i = 0; i < n; ++i) {
            g += (target - g) * mSmooth;
            outL[i] += mWetL[i] * g;
            outR[i] += mWetR[i] * g;
        }
        gain = std::fabs(target - g) < 1e-6f ? target : g;
        return true;
    };
    const float dblT = flag(vfx::kDoublerOn) ? get(vfx::kDoublerMix) : 0.0f;
    if (dblT > 0.0f && mDoublerS == 0.0f) mDoubler.restart();
    add(dblT, mDoublerS, [&] { mDoubler.process(x, n, mWetL, mWetR, 1.0f); });

    const float delayT = flag(vfx::kDelayOn) ? get(vfx::kDelayMix) : 0.0f;
    if (delayT > 0.0f && mDelayIdle) {
        mDelay.restart();
        mDelayIdle = false;
    }
    if (!add(delayT, mDelayS, [&] {
            DelaySettings s;
            const float bpm = mBpm.load(std::memory_order_relaxed);
            s.timeMs = std::min(kMaxDelayMs, 60000.0f / (bpm > 0.0f ? bpm : 120.0f) *
                                                 vfx::delayBeats(static_cast<int>(std::lround(get(vfx::kDelayBeats)))));
            s.feedback = get(vfx::kDelayFeedback);
            s.toneHz = 4500.0f;
            s.pingPong = true;
            mDelay.process(x, nullptr, n, mWetL, mWetR, s);
        }))
        mDelayIdle = true;

    const float revT = flag(vfx::kReverbOn) ? get(vfx::kReverbMix) : 0.0f;
    if (revT > 0.0f && mReverbIdle) {
        mReverb.restart();
        mReverbIdle = false;
    }
    if (!add(revT, mReverbS, [&] {
            ReverbSettings s;
            s.decayS = get(vfx::kReverbDecay);
            s.toneHz = 6500.0f;
            s.predelayMs = 30.0f;
            mReverb.process(x, nullptr, n, mWetL, mWetR, s);
        }))
        mReverbIdle = true;
}
