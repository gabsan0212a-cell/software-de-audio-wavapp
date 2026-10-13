#include "engine/LiveInput.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "engine/Mixer.h"

namespace {

float smoothCoef(float tauSec, int sampleRate) { return 1.0f - std::exp(-1.0f / (tauSec * sampleRate)); }

void atomicMax(std::atomic<float> &a, float v) {
    float old = a.load(std::memory_order_relaxed);
    while (v > old && !a.compare_exchange_weak(old, v, std::memory_order_relaxed)) {}
}

}  // namespace

// ---------------------------------------------------------------- cola

SpscRing::SpscRing(size_t capacityPow2) : mBuf(capacityPow2), mMask(capacityPow2 - 1) {}

size_t SpscRing::available() const {
    return static_cast<size_t>(mWrite.load(std::memory_order_acquire) - mRead.load(std::memory_order_acquire));
}

size_t SpscRing::write(const float *v, size_t n) {
    const uint64_t w = mWrite.load(std::memory_order_relaxed);
    const uint64_t r = mRead.load(std::memory_order_acquire);
    n = std::min(n, mBuf.size() - static_cast<size_t>(w - r));
    for (size_t i = 0; i < n; ++i) mBuf[(w + i) & mMask] = v[i];
    mWrite.store(w + n, std::memory_order_release);
    return n;
}

size_t SpscRing::read(float *v, size_t n) {
    const uint64_t r = mRead.load(std::memory_order_relaxed);
    const uint64_t w = mWrite.load(std::memory_order_acquire);
    n = std::min(n, static_cast<size_t>(w - r));
    for (size_t i = 0; i < n; ++i) v[i] = mBuf[(r + i) & mMask];
    mRead.store(r + n, std::memory_order_release);
    return n;
}

void SpscRing::skip(size_t n) {
    const uint64_t r = mRead.load(std::memory_order_relaxed);
    const uint64_t w = mWrite.load(std::memory_order_acquire);
    mRead.store(r + std::min(n, static_cast<size_t>(w - r)), std::memory_order_release);
}

// ---------------------------------------------------------------- entrada en vivo

LiveInput::LiveInput(int sampleRate, Mixer &mixer) : mRate(sampleRate), mMixer(mixer) {
    mEq.setSampleRate(sampleRate);
}

LiveInput::~LiveInput() {
    if (mRecording.load()) stopRecording();
}

void LiveInput::setGainDb(float db) {
    db = std::clamp(db, kGainMin, kGainMax);
    mGainTarget.store(std::pow(10.0f, db / 20.0f));
}

void LiveInput::setActive(bool on, int channels) {
    mChannels.store(std::clamp(channels, 1, 2));
    if (on && !mActive.load()) {
        // El hilo de entrada todavía no corre: se puede limpiar su estado.
        mEq.resetState();
        mGainS = mGainTarget.load();
    }
    mActive.store(on);
    if (!on) {
        mPeakL.store(0.0f);
        mPeakR.store(0.0f);
    }
}

void LiveInput::readPeaks(float *lr) {
    lr[0] = mPeakL.exchange(0.0f);
    lr[1] = mPeakR.exchange(0.0f);
}

// ---------------------------------------------------------------- hilo de entrada

