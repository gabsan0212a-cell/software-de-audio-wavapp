#include "AudioEngine.h"

#include <android/log.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

#define LOG_TAG "WavApp"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

AudioEngine::~AudioEngine() { stop(); }

bool AudioEngine::start() {
    std::lock_guard<std::mutex> lk(mStreamMutex);
    mWanted = true;
    if (!mPrefetchRun.exchange(true)) {
        mPrefetchThread = std::thread([this] {
            while (mPrefetchRun.load()) {
                mMixer.prefetch();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }
    return mStream ? true : openStream();
}

bool AudioEngine::openStream() {
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

void AudioEngine::stop() {
    {
        std::lock_guard<std::mutex> lk(mStreamMutex);
        mWanted = false;
        if (mStream) {
            mStream->stop();
            mStream->close();
            mStream.reset();
        }
    }
    if (mPrefetchRun.exchange(false) && mPrefetchThread.joinable()) mPrefetchThread.join();
}

int32_t AudioEngine::deviceRate() {
    std::lock_guard<std::mutex> lk(mStreamMutex);
    return mStream ? mStream->getSampleRate() : 0;
}

void AudioEngine::outputInfo(int32_t out[5]) {
    std::lock_guard<std::mutex> lk(mStreamMutex);
    for (int i = 0; i < 5; ++i) out[i] = 0;
    if (!mStream) return;
    out[0] = mStream->getSampleRate();
    out[1] = mStream->getFramesPerBurst();
    out[2] = mStream->getBufferSizeInFrames();
    auto x = mStream->getXRunCount();
    out[3] = x ? x.value() : -1;
    out[4] = mStream->getSharingMode() == oboe::SharingMode::Exclusive ? 1 : 0;
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream *stream, void *audioData,
                                                   int32_t numFrames) {
    mMixer.process(static_cast<float *>(audioData), numFrames, stream->getChannelCount());
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) {
    // Ej.: se conectan o desconectan audífonos. Reabrimos si la app sigue en pantalla.
    LOGE("stream cerrado: %s", oboe::convertToText(error));
    std::lock_guard<std::mutex> lk(mStreamMutex);
    if (mStream.get() != stream) return;  // aviso tardío de un stream que ya cambiamos
    mStream.reset();
    if (mWanted) openStream();
}

int AudioEngine::addMapped(std::unique_ptr<MappedAudio> audio, std::string &error) {
    if (!audio) return -1;
    if (audio->sampleRate() != kSampleRate) {
        error = "La pista no está a 48 kHz";
        return -1;
    }
    std::vector<float> peaks = computePeaks(*audio, kPeakBucket);
    std::lock_guard<std::mutex> lk(mTracksMutex);
    const int slot = mMixer.freeSlot();
    if (slot < 0) {
        error = "Límite de 12 pistas";
        return -1;
    }
    mPeaks[slot] = std::move(peaks);
    mPeakChannels[slot] = audio->channels();
    mFrames[slot] = audio->frames();
    if (!mMixer.setTrack(slot, std::move(audio))) {
        error = "No se pudo agregar la pista";
        return -1;
    }
    return slot;
}

int AudioEngine::importTrack(int fd, const std::string &dstPath, ImportInfo &info, std::string &error) {
    if (mMixer.freeSlot() < 0) {
        close(fd);
        error = "Límite de 12 pistas";
        return -1;
    }
    FILE *f = fdopen(fd, "rb");
    if (!f) close(fd);
    error = importWav(f, dstPath, kSampleRate, info, &mProgress);
    if (!error.empty()) return -1;
    auto audio = MappedAudio::open(dstPath, error);
    const int slot = addMapped(std::move(audio), error);
    if (slot < 0) std::remove(dstPath.c_str());
    return slot;
}

int AudioEngine::addTrackFile(const std::string &path, std::string &error) {
    return addMapped(MappedAudio::open(path, error), error);
}

int AudioEngine::addDemoTrack(int index, const std::string &path, std::string &error) {
    error = writeDemoTrack(index, path, kSampleRate);
    if (!error.empty()) return -1;
    const int slot = addTrackFile(path, error);
    if (slot < 0) std::remove(path.c_str());
    return slot;
}

void AudioEngine::removeTrack(int slot) {
    std::lock_guard<std::mutex> lk(mTracksMutex);
    mMixer.takeTrack(slot);  // libera el mapeo al salir de aquí
    if (slot >= 0 && slot < kMaxTracks) {
        mPeaks[slot].clear();
        mPeaks[slot].shrink_to_fit();
        mPeakChannels[slot] = 0;
        mFrames[slot] = 0;
    }
}

int AudioEngine::trackChannels(int slot) const {
    std::lock_guard<std::mutex> lk(mTracksMutex);
    return slot >= 0 && slot < kMaxTracks ? mPeakChannels[slot] : 0;
}

int64_t AudioEngine::trackFrames(int slot) const {
    std::lock_guard<std::mutex> lk(mTracksMutex);
    return slot >= 0 && slot < kMaxTracks ? mFrames[slot] : 0;
}

std::vector<float> AudioEngine::overview(int slot, int columns) const {
    std::lock_guard<std::mutex> lk(mTracksMutex);
    std::vector<float> out;
    if (slot < 0 || slot >= kMaxTracks || columns <= 0 || mPeaks[slot].empty()) return out;
    const int ch = mPeakChannels[slot];
    const std::vector<float> &p = mPeaks[slot];
    const int64_t buckets = static_cast<int64_t>(p.size()) / (ch * 2);
    out.assign(static_cast<size_t>(columns) * ch * 2, 0.0f);
    for (int col = 0; col < columns; ++col) {
        int64_t b0 = buckets * col / columns, b1 = buckets * (col + 1) / columns;
        if (b1 <= b0) b1 = std::min(buckets, b0 + 1);
        for (int c = 0; c < ch; ++c) {
            float mn = 0.0f, mx = 0.0f;
            for (int64_t b = b0; b < b1; ++b) {
                mn = std::min(mn, p[(b * ch + c) * 2]);
                mx = std::max(mx, p[(b * ch + c) * 2 + 1]);
            }
            out[(static_cast<size_t>(col) * ch + c) * 2] = mn;
            out[(static_cast<size_t>(col) * ch + c) * 2 + 1] = mx;
        }
    }
    return out;
}
