#include "engine/Exporter.h"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "engine/Mixer.h"
#include "dsp/Resampler.h"

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 4096;

void put32(std::vector<uint8_t> &v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void put16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void tag(std::vector<uint8_t> &v, const char *t) { v.insert(v.end(), t, t + 4); }

int64_t outFrames(int64_t frames48k, int rate) {
    return rate == kRate ? frames48k : Resampler::outputFrames(frames48k, kRate, rate);
}

// Encabezado completo con los tamaños ya conocidos. Float lleva fmt de 18 bytes y "fact".
std::vector<uint8_t> header(const ExportFormat &f, uint32_t dataBytes, uint32_t frames) {
    const int bytes = f.bits / 8, block = 2 * bytes;
    std::vector<uint8_t> h;
    tag(h, "RIFF");
    put32(h, 0);  // se completa abajo
    tag(h, "WAVE");
    tag(h, "fmt ");
    put32(h, f.isFloat ? 18 : 16);
    put16(h, f.isFloat ? 3 : 1);
    put16(h, 2);
    put32(h, static_cast<uint32_t>(f.sampleRate));
    put32(h, static_cast<uint32_t>(f.sampleRate * block));
    put16(h, static_cast<uint16_t>(block));
    put16(h, static_cast<uint16_t>(f.bits));
    if (f.isFloat) {
        put16(h, 0);
        tag(h, "fact");
        put32(h, 4);
        put32(h, frames);
    }
    tag(h, "data");
    put32(h, dataBytes);
    const uint32_t riff = static_cast<uint32_t>(h.size() - 8 + dataBytes);
    std::memcpy(&h[4], &riff, 4);
    return h;
}

bool writeAll(int fd, const void *p, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    while (n > 0) {
        const ssize_t w = ::write(fd, b, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        b += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

// Convierte float intercalado al formato pedido. Enteros: dither triangular (TPDF) de 1 LSB.
class Converter {
public:
    explicit Converter(const ExportFormat &f) : mF(f) {}
    void convert(const float *in, size_t samples, std::vector<uint8_t> &out) {
        out.clear();
        if (mF.isFloat) {
            out.resize(samples * 4);
            std::memcpy(out.data(), in, samples * 4);
            return;
        }
        const int bytes = mF.bits / 8;
        const double scale = mF.bits == 16 ? 32768.0 : 8388608.0;
        const double lo = -scale, hi = scale - 1;
        out.resize(samples * bytes);
        uint8_t *o = out.data();
        for (size_t i = 0; i < samples; ++i) {
            const double d = rnd() - rnd();  // triangular en [-1, 1] LSB
            const double v = std::clamp(std::nearbyint(in[i] * scale + d), lo, hi);
            const int32_t q = static_cast<int32_t>(v);
            for (int b = 0; b < bytes; ++b) *o++ = static_cast<uint8_t>(q >> (8 * b));
        }
    }

private:
    double rnd() {  // xorshift32 → [0, 1)
        mSeed ^= mSeed << 13;
        mSeed ^= mSeed >> 17;
        mSeed ^= mSeed << 5;
        return mSeed / 4294967296.0;
    }
    ExportFormat mF;
    uint32_t mSeed = 0x9E3779B9u;
};

}  // namespace

int64_t exportBytes(int64_t frames48k, const ExportFormat &fmt) {
    return outFrames(frames48k, fmt.sampleRate) * 2 * (fmt.bits / 8) + (fmt.isFloat ? 58 : 44);
}

ExportResult exportMix(const Mixer &live, int fd, const ExportFormat &fmtIn, std::atomic<float> *progress,
                       const std::atomic<bool> *cancel) {
    ExportResult res;
    ExportFormat fmt = fmtIn;
    if (fmt.isFloat) fmt.bits = 32;
    if (fmt.bits != 16 && fmt.bits != 24 && fmt.bits != 32) fmt.bits = 24;
    if (fmt.bits == 32) fmt.isFloat = true;  // 32 bits es float
    if (fmt.sampleRate != 44100) fmt.sampleRate = kRate;
    if (progress) progress->store(0.0f);

    auto fail = [&](const std::string &e) {
        if (fd >= 0) ::close(fd);
        res.error = e;
        return res;
    };
    if (fd < 0) return fail("No se pudo crear el archivo");

    auto mixer = std::make_unique<Mixer>(kRate);  // en el heap: es grande
    std::string err;
    if (!live.cloneInto(*mixer, err)) return fail(err);
    const int64_t length = mixer->length();
    if (length <= 0) return fail("No hay pistas para exportar");
    mixer->prepareOffline();

    const int64_t frames = outFrames(length, fmt.sampleRate);
    const uint64_t dataBytes = static_cast<uint64_t>(frames) * 2 * (fmt.bits / 8);
    if (dataBytes > 0xFFFFFFFFull - 100) return fail("La mezcla es demasiado larga para un WAV (más de 4 GB)");
    const std::vector<uint8_t> h = header(fmt, static_cast<uint32_t>(dataBytes), static_cast<uint32_t>(frames));
    if (!writeAll(fd, h.data(), h.size())) return fail("No se pudo escribir el archivo (¿falta espacio?)");

    std::unique_ptr<Resampler> rs;
    if (fmt.sampleRate != kRate) rs = std::make_unique<Resampler>(2, kRate, fmt.sampleRate);
    Converter conv(fmt);
    std::vector<float> block(kBlock * 2), resampled;
    std::vector<uint8_t> bytes;
    int64_t done = 0, written = 0;

    auto emit = [&](const float *x, int64_t n) -> bool {
        n = std::min(n, frames - written);  // nunca más de lo que dice el encabezado
        if (n <= 0) return true;
        conv.convert(x, static_cast<size_t>(n * 2), bytes);
        if (!writeAll(fd, bytes.data(), bytes.size())) return false;
        written += n;
        return true;
    };

    while (done < length) {
        if (cancel && cancel->load()) return fail("Exportación cancelada");
        const int n = static_cast<int>(std::min<int64_t>(kBlock, length - done));
        mixer->process(block.data(), n, 2);
        bool ok;
        if (rs) {
            resampled.clear();
            rs->process(block.data(), static_cast<size_t>(n), resampled);
            ok = emit(resampled.data(), static_cast<int64_t>(resampled.size() / 2));
        } else {
            ok = emit(block.data(), n);
        }
        if (!ok) return fail("No se pudo escribir el archivo (¿falta espacio?)");
        done += n;
        if (progress) progress->store(static_cast<float>(done) / static_cast<float>(length));
    }
    if (rs) {
        resampled.clear();
        rs->finish(resampled);
        if (!emit(resampled.data(), static_cast<int64_t>(resampled.size() / 2)))
            return fail("No se pudo escribir el archivo (¿falta espacio?)");
    }
    // Por si el remuestreo quedó corto: se completa con silencio hasta lo prometido.
    std::fill(block.begin(), block.end(), 0.0f);
    while (written < frames) {
        if (!emit(block.data(), std::min<int64_t>(kBlock, frames - written)))
            return fail("No se pudo escribir el archivo (¿falta espacio?)");
    }
    if (::close(fd) != 0) {
        res.error = "No se pudo terminar de guardar el archivo";
        return res;
    }
    res.frames = frames;
    res.bytes = static_cast<int64_t>(h.size() + dataBytes);
    if (progress) progress->store(1.0f);
    return res;
}
