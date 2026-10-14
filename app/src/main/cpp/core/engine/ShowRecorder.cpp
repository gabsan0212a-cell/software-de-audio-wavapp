#include "engine/ShowRecorder.h"

#include <algorithm>
#include <chrono>
#include <vector>

ShowRecorder::~ShowRecorder() {
    if (mRecording.load() || mThread.joinable()) stop();
}

bool ShowRecorder::start(const std::string &path, std::string &error) {
    if (mRecording.load()) {
        error = "ya se está grabando";
        return false;
    }
    if (mThread.joinable()) mThread.join();
    if (!mWriter.open(path, 2, mRate)) {
        error = "no se pudo crear el archivo";
        return false;
    }
    mPath = path;
    mRing.skip(mRing.available());  // nadie escribe todavía
    mFrames.store(0);
    mDropped.store(0);
    mWriterRun.store(true);
    mThread = std::thread([this] { writerLoop(); });
    mRecording.store(true, std::memory_order_release);
    return true;
}

void ShowRecorder::push(const float *l, const float *r, int n) {
    if (!mRecording.load(std::memory_order_acquire)) return;
    for (int done = 0; done < n;) {
        const int k = std::min(256, n - done);
        for (int i = 0; i < k; ++i) {
            mInter[2 * i] = l[done + i];
            mInter[2 * i + 1] = r[done + i];
        }
        const size_t want = static_cast<size_t>(2 * k);
        const size_t room = (mRing.space() / 2) * 2;  // solo cuadros completos
        const size_t wrote = mRing.write(mInter, std::min(want, room));
        if (wrote < want) mDropped.fetch_add(static_cast<int64_t>((want - wrote) / 2), std::memory_order_relaxed);
        done += k;
    }
}

void ShowRecorder::writerLoop() {
    std::vector<float> buf(8192 * 2);
    for (;;) {
        const bool run = mWriterRun.load();
        const size_t got = mRing.read(buf.data(), buf.size());
        if (got > 0) {
            mWriter.write(buf.data(), got / 2);
            mFrames.fetch_add(static_cast<int64_t>(got / 2));
            continue;
        }
        if (!run) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

ShowRecorder::Result ShowRecorder::stop() {
    Result res;
    const bool was = mRecording.exchange(false);
    // Lo que el hilo de salida alcance a dejar en la cola se escribe antes de cerrar.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mWriterRun.store(false);
    if (mThread.joinable()) mThread.join();
    if (!was) {
        res.error = "no se estaba grabando";
        return res;
    }
    res.frames = mWriter.frames();
    res.dropped = mDropped.load();
    res.path = mPath;
    if (!mWriter.close()) res.error = "no se pudo escribir el archivo";
    return res;
}
