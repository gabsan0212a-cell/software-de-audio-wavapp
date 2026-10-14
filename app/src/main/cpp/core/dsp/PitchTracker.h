#pragma once

#include <vector>

// Sigue el tono de una voz en vivo (para el auto-tune). Baja la señal a ~16 kHz y cada ~4 ms
// busca el período con YIN sobre lo más reciente: la ventana está anclada al final, así la
// estimación describe los últimos milisegundos y no el centro de una ventana larga. Decide si
// hay nota con histéresis (no parpadea) y no se cree un salto de octava suelto.
class PitchTracker {
public:
    explicit PitchTracker(int sampleRate);

    // Rango de la voz (no reserva memoria: se puede cambiar en el hilo de audio).
    void setRange(float minHz, float maxHz);
    void reset();

    // ---------- Hilo de audio
    // Agrega `n` muestras (mono). Devuelve true si hubo una estimación nueva.
    bool process(const float *x, int n);
    bool voiced() const { return mVoiced; }
    float hz() const { return mVoiced ? mHz : 0.0f; }
    // Período de la nota en muestras de la frecuencia original (0 = sin nota).
    float period() const { return mVoiced ? static_cast<float>(mRate) / mHz : 0.0f; }
    float clarity() const { return mClarity; }
    float levelDb() const { return mLevelDb; }  // nivel de lo último analizado (dBFS)

    static constexpr float kFloorDb = -55.0f;  // más bajo que esto no se busca nota
    static constexpr float kLowestHz = 50.0f;  // la nota más grave que se puede buscar

private:
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float run(float x) {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    void analyze();

    int mRate, mFactor, mDecRate;
    int mHop, mWindow, mMinWindow, mMinLag = 2, mMaxLag = 2, mHistLen = 0;
    Biquad mLp[2];
    int mPhase = 0;               // cuenta para quedarse con 1 de cada mFactor muestras
    std::vector<float> mHist;     // lo último, a la frecuencia baja (el más nuevo al final)
    int mFill = 0;                // cuántas válidas hay en mHist
    int mSinceHop = 0;
    std::vector<float> mDiff;

    bool mVoiced = false;
    float mHz = 0.0f, mClarity = 0.0f, mLevelDb = -120.0f;
    int mOnCount = 0, mOffCount = 0;
    float mPendingHz = 0.0f;  // un salto de octava espera una confirmación
};
