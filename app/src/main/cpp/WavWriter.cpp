#include "WavWriter.h"

#include <cstring>

namespace {

void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

void header(uint8_t *h, int channels, int sampleRate, uint32_t dataBytes) {
    std::memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + dataBytes);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 3);  // WAVE_FORMAT_IEEE_FLOAT
    put16(h + 22, channels);
    put32(h + 24, sampleRate);
    put32(h + 28, sampleRate * channels * 4);
    put16(h + 32, channels * 4);
    put16(h + 34, 32);
    std::memcpy(h + 36, "data", 4);
    put32(h + 40, dataBytes);
}

}  // namespace

WavWriter::~WavWriter() {
    if (mFile) std::fclose(mFile);
}

bool WavWriter::open(const std::string &path, int channels, int sampleRate) {
    mFile = std::fopen(path.c_str(), "wb");
    if (!mFile) return false;
    mChannels = channels;
    mFrames = 0;
    uint8_t h[kHeaderBytes];
    header(h, channels, sampleRate, 0);
    mOk = std::fwrite(h, 1, kHeaderBytes, mFile) == kHeaderBytes;
    // Guardamos la frecuencia en el encabezado provisional; close() solo corrige tamaños.
    return mOk;
}

bool WavWriter::write(const float *interleaved, size_t frames) {
    if (!mFile || !mOk) return false;
    // Android y las PC son little-endian, igual que WAV: se escribe tal cual.
    size_t n = std::fwrite(interleaved, sizeof(float) * mChannels, frames, mFile);
    mFrames += static_cast<int64_t>(n);
    mOk = (n == frames);
    return mOk;
}

bool WavWriter::close() {
    if (!mFile) return false;
    const uint64_t bytes = static_cast<uint64_t>(mFrames) * mChannels * 4;
    // Más de 4 GB no cabe en un WAV normal: se marca como error.
    bool ok = mOk && bytes <= 0xFFFFFFFFull - 36;
    if (ok) {
        uint8_t b[4];
        put32(b, static_cast<uint32_t>(36 + bytes));
        ok = std::fseek(mFile, 4, SEEK_SET) == 0 && std::fwrite(b, 1, 4, mFile) == 4;
        put32(b, static_cast<uint32_t>(bytes));
        ok = ok && std::fseek(mFile, 40, SEEK_SET) == 0 && std::fwrite(b, 1, 4, mFile) == 4;
    }
    ok = (std::fclose(mFile) == 0) && ok;
    mFile = nullptr;
    return ok;
}
