#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "inst/Instruments.h"

// Lo que toca un instrumento: un patrón de notas (en pulsos, así sigue al tempo) que se repite.
// Se renderiza fuera de tiempo real a una pista de audio común (con todo lo que tienen las
// pistas: volumen, EQ, efectos, edición, exportar).
namespace inst {

constexpr int kPpq = 96;                  // divisiones por negra
constexpr int kStepTicks = kPpq / 4;      // un paso de la cuadrícula: semicorchea (1/16)
constexpr int kStepsPerBar = 16;          // 4/4
constexpr int kBarTicks = kPpq * 4;
constexpr int kMaxBars = 16;
constexpr int kMaxRepeats = 64;
constexpr int kMaxNotes = 4096;
constexpr double kMaxTailSeconds = 8.0;   // lo que puede seguir sonando después del patrón

struct Note {
    int tick = 0;
    int len = kStepTicks;
    int pitch = 60;  // en la batería, la pieza (0..7)
    int vel = 100;   // 1..127
    bool operator==(const Note &o) const { return tick == o.tick && len == o.len && pitch == o.pitch && vel == o.vel; }
    bool operator!=(const Note &o) const { return !(*this == o); }
};

struct Pattern {
    int bars = 4;
    int repeats = 4;     // cuántas veces suena seguido
    float swing = 0.0f;  // 0..1: corre las semicorcheas de contratiempo (1 = medio paso)
    std::vector<Note> notes;  // ordenadas por pulso y nota
    bool operator==(const Pattern &o) const {
        return bars == o.bars && repeats == o.repeats && swing == o.swing && notes == o.notes;
    }
    bool operator!=(const Pattern &o) const { return !(*this == o); }
};

// Un instrumento de una pista: su sonido y lo que toca.
struct Part {
    Sound sound;
    Pattern pattern;
};

// Ordena, ajusta a los rangos y quita repetidas (misma nota en el mismo pulso).
void normalize(Pattern &p, int kind);
int patternTicks(const Pattern &p);
double framesPerTick(double bpm, int sampleRate);
// Cuadros que dura una vuelta del patrón.
double patternFrames(const Pattern &p, double bpm, int sampleRate);
// La nota que suena en `tick` con esa altura (o pieza), o -1.
int findNote(const Pattern &p, int tick, int pitch);

// Renderiza las repeticiones del patrón (estéreo intercalado) más la cola de lo último que suena.
// Una vuelta se calcula una vez y se repite sumando su cola sobre la siguiente (idéntico a tocarlo
// seguido). Vacío si se canceló.
std::vector<float> render(const Part &part, double bpm, int sampleRate, const std::atomic<bool> *cancel = nullptr);

// Huella de lo que se renderiza (sonido, patrón y tempo): si no cambió, el archivo sigue sirviendo.
uint64_t renderHash(const Part &part, double bpm);

// ---------- Ayudas para escribir música
bool inScale(int pitch, int key, bool minor);
// Tríada de la escala sobre `root` (o mayor si `root` no está en la escala).
std::vector<int> chord(int root, int key, bool minor);
// Filas de la cuadrícula, de arriba a abajo: piezas de la batería o notas (solo las de la
// escala o todas).
std::vector<int> rows(int kind, int key, bool minor, bool scaleOnly);
std::string noteName(int midi);  // "Do4", "Fa♯3"
std::string keyName(int key, bool minor);  // "La menor"
// Una idea para empezar: un patrón que suena bien con ese instrumento y esa tonalidad. `variant`
// da otras ideas.
Pattern starter(const Sound &s, int key, bool minor, int bars, int variant);

// ---------- Estilos: varios instrumentos que suenan bien juntos, con su tempo y tonalidad (se
// agregan con un toque y después se cambia todo lo que se quiera).
struct StylePart {
    int kind;
    const char *preset;
    float volDb;
};

struct Style {
    const char *name;
    const char *detail;  // qué trae
    int bpm;
    int key;
    bool minor;
    int variant;  // la misma idea para todos: los acordes coinciden
    float swing;  // el mismo en todos
    std::vector<StylePart> parts;
};

int styleCount();
const Style &style(int index);

}  // namespace inst
