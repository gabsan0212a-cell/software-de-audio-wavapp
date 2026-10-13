#pragma once

#include <oboe/Oboe.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

class LiveInput;

// Entrada de audio de Android (Oboe): el micrófono del celular, el de los audífonos o una
// interfaz USB. Entrega lo que entra al núcleo (LiveInput) a 48 kHz; si el aparato usa otra
// frecuencia, Oboe convierte.
class AudioInput : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    static constexpr int kSampleRate = 48000;

    explicit AudioInput(LiveInput &live) : mLive(live) {}
    ~AudioInput() override;

    // deviceId 0 = el que elija el sistema. Devuelve los canales abiertos (1 o 2) o 0 si falló.
    int start(int deviceId, int channels);
    void stop();
    int latencyFrames();  // -1 si no se sabe
    // Se desconectó sola (p. ej. se quitó la interfaz USB). Se lee una vez.
    bool lost() { return mLost.exchange(false); }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override;

private:
    bool open(int deviceId, int channels, oboe::InputPreset preset);

    LiveInput &mLive;
    std::mutex mMutex;
    std::shared_ptr<oboe::AudioStream> mStream;
    std::atomic<bool> mLost{false};
};
