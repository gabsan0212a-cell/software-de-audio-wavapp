#pragma once

#include <array>
#include <atomic>

#include "dsp/Delay.h"
#include "dsp/Dynamics.h"
#include "dsp/Reverb.h"

// Efectos de una pista: un compresor en la señal y dos envíos (delay y reverb) que se suman
// aparte, después del volumen y el paneo de la pista (como en un estudio). Sus ajustes son una
// lista de números (Param); cada uno tiene su rango, su valor inicial y su paso.
namespace fx {

enum Param : int {
    kCompOn = 0, kCompThreshold, kCompRatio, kCompAttack, kCompRelease, kCompMakeup,
    kDelayOn, kDelayTime, kDelayFeedback, kDelayMix, kDelayTone, kDelayPingPong,
    kReverbOn, kReverbMix, kReverbDecay, kReverbTone, kReverbPredelay,
    kCount
};

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

    explicit TrackFx(int sampleRate);  // reserva la memoria (hilo de la interfaz)

    // ---------- Hilo de la interfaz
    void set(int p, float v) {
        if (p >= 0 && p < fx::kCount) mParams[static_cast<size_t>(p)].store(v, std::memory_order_relaxed);
    }
    float get(int p) const { return mParams[static_cast<size_t>(p)].load(std::memory_order_relaxed); }
    void copyParams(const TrackFx &o);
    float takeReduction() { return mReduction.exchange(0.0f, std::memory_order_relaxed); }  // dB del compresor

    // ---------- Hilo de audio (n ≤ kMaxBlock)
    void restart();  // al arrancar desde silencio: delay y reverb sin ecos viejos
    void processInsert(float *l, float *r, int n);  // compresor; `r` puede ser nullptr
    // Escribe los envíos (estéreo) en wetL/wetR. false si no hay nada (no hace falta sumarlos).
    bool processSends(const float *l, const float *r, int n, float *wetL, float *wetR);

private:
    bool on(int p) const { return get(p) > 0.5f; }

    std::array<std::atomic<float>, fx::kCount> mParams;
    std::atomic<float> mReduction{0.0f};
    Compressor mComp;
    StereoDelay mDelay;
    Reverb mReverb;
    // Cantidad suavizada de cada envío (0 = apagado y en silencio) y si hay que vaciarlo al prender.
    float mDelayGain = 0.0f, mReverbGain = 0.0f, mSmooth;
    bool mDelayIdle = true, mReverbIdle = true;
    float mTmpL[kMaxBlock], mTmpR[kMaxBlock];
};