void LiveInput::push(const float *in, int frames, int ch) {
    if (!mActive.load(std::memory_order_relaxed) || ch <= 0 || frames <= 0) return;
    mLastPush.store(frames, std::memory_order_relaxed);
    const bool stereo = ch >= 2;  // con más canales se usan los dos primeros
    const float gainT = mGainTarget.load(std::memory_order_relaxed);
    const float coef = smoothCoef(0.015f, mRate);
    const bool monitor = mMonitor.load(std::memory_order_relaxed);
    const bool rec = mRecording.load(std::memory_order_acquire);
    if (rec && !mRecStarted.load(std::memory_order_relaxed) && mMixer.isRolling()) {
        // La toma empieza cuando la mezcla ya corre: así se sabe con qué momento se alinea.
        mRecStart.store(mMixer.position());
        mRecStarted.store(true);
    }
    const bool writeRec = rec && mRecStarted.load(std::memory_order_relaxed);
    float pkL = 0.0f, pkR = 0.0f;
    for (int done = 0; done < frames;) {
        const int n = std::min(kBlock, frames - done);
        const float *src = in + static_cast<size_t>(done) * ch;
        for (int i = 0; i < n; ++i) {
            mGainS += (gainT - mGainS) * coef;
            const float l = src[static_cast<size_t>(i) * ch] * mGainS;
            const float r = stereo ? src[static_cast<size_t>(i) * ch + 1] * mGainS : l;
            mL[i] = l;
            mR[i] = r;
            pkL = std::max(pkL, std::fabs(l));
            pkR = std::max(pkR, std::fabs(r));
        }
        if (std::fabs(gainT - mGainS) < 1e-7f) mGainS = gainT;

        // Grabación: tal cual entra (con la ganancia, sin EQ). Solo cuadros completos.
        if (writeRec) {
            const float *data = mL;
            if (mRecChannels == 2) {
                for (int i = 0; i < n; ++i) {
                    mInter[2 * i] = mL[i];
                    mInter[2 * i + 1] = mR[i];
                }
                data = mInter;
            }
            const size_t want = static_cast<size_t>(n) * mRecChannels;
            const size_t room = (mRecRing.space() / mRecChannels) * mRecChannels;
            const size_t written = mRecRing.write(data, std::min(want, room));
            if (written < want) mRecDropped.fetch_add(static_cast<int64_t>((want - written) / mRecChannels));
        }

        // EQ en vivo → escucha y análisis.
        mEq.process(mL, stereo ? mR : nullptr, n);
        if (!stereo) std::copy(mL, mL + n, mR);
        if (monitor) {
            for (int i = 0; i < n; ++i) {
                mInter[2 * i] = mL[i];
                mInter[2 * i + 1] = mR[i];
            }
            // Solo cuadros completos; si no caben, la salida se pone al día sola.
            mMonitorRing.write(mInter, std::min(static_cast<size_t>(2 * n), (mMonitorRing.space() / 2) * 2));
        }
        mMixer.writeInputTap(mL, mR, n);
        done += n;
    }
    atomicMax(mPeakL, pkL);
    atomicMax(mPeakR, pkR);
}

// ---------------------------------------------------------------- hilo de salida

void LiveInput::mixMonitor(float *l, float *r, int n) {
    const bool on = monitoring();
    if (on && !mWasMonitoring) mMonitorRing.skip(mMonitorRing.available());  // lo viejo no se oye
    mWasMonitoring = on;
    const float gainT = on ? mMonitorGain.load(std::memory_order_relaxed) : 0.0f;
    if (!on && mMonGainS < 1e-5f) {
        mMonGainS = 0.0f;
        return;
    }
    // Retraso mínimo: si se juntó de más (la entrada llega en ráfagas), se tira lo más viejo.
    const size_t keep = 2 * static_cast<size_t>(n + 2 * mLastPush.load(std::memory_order_relaxed));
    const size_t avail = mMonitorRing.available();
    if (avail > keep) mMonitorRing.skip(((avail - keep) / 2) * 2);
    float buf[2 * 256];
    n = std::min(n, 256);
    const int got = static_cast<int>(mMonitorRing.read(buf, static_cast<size_t>(2 * n)) / 2);
    const float coef = smoothCoef(0.010f, mRate);
    for (int i = 0; i < n; ++i) {
        mMonGainS += (gainT - mMonGainS) * coef;
        if (i < got) {
            l[i] += buf[2 * i] * mMonGainS;
            r[i] += buf[2 * i + 1] * mMonGainS;
        }
    }
    if (std::fabs(gainT - mMonGainS) < 1e-7f) mMonGainS = gainT;
}

// ---------------------------------------------------------------- grabación

