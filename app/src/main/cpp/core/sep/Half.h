#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// float16 (IEEE 754, "half") a float32. Los pesos grandes del modelo vienen así (la mitad de
// tamaño) y se pasan a float32 justo antes de usarlos.
inline float halfToFloat(uint16_t h) {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x3FFu;
    uint32_t bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {  // subnormal: se normaliza
            exp = 127 - 15 + 1;
            while ((mant & 0x400u) == 0) {
                mant <<= 1;
                --exp;
            }
            bits = sign | (exp << 23) | ((mant & 0x3FFu) << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);  // infinito o NaN
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// Muchos a la vez. En ARM de 64 bits el compilador usa la instrucción de conversión (rápida).
inline void halfToFloat(const uint16_t *in, float *out, size_t n) {
#if defined(__aarch64__)
    const __fp16 *h = reinterpret_cast<const __fp16 *>(in);
    for (size_t i = 0; i < n; ++i) out[i] = static_cast<float>(h[i]);
#else
    for (size_t i = 0; i < n; ++i) out[i] = halfToFloat(in[i]);
#endif
}
