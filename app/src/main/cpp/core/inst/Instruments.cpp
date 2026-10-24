#include "inst/Instruments.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "inst/Patches.h"

namespace inst {

namespace {

const ParamInfo kInfo[kParamCount] = {
    {"brillo", -1, 1, 0, 0.01, false},     // oscuro .. brillante
    {"ataque", 0, 1, 0, 0.01, false},      // 0 = como el preset; más = entra más suave
    {"duracion", 0.25f, 4, 1, 0.01, true}, // cuánto suena cada nota (×)
    {"distorsion", 0, 1, 0, 0.01, false},
    {"deslizar", 0, 1, 0, 0.01, false},    // entre notas encimadas (instrumentos de una nota)
    {"afinacion", -12, 12, 0, 1, false},   // semitonos
};

const char *const kKindNames[kKindCount] = {"Batería", "808", "Bajo", "Guitarra", "Sintetizador"};
const char *const kDrumNames[kDrumPieces] = {"Bombo", "Caja", "Aplauso", "Hi-hat", "Hi-hat abierto",
                                             "Tom", "Platillo", "Percusión"};

// ---------------------------------------------------------------- baterías
// {tipo, Hz, caída st, caída ms, tono ms, ruido, ruido ms, ruido Hz, golpe, saturación, volumen, paneo}
const Kit kKits[] = {
    {"Trap",
     {{dKick, 50, 26, 30, 380, 0, 0, 0, 0.45f, 0.4f, 0.95f, 0},
      {dSnare, 190, 5, 14, 65, 0.78f, 150, 3800, 0.3f, 0.25f, 0.62f, 0},
      {dClap, 0, 0, 0, 0, 1, 190, 1300, 0, 0.1f, 0.62f, 0.05f},
      {dHat, 330, 0, 0, 42, 0.35f, 40, 9500, 0, 0, 0.34f, 0.18f},
      {dHat, 330, 0, 0, 340, 0.35f, 300, 8500, 0, 0, 0.27f, 0.18f},
      {dTom, 105, 9, 70, 320, 0.08f, 60, 900, 0.2f, 0.1f, 0.58f, -0.22f},
      {dCymbal, 440, 0, 0, 1500, 0.5f, 1200, 7000, 0, 0, 0.24f, -0.12f},
      {dRim, 1700, 0, 0, 22, 0.45f, 18, 2600, 0.4f, 0, 0.42f, 0.12f}},
     0},
    {"Boom bap",
     {{dKick, 57, 18, 22, 200, 0.06f, 30, 1500, 0.75f, 0.55f, 0.95f, 0},
      {dSnare, 205, 3, 18, 115, 0.62f, 230, 2300, 0.35f, 0.4f, 0.75f, 0},
      {dClap, 0, 0, 0, 0, 1, 230, 1100, 0, 0.2f, 0.6f, 0.05f},
      {dHat, 300, 0, 0, 55, 0.5f, 50, 7200, 0, 0, 0.3f, 0.2f},
      {dHat, 300, 0, 0, 280, 0.5f, 250, 6800, 0, 0, 0.24f, 0.2f},
      {dTom, 95, 7, 60, 300, 0.1f, 50, 800, 0.2f, 0.2f, 0.55f, -0.2f},
      {dCymbal, 400, 0, 0, 1300, 0.55f, 1000, 6000, 0, 0, 0.22f, -0.12f},
      {dRim, 1250, 0, 0, 28, 0.4f, 20, 2000, 0.5f, 0, 0.4f, 0.1f}},
     9000},
    {"Reggaetón",
     {{dKick, 55, 20, 25, 220, 0.04f, 25, 2000, 0.6f, 0.45f, 0.95f, 0},
      {dSnare, 235, 4, 12, 60, 0.7f, 125, 4600, 0.35f, 0.25f, 0.62f, 0},
      {dClap, 0, 0, 0, 0, 1, 160, 1500, 0, 0.1f, 0.6f, 0.05f},
      {dHat, 360, 0, 0, 38, 0.4f, 35, 10500, 0, 0, 0.3f, 0.18f},
      {dHat, 360, 0, 0, 260, 0.4f, 230, 9500, 0, 0, 0.24f, 0.18f},
      {dTom, 145, 6, 40, 220, 0.1f, 40, 1200, 0.3f, 0.1f, 0.5f, -0.25f},
      {dCymbal, 460, 0, 0, 1300, 0.5f, 1000, 7500, 0, 0, 0.22f, -0.12f},
      {dShaker, 0, 0, 0, 0, 1, 55, 7500, 0, 0, 0.26f, -0.15f}},
     0},
    {"Pop / rock",
     {{dKick, 60, 12, 14, 160, 0.15f, 25, 3000, 0.95f, 0.25f, 0.92f, 0},
      {dSnare, 190, 3, 14, 125, 0.72f, 260, 3100, 0.45f, 0.2f, 0.72f, 0},
      {dClap, 0, 0, 0, 0, 1, 200, 1200, 0, 0.1f, 0.55f, 0.05f},
      {dHat, 410, 0, 0, 60, 0.7f, 55, 8200, 0, 0, 0.3f, 0.22f},
      {dHat, 410, 0, 0, 460, 0.7f, 420, 7800, 0, 0, 0.24f, 0.22f},
      {dTom, 92, 5, 80, 460, 0.12f, 60, 700, 0.3f, 0.1f, 0.6f, -0.25f},
      {dCymbal, 430, 0, 0, 2300, 0.7f, 2000, 6500, 0, 0, 0.24f, -0.15f},
      {dShaker, 0, 0, 0, 0, 1, 70, 6000, 0, 0, 0.24f, -0.15f}},
     0},
    {"Electrónica",
     {{dKick, 48, 30, 12, 260, 0, 0, 0, 0.85f, 0.6f, 0.95f, 0},
      {dSnare, 215, 5, 10, 80, 0.82f, 180, 5200, 0.3f, 0.2f, 0.6f, 0},
      {dClap, 0, 0, 0, 0, 1, 260, 1200, 0, 0.15f, 0.65f, 0.05f},
      {dHat, 380, 0, 0, 40, 0.6f, 35, 11000, 0, 0, 0.3f, 0.15f},
      {dHat, 380, 0, 0, 380, 0.6f, 340, 10000, 0, 0, 0.25f, 0.15f},
      {dTom, 120, 10, 50, 260, 0.05f, 40, 1000, 0.2f, 0.2f, 0.52f, -0.2f},
      {dCymbal, 470, 0, 0, 1800, 0.6f, 1500, 8000, 0, 0, 0.22f, -0.1f},
      {dRim, 1900, 0, 0, 18, 0.35f, 15, 3000, 0.4f, 0, 0.4f, 0.12f}},
     0},
    {"Lo-fi",
     {{dKick, 55, 14, 20, 180, 0.05f, 30, 1200, 0.35f, 0.2f, 0.88f, 0},
      {dSnare, 180, 3, 16, 100, 0.55f, 160, 1900, 0.2f, 0.3f, 0.66f, 0},
      {dClap, 0, 0, 0, 0, 1, 170, 1000, 0, 0.1f, 0.5f, 0.05f},
      {dHat, 280, 0, 0, 50, 0.5f, 45, 5600, 0, 0, 0.28f, 0.2f},
      {dHat, 280, 0, 0, 250, 0.5f, 220, 5200, 0, 0, 0.22f, 0.2f},
      {dTom, 90, 6, 60, 280, 0.08f, 40, 700, 0.2f, 0.2f, 0.5f, -0.2f},
      {dCymbal, 380, 0, 0, 1000, 0.5f, 900, 5200, 0, 0, 0.2f, -0.12f},
      {dShaker, 0, 0, 0, 0, 1, 60, 4600, 0, 0, 0.22f, -0.15f}},
     6500},
};

// ---------------------------------------------------------------- melódicos

SynthPatch synthBase() {
    SynthPatch p{};
    p.mode = mSub;
    p.wave1 = wSaw;
    p.wave2 = wSaw;
    p.mix2 = 0.0f;
    p.semi2 = 0.0f;
    p.detune = 0.0f;
    p.unison = 1;
    p.spread = 0.0f;
    p.sub = 0.0f;
    p.noise = 0.0f;
    p.cutoff = 4000.0f;
    p.reso = 0.1f;
    p.envAmt = 0.0f;
    p.a = 0.003f;
    p.d = 0.3f;
    p.s = 0.8f;
    p.r = 0.2f;
    p.fa = 0.001f;
    p.fd = 0.3f;
    p.fs = 0.0f;
    p.fr = 0.3f;
    p.fmRatio = 1.0f;
    p.fmIndex = 0.0f;
    p.fmDecay = 1.0f;
    p.tine = 0.0f;
    p.vibrato = 0.0f;
    p.drive = 0.0f;
    p.gain = 0.3f;
    p.mono = false;
    p.glideMs = 0.0f;
    return p;
}

template <typename F>
MelodicPreset synth(const char *name, F f) {
    MelodicPreset m{};
    m.name = name;
    m.voicing = vSynth;
    m.synth = synthBase();
    f(m.synth);
    return m;
}

// {T60, brillo, pulsada, ataque, al soltar, rasgueo ms, caja, distorsión, amplificador Hz, volumen}
MelodicPreset strung(const char *name, StringPatch s) {
    MelodicPreset m{};
    m.name = name;
    m.voicing = vString;
    m.synth = synthBase();
    m.str = s;
    return m;
}

// {golpe st, golpe ms, T60, saturación, click, filtro Hz, deslizamiento ms, armónico, volumen}
MelodicPreset boom(BoomPatch b) {
    MelodicPreset m{};
    m.name = b.name;
    m.voicing = vBoom;
    m.synth = synthBase();
    m.boom = b;
    return m;
}

const std::vector<MelodicPreset> &presets808() {
    static const std::vector<MelodicPreset> v = {
        boom({"808 clásico", 12, 18, 1.7f, 0.3f, 0.25f, 6000, 0, 0.06f, 0.82f}),
        boom({"808 distorsionado", 12, 15, 1.9f, 0.8f, 0.3f, 4800, 0, 0.0f, 0.62f}),
        boom({"808 corto", 19, 12, 0.5f, 0.4f, 0.5f, 7000, 0, 0.05f, 0.85f}),
        boom({"808 deslizante", 9, 20, 2.3f, 0.45f, 0.2f, 5000, 110, 0.05f, 0.75f}),
        boom({"Sub limpio", 4, 20, 2.0f, 0.08f, 0.05f, 2500, 0, 0.0f, 0.9f}),
    };
    return v;
}

const std::vector<MelodicPreset> &presetsBass() {
    static const std::vector<MelodicPreset> v = {
        strung("Bajo eléctrico", {2.6f, 0.25f, 0.22f, 0.3f, 0.07f, 0, 0, 0.12f, 0, 0.6f}),
        synth("Bajo sintetizado", [](SynthPatch &p) {
            p.wave1 = wSaw; p.wave2 = wSquare; p.mix2 = 0.45f; p.sub = 0.35f; p.cutoff = 480; p.reso = 0.25f;
            p.envAmt = 2.6f; p.fd = 0.22f; p.fs = 0.15f; p.a = 0.002f; p.d = 0.3f; p.s = 0.8f; p.r = 0.08f;
            p.drive = 0.25f; p.gain = 0.55f; p.mono = true; p.glideMs = 35;
        }),
        synth("Sub", [](SynthPatch &p) {
            p.wave1 = wSine; p.wave2 = wTri; p.mix2 = 0.18f; p.semi2 = 12; p.cutoff = 2500; p.reso = 0;
            p.a = 0.004f; p.d = 0.1f; p.s = 1.0f; p.r = 0.08f; p.drive = 0.1f; p.gain = 0.6f; p.mono = true;
            p.glideMs = 40;
        }),
        synth("Acid", [](SynthPatch &p) {
            p.wave1 = wSaw; p.cutoff = 280; p.reso = 0.78f; p.envAmt = 3.6f; p.fd = 0.18f; p.fs = 0.0f;
            p.a = 0.002f; p.d = 0.25f; p.s = 0.7f; p.r = 0.05f; p.drive = 0.45f; p.gain = 0.45f; p.mono = true;
            p.glideMs = 60;
        }),
        synth("Bajo pulsado", [](SynthPatch &p) {
            p.wave1 = wSquare; p.wave2 = wSaw; p.mix2 = 0.5f; p.cutoff = 380; p.reso = 0.2f; p.envAmt = 3.0f;
            p.fd = 0.12f; p.fs = 0.0f; p.a = 0.002f; p.d = 0.45f; p.s = 0.25f; p.r = 0.1f; p.sub = 0.4f;
            p.drive = 0.2f; p.gain = 0.5f; p.mono = true;
        }),
    };
    return v;
}

const std::vector<MelodicPreset> &presetsGuitar() {
    static const std::vector<MelodicPreset> v = {
        strung("Acústica", {3.2f, 0.62f, 0.13f, 0.8f, 0.1f, 11, 0.75f, 0, 0, 0.2f}),
        strung("Nylon (clásica)", {2.6f, 0.35f, 0.2f, 0.45f, 0.12f, 14, 0.85f, 0, 0, 0.21f}),
        strung("Eléctrica limpia", {4.0f, 0.75f, 0.1f, 0.85f, 0.08f, 8, 0, 0.15f, 6500, 0.38f}),
        strung("Eléctrica distorsionada", {5.0f, 0.8f, 0.12f, 0.9f, 0.08f, 6, 0, 0.9f, 4200, 0.32f}),
        strung("Muteada", {0.2f, 0.45f, 0.15f, 0.6f, 0.05f, 5, 0, 0.6f, 4500, 0.36f}),
    };
    return v;
}

const std::vector<MelodicPreset> &presetsSynth() {
    static const std::vector<MelodicPreset> v = {
        synth("Pluck", [](SynthPatch &p) {
            p.wave1 = wSaw; p.unison = 2; p.detune = 9; p.spread = 0.5f; p.cutoff = 700; p.reso = 0.2f;
            p.envAmt = 4.2f; p.fd = 0.17f; p.fs = 0.0f; p.fr = 0.2f; p.a = 0.001f; p.d = 0.4f; p.s = 0.0f;
            p.r = 0.25f; p.gain = 0.45f;
        }),
        synth("Pad", [](SynthPatch &p) {
            p.wave1 = wSaw; p.unison = 5; p.detune = 18; p.spread = 0.9f; p.wave2 = wSaw; p.mix2 = 0.2f;
            p.semi2 = 12; p.cutoff = 1300; p.reso = 0.1f; p.envAmt = 1.0f; p.fa = 0.8f; p.fd = 1.5f; p.fs = 0.6f;
            p.fr = 1.5f; p.a = 0.6f; p.d = 1.0f; p.s = 0.85f; p.r = 1.4f; p.gain = 0.2f;
        }),
        synth("Lead", [](SynthPatch &p) {
            p.wave1 = wSaw; p.wave2 = wSquare; p.mix2 = 0.5f; p.unison = 2; p.detune = 7; p.spread = 0.3f;
            p.cutoff = 2400; p.reso = 0.3f; p.envAmt = 1.5f; p.fd = 0.3f; p.fs = 0.5f; p.a = 0.005f; p.d = 0.2f;
            p.s = 0.8f; p.r = 0.15f; p.vibrato = 12; p.drive = 0.2f; p.gain = 0.4f; p.mono = true;
            p.glideMs = 45;
        }),
        synth("Supersaw", [](SynthPatch &p) {
            p.wave1 = wSaw; p.unison = 5; p.detune = 26; p.spread = 1.0f; p.cutoff = 6500; p.reso = 0.08f;
            p.envAmt = 0.5f; p.fd = 0.4f; p.fs = 0.6f; p.a = 0.003f; p.d = 0.3f; p.s = 0.9f; p.r = 0.35f;
            p.gain = 0.2f;
        }),
        synth("Piano eléctrico", [](SynthPatch &p) {
            p.mode = mFm; p.fmRatio = 1.0f; p.fmIndex = 2.2f; p.fmDecay = 0.8f; p.tine = 0.35f; p.a = 0.002f;
            p.d = 2.6f; p.s = 0.0f; p.r = 0.35f; p.gain = 0.22f; p.spread = 0.3f;
        }),
        synth("Órgano", [](SynthPatch &p) {
            p.mode = mOrgan; p.a = 0.004f; p.d = 0.1f; p.s = 1.0f; p.r = 0.07f; p.vibrato = 7; p.gain = 0.22f;
            p.spread = 0.4f;
        }),
        synth("Campanas", [](SynthPatch &p) {
            p.mode = mFm; p.fmRatio = 3.5f; p.fmIndex = 3.2f; p.fmDecay = 1.4f; p.a = 0.002f; p.d = 3.2f;
            p.s = 0.0f; p.r = 1.6f; p.gain = 0.28f; p.spread = 0.3f;
        }),
        synth("Cuerdas", [](SynthPatch &p) {
            p.wave1 = wSaw; p.unison = 3; p.detune = 12; p.spread = 0.8f; p.cutoff = 2800; p.reso = 0.05f;
            p.envAmt = 0.6f; p.fa = 0.4f; p.fd = 1.0f; p.fs = 0.7f; p.fr = 0.8f; p.a = 0.35f; p.d = 0.5f;
            p.s = 0.9f; p.r = 0.8f; p.vibrato = 8; p.gain = 0.22f;
        }),
    };
    return v;
}

const std::vector<MelodicPreset> &listFor(int kind) {
    switch (kind) {
        case k808: return presets808();
        case kBass: return presetsBass();
        case kGuitar: return presetsGuitar();
        default: return presetsSynth();
    }
}

}  // namespace

const Kit &kit(int preset) { return kKits[std::clamp(preset, 0, kitCount() - 1)]; }
int kitCount() { return static_cast<int>(sizeof kKits / sizeof kKits[0]); }

const MelodicPreset &melodic(int kind, int preset) {
    const auto &v = listFor(kind);
    return v[static_cast<size_t>(std::clamp(preset, 0, static_cast<int>(v.size()) - 1))];
}
int melodicCount(int kind) { return static_cast<int>(listFor(kind).size()); }

const ParamInfo &paramInfo(int p) { return kInfo[std::clamp(p, 0, kParamCount - 1)]; }

bool paramApplies(int kind, int p) {
    switch (kind) {
        case kDrums: return p == kTone || p == kDecay || p == kDrive || p == kTune;
        case k808: return p != kAttack;
        case kGuitar: return p != kGlide;
        default: return p >= 0 && p < kParamCount;
    }
}

Values defaultValues() {
    Values v{};
    for (int p = 0; p < kParamCount; ++p) v[static_cast<size_t>(p)] = kInfo[p].def;
    return v;
}

float clampParam(int p, float v) {
    const ParamInfo &i = paramInfo(p);
    if (!std::isfinite(v)) v = i.def;
    v = std::clamp(v, i.min, i.max);
    v = static_cast<float>(std::round(static_cast<double>(v) / i.step) * i.step);
    return std::clamp(v, i.min, i.max);
}

const char *kindName(int kind) { return kKindNames[std::clamp(kind, 0, kKindCount - 1)]; }

int presetCount(int kind) { return kind == kDrums ? kitCount() : melodicCount(kind); }

const char *presetName(int kind, int preset) {
    return kind == kDrums ? kit(preset).name : melodic(kind, preset).name;
}

const char *drumName(int piece) { return kDrumNames[std::clamp(piece, 0, kDrumPieces - 1)]; }

void noteRange(int kind, int &lo, int &hi) {
    switch (kind) {
        case kDrums: lo = 0; hi = kDrumPieces - 1; break;
        case k808: lo = 24; hi = 50; break;     // Do1 .. Re3
        case kBass: lo = 28; hi = 55; break;    // Mi1 .. Sol3
        case kGuitar: lo = 40; hi = 79; break;  // Mi2 .. Sol5
        default: lo = 36; hi = 96; break;       // Do2 .. Do7
    }
}

int homePitch(int kind) {
    switch (kind) {
        case kDrums: return 0;
        case k808: return 33;
        case kBass: return 36;
        case kGuitar: return 52;
        default: return 60;
    }
}

Sound clampSound(Sound s) {
    s.kind = std::clamp(s.kind, 0, kKindCount - 1);
    s.preset = std::clamp(s.preset, 0, presetCount(s.kind) - 1);
    for (int p = 0; p < kParamCount; ++p) s.values[static_cast<size_t>(p)] = clampParam(p, s.values[static_cast<size_t>(p)]);
    return s;
}

bool isMono(const Sound &s) {
    if (s.kind == k808) return true;
    if (s.kind == kBass || s.kind == kSynth) {
        const MelodicPreset &m = melodic(s.kind, s.preset);
        return m.voicing == vSynth ? m.synth.mono : s.kind == kBass;
    }
    return false;
}

}  // namespace inst
