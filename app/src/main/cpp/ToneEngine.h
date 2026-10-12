#pragma once

#include <atomic>
#include <memory>
#include <oboe/Oboe.h>

// Paso 0: motor mínimo que genera un tono senoidal con Oboe.
class ToneEngine : public oboe::AudioStreamDataCallback,
                   public oboe::AudioStreamErrorCallback {
public:
    bool start();
    void stop();
    void setToneOn(bool on) { mToneOn.store(on); }
    void setFrequency(float hz) { mFrequency.store(hz); }
    int32_t sampleRate() const;

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream,
                                          void *audioData,
                                          int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override;

private:
    std::shared_ptr<oboe::AudioStream> mStream;
    std::atomic<bool> mToneOn{false};
    std::atomic<float> mFrequency{440.0f};
    double mPhase = 0.0;
    float mGain = 0.0f;  // rampa para evitar clics al encender/apagar
};
