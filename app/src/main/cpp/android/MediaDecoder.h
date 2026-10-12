#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "engine/Decoder.h"

// Decodificador de Android (MediaExtractor + MediaCodec, desde C++ con el NDK): MP3, AAC/M4A,
// FLAC, OGG, Opus... lo que soporte el celular. Pide la salida en float para no perder bits
// (p. ej. FLAC de 24 bits); si el códec no puede, usa la que dé.
std::string decodeWithMediaCodec(int fd, int64_t length, PcmSink &sink, std::string &codec,
                                 std::atomic<float> *progress);
