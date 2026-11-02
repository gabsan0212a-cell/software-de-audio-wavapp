#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "dsp/Loudness.h"

class Mixer;

// Lo que suena en el celular (la música o los videos de otras apps), de dos formas:
// - nativa (lo normal): Android (Visualizer) da pedazos sueltos de lo que sale, mono, sin
//   permiso de grabar la pantalla. Se guarda el último y la pantalla lo analiza (espectro y
//   espectrograma) junto con el pico y el RMS que mide Android;
// - captura completa: la plataforma captura lo que se reproduce (con el permiso de grabar o
//   transmitir) y manda el audio seguido: va al analizador si es la fuente elegida y se mide su
//   volumen percibido (LUFS).
// No suena en la app.
class MediaListen {
public:
    // readState(): abierta (0/1), pico L, pico R (dB, con caída de 24 dB/s), segundos seguidos sin
    // sonido, nativa (0/1), RMS (dB, con caída de 12 dB/s). Sin sonido: kFloorDb.
    static constexpr int kStateCount = 6;
    static constexpr float kFloorDb = -120.0f;
    static constexpr int kMaxSnapshot = 2048;
    static constexpr float kNoLevel = -1000.0f;  // Android no dio el nivel (dB)

    MediaListen(Mixer &mixer, int sampleRate);
    MediaListen(const MediaListen &) = delete;
    MediaListen &operator=(const MediaListen &) = delete;

    // ---------- Hilo de la interfaz
    // La plataforma empezó a capturar (1 o 2 canales; `native` = en pedazos sueltos) o paró. Se
    // llama antes de que empiece a mandar audio y después de que dejó de hacerlo.
    void setActive(bool on, int channels, bool native = false);
    bool active() const { return mActive.load(); }
    bool isNative() const { return mNative.load(); }
    // El último pedazo (modo nativo), si llegó uno desde la lectura anterior: muestras -1..1,
    // cuántas, su frecuencia y su RMS (lineal).
    bool takeSnapshot(float *mono, int &n, int &sampleRate, float &rms);
    int channels() const { return mChannels.load(); }
    void readState(float *out);  // una sola lectora (la pantalla)
    // LUFS de lo capturado: bloques de 100 ms para una sola lectora. requestLoudnessReset()
    // empieza una medición nueva y devuelve su número (los bloques de antes traen otro).
    uint32_t requestLoudnessReset() { return mLoudGenReq.fetch_add(1) + 1; }
    int readLoudness(LoudnessBlock *out, int max);

    // ---------- Hilo de la captura (sin reservar memoria)
    // `frames` cuadros intercalados de 16 bits, con los canales de setActive.
    void push(const int16_t *pcm, int frames);
    // Modo nativo: un pedazo de 8 bits sin signo (128 = 0) a `sampleRate`, con el pico y el RMS de
    // lo que suena (dB; kNoLevel = no se sabe: se calcula del pedazo).
    void pushSnapshot(const uint8_t *wave, int n, int sampleRate, float peakDb, float rmsDb);

private:
    static constexpr int kChunk = 512;
    static constexpr int kLoudRing = 256;  // 25 s de bloques: la pantalla los lee mucho antes

    Mixer &mMixer;
    const int mRate;
    std::atomic<bool> mActive{false}, mNative{false};
    std::atomic<int> mChannels{2};
    std::atomic<float> mPeakL{0.0f}, mPeakR{0.0f};
    std::atomic<int64_t> mFrames{0}, mLastSound{0};
    // LUFS
    LoudnessFrontEnd mFront;
    std::atomic<uint32_t> mLoudGenReq{0};
    uint32_t mLoudGen = 0;  // solo el hilo de la captura
    LoudnessBlock mLoudBuf[kLoudRing];
    std::atomic<uint64_t> mLoudWrite{0}, mLoudRead{0};
    float mL[kChunk], mR[kChunk];
    // Modo nativo: el último pedazo (lo escribe el hilo de la plataforma, lo lee la pantalla).
    std::mutex mSnapMutex;
    std::vector<float> mSnap;
    int mSnapN = 0, mSnapRate = 48000;
    uint64_t mSnapSeq = 0, mSnapTaken = 0;
    std::atomic<float> mRms{0.0f};
    std::atomic<int64_t> mOpenMs{0}, mLastSoundMs{0};
    // Lo que se muestra (solo la lectora): con caída.
    float mShowL = kFloorDb, mShowR = kFloorDb, mShowRms = kFloorDb;
    int64_t mShowMs = -1;
};
