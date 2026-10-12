#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Audio decodificado a float intercalado (-1..1), en su frecuencia original.
struct Clip {
    std::vector<float> data;
    int32_t channels = 0;
    int32_t sampleRate = 0;
    int32_t bits = 0;
    bool isFloat = false;
    int64_t frames = 0;
};

// Lee un WAV PCM (8/16/24/32 bits enteros) o float (32/64). Cierra el FILE*.
// Devuelve "" si todo bien, o un mensaje de error.
std::string readWav(FILE *f, Clip &out);
