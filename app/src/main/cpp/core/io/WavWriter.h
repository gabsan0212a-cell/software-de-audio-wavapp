#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// Escribe WAV float de 32 bits (formato interno de las pistas). El encabezado mide 44 bytes,
// así las muestras quedan alineadas a 4 bytes y el archivo se puede mapear directo en memoria.
class WavWriter {
public:
    WavWriter() = default;
    WavWriter(const WavWriter &) = delete;
    WavWriter &operator=(const WavWriter &) = delete;
    ~WavWriter();

    bool open(const std::string &path, int channels, int sampleRate);
    bool write(const float *interleaved, size_t frames);
    // Completa los tamaños del encabezado y cierra. Devuelve false si algo falló.
    bool close();

    int64_t frames() const { return mFrames; }
    static constexpr int kHeaderBytes = 44;

private:
    FILE *mFile = nullptr;
    int mChannels = 0;
    int64_t mFrames = 0;
    bool mOk = false;
};
