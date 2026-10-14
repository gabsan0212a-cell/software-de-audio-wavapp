#pragma once

#include "dsp/Delay.h"

// Doblaje de voz (como grabar la voz dos veces): dos copias apenas atrasadas y desafinadas
// (unos 5 a 8 cents, moviéndose lento), una a cada lado. Ensancha la voz sin que suene a eco.
class Doubler {
public:
    void allocate(int sampleRate);  // hilo de la interfaz
    void restart();
    // Suma las dos copias a outL/outR, con `mix` (0..1).
    void process(const float *in, int n, float *outL, float *outR, float mix);

private:
    int mRate = 48000;
    DelayLine mLine;
    double mPhase[2] = {0.0, 0.25};
    float mLp[2] = {0.0f, 0.0f};  // las copias, un poco más oscuras
    float mLpCoef = 0.5f;
};
