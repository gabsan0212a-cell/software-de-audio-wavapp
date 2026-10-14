#include "AudioInput.h"
#include <chrono>
#include <cmath>
#include <vector>
#include "engine/LiveInput.h"
int AudioInput::start(int, int channels, bool) {
    stop();
    mRun.store(true);
    const int ch = channels == 2 ? 2 : 1;
    mThread = std::thread([this, ch] {
        std::vector<float> buf(480 * 2);
        double t = 0, fase = 0;
        auto next = std::chrono::steady_clock::now();
        while (mRun.load()) {
            for (int i = 0; i < 480; ++i, t += 1.0 / 48000) {
                // Frases de "voz" (2.6 s cantando, 0.4 s de respiro): vocal con armónicos que sube y
                // baja despacio (un poco desafinada, para que el auto-tune trabaje).
                const double u = std::fmod(t, 3.0);
                const double env = u < 2.6 ? std::min(1.0, u / 0.05) * std::min(1.0, (2.6 - u) / 0.05) : 0.0;
                const double f = 180 + 40 * std::sin(2 * M_PI * 0.3 * t);
                fase += f / 48000;
                if (fase >= 1.0) fase -= 1.0;
                double x = 0;
                for (int k = 1; k <= 6; ++k) x += std::sin(2 * M_PI * fase * k) / (k * 1.3);
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
