#pragma once

// Herramientas para una voz en vivo (mono): filtro de graves, compuerta de ruido y de-esser.

// Filtro de dos polos: pasa-altos (el retumbe del micrófono) o pasa-banda (las "eses").
class MonoBiquad {
public:
    void highPass(double freq, double q, int sampleRate);
    // Pasa-banda con 0 dB en el centro: x + (g - 1)·banda es un EQ de campana con ganancia g.
    void bandPass(double freq, double q, int sampleRate);
    void reset() { mZ1 = mZ2 = 0.0; }
    inline float run(float x) {
        const double y = mB0 * x + mZ1;
        mZ1 = mB1 * x - mA1 * y + mZ2;
        mZ2 = mB2 * x - mA2 * y;
        return static_cast<float>(y);
    }
    void process(float *x, int n) {
        for (int i = 0; i < n; ++i) x[i] = run(x[i]);
        if (mZ1 > -1e-20 && mZ1 < 1e-20) mZ1 = 0.0;  // sin números diminutos (lentos)
        if (mZ2 > -1e-20 && mZ2 < 1e-20) mZ2 = 0.0;
    }

private:
    double mB0 = 1, mB1 = 0, mB2 = 0, mA1 = 0, mA2 = 0, mZ1 = 0, mZ2 = 0;
};

struct GateSettings {
    float thresholdDb = -50.0f;  // abre arriba de esto
    float rangeDb = -30.0f;      // cuánto baja cerrada (no es silencio total: suena natural)
    float holdMs = 90.0f;        // espera antes de empezar a cerrar
    float releaseMs = 160.0f;
};

// Compuerta: entre frases baja el ruido del lugar, el aire y lo que se cuela de las bocinas.
// Abre en ~0.5 ms; cierra 6 dB más abajo de donde abre (no parpadea) y después de esperar.
class NoiseGate {
public:
    void setup(int sampleRate);
    void reset();
    // Mono, en el lugar.
    void process(float *x, int n, const GateSettings &s);
    bool open() const { return mOpen; }
    float gainDb() const;  // cuánto está bajando ahora (≤ 0)

private:
    int mRate = 48000;
    float mEnv = 0.0f;   // pico con caída
    float mGain = 1.0f;  // ganancia actual (lineal)
    bool mOpen = false;
    int mHold = 0;
};

struct DeEssSettings {
    float thresholdDb = -30.0f;  // nivel de las "eses" desde donde empieza a bajar
    float freqHz = 6500.0f;      // centro de la banda de las "eses" (~4 a 10 kHz)
    float maxCutDb = 12.0f;
};

// De-esser: cuando las "s", "ch" y "z" pasan del umbral, baja solo esa banda (4:1) con un EQ de
// campana que se mueve: lo demás de la voz queda igual.
class DeEsser {
public:
    void setup(int sampleRate);
    void reset();
    // Mono, en el lugar. Devuelve la reducción máxima (dB, ≥ 0).
    float process(float *x, int n, const DeEssSettings &s);

private:
    int mRate = 48000;
    float mFreq = -1.0f;
    MonoBiquad mBand;
    float mEnv = 0.0f, mCutDb = 0.0f;
};
