#pragma once

#include <cstdint>
#include <vector>

// Volumen percibido como lo miden Spotify, YouTube o la TV (ITU-R BS.1770-4 y EBU R128):
// ponderación K, energía en bloques de 100 ms, momentáneo (400 ms), corto plazo (3 s),
// integrado con compuertas (absoluta −70 LUFS y relativa −10 LU), rango de volumen (LRA,
// EBU Tech 3342) y pico real (sobremuestreo ×4).

constexpr float kLoudnessNone = -200.0f;  // "sin dato"

struct LoudnessBlock {
    double energy = 0.0;  // potencia media ponderada K de 100 ms, sumada en los dos canales
    float peak = 0.0f;    // pico real del bloque (lineal)
    uint32_t gen = 0;     // a qué medición pertenece (el mezclador la cambia al reiniciar)
};

// Lo que va pegado al audio (barato): filtros K, suma de 100 ms y pico real.
class LoudnessFrontEnd {
public:
    explicit LoudnessFrontEnd(int sampleRate = 48000);
    void reset();
    // Procesa n cuadros estéreo (no los cambia). Devuelve cuántos bloques de 100 ms terminó
    // (los deja en `out`, hasta `max`; con n ≤ 4800 es a lo más uno).
    int process(const float *l, const float *r, int n, LoudnessBlock *out, int max);

private:
    struct Bq {
        double b0, b1, b2, a1, a2;
    };
    int mRate, mBlockFrames;
    Bq mPre, mHp;
    double mZ[2][4] = {};  // estado de los dos filtros por canal
    double mSum = 0.0;
    int mCount = 0;
    float mPeak = 0.0f;
    static constexpr int kTaps = 12, kPhases = 4;
    float mHist[2][2 * kTaps] = {};  // últimas muestras de cada canal, dos veces (para el pico real)
    int mHistPos = 0;
    float mCoef[kPhases][kTaps];
};

// Las cuentas a partir de los bloques de 100 ms. Las compuertas usan histogramas de 0.1 LU,
// así cada lectura cuesta lo mismo aunque la medición dure horas.
class LoudnessMeter {
public:
    static constexpr int kHistory = 600;  // 60 s de historia (10 valores por segundo)

    LoudnessMeter() { reset(); }
    void reset();
    void add(const LoudnessBlock &b);

    float momentary() const;      // LUFS (o kLoudnessNone)
    float shortTerm() const;
    float integrated() const;
    float range() const;          // LU
    float truePeakDb() const;     // dBTP
    float maxMomentary() const { return mMaxMomentary; }
    float maxShortTerm() const { return mMaxShortTerm; }
    double seconds() const { return mBlocks * 0.1; }
    int64_t blocks() const { return mBlocks; }
    // Los últimos n valores (10 por segundo, el más nuevo al final); kLoudnessNone donde no hay.
    void history(float *momentary, float *shortTerm, int n) const;

    static float toLufs(double energy);

private:
    static constexpr int kBins = 800;  // de −70 a +10 LUFS en pasos de 0.1
    struct Histogram {
        std::vector<double> sum;
        std::vector<int64_t> count;
        void clear();
        void add(double energy);
        // Promedio de energía de los valores sobre `aboveLufs` (con la compuerta absoluta).
        double meanAbove(float aboveLufs, int64_t *n) const;
    };
    double meanOfLast(int n) const;

    std::vector<double> mRecent;  // energías de los últimos 30 bloques (anillo)
    int64_t mBlocks = 0;
    Histogram mGating, mShort;    // bloques de 400 ms (integrado) y de 3 s cada 1 s (LRA)
    float mPeak = 0.0f;
    float mMaxMomentary = kLoudnessNone, mMaxShortTerm = kLoudnessNone;
    std::vector<float> mHistM, mHistS;  // anillos de historia
    int64_t mHistCount = 0;
};
