#include "AudioInput.h"

#include <android/api-level.h>
#include <android/log.h>

#include <algorithm>

#include "engine/LiveInput.h"

#define LOG_TAG "WavApp"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

AudioInput::~AudioInput() { stop(); }

int AudioInput::start(int deviceId, int channels) {
    stop();
    std::lock_guard<std::mutex> lk(mMutex);
    channels = std::clamp(channels, 1, 2);
    mLost.store(false);
    // "VoicePerformance" (Android 10+) es para escucharse en vivo: el menor retraso y sin el
    // procesamiento de voz. Si el aparato no lo acepta, "VoiceRecognition" (sin control
    // automático de volumen en casi todos los celulares).
    bool ok = false;
    if (android_get_device_api_level() >= 29) ok = open(deviceId, channels, oboe::InputPreset::VoicePerformance);
    if (!ok) ok = open(deviceId, channels, oboe::InputPreset::VoiceRecognition);
    if (!ok && channels == 2) ok = open(deviceId, 1, oboe::InputPreset::VoiceRecognition);  // sin estéreo
    return ok ? std::clamp(mStream->getChannelCount(), 1, 2) : 0;
}

bool AudioInput::open(int deviceId, int channels, oboe::InputPreset preset) {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Input)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(channels)
        ->setSampleRate(kSampleRate)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setInputPreset(preset)
        ->setDataCallback(this)
        ->setErrorCallback(this);
    if (deviceId > 0) builder.setDeviceId(deviceId);
    oboe::Result r = builder.openStream(mStream);
    if (r != oboe::Result::OK) {
        LOGE("entrada openStream: %s", oboe::convertToText(r));
        mStream.reset();
        return false;
    }
    r = mStream->requestStart();
    if (r != oboe::Result::OK) {
        LOGE("entrada requestStart: %s", oboe::convertToText(r));
        mStream->close();
        mStream.reset();
        return false;
    }
    LOGI("entrada: %d Hz, %d canal(es), ráfaga %d, %s", mStream->getSampleRate(), mStream->getChannelCount(),
         mStream->getFramesPerBurst(), mStream->getSharingMode() == oboe::SharingMode::Exclusive ? "exclusivo" : "compartido");
    return true;
}

void AudioInput::stop() {
    std::lock_guard<std::mutex> lk(mMutex);
    if (mStream) {
        mStream->stop();
        mStream->close();
        mStream.reset();
    }
}

int AudioInput::latencyFrames() {
    std::lock_guard<std::mutex> lk(mMutex);
    if (!mStream) return -1;
    auto ms = mStream->calculateLatencyMillis();
    return ms ? static_cast<int>(ms.value() * kSampleRate / 1000.0) : -1;
}

oboe::DataCallbackResult AudioInput::onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) {
    mLive.push(static_cast<const float *>(audioData), numFrames, stream->getChannelCount());
    return oboe::DataCallbackResult::Continue;
}

void AudioInput::onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) {
    LOGE("entrada cerrada: %s", oboe::convertToText(error));
    std::lock_guard<std::mutex> lk(mMutex);
    if (mStream.get() != stream) return;
    mStream.reset();
    mLost.store(true);  // la pantalla lo ve y avisa
}
