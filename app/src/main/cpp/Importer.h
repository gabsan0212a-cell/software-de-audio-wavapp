#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "MappedAudio.h"

struct ImportInfo {
    int srcRate = 0;
    int srcChannels = 0;
    int srcBits = 0;
    bool srcFloat = false;
    int channels = 0;      // canales guardados (1 o 2)
    int64_t frames = 0;    // cuadros guardados (a la frecuencia del proyecto)
    bool dualMono = false; // era estéreo con L = R: se guardó en mono, como en el prototipo
};

// Convierte un WAV al formato interno: float 32, `sampleRate`, 1 o 2 canales.
// Cierra `src`. `progress` (opcional) va de 0 a 1. Devuelve "" o un mensaje de error.
std::string importWav(FILE *src, const std::string &dstPath, int sampleRate, ImportInfo &info,
                      std::atomic<float> *progress = nullptr);

// Pistas de prueba del prototipo (12 s, mono): 0 Bajo, 1 Voz, 2 Guitarra, 3 Teclado.
constexpr int kDemoTracks = 4;
std::vector<float> synthDemo(int index, int sampleRate);
std::string writeDemoTrack(int index, const std::string &dstPath, int sampleRate);

// Picos para dibujar la forma de onda: por bloque de `framesPerBucket` y por canal, min y max.
// Distribución: [bucket][canal][min, max].
std::vector<float> computePeaks(const MappedAudio &audio, int framesPerBucket);
