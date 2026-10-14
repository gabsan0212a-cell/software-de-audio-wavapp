#pragma once
// Entrada de escritorio (vista previa): una "voz" de prueba a ritmo real.
#include <atomic>
#include <cstdint>
#include <thread>
class LiveInput;
class AudioInput {
public:
    explicit AudioInput(LiveInput &live) : mLive(live) {}
    ~AudioInput() { stop(); }
    int start(int deviceId, int channels);
    void stop();
    int latencyFrames() { return mRun.load() ? 480 : -1; }
    bool lost() { return false; }
    bool open() const { return mRun.load(); }
    void pull(int) {}  // aquí la "voz" llega desde su propio hilo
    void stats(int64_t out[2]) const { out[0] = out[1] = 0; }
private:
    LiveInput &mLive;
    std::thread mThread;
    std::atomic<bool> mRun{false};
};
