#include "dsp/Fft.h"

#include <cmath>
#include <utility>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

FFT::FFT(int n) : mN(n), mRev(n), mCos(n / 2), mSin(n / 2) {
    int bits = 0;
    while ((1 << bits) < n) ++bits;
    for (int i = 0; i < n; ++i) {
        int r = 0, x = i;
        for (int b = 0; b < bits; ++b) {
            r = (r << 1) | (x & 1);
            x >>= 1;
        }
        mRev[i] = r;
    }
    for (int i = 0; i < n / 2; ++i) {
        mCos[i] = std::cos(2 * kPi * i / n);
        mSin[i] = -std::sin(2 * kPi * i / n);
    }
}

void FFT::forward(double *re, double *im) const {
    const int n = mN;
    for (int i = 0; i < n; ++i) {
        const int j = mRev[i];
        if (j > i) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int size = 2; size <= n; size <<= 1) {
        const int half = size >> 1, step = n / size;
        for (int i = 0; i < n; i += size) {
            for (int j = 0, k = 0; j < half; ++j, k += step) {
                const int a = i + j, b = a + half;
                const double wr = mCos[k], wi = mSin[k];
                const double tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}
