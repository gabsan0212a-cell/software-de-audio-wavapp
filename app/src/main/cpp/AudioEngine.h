#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <oboe/Oboe.h>

#include "WavReader.h"

// Etapa 1a: reproduce un WAV cargado en memoria.
class AudioEngine : public oboe::AudioStreamDataCallback,
                    public oboe::AudioStreamErrorCallback {
public:
    bool start();
    void stop();
    int32_t deviceRate() const;

    std::string loadWav(int fd);  // "" si todo bien
    void play() { mPlaying.store(true); }
    void pause() { mPlaying.store(false); }
    void stopPlayback() { mPlaying.store(false); mResetRequested.store(true); }
    bool isPlaying() const { return mPlaying.load(); }
    double positionSeconds() const;
    const Clip *clip() const { return mClip.load(); }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream,
                                          void *audioData,
                                          int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override;

private:
    std::shared_ptr<oboe::AudioStream> mStream;
    // El hilo de audio solo lee mClip. El clip anterior se libera en la carga siguiente,
    // cuando ya es seguro que el callback no lo usa.
    std::atomic<Clip *> mClip{nullptr};
    std::unique_ptr<Clip> mOwned, mRetired;
    std::atomic<bool> mPlaying{false};
    std::atomic<bool> mResetRequested{false};
    std::atomic<double> mPos{0.0};  // en frames del clip (fraccional)
    float mGain = 0.0f;             // rampa para evitar clics en play/pausa
};
