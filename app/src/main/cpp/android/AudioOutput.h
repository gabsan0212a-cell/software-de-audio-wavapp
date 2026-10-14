#pragma once

#include <oboe/Oboe.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

class AudioInput;
class Mixer;

// Salida de audio de Android (Oboe): le pide la mezcla al núcleo en cada ráfaga.
// El proyecto trabaja a 48 kHz; si el celular usa otra frecuencia, Oboe convierte.
// Tras unos segundos sin sonar la salida se duerme (ahorra batería) y start() la despierta.
// Con la entrada abierta, en cada ráfaga primero lee la entrada (full duplex) y no se duerme.
// En modo en vivo usa el búfer más chico seguro (2 ráfagas) y lo agranda solo si hay cortes.
// Le avisa al sistema cuánto trabajo hace cada ráfaga (ADPF) para que el procesador no baje
// de velocidad en medio de la canción.
class AudioOutput : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    static constexpr int kSampleRate = 48000;

    explicit AudioOutput(Mixer &mixer) : mMixer(mixer) {}
    ~AudioOutput() override;

    bool start();  // abre la salida, o la despierta si estaba dormida
    void stop();
    void setInput(AudioInput *in) { mInput.store(in, std::memory_order_release); }  // nullptr = sin entrada
    void setLiveMode(bool on) { mLiveMode.store(on, std::memory_order_relaxed); }
    // [frecuencia real, ráfaga, búfer en cuadros, cortes (xruns), exclusivo 1/0, dormida 1/0];
    // ceros si está cerrada.
    void info(int32_t out[6]);
    int latencyFrames();  // retraso de la salida (cuadros); -1 si no se sabe

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override;

private:
    bool openStream();
    void tuneBuffer(oboe::AudioStream *stream, int numFrames);

    Mixer &mMixer;
    std::mutex mStreamMutex;
    std::shared_ptr<oboe::AudioStream> mStream;
    bool mWanted = false;  // la app quiere el audio abierto
    std::atomic<bool> mSleeping{false};
    std::atomic<AudioInput *> mInput{nullptr};
    std::atomic<bool> mLiveMode{false};
    // Hilo de audio
    int64_t mIdleFrames = 0;
    int mBursts = 0, mLiveBursts = 2;
    bool mWasLive = false;
    int64_t mTuneFrames = 0;
    int32_t mLastXruns = 0;
};
