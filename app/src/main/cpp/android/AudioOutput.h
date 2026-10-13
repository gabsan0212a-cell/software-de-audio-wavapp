#pragma once

#include <oboe/Oboe.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

class Mixer;

// Salida de audio de Android (Oboe): le pide la mezcla al núcleo en cada ráfaga.
// El proyecto trabaja a 48 kHz; si el celular usa otra frecuencia, Oboe convierte.
// Tras unos segundos sin sonar la salida se duerme (ahorra batería) y start() la despierta.
class AudioOutput : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    static constexpr int kSampleRate = 48000;

    explicit AudioOutput(Mixer &mixer) : mMixer(mixer) {}
    ~AudioOutput() override;

    bool start();  // abre la salida, o la despierta si estaba dormida
    void stop();
    // [frecuencia real, ráfaga, búfer en cuadros, cortes (xruns), exclusivo 1/0, dormida 1/0];
    // ceros si está cerrada.
    void info(int32_t out[6]);
    int latencyFrames();  // retraso de la salida (cuadros); -1 si no se sabe

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override;

private:
    bool openStream();

    Mixer &mMixer;
    std::mutex mStreamMutex;
    std::shared_ptr<oboe::AudioStream> mStream;
    bool mWanted = false;  // la app quiere el audio abierto
    std::atomic<bool> mSleeping{false};
    int64_t mIdleFrames = 0;  // hilo de audio
};
