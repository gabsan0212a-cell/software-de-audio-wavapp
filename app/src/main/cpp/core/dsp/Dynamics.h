#pragma once

// Compresor de una pista: detector de picos estéreo (los dos canales juntos), rodilla suave
// de 6 dB, ataque y liberación en la ganancia (en dB) y ganancia de compensación. La ganancia
// se calcula cada 8 muestras y entre medio va en línea recta (así no cuesta casi nada).
struct CompSettings {
    float thresholdDb = -18.0f;
    float ratio = 3.0f;
    float attackMs = 10.0f;
    float releaseMs = 150.0f;
    float makeupDb = 0.0f;
};

class Compressor {
public:
    void setup(int sampleRate) {
        mRate = sampleRate;
        reset();
    }
    void reset() {
        mEnvDb = 0.0f;
        mGain = -1.0f;
    }
    // Comprime en el lugar (`r` puede ser nullptr: pista mono). Devuelve la reducción máxima
    // (dB, ≥ 0) de esta llamada.
    float process(float *l, float *r, int n, const CompSettings &s);
    // Curva estática: cuánto baja (dB, ≤ 0) una entrada de `inDb`.
    static float gainComputer(float inDb, float thresholdDb, float ratio, float kneeDb = 6.0f);

private:
    int mRate = 48000;
    float mEnvDb = 0.0f;  // reducción suavizada (≤ 0)
    float mGain = -1.0f;  // última ganancia lineal aplicada (-1 = todavía no)
};
