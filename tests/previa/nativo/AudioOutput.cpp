#include "AudioOutput.h"
#include <chrono>
#include <vector>
#include "engine/Mixer.h"
bool AudioOutput::start() {
    std::lock_guard<std::mutex> lk(mMutex);
    if (mRunning) return true;
    mRunning = true;
    mThread = std::thread([this] {
        std::vector<float> buf(480 * 2);
        auto t = std::chrono::steady_clock::now();
        while (mRunning) {
            mMixer.process(buf.data(), 480, 2);
            t += std::chrono::milliseconds(10);
            std::this_thread::sleep_until(t);
        }
    });
    return true;
}
void AudioOutput::stop() {
    std::lock_guard<std::mutex> lk(mMutex);
    mRunning = false;
    if (mThread.joinable()) mThread.join();
}
void AudioOutput::info(int32_t out[6]) {
    out[0] = 48000; out[1] = 480; out[2] = 960; out[3] = 0; out[4] = 0; out[5] = 0;
}
