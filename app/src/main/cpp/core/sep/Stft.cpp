#include "sep/Stft.h"

#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

// Índice con reflejo (como numpy/torch "reflect": el borde no se repite).
inline int reflect(int i, int length) {
    if (i < 0) i = -i;
    if (i >= length) i = 2 * (length - 1) - i;
    return i;
}
}  // namespace

StereoStft::StereoStft(int nfft, int hop) : mN(nfft), mHop(hop), mFft(nfft), mWindow(static_cast<size_t>(nfft)) {
    // Hann periódica (torch.hann_window), redondeada a float como la de PyTorch.
    for (int i = 0; i < nfft; ++i)
        mWindow[static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / nfft));
}

void StereoStft::forward(const float *l, const float *r, int length, float *spec) const {
    const int n = mN, half = mN / 2, pad = mN / 2;
    const int frames = this->frames(length);
    std::vector<double> re(static_cast<size_t>(n)), im(static_cast<size_t>(n));
    for (int t = 0; t < frames; ++t) {
        const int start = t * mHop - pad;
        for (int k = 0; k < n; ++k) {
            const int i = reflect(start + k, length);
            re[static_cast<size_t>(k)] = l[i] * mWindow[static_cast<size_t>(k)];
            im[static_cast<size_t>(k)] = r[i] * mWindow[static_cast<size_t>(k)];
        }
        mFft.forward(re.data(), im.data());
        float *out = spec + static_cast<size_t>(t) * (half + 1) * 4;
        for (int f = 0; f <= half; ++f) {
            const int g = f == 0 ? 0 : n - f;
            const double zr = re[static_cast<size_t>(f)], zi = im[static_cast<size_t>(f)];
            const double wr = re[static_cast<size_t>(g)], wi = im[static_cast<size_t>(g)];
            // Z[f] = L[f] + i·R[f] y Z[n-f] = conj(L[f]) + i·conj(R[f]).
            out[4 * f + 0] = static_cast<float>(0.5 * (zr + wr));
            out[4 * f + 1] = static_cast<float>(0.5 * (zi - wi));
            out[4 * f + 2] = static_cast<float>(0.5 * (zi + wi));
            out[4 * f + 3] = static_cast<float>(-0.5 * (zr - wr));
        }
    }
}

void StereoStft::inverse(const float *spec, int frames, float *l, float *r) const {
    const int n = mN, half = mN / 2, pad = mN / 2;
    const int total = n + mHop * (frames - 1);
    std::vector<double> outL(static_cast<size_t>(total), 0.0), outR(static_cast<size_t>(total), 0.0);
    std::vector<double> env(static_cast<size_t>(total), 0.0);
    std::vector<double> re(static_cast<size_t>(n)), im(static_cast<size_t>(n));
    for (int t = 0; t < frames; ++t) {
        const float *in = spec + static_cast<size_t>(t) * (half + 1) * 4;
        // Espectro completo (hermítico) de cada canal; como irfft, se ignora la parte imaginaria
        // de la frecuencia 0 y de la de Nyquist. Se arma Z = L + i·R y se usa conj(FFT(conj(Z))).
        for (int f = 0; f <= half; ++f) {
            double lr = in[4 * f + 0], li = in[4 * f + 1], rr = in[4 * f + 2], ri = in[4 * f + 3];
            if (f == 0 || f == half) li = ri = 0.0;
            re[static_cast<size_t>(f)] = lr - ri;
            im[static_cast<size_t>(f)] = -(li + rr);
            if (f != 0 && f != half) {
                re[static_cast<size_t>(n - f)] = lr + ri;
                im[static_cast<size_t>(n - f)] = -(rr - li);
            }
        }
        mFft.forward(re.data(), im.data());
        const int at = t * mHop;
        for (int k = 0; k < n; ++k) {
            const double w = mWindow[static_cast<size_t>(k)];
            outL[static_cast<size_t>(at + k)] += re[static_cast<size_t>(k)] / n * w;
            outR[static_cast<size_t>(at + k)] += -im[static_cast<size_t>(k)] / n * w;
            env[static_cast<size_t>(at + k)] += w * w;
        }
    }
    const int length = mHop * (frames - 1);
    for (int i = 0; i < length; ++i) {
        const double e = env[static_cast<size_t>(i + pad)];
        const double g = e > 1e-11 ? 1.0 / e : 0.0;
        l[i] = static_cast<float>(outL[static_cast<size_t>(i + pad)] * g);
        r[i] = static_cast<float>(outR[static_cast<size_t>(i + pad)] * g);
    }
}