bool LiveInput::startRecording(const std::string &path, std::string &error) {
    if (mRecording.load()) {
        error = "ya se está grabando";
        return false;
    }
    if (!mActive.load()) {
        error = "la entrada está cerrada";
        return false;
    }
    if (mWriterThread.joinable()) mWriterThread.join();
    mRecChannels = std::clamp(mChannels.load(), 1, 2);
    if (!mWriter.open(path, mRecChannels, mRate)) {
        error = "no se pudo crear el archivo";
        return false;
    }
    mRecPath = path;
    mRecRing.skip(mRecRing.available());  // nadie escribe todavía (no se está grabando)
    {
        std::lock_guard<std::mutex> lk(mWaveMutex);
        mWave.clear();
    }
    mRecFrames.store(0);
    mRecDropped.store(0);
    mRecStart.store(0);
    mRecStarted.store(false);
    mWriterRun.store(true);
    mWriterThread = std::thread([this] { writerLoop(); });
    mRecording.store(true, std::memory_order_release);
    return true;
}

void LiveInput::writerLoop() {
    const int ch = mRecChannels;
    std::vector<float> buf(static_cast<size_t>(8192) * ch);
    float peak = 0.0f;
    int count = 0;
    std::vector<float> wave;
    for (;;) {
        const bool run = mWriterRun.load();
        const size_t got = mRecRing.read(buf.data(), buf.size());
        if (got > 0) {
            const size_t frames = got / ch;
            mWriter.write(buf.data(), frames);
            for (size_t f = 0; f < frames; ++f) {
                for (int c = 0; c < ch; ++c) peak = std::max(peak, std::fabs(buf[f * ch + c]));
                if (++count == kWaveStep) {
                    wave.push_back(peak);
                    peak = 0.0f;
                    count = 0;
                }
            }
            if (!wave.empty()) {
                std::lock_guard<std::mutex> lk(mWaveMutex);
                mWave.insert(mWave.end(), wave.begin(), wave.end());
                wave.clear();
            }
            mRecFrames.fetch_add(static_cast<int64_t>(frames));
            continue;  // vaciar rápido
        }
        if (!run) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (count > 0) {
        std::lock_guard<std::mutex> lk(mWaveMutex);
        mWave.push_back(peak);
    }
}

Take LiveInput::stopRecording() {
    Take t;
    if (!mRecording.exchange(false)) {
        t.error = "no se estaba grabando";
        return t;
    }
    // Lo que el hilo de entrada alcance a dejar en la cola se escribe antes de cerrar.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mWriterRun.store(false);
    if (mWriterThread.joinable()) mWriterThread.join();
    t.channels = mRecChannels;
    t.frames = mWriter.frames();
    const bool ok = mWriter.close();
    t.path = mRecPath;
    t.start = mRecStart.load();
    t.dropped = mRecDropped.load();
    if (!ok) t.error = "no se pudo escribir el archivo";
    return t;
}

double LiveInput::recordedSeconds() const { return static_cast<double>(mRecFrames.load()) / mRate; }

std::vector<float> LiveInput::recentWave(int cols, double seconds) const {
    std::vector<float> out(static_cast<size_t>(std::max(0, cols)), 0.0f);
    if (cols <= 0 || seconds <= 0) return out;
    std::lock_guard<std::mutex> lk(mWaveMutex);
    const double span = seconds * mRate / kWaveStep;  // valores de onda a lo ancho
    const double n = static_cast<double>(mWave.size());
    for (int c = 0; c < cols; ++c) {
        const double a = n - span + span * c / cols, b = n - span + span * (c + 1) / cols;
        float m = 0.0f;
        for (int64_t k = static_cast<int64_t>(std::floor(std::max(0.0, a))); k < static_cast<int64_t>(std::ceil(b)); ++k) {
            if (k >= 0 && k < static_cast<int64_t>(mWave.size())) m = std::max(m, mWave[static_cast<size_t>(k)]);
        }
        out[static_cast<size_t>(c)] = std::min(1.0f, m);
    }
    return out;
}
