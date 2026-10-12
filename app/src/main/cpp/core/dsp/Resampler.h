#pragma once

#include <cstdint>
#include <vector>

// Conversión de frecuencia de muestreo fuera de tiempo real (al importar), con filtro sinc
// y ventana Kaiser (~100 dB de rechazo, plano hasta ~0.9 del Nyquist más bajo).
// Para frecuencias comunes (44.1k↔48k, 96k, 22.05k...) las fases son exactas.
class Resampler {
public:
    Resampler(int channels, int inRate, int outRate);

    // Agrega `frames` cuadros intercalados y añade a `out` toda la salida ya calculable.
    void process(const float *in, size_t frames, std::vector<float> &out);
    // Fin de la entrada: añade a `out` la cola pendiente.
    void finish(std::vector<float> &out);

    // Cuadros de salida que producen `inFrames` cuadros de entrada.
    static int64_t outputFrames(int64_t inFrames, int inRate, int outRate);

private:
    void produce(std::vector<float> &out, int64_t limit);

    int mCh;
    int64_t mNum, mDen;  // cada cuadro de salida avanza mNum/mDen cuadros de entrada
    int mHalf;           // medio ancho del filtro, en cuadros de entrada
    int mTaps;           // 2 * mHalf
    int mPhases;
    bool mExact;         // mPhases == mDen: sin interpolar entre fases
    std::vector<float> mTable;               // (mPhases + 1) filas × mTaps
    std::vector<std::vector<float>> mHist;   // historial de entrada, por canal
    int64_t mHistStart;                      // índice absoluto de mHist[c][0]
    int64_t mInFrames = 0;
    int64_t mOutFrames = 0;
    int64_t mPosInt = 0, mPosRem = 0;        // posición del próximo cuadro de salida
};
