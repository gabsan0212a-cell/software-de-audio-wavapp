#pragma once

#include <atomic>
#include <cstdint>
#include <string>

class Mixer;

struct ExportFormat {
    int bits = 24;          // 16, 24 o 32
    bool isFloat = false;   // 32 bits float
    int sampleRate = 48000; // 48000 o 44100
};

struct ExportResult {
    std::string error;  // "" si salió bien
    int64_t frames = 0;
    int64_t bytes = 0;
};

// Renderiza la mezcla completa fuera de tiempo real (con una copia del mezclador, así lo que
// suena en vivo no se toca) y la escribe como WAV en `fd`, que se cierra al final. El WAV se
// escribe de corrido, sin volver atrás, así funciona en cualquier carpeta que elija el usuario.
ExportResult exportMix(const Mixer &live, int fd, const ExportFormat &fmt, std::atomic<float> *progress,
                       const std::atomic<bool> *cancel);

// Tamaño aproximado del archivo, para mostrarlo antes de exportar.
int64_t exportBytes(int64_t frames48k, const ExportFormat &fmt);
