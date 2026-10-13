#include "AudioOutput.h"

#include <android/log.h>

#include "engine/Mixer.h"

#define LOG_TAG "WavApp"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

AudioOutput::~AudioOutput() { stop(); }

namespace {
constexpr int kIdleSeconds = 5;  // sin sonar este tiempo, la salida se duerme
}

bool AudioOutput::start() {
    std::lock_guard<std::mutex> lk(mStreamMutex);
    mWanted = true;
    if (!mStream) return openStream();
    if (!mSleeping.load()) return true;
    // Dormida: esperar a que termine de detenerse y volver a arrancar.
    oboe::StreamState state = mStream->getState();
    if (state == oboe::StreamState::Stopping) {
        oboe::StreamState next = state;
        mStream->waitForStateChange(state, &next, 200 * 1000000LL);
    }
    mIdleFrames = 0;
    mSleeping.store(false);
    if (mStream->requestStart() == oboe::Result::OK) return true;
    LOGE("no se pudo despertar la salida: se abre de nuevo");
    mStream->close();
    mStream.reset();
    return openStream();
}

bool AudioOutput::openStream() {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(oboe::ChannelCount::Stereo)
        ->setSampleRate(kSampleRate)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setDataCallback(this)
        ->setErrorCallback(this);
    mSleeping.store(false);
    mIdleFrames = 0;
    oboe::Result r = builder.openStream(mStream);
    if (r != oboe::Result::OK) {
        LOGE("openStream: %s", oboe::convertToText(r));
        mStream.reset();
        return false;
    }
    // 4 ráfagas de búfer (~8 ms): responde al instante y aguanta mejor los picos de trabajo.
    mStream->setBufferSizeInFrames(mStream->getFramesPerBurst() * 4);
    r = mStream->requestStart();
    if (r != oboe::Result::OK) {
        LOGE("requestStart: %s", oboe::convertToText(r));
        mStream->close();
        mStream.reset();
        return false;
    }
    LOGI("audio: %d Hz, ráfaga %d, %s", mStream->getSampleRate(), mStream->getFramesPerBurst(),
         mStream->getSharingMode() == oboe::SharingMode::Exclusive ? "exclusivo" : "compartido");
    return true;
}

void AudioOutput::stop() {
    std::lock_guard<std::mutex> lk(mStreamMutex);
    mWanted = false;
    if (mStream) {
        mStream->stop();
        mStream->close();
        mStream.reset();
    }
}

void AudioOutput::info(int32_t out[6]) {
    std::lock_guard<std::mutex> lk(mStreamMutex);
    for (int i = 0; i < 6; ++i) out[i] = 0;
    if (!mStream) return;
    out[0] = mStream->getSampleRate();
    out[1] = mStream->getFramesPerBurst();
    out[2] = mStream->getBufferSizeInFrames();
    auto x = mStream->getXRunCount();
    out[3] = x ? x.value() : -1;
    out[4] = mStream->getSharingMode() == oboe::SharingMode::Exclusive ? 1 : 0;
    out[5] = mSleeping.load() ? 1 : 0;
}

oboe::DataCallbackResult AudioOutput::onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) {
    mMixer.process(static_cast<float *>(audioData), numFrames, stream->getChannelCount());
    // Sin sonar un rato: se duerme (el sistema apaga la ruta de audio y ahorra batería).
    mIdleFrames = mMixer.isRolling() ? 0 : mIdleFrames + numFrames;
    if (mIdleFrames > static_cast<int64_t>(stream->getSampleRate()) * kIdleSeconds) {
        mSleeping.store(true);
        return oboe::DataCallbackResult::Stop;
    }
    return oboe::DataCallbackResult::Continue;
}

void AudioOutput::onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) {
    // Ej.: se conectan o desconectan audífonos. Reabrimos si la app sigue en pantalla.
    LOGE("stream cerrado: %s", oboe::convertToText(error));
    std::lock_guard<std::mutex> lk(mStreamMutex);
    if (mStream.get() != stream) return;  // aviso tardío de un stream que ya cambiamos
    mStream.reset();
    if (mWanted) openStream();
}
