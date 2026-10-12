#include "AudioEngine.h"

#include <algorithm>
#include <android/log.h>
#include <cstdio>

#define LOG_TAG "WavApp"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool AudioEngine::start() {
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

void AudioEngine::stop() {
    if (!mStream) return;
    mStream->stop();
    mStream->close();
    mStream.reset();
}

int32_t AudioEngine::deviceRate() const {
    return mStream ? mStream->getSampleRate() : 0;
}

std::string AudioEngine::loadWav(int fd) {
    auto clip = std::make_unique<Clip>();
    std::string err = readWav(fdopen(fd, "rb"), *clip);
    if (!err.empty()) return err;
    mPlaying.store(false);
    mResetRequested.store(true);
    mRetired = std::move(mOwned);  // libera el retirado de la carga anterior
    mOwned = std::move(clip);
    mClip.store(mOwned.get());
    return "";
}

double AudioEngine::positionSeconds() const {
    const Clip *c = mClip.load();
    return c ? mPos.load() / c->sampleRate : 0.0;
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream *stream,
                                                   void *audioData,
                                                   int32_t numFrames) {
    auto *out = static_cast<float *>(audioData);
    const int outCh = stream->getChannelCount();
    const Clip *c = mClip.load();
    double pos = mPos.load();
    if (mResetRequested.exchange(false)) pos = 0.0;

    // Interpolación lineal si el WAV no tiene la misma frecuencia que el celular.
    const double step = c ? static_cast<double>(c->sampleRate) / stream->getSampleRate() : 0.0;
    const float rampStep = 1.0f / 256.0f;

    for (int i = 0; i < numFrames; ++i) {
        const float target = (c && mPlaying.load(std::memory_order_relaxed)) ? 1.0f : 0.0f;
        if (mGain < target) mGain = std::min(mGain + rampStep, target);
        else if (mGain > target) mGain = std::max(mGain - rampStep, target);

        if (!c || mGain == 0.0f) {
            for (int ch = 0; ch < outCh; ++ch) *out++ = 0.0f;
            continue;
        }
        const int64_t idx = static_cast<int64_t>(pos);
        if (idx + 1 >= c->frames) {  // fin del archivo
            mPlaying.store(false);
            mGain = 0.0f;
            pos = 0.0;
            for (int ch = 0; ch < outCh; ++ch) *out++ = 0.0f;
            continue;
        }
        const float frac = static_cast<float>(pos - idx);
        const float *a = &c->data[idx * c->channels];
        const float *b = a + c->channels;
        for (int ch = 0; ch < outCh; ++ch) {
            const int src = ch < c->channels ? ch : c->channels - 1;  // mono → ambos lados
            *out++ = mGain * (a[src] + frac * (b[src] - a[src]));
        }
        pos += step;
    }
    mPos.store(pos);
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream *, oboe::Result error) {
    // Ej.: se conectan/desconectan audífonos. Reabrimos el stream.
    LOGE("stream cerrado: %s, reabriendo", oboe::convertToText(error));
    mStream.reset();
    start();
}
