#include "engine/Ring.h"

#include <algorithm>

SpscRing::SpscRing(size_t capacityPow2) : mBuf(capacityPow2), mMask(capacityPow2 - 1) {}

size_t SpscRing::available() const {
    return static_cast<size_t>(mWrite.load(std::memory_order_acquire) - mRead.load(std::memory_order_acquire));
}

size_t SpscRing::write(const float *v, size_t n) {
    const uint64_t w = mWrite.load(std::memory_order_relaxed);
    const uint64_t r = mRead.load(std::memory_order_acquire);
    n = std::min(n, mBuf.size() - static_cast<size_t>(w - r));
    for (size_t i = 0; i < n; ++i) mBuf[(w + i) & mMask] = v[i];
    mWrite.store(w + n, std::memory_order_release);
    return n;
}

size_t SpscRing::read(float *v, size_t n) {
    const uint64_t r = mRead.load(std::memory_order_relaxed);
    const uint64_t w = mWrite.load(std::memory_order_acquire);
    n = std::min(n, static_cast<size_t>(w - r));
    for (size_t i = 0; i < n; ++i) v[i] = mBuf[(r + i) & mMask];
    mRead.store(r + n, std::memory_order_release);
    return n;
}

void SpscRing::skip(size_t n) {
    const uint64_t r = mRead.load(std::memory_order_relaxed);
    const uint64_t w = mWrite.load(std::memory_order_acquire);
    mRead.store(r + std::min(n, static_cast<size_t>(w - r)), std::memory_order_release);
}

