#pragma once

#include <vector>

#include "dsp/Fft.h"

// STFT estéreo y su inversa, iguales a torch.stft / torch.istft con center=True (relleno por
// reflejo de n/2 a cada lado), ventana Hann periódica del largo de la FFT y sin normalizar.
// Los dos canales van juntos en una sola FFT compleja (izquierdo real, derecho imaginario).
//
// Espectro: [cuadro][frecuencia][canal][re, im], frecuencia = 0 .. n/2.
class StereoStft {
public:
    StereoStft(int nfft, int hop);

    int nfft() const { return mN; }
    int hop() const { return mHop; }
    int bins() const { return mN / 2 + 1; }
    int frames(int length) const { return 1 + length / mHop; }

    // `length` cuadros de audio → frames(length) cuadros de espectro. length > n/2.
    void forward(const float *l, const float *r, int length, float *spec) const;
    // Inversa de `frames` cuadros → hop * (frames - 1) muestras por canal (como istft sin length).
    void inverse(const float *spec, int frames, float *l, float *r) const;

private:
    int mN, mHop;
    FFT mFft;
    std::vector<double> mWindow;
};
