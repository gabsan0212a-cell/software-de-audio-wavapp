#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

// Recibe audio decodificado (float -1..1 intercalado) y lo convierte al formato interno.
class PcmSink {
public:
    virtual ~PcmSink() = default;
    // Antes del primer write. Devuelve "" o un error.
    virtual std::string begin(int sampleRate, int channels) = 0;
    virtual std::string write(const float *interleaved, size_t frames) = 0;
};

// Decodificador de formatos comprimidos (MP3, AAC/M4A, FLAC, OGG, Opus...). Lo pone la
// plataforma (en Android, el de MediaCodec desde C++). Lee `fd` (sin cerrarlo) de `length`
// bytes, manda el audio a `sink`, llena `codec` con un nombre corto ("MP3", "AAC"...) y
// avanza `progress` de 0 a 1. Devuelve "" o un error.
using DecodeFunction = std::function<std::string(int fd, int64_t length, PcmSink &sink, std::string &codec,
                                                 std::atomic<float> *progress)>;
