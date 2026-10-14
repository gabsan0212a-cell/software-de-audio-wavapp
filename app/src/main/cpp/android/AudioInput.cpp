#include "AudioInput.h"

#include <android/api-level.h>
#include <android/log.h>

#include <algorithm>
#include <climits>

#include "engine/LiveInput.h"

#define LOG_TAG "WavApp"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

AudioInput::~AudioInput() { stop(); }

int AudioInput::start(int deviceId, int channels, bool pull) {
    stop();
    std::lock_guard<std::mutex> lk(mMutex);
    channels = std::clamp(channels, 1, 2);
    mDevice = deviceId;
    mChannelsWanted = channels;
    mLost.store(false);
    mStuck.store(false);
    mPull.store(pull);
    mTmp.assign(static_cast<size_t>(kReadFrames) * 2, 0.0f);
    mOutFrames = 0;
    mPulled.store(0);
    // "VoicePerformance" (Android 10+) es para escucharse en vivo: el menor retraso y sin el
    // procesamiento de voz. Si el aparato no lo acepta, "VoiceRecognition" (sin control
    // automático de volumen en casi todos los celulares).
    bool ok = false;
    if (android_get_device_api_level() >= 29) ok = openStream(deviceId, channels, oboe::InputPreset::VoicePerformance, pull);
    if (!ok) ok = openStream(deviceId, channels, oboe::InputPreset::VoiceRecognition, pull);
    if (!ok && channels == 2) ok = openStream(deviceId, 1, oboe::InputPreset::VoiceRecognition, pull);  // sin estéreo
    if (!ok) return 0;
    mChannels = std::clamp(mStream->getChannelCount(), 1, 2);
    mOpen.store(true, std::memory_order_release);
    return mChannels;
}

bool AudioInput::openStream(int deviceId, int channels, oboe::InputPreset preset, bool pull) {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Input)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(channels)
        ->setSampleRate(kSampleRate)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setInputPreset(preset)
        ->setErrorCallback(this);
    // A la par: sin callback; la salida la vacía en cada ráfaga (con espacio de sobra entre una
    // ráfaga de la salida y la siguiente). Si no, con su propio hilo.
    if (pull) builder.setBufferCapacityInFrames(kCapacity);
    else builder.setDataCallback(this);
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
    LOGI("entrada %s: %d Hz, %d canal(es), ráfaga %d, capacidad %d, %s", pull ? "a la par" : "con hilo propio",
         mStream->getSampleRate(), mStream->getChannelCount(), mStream->getFramesPerBurst(),
         mStream->getBufferCapacityInFrames(),
         mStream->getSharingMode() == oboe::SharingMode::Exclusive ? "exclusivo" : "compartido");
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
    if (!mOpen.load(std::memory_order_acquire) || !mPull.load(std::memory_order_relaxed)) return;
    // Nunca espera en el hilo de audio: si la pantalla está abriendo o cerrando, esta vez no lee.
    std::unique_lock<std::mutex> lk(mMutex, std::try_to_lock);
    if (!lk.owns_lock() || !mStream) return;
    oboe::AudioStream *s = mStream.get();
    // Todo lo que llegó, directo al núcleo (sin esperar). La reserva de la escucha (DriftFifo, en
    // el núcleo) une el reloj de la entrada con el de la salida: sin huecos ni saltos.
    for (int vueltas = 0; vueltas < 8; ++vueltas) {
        auto r = s->read(mTmp.data(), kReadFrames, 0);
        const int got = r ? r.value() : 0;
        if (got <= 0) break;
        mLive.push(mTmp.data(), got, mChannels);
        mPulled.fetch_add(got, std::memory_order_relaxed);
        if (got < kReadFrames) break;  // no hay más
    }
    // Abierta 2 s y no llegó nada: esta forma no sirve en este aparato.
    mOutFrames += frames;
    if (mOutFrames > 2LL * kSampleRate && mPulled.load(std::memory_order_relaxed) == 0) mStuck.store(true);
}

void AudioInput::info(int32_t *out) {
    std::lock_guard<std::mutex> lk(mMutex);
    const bool open = mOpen.load() && mStream;
    int64_t underruns = 0, skips = 0;
    int level = 0, safety = 0;
    mLive.monitorStats(underruns, skips, level, safety);
    out[0] = open ? (mPull.load() ? 1 : 2) : 0;
    out[1] = static_cast<int32_t>(mPulled.load() / kSampleRate);
    out[2] = static_cast<int32_t>(std::min<int64_t>(underruns, INT32_MAX));
    out[3] = static_cast<int32_t>(std::min<int64_t>(skips, INT32_MAX));
    out[4] = safety;
    out[5] = open ? mStream->getFramesPerBurst() : 0;
    out[6] = open ? mStream->getBufferCapacityInFrames() : 0;
}

oboe::DataCallbackResult AudioInput::onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) {
    // Con hilo propio (si a la par no funcionó en este aparato).
    mLive.push(static_cast<const float *>(audioData), numFrames, stream->getChannelCount());
    mPulled.fetch_add(numFrames, std::memory_order_relaxed);
    return oboe::DataCallbackResult::Continue;
}

void AudioInput::onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) {
    LOGE("entrada cerrada: %s", oboe::convertToText(error));
    std::lock_guard<std::mutex> lk(mMutex);
    if (mStream.get() != stream) return;
    mOpen.store(false, std::memory_order_release);
    mStream.reset();
    mLost.store(true);  // la pantalla lo ve y avisa
}
