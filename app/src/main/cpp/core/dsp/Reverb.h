#pragma once

#include "dsp/Delay.h"

struct ReverbSettings {
    float decayS = 1.6f;       // tiempo en que la cola baja 60 dB
    float toneHz = 7000.0f;    // brillo de la cola
    float predelayMs = 20.0f;  // espera antes de que empiece
};

// Reverb de red de retardos con realimentación (FDN) de 8 líneas y matriz de Householder:
// pre-delay → 4 difusores (pasa-todo) → 8 líneas con pérdidas (tiempo y brillo) → salida
// estéreo decorrelacionada. Cuatro líneas se mueven un poco (modulación lenta) para que la cola
// no suene metálica. El nivel de la cola no depende del tiempo: "cantidad" es lo que manda.
class Reverb {
public:
    void allocate(int sampleRate);  // hilo de la interfaz
    void restart();
    // Escribe solo la reverb (sin la señal original) en outL/outR. `r` puede ser nullptr (mono).
    void process(const float *l, const float *r, int n, float *outL, float *outR, const ReverbSettings &s);

private:
    static constexpr int kLines = 8, kDiff = 4;
    int mRate = 48000;
    DelayLine mPre, mDiff[kDiff], mLine[kLines];
    int mDiffLen[kDiff] = {}, mLen[kLines] = {};
    float mDamp[kLines] = {};
    double mLfo = 0.0;  // fase de la modulación (ciclos)
};
