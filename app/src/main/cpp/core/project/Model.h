#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/Mixer.h"
#include "engine/VoiceFx.h"

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
    fx::Values fx = fx::defaults();  // compresor, delay y reverb (engine/Effects.h)
    // Automatización: 0 apagada (suenan los valores fijos), 1 leer, 2 grabar movimientos.
    int autoMode = 1;
    autom::Lane autoPts[autom::kParamCount];  // volumen (dB) y paneo, por cuadro
    std::vector<float> peaks;      // forma de onda: [bloque][canal][min, max]
};

struct MasterModel {
    float volDb = 0.0f;  // -70 (= -∞) .. +6
    bool mute = false;
    bool limiter = true;
    int notch = 0;       // 0, 50 o 60 Hz
    int exportBits = 24; // 16, 24 o 32 (float)
    int exportRate = 48000;
    int loudTarget = -14;        // LUFS de referencia: −14 (Spotify, YouTube), −16 (Apple) o −23 (TV)
    bool exportNormalize = false;  // al exportar, ajustar el volumen a ese objetivo
};

// La canción preparada para cantar en vivo: al quitarle la voz quedan el instrumental y la voz
// original (guía), más lo que se sabe de la canción (dsp/SongAnalysis).
struct SongModel {
    int sourceId = 0;        // la pista de la que salió (queda en silencio)
    int instrumentalId = 0;  // 0 = no hay canción preparada
    int vocalId = 0;         // la voz original (empieza en silencio); 0 = se quitó
    int key = -1;            // tónica 0..11 (0 = Do), -1 = no se sabe
    bool minor = false;
    float keyScore = 0.0f;   // qué tan clara es la tonalidad (0..1)
    float bpm = 0.0f;
    std::vector<float> beats;                 // segundos
    std::vector<float> vocalStart, vocalEnd;  // segundos: dónde canta la voz original
    std::vector<float> envelope;              // la voz original, 10 por segundo, 0..1
    std::array<float, 12> notes{};            // cuánto usa la voz cada nota (0..1)
    bool ready() const { return instrumentalId != 0; }
};

struct ProjectModel {
    int nextId = 1;
    MasterModel master;
    std::vector<TrackModel> tracks;  // en el orden de la pantalla
    // Entrada en vivo (micrófono o interfaz): su EQ se guarda como el de una pista con id 0.
    TrackModel input;
    float inputGainDb = 0.0f;   // -24 .. +24
    float monitorDb = 0.0f;     // volumen de la escucha: -60 (= -∞) .. +6
    vfx::Values voice = vfx::defaults();  // la voz en vivo (auto-tune y efectos)
    SongModel song;
};
