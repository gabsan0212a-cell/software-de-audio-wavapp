#include "dsp/LiveSpectrogram.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "dsp/AnalysisView.h"

LiveSpectrogram::LiveSpectrogram() : mData(static_cast<size_t>(kCols) * kRows, 0) {}

void LiveSpectrogram::reset() {
    std::fill(mData.begin(), mData.end(), 0);
    mCount = 0;
    mNextMs = -1;
}

void LiveSpectrogram::push(const SpectrumView &s, int64_t nowMs) {
    if (mNextMs < 0 || nowMs - mNextMs > 1000) mNextMs = nowMs;  // al empezar o tras una pausa larga: una sola
    if (nowMs < mNextMs) return;
    // La columna: el máximo de cada banda. El espectro del analizador es magnitud / N (como Web
    // Audio): 12 dB por debajo del de los archivos, de ahí el +12.
    float db[kRows];
    AnalysisView::spectrumColumns(s, db, kRows);  // las mismas bandas: de 20 Hz a 20 kHz
    uint8_t col[kRows];
    for (int r = 0; r < kRows; ++r)
        col[r] = static_cast<uint8_t>(std::clamp(std::lround((db[r] + 12.0f + 90.0f) / 80.0f * 255.0f), 0L, 255L));
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

int LiveSpectrogram::readSince(uint64_t since, uint8_t *out) const {
    const uint64_t from = std::max(since, mCount > static_cast<uint64_t>(kCols) ? mCount - kCols : 0);
    if (from >= mCount) return 0;
    const int n = static_cast<int>(mCount - from);
    for (int c = 0; c < n; ++c)
        std::memcpy(out + static_cast<size_t>(c) * kRows, mData.data() + ((from + c) % kCols) * kRows, kRows);
    return n;
}
