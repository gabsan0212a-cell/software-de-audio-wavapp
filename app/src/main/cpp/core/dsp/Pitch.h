#pragma once

#include <cmath>
#include <vector>

// Detector de tono YIN (de Cheveigné y Kawahara, 2002) para una voz o un instrumento de una sola
// nota a la vez. Sirve para analizar la voz original de la canción y para el auto-tune en vivo.
class PitchDetector {
public:
    // `window` muestras por análisis; busca entre minHz y maxHz.
    PitchDetector(int sampleRate, int window, float minHz = 70.0f, float maxHz = 1000.0f);

    struct Result {
        float hz = 0.0f;         // 0 = sin tono (ruido, consonante o silencio)
        float clarity = 0.0f;    // 1 - aperiodicidad (1 = tono puro)
    };
    // Analiza x[0 .. window + maxLag()) (necesita ese largo).
    Result detect(const float *x, float threshold = 0.15f);

    int window() const { return mW; }
    int maxLag() const { return mMaxLag; }
    int needed() const { return mW + mMaxLag; }

private:
    int mRate, mW, mMinLag, mMaxLag;
    std::vector<float> mDiff;
};

// Nota MIDI (con decimales) de una frecuencia: 69 = La 440 Hz.
inline float hzToMidi(float hz) { return 69.0f + 12.0f * std::log2(hz / 440.0f); }
inline float midiToHz(float midi) { return 440.0f * std::exp2((midi - 69.0f) / 12.0f); }
