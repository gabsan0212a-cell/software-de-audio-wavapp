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
    int start(int deviceId, int channels, bool pull = true);
    void stop();
    int latencyFrames() { return mRun.load() ? 480 : -1; }
    bool lost() { return false; }
    bool open() const { return mRun.load(); }
    bool stuck() const { return false; }
    int deviceId() const { return 0; }
    int channelsWanted() const { return 1; }
    void pull(int) {}  // aquí la "voz" llega desde su propio hilo
    static constexpr int kInfoCount = 7;
    void info(int32_t *out) {
        for (int i = 0; i < kInfoCount; ++i) out[i] = 0;
        out[0] = mRun.load() ? 2 : 0;
    }
private:
    LiveInput &mLive;
    std::thread mThread;
    std::atomic<bool> mRun{false};
};
