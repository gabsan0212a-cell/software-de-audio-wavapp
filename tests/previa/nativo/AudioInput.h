#pragma once
// Entrada de escritorio (vista previa): una "voz" de prueba a ritmo real.
#include <atomic>
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
private:
    LiveInput &mLive;
    std::thread mThread;
    std::atomic<bool> mRun{false};
};
