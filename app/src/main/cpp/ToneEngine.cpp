#include "ToneEngine.h"

#include <android/log.h>
#include <cmath>

#define LOG_TAG "WavApp"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static constexpr double kTwoPi = 6.283185307179586;
static constexpr float kAmplitude = 0.25f;  // -12 dBFS

bool ToneEngine::start() {
    if (mStream) return true;
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(oboe::ChannelCount::Stereo)
        ->setDataCallback(this)
        ->setErrorCallback(this);
    oboe::Result r = builder.openStream(mStream);
    if (r != oboe::Result::OK) {
        LOGE("openStream: %s", oboe::convertToText(r));
        mStream.reset();
        return false;
    }
    r = mStream->requestStart();
    if (r != oboe::Result::OK) {
        LOGE("requestStart: %s", oboe::convertToText(r));
        mStream->close();
        mStream.reset();
        return false;
    }
    return true;
}

void ToneEngine::stop() {
    if (!mStream) return;
    mStream->stop();
    mStream->close();
    mStream.reset();
}

int32_t ToneEngine::sampleRate() const {
    return mStream ? mStream->getSampleRate() : 0;
}

oboe::DataCallbackResult ToneEngine::onAudioReady(oboe::AudioStream *stream,
                                                  void *audioData,
                                                  int32_t numFrames) {
    auto *out = static_cast<float *>(audioData);
    const int channels = stream->getChannelCount();
    const double inc = kTwoPi * mFrequency.load() / stream->getSampleRate();
    const float target = mToneOn.load() ? kAmplitude : 0.0f;
    const float step = 1.0f / 256.0f * kAmplitude;

    for (int i = 0; i < numFrames; ++i) {
        if (mGain < target) mGain = std::fmin(mGain + step, target);
        else if (mGain > target) mGain = std::fmax(mGain - step, target);
        const float s = mGain * static_cast<float>(std::sin(mPhase));
        mPhase += inc;
        if (mPhase >= kTwoPi) mPhase -= kTwoPi;
        for (int c = 0; c < channels; ++c) *out++ = s;
    }
    return oboe::DataCallbackResult::Continue;
}

void ToneEngine::onErrorAfterClose(oboe::AudioStream *, oboe::Result error) {
    // Ej.: se conectan/desconectan audífonos. Reabrimos el stream.
    LOGE("stream cerrado: %s, reabriendo", oboe::convertToText(error));
    mStream.reset();
    start();
}
