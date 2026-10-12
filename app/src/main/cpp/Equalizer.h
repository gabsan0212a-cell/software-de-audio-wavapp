#pragma once

#include <atomic>
#include <cstdint>

constexpr int kMaxBands = 12;

// Mismos tipos y fórmulas que BiquadFilterNode de Web Audio (los del prototipo).
enum FilterType : int { kPeaking = 0, kLowShelf, kHighShelf, kHighPass, kLowPass, kNotch, kFilterTypes };

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;  // normalizados (a0 = 1)
};

Biquad designBiquad(int type, double freq, double gainDb, double q, double sampleRate);
double biquadMagnitudeDb(const Biquad &c, double freq, double sampleRate);
bool filterHasGain(int type);

// EQ paramétrico de una pista: hasta 12 bandas que se agregan y quitan. Solo se procesan
// las bandas que existen y están activas; sin bandas no hace ningún cálculo.
// Después de los filtros va la auto-ganancia: baja el nivel lo que el EQ sube, para que no
// se recorte (igual que el prototipo).
class Equalizer {
public:
    void setSampleRate(int sampleRate) { mRate = sampleRate; }

    // ---------- Hilo de la interfaz
    void setBand(int band, bool present, bool on, int type, float freq, float gainDb, float q);
    void setBypass(bool on);
    void setAutoGain(bool on);
    void clear();  // sin bandas, sin bypass, auto-ganancia encendida
    float trimDb() const { return mTrimDb.load(); }  // compensación actual (≤ 0)
    // Respuesta en dB en `freqs`. band = -1: todo el EQ (plano si está en bypass);
    // band >= 0: solo esa banda, si existe.
    void response(int band, const float *freqs, float *outDb, int n) const;

    // ---------- Hilo de audio
    void resetState();
    // Filtra en el lugar. `r` puede ser nullptr (pista mono).
    void process(float *l, float *r, int n);

private:
    struct Params {
        std::atomic<bool> present{false}, on{true};
        std::atomic<int> type{kPeaking};
        std::atomic<float> freq{1000.0f}, gain{0.0f}, q{1.0f};
    };
    struct State {
        bool active = false;
        int type = kPeaking;
        double f = 1000, g = 0, q = 1;
        Biquad c;
        double z1[2] = {0, 0}, z2[2] = {0, 0};
    };
    Biquad targetBiquad(int band) const;
    void recomputeTrim();

    int mRate = 48000;
    Params mParams[kMaxBands];
    std::atomic<bool> mBypass{false}, mAuto{true};
    std::atomic<float> mTrimDb{0.0f};

    // Estado del hilo de audio
    State mState[kMaxBands];
    double mTrimS = 1.0;
};
