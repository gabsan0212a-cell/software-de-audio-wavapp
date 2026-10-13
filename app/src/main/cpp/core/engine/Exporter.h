#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "dsp/Loudness.h"

class Mixer;

struct ExportFormat {
    int bits = 24;          // 16, 24 o 32
    bool isFloat = false;   // 32 bits float
    int sampleRate = 48000; // 48000 o 44100
    // Ajustar el volumen a estos LUFS (p. ej. −14); 0 = como suena.
    float targetLufs = 0.0f;
};

struct ExportResult {
    std::string error;  // "" si salió bien
    int64_t frames = 0;
    int64_t bytes = 0;
    float lufs = kLoudnessNone;      // integrado de lo que se escribió
    float truePeak = kLoudnessNone;  // dBTP
    float gainDb = 0.0f;             // lo que se ajustó el master para llegar al objetivo
};

// Volumen de la mezcla completa, medido fuera de tiempo real (lo que saldría al exportar).
struct LoudnessReport {
    std::string error;
    float integrated = kLoudnessNone, range = kLoudnessNone, truePeak = kLoudnessNone;
    float maxMomentary = kLoudnessNone, maxShortTerm = kLoudnessNone;
    double seconds = 0.0;
    std::vector<float> shortTerm;  // corto plazo cada 100 ms, a lo largo de la canción
};

// Renderiza la mezcla completa fuera de tiempo real (con una copia del mezclador, así lo que
// suena en vivo no se toca) y la escribe como WAV en `fd`, que se cierra al final. El WAV se
// escribe de corrido, sin volver atrás, así funciona en cualquier carpeta que elija el usuario.
// Con un objetivo de LUFS, primero se mide (dos pasadas) y se ajusta el master antes del
// limitador; si hay que subir, el limitador queda prendido para que no se recorte.
ExportResult exportMix(const Mixer &live, int fd, const ExportFormat &fmt, std::atomic<float> *progress,
                       const std::atomic<bool> *cancel);

// Mide la mezcla completa con `gainDb` de más en el master (antes del limitador). El progreso
// va de `from` a `to`.
LoudnessReport measureMix(const Mixer &live, float gainDb, bool forceLimiter, std::atomic<float> *progress,
                          float from, float to, const std::atomic<bool> *cancel);

// Tamaño aproximado del archivo, para mostrarlo antes de exportar.
int64_t exportBytes(int64_t frames48k, const ExportFormat &fmt);
