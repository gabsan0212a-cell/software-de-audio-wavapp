#pragma once

#include <array>
#include <atomic>

#include "dsp/AutoTune.h"
#include "dsp/Delay.h"
#include "dsp/Doubler.h"
#include "dsp/Dynamics.h"
#include "dsp/Reverb.h"
#include "dsp/VoiceDynamics.h"
#include "engine/Effects.h"

// La voz en vivo para cantar sobre la pista (mono):
//   graves → compuerta → auto-tune → (EQ de la entrada) → de-esser → compresor → volumen,
// y sale en estéreo con el doblaje y los envíos de delay (a tiempo con la canción) y reverb.
// Sus ajustes son una lista de números, como los efectos de las pistas.
namespace vfx {

enum Param : int {
    kOn = 0,
    kTuneOn, kTuneAmount, kTuneSpeed, kTuneKey, kTuneScale, kRange,
    kHighPass,
    kGateOn, kGateThreshold,
    kDeessOn, kDeessThreshold,
    kCompOn, kCompThreshold, kCompRatio, kCompMakeup,
    kDoublerOn, kDoublerMix,
    kDelayOn, kDelayBeats, kDelayFeedback, kDelayMix,
    kReverbOn, kReverbMix, kReverbDecay,
    kLevel,
    kCount
};

// kTuneScale: 0 = la de la canción preparada, 1 mayor, 2 menor, 3 cromática (todas las notas).
enum ScaleChoice : int { kScaleSong = 0, kScaleMajorChoice, kScaleMinorChoice, kScaleChromaticChoice };
// kRange: 0 grave, 1 media, 2 aguda (dónde busca la nota).
void rangeHz(int choice, float &minHz, float &maxHz);
// kDelayBeats: 0 = 1/4, 1 = 1/8 con punto, 2 = 1/8, 3 = 1/16 (en pulsos).
float delayBeats(int choice);

using Info = fx::Info;
using Values = std::array<float, kCount>;

const Info &info(int p);
Values defaults();
float clampValue(int p, float v);  // dentro del rango y redondeado a su paso

struct Preset {
    const char *name;
    Values values;
};
int presetCount();
const Preset &preset(int i);

}  // namespace vfx

class VoiceFx {
public:
    static constexpr int kMaxBlock = 256;
    // Datos para la pantalla: [nota que se canta (MIDI con decimales; 0 = nada), como sale,
    // nota de la escala (-1 = nada), compuerta abierta, de-esser (dB), compresor (dB),
    // retraso del auto-tune (ms), tónica en uso (-1 = cromática), escala en uso (0 mayor, 1 menor,
    // 2 cromática), la escala viene de la canción (1/0)].
    static constexpr int kMeterCount = 10;

    explicit VoiceFx(int sampleRate);  // reserva la memoria (hilo de la interfaz)

    // ---------- Hilo de la interfaz
    void set(int p, float v);
    float get(int p) const { return mParams[static_cast<size_t>(p)].load(std::memory_order_relaxed); }
    bool on() const { return get(vfx::kOn) > 0.5f; }
    void setSongKey(int key, bool minor);  // la de la canción preparada (-1 = no se sabe)
    void setTempo(float bpm);              // para el delay (0 = no se sabe: 120)
    void meters(float *out);               // kMeterCount valores; reinicia los picos

    // ---------- Hilo de audio (mono, n ≤ kMaxBlock)
    void restart();
    // Antes del EQ de la entrada: graves, compuerta y auto-tune (en el lugar).
    void processPre(float *x, int n);
    // Después del EQ: de-esser, compresor y volumen; sale en estéreo con doblaje, delay y reverb.
    void processPost(float *x, int n, float *outL, float *outR);

private:
    bool flag(int p) const { return get(p) > 0.5f; }
    void resolveScale(int &key, int &scale, bool &fromSong) const;

    int mRate;
    std::array<std::atomic<float>, vfx::kCount> mParams;
    std::atomic<int> mSongKey{-1};
    std::atomic<bool> mSongMinor{false};
    std::atomic<float> mBpm{0.0f};

    MonoBiquad mHp;
    float mHpFreq = -1.0f;
    int mRange = -1;
    NoiseGate mGate;
    AutoTune mTune;
    DeEsser mDeess;
    Compressor mComp;
    Doubler mDoubler;
    StereoDelay mDelay;
    Reverb mReverb;

    // Lo que se va suavizando (hilo de audio)
    float mLevelS = 1.0f, mDoublerS = 0.0f, mDelayS = 0.0f, mReverbS = 0.0f, mSmooth;
    bool mDelayIdle = true, mReverbIdle = true;
    float mWetL[kMaxBlock], mWetR[kMaxBlock];

    // Para la pantalla
    std::atomic<bool> mGateOpen{false};
    std::atomic<float> mDeessPeak{0.0f}, mCompPeak{0.0f};
};
