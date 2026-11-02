#pragma once

#include <atomic>
#include <cstdint>

#include "dsp/Loudness.h"

class Mixer;

// Lo que suena en el celular (la música o los videos de otras apps). La plataforma lo captura,
// con el permiso del usuario, y lo manda aquí desde su hilo. No suena en la app: solo se analiza
// (va al analizador si es la fuente elegida) y se mide su volumen percibido (LUFS).
class MediaListen {
public:
    // readState(): abierta (0/1), pico L, pico R (lineales, desde la lectura anterior), segundos
    // seguidos sin sonido.
    static constexpr int kStateCount = 4;

    MediaListen(Mixer &mixer, int sampleRate);
    MediaListen(const MediaListen &) = delete;
    MediaListen &operator=(const MediaListen &) = delete;

    // ---------- Hilo de la interfaz
    // La plataforma empezó a capturar (1 o 2 canales) o paró. Se llama antes de que empiece a
    // mandar audio y después de que dejó de hacerlo.
    void setActive(bool on, int channels);
    bool active() const { return mActive.load(); }
    int channels() const { return mChannels.load(); }
    void readState(float *out);
    // LUFS de lo capturado: bloques de 100 ms para una sola lectora. requestLoudnessReset()
    // empieza una medición nueva y devuelve su número (los bloques de antes traen otro).
    uint32_t requestLoudnessReset() { return mLoudGenReq.fetch_add(1) + 1; }
    int readLoudness(LoudnessBlock *out, int max);

    // ---------- Hilo de la captura (sin reservar memoria)
    // `frames` cuadros intercalados de 16 bits, con los canales de setActive.
    void push(const int16_t *pcm, int frames);

private:
    static constexpr int kChunk = 512;
    static constexpr int kLoudRing = 256;  // 25 s de bloques: la pantalla los lee mucho antes

    Mixer &mMixer;
    const int mRate;
    std::atomic<bool> mActive{false};
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
};
