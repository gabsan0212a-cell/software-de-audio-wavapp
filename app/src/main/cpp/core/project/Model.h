#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/Mixer.h"

// El proyecto tal como lo guarda la sesión y lo ve la pantalla. El Studio es su dueño.

struct EqBandModel {
    int slot = 0;         // banda del Equalizer (0..11)
    int type = 0;         // FilterType
    float freq = 1000.0f;
    float gain = 0.0f;
    float q = 1.4f;
    bool on = true;
};

struct TrackModel {
    int id = 0;
    int slot = -1;                 // espacio del mezclador
    std::string name;
    std::string file;              // nombre del archivo interno (en la carpeta de audio)
    std::string info;              // de dónde vino, para mostrar ("44.1 kHz → 48 kHz · 16 bits · estéreo")
    int channels = 0;
    int64_t frames = 0;
    float volDb = 0.0f;            // -60 (= -∞) .. +6, en pasos de 0.5
    int pan = 0;                   // -100 .. 100
    bool mute = false, solo = false, phase = false;
    bool eqBypass = false, eqAuto = true;
    std::vector<EqBandModel> bands;  // en el orden en que se agregaron
    std::vector<Region> regions;
    bool hasRegions = false;       // al leer la sesión: traía pedazos (si no, el archivo completo)
    std::vector<float> peaks;      // forma de onda: [bloque][canal][min, max]
};

struct MasterModel {
    float volDb = 0.0f;  // -70 (= -∞) .. +6
    bool mute = false;
    bool limiter = true;
    int notch = 0;       // 0, 50 o 60 Hz
    int exportBits = 24; // 16, 24 o 32 (float)
    int exportRate = 48000;
};

struct ProjectModel {
    int nextId = 1;
    MasterModel master;
    std::vector<TrackModel> tracks;  // en el orden de la pantalla
};
