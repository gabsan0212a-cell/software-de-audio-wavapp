#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <vector>

class MappedAudio;

// Lo que se sabe de una canción para cantarla: tonalidad, tempo, pulsos y dónde canta la voz
// original. Sale del instrumental (acordes, golpes) y de la voz separada (sus notas).
struct SongInfo {
    int key = -1;          // tónica 0..11 (0 = Do), -1 = no se sabe
    bool minor = false;
    float keyScore = 0.0f; // qué tan clara es la tonalidad (correlación, 0..1)
    float bpm = 0.0f;      // 0 = no se sabe
    std::vector<float> beats;  // segundos
    std::vector<float> vocalStart, vocalEnd;  // segundos: partes donde canta la voz original
    std::vector<float> envelope;              // la voz original, 10 por segundo, 0..1
    std::array<float, 12> notes{};   // cuánto usa la voz cada nota (0..1; 0 = Do)
    std::array<float, 12> chroma{};  // el instrumental (0..1)
};

namespace song {

constexpr float kEnvelopeRate = 10.0f;  // valores por segundo de `envelope`

// Tonalidad que mejor encaja con un perfil de 12 notas (perfiles de Krumhansl-Kessler).
void keyFromProfile(const std::array<float, 12> &profile, int &key, bool &minor, float &score);
// Las 7 notas de la escala (0..11) de una tonalidad mayor o menor natural.
std::array<int, 7> scaleNotes(int key, bool minor);

// Fuerza de ataques (cambios de energía por bandas) de una señal mono, `fps` valores por segundo.
std::vector<float> onsetEnvelope(const float *mono, size_t n, int sampleRate, float &fps);
// Tempo (BPM) a partir de la fuerza de ataques (autocorrelación con preferencia por ~120).
float tempo(const std::vector<float> &onset, float fps);
// Pulsos (segundos) con programación dinámica (Ellis 2007), dado el tempo.
std::vector<float> beats(const std::vector<float> &onset, float fps, float bpm);

}  // namespace song

// Analiza la canción. Cualquiera de las dos puede faltar (nullptr). `progress` va de 0 a 1.
// Devuelve false si se canceló.
bool analyzeSong(const MappedAudio *instrumental, const MappedAudio *vocal, SongInfo &out,
                 const std::atomic<bool> *cancel = nullptr, const std::function<void(float)> &progress = nullptr);
