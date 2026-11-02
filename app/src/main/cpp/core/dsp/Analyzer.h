#pragma once

#include <cstdint>
#include <vector>

#include "dsp/Fft.h"

class Mixer;
class MappedAudio;

// Un espectro listo para leer: dB por bin, cuántos bins hay y cuántos Hz mide cada uno.
struct SpectrumView {
    const float *db;
    int bins;
    float binHz;
};

struct HumInfo {
    int freq = 0;      // 50 o 60; 0 = sin zumbido
    int count = 0;     // armónicos marcados (de 4)
    float db = 0.0f;   // prominencia promedio sobre el piso
};

// Análisis en vivo, igual que los AnalyserNode del prototipo:
// - espectro mono de 16384 puntos (Blackman, suavizado 0.85) para el espectro, el zumbido y
//   el fondo de la curva del EQ;
// - espectros L y R de 2048 puntos (suavizado 0.55) para el goniómetro "por fuentes";
// - los últimos 2048 cuadros L/R para fase, paneo y el goniómetro clásico.
// Se usa desde un solo hilo (el de la pantalla).
class Analyzer {
public:
    static constexpr int kSpecSize = 16384, kSpecBins = kSpecSize / 2;
    static constexpr int kFanSize = 2048, kFanBins = kFanSize / 2;
    static constexpr int kTime = 2048;

    explicit Analyzer(int sampleRate);

    // Lee lo más reciente del mezclador. Devuelve false si no llegó audio nuevo.
    bool update(const Mixer &m);
    // Igual, con señal propia (para pruebas): kSpecSize cuadros, el más reciente al final.
    void updateFrom(const float *l, const float *r);
    void reset();
    // Igual, y además ignora el audio que el mezclador ya había mandado (al cambiar de fuente,
    // para que no se cuele lo anterior).
    void reset(const Mixer &m);

    const float *spectrumDb() const { return mSpecDb.data(); }  // kSpecBins valores
    const float *fanDb() const { return mFanE.data(); }         // energía L+R por bin (dB)
    const float *fanPan() const { return mFanP.data(); }        // -1 (L) .. 1 (R) por bin
    const float *timeL() const { return mL.data() + (kSpecSize - kTime); }
    const float *timeR() const { return mR.data() + (kSpecSize - kTime); }
    float binHz() const { return static_cast<float>(mRate) / kSpecSize; }
    SpectrumView spectrum() const { return {mSpecDb.data(), kSpecBins, binHz()}; }

    float peakL = 0, peakR = 0, rmsL = 0, rmsR = 0;
    float correlation = 1;  // de -1 a 1 (instantánea; sin señal queda la anterior)
    HumInfo hum;

private:
    void analyze();
    void detectHum();

    int mRate;
    uint64_t mLastWrite = ~0ull;
    uint64_t mIgnoreBefore = 0;
    FFT mSpecFft, mFanFft;
    std::vector<double> mSpecWin, mFanWin, mRe, mIm;
    std::vector<float> mL, mR;
    std::vector<double> mSpecMag, mFanMagL, mFanMagR;  // suavizados (lineal)
    std::vector<float> mSpecDb, mFanE, mFanP;
};

// Lo que suena en el celular "de forma nativa" (el Visualizer de Android): pedazos sueltos de
// ~1024 muestras mono, no un flujo seguido. Cada pedazo se analiza solo: ventana Hann, FFT de
// 4096 puntos (con ceros: la curva queda suave) y un suavizado que sube rápido y baja lento, como
// un analizador de barras. Misma escala en dB que Analyzer. Se usa desde un solo hilo.
class SnapshotSpectrum {
public:
    static constexpr int kMaxSamples = 2048, kFft = 4096, kBins = kFft / 2;

    SnapshotSpectrum();
    void reset();
    // n ≤ kMaxSamples muestras (-1..1) a `sampleRate`.
    void update(const float *mono, int n, int sampleRate);
    SpectrumView spectrum() const { return {mDb.data(), kBins, mBinHz}; }

private:
    FFT mFft;
    std::vector<double> mRe, mIm, mWin, mMag;
    std::vector<float> mDb;
    int mWinN = 0;
    float mBinHz = 48000.0f / kFft;
};

// Espectrograma del archivo completo, como el prototipo: `cols` momentos × `rows` bandas
// logarítmicas de 20 Hz a `hiHz`; cada valor es 0 (silencio) .. 255 (fuerte).
// Distribución: [col * rows + row], fila 0 = graves.
std::vector<uint8_t> spectrogram(const MappedAudio &a, int cols, int rows, float *hiHz);
