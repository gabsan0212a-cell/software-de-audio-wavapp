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
    mBuf.assign(static_cast<size_t>(kMaxPull) * 2, 0.0f);
    // "VoicePerformance" (Android 10+) es para escucharse en vivo: el menor retraso y sin el
    // procesamiento de voz. Si el aparato no lo acepta, "VoiceRecognition" (sin control
    // automático de volumen en casi todos los celulares).
    bool ok = false;
    if (android_get_device_api_level() >= 29) ok = openStream(deviceId, channels, oboe::InputPreset::VoicePerformance);
    if (!ok) ok = openStream(deviceId, channels, oboe::InputPreset::VoiceRecognition);
    if (!ok && channels == 2) ok = openStream(deviceId, 1, oboe::InputPreset::VoiceRecognition);  // sin estéreo
    if (!ok) return 0;
    mPriming = true;  // la salida todavía no lee (mOpen está en false)
    mCushion = 1;
    mOpen.store(true, std::memory_order_release);
    return std::clamp(mStream->getChannelCount(), 1, 2);
}

bool AudioInput::openStream(int deviceId, int channels, oboe::InputPreset preset) {
    oboe::AudioStreamBuilder builder;
    // Sin callback de datos: la lee la salida en el suyo (full duplex).
    builder.setDirection(oboe::Direction::Input)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(channels)
        ->setSampleRate(kSampleRate)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setInputPreset(preset)
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
    std::lock_guard<std::mutex> lk(mMutex);  // espera a que la salida termine de leer
    mOpen.store(false, std::memory_order_release);
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

void AudioInput::pull(int frames) {
    if (!mOpen.load(std::memory_order_acquire)) return;
    // Nunca espera en el hilo de audio: si la pantalla está abriendo o cerrando, esta vez no lee.
    std::unique_lock<std::mutex> lk(mMutex, std::try_to_lock);
    if (!lk.owns_lock() || !mStream) return;
    oboe::AudioStream *s = mStream.get();
    const int ch = std::max(1, s->getChannelCount());
    frames = std::min(frames, kMaxPull);
    auto available = s->getAvailableFrames();
    if (available) {
        const int burst = std::max(1, s->getFramesPerBurst());
        const int avail = available.value();
        const int cushion = mCushion * burst;
        if (mPriming) {
            if (avail < frames + cushion) return;  // junta la reserva antes de empezar
            mPriming = false;
        }
        // Si se juntó de más (llegó de golpe o los relojes de entrada y salida no son iguales),
        // se tira lo más viejo: el retraso no crece.
        int extra = avail - (frames + cushion + burst);
        while (extra > 0) {
            auto r = s->read(mBuf.data(), std::min(extra, kMaxPull), 0);
            if (!r || r.value() <= 0) break;
            mDropped.fetch_add(r.value(), std::memory_order_relaxed);
            extra -= r.value();
        }
    }
    auto r = s->read(mBuf.data(), frames, 0);
    const int got = r ? r.value() : 0;
    if (got > 0) mLive.push(mBuf.data(), got, ch);
    if (got < frames && available) {
        // Se quedó sin muestras: vuelve a juntar la reserva, ahora una ráfaga más grande (así un
        // aparato con ráfagas de entrada largas encuentra solo su punto).
        mPriming = true;
        if (mCushion < kMaxCushion) ++mCushion;
        mUnderruns.fetch_add(1, std::memory_order_relaxed);
    }
}

void AudioInput::stats(int64_t out[2]) const {
    out[0] = mUnderruns.load(std::memory_order_relaxed);
    out[1] = mDropped.load(std::memory_order_relaxed);
}

void AudioInput::onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) {
    LOGE("entrada cerrada: %s", oboe::convertToText(error));
    std::lock_guard<std::mutex> lk(mMutex);
    if (mStream.get() != stream) return;
    mOpen.store(false, std::memory_order_release);
    mStream.reset();
    mLost.store(true);  // la pantalla lo ve y avisa
}
