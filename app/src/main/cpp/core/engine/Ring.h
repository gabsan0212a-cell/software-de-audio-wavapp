#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

// Cola sin bloqueos para un hilo que escribe y otro que lee (muestras float).
class SpscRing {
public:
    explicit SpscRing(size_t capacityPow2);
    size_t write(const float *v, size_t n);  // escribe lo que quepa; devuelve cuántas
    size_t read(float *v, size_t n);
    size_t available() const;
    size_t space() const { return mBuf.size() - available(); }  // solo quien escribe
    void skip(size_t n);  // solo quien lee

private:
    std::vector<float> mBuf;
    size_t mMask;
    std::atomic<uint64_t> mWrite{0}, mRead{0};
};
