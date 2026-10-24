#pragma once

#include <array>
#include <atomic>

#include "dsp/AutoTune.h"
#include "dsp/Delay.h"
#include "dsp/Doubler.h"
#include "dsp/Dynamics.h"
#include "dsp/Reverb.h"
#include "dsp/VoiceDynamics.h"

// Efectos de una pista: los de la voz en vivo antes del EQ (graves, compuerta y auto-tune),
// de-esser y compresor después, y tres envíos (doblaje, delay y reverb) que se suman aparte,
// después del volumen y el paneo de la pista (como en un estudio). Sus ajustes son una lista de
// números (Param); cada uno tiene su rango, su valor inicial y su paso.
namespace fx {

enum Param : int {
    kCompOn = 0, kCompThreshold, kCompRatio, kCompAttack, kCompRelease, kCompMakeup,
    kDelayOn, kDelayTime, kDelayFeedback, kDelayMix, kDelayTone, kDelayPingPong,
    kReverbOn, kReverbMix, kReverbDecay, kReverbTone, kReverbPredelay,
    // Los de la voz en vivo (engine/VoiceFx), para las grabaciones:
    kHpOn, kHpFreq,
    kGateOn, kGateThreshold,
    kTuneOn, kTuneAmount, kTuneSpeed, kTuneKey, kTuneScale, kTuneRange,
    kDeessOn, kDeessThreshold,
    kDoublerOn, kDoublerMix,
    kCount
};
// kTuneScale y kTuneRange: como en la voz en vivo (vfx::ScaleChoice, vfx::rangeHz).

struct Info {
    const char *key;  // nombre en la sesión
    float min, max, def;
    double step;  // en double: 0.01 redondea a lo mismo que escribir 0.16f
    bool log;     // el control va en escala logarítmica (tiempos, frecuencias, proporción)
};

using Values = std::array<float, kCount>;

const Info &info(int p);
Values defaults();
// Dentro del rango y redondeado a su paso (los interruptores quedan en 0 o 1).
float clampValue(int p, float v);
bool anyOn(const Values &v);

struct Preset {
    const char *name;
    Values values;
};
int presetCount();
const Preset &preset(int i);

}  // namespace fx

class TrackFx {
public:
    static constexpr int kMaxBlock = 64;  // el mezclador procesa de a 64 cuadros
    // Lo que se atrasa el auto-tune (en promedio, con voz): el mezclador lee la pista esto antes,
    // así la voz afinada cae en su lugar.
    static constexpr int kTuneLatency = 128;

    explicit TrackFx(int sampleRate);  // reserva la memoria (hilo de la interfaz)

    // ---------- Hilo de la interfaz
    void set(int p, float v) {
        if (p >= 0 && p < fx::kCount) mParams[static_cast<size_t>(p)].store(v, std::memory_order_relaxed);
    }
    float get(int p) const { return mParams[static_cast<size_t>(p)].load(std::memory_order_relaxed); }
    void copyParams(const TrackFx &o);
    float takeReduction() { return mReduction.exchange(0.0f, std::memory_order_relaxed); }  // dB del compresor
    // La escala "de la canción" del auto-tune (-1 = no se sabe: todas las notas).
    void setSongKey(int key, bool minor);
    // Cuadros que hay que adelantar la lectura de la pista (el auto-tune atrasa).
    int latency() const { return get(fx::kTuneOn) > 0.5f ? kTuneLatency : 0; }

    // ---------- Hilo de audio (n ≤ kMaxBlock)
    void restart();  // al arrancar desde silencio: delay y reverb sin ecos viejos
    // Antes del EQ: graves, compuerta y auto-tune (el auto-tune deja la pista en mono).
    void processPre(float *l, float *r, int n);
    void processInsert(float *l, float *r, int n);  // de-esser y compresor; `r` puede ser nullptr
    // Escribe los envíos (estéreo) en wetL/wetR. false si no hay nada (no hace falta sumarlos).
    bool processSends(const float *l, const float *r, int n, float *wetL, float *wetR);

private:
    bool on(int p) const { return get(p) > 0.5f; }

    std::array<std::atomic<float>, fx::kCount> mParams;
    std::atomic<float> mReduction{0.0f};
    std::atomic<int> mSongKey{-1};
    std::atomic<bool> mSongMinor{false};
    int mRate;
    Compressor mComp;
    StereoDelay mDelay;
    Reverb mReverb;
    MonoBiquad mHpL, mHpR;
    float mHpFreq = -1.0f;
    NoiseGate mGateL, mGateR;
    AutoTune mTune;
    int mRange = -1;
    bool mTuneWasOn = false;
    DeEsser mDeessL, mDeessR;
    Doubler mDoubler;
    // Cantidad suavizada de cada envío (0 = apagado y en silencio) y si hay que vaciarlo al prender.
    float mDelayGain = 0.0f, mReverbGain = 0.0f, mDoublerGain = 0.0f, mSmooth;
    bool mDelayIdle = true, mReverbIdle = true, mDoublerIdle = true;
    float mTmpL[kMaxBlock], mTmpR[kMaxBlock], mMono[kMaxBlock];
};
