#include "dsp/LiveSpectrogram.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "dsp/Analyzer.h"

LiveSpectrogram::LiveSpectrogram() : mData(static_cast<size_t>(kCols) * kRows, 0) {}

void LiveSpectrogram::reset() {
    std::fill(mData.begin(), mData.end(), 0);
    mCount = 0;
    mNextMs = -1;
}

void LiveSpectrogram::push(const Analyzer &a, int64_t nowMs) {
    if (mNextMs < 0 || nowMs - mNextMs > 1000) mNextMs = nowMs;  // al empezar o tras una pausa larga: una sola
    if (nowMs < mNextMs) return;
    // La columna: el máximo de cada banda. El espectro del analizador es magnitud / N (como Web
    // Audio): 12 dB por debajo del de los archivos, de ahí el +12.
    uint8_t col[kRows];
    const float hz = a.binHz();
    const float *sp = a.spectrumDb();
    for (int r = 0; r < kRows; ++r) {
        const float f0 = 20.0f * std::pow(1000.0f, static_cast<float>(r) / kRows);
        const float f1 = 20.0f * std::pow(1000.0f, static_cast<float>(r + 1) / kRows);
        const int i0 = std::clamp(static_cast<int>(f0 / hz), 0, Analyzer::kSpecBins - 1);
        const int i1 = std::clamp(static_cast<int>(f1 / hz), i0, Analyzer::kSpecBins - 1);
        float m = -200.0f;
        for (int i = i0; i <= i1; ++i) m = std::max(m, sp[i]);
        col[r] = static_cast<uint8_t>(std::clamp(std::lround((m + 12.0f + 90.0f) / 80.0f * 255.0f), 0L, 255L));
    }
    while (mNextMs <= nowMs) {
        std::memcpy(mData.data() + static_cast<size_t>(mCount % kCols) * kRows, col, kRows);
        ++mCount;
        mNextMs += kStepMs;
    }
}

void LiveSpectrogram::read(uint8_t *out) const {
    // Las que todavía no hubo quedan en silencio, a la izquierda.
    const uint64_t have = std::min<uint64_t>(mCount, kCols);
    const size_t empty = static_cast<size_t>(kCols - have);
    std::fill(out, out + empty * kRows, 0);
    for (uint64_t c = 0; c < have; ++c) {
        const uint64_t src = (mCount - have + c) % kCols;
        std::memcpy(out + (empty + c) * kRows, mData.data() + src * kRows, kRows);
    }
}
