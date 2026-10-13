#include "AudioInput.h"
#include <chrono>
#include <cmath>
#include <vector>
#include "engine/LiveInput.h"
int AudioInput::start(int, int channels) {
    stop();
    mRun.store(true);
    const int ch = channels == 2 ? 2 : 1;
    mThread = std::thread([this, ch] {
        std::vector<float> buf(480 * 2);
        double t = 0;
        auto next = std::chrono::steady_clock::now();
        while (mRun.load()) {
            for (int i = 0; i < 480; ++i, t += 1.0 / 48000) {
                // Frases de "voz": vocal con armónicos que sube y baja.
                const double env = std::pow(std::max(0.0, std::sin(2 * M_PI * 0.6 * t)), 2.0);
                const double f = 180 + 40 * std::sin(2 * M_PI * 0.3 * t);
                double x = 0;
                for (int k = 1; k <= 6; ++k) x += std::sin(2 * M_PI * f * k * t) / (k * 1.3);
                const float v = static_cast<float>(0.25 * env * x);
                for (int c = 0; c < ch; ++c) buf[i * ch + c] = v;
            }
            mLive.push(buf.data(), 480, ch);
            next += std::chrono::milliseconds(10);
            std::this_thread::sleep_until(next);
        }
    });
    return ch;
}
void AudioInput::stop() {
    mRun.store(false);
    if (mThread.joinable()) mThread.join();
}
