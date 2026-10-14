#pragma once
// Salida de audio de escritorio (vista previa): pide la mezcla a ritmo real y la descarta.
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
class AudioInput;
class Mixer;
class AudioOutput {
public:
    explicit AudioOutput(Mixer &mixer) : mMixer(mixer) {}
    ~AudioOutput() { stop(); }
    bool start();
    void stop();
    void info(int32_t out[6]);
    int latencyFrames() { return 960; }
    void setInput(AudioInput *) {}
    void setLiveMode(bool) {}
private:
    Mixer &mMixer;
    std::thread mThread;
    std::atomic<bool> mRunning{false};
    std::mutex mMutex;
};
