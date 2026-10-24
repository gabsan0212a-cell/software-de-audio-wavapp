#pragma once

#include <array>
#include <cstdint>
#include <memory>

// Instrumentos para crear música: batería, 808, bajo, guitarra, sintetizador y piano. Todos se
// sintetizan aquí (no hay muestras grabadas): cada uno tiene presets (un sonido de partida) y
// unos pocos ajustes en común (brillo, ataque, duración, distorsión, deslizamiento, afinación,
// movimiento y ambiente). Movimiento (chorus, trémolo o vibrato) y ambiente (reverb) van dentro
// del instrumento: así suena igual al tocarlo en vivo que en la pista.
// El mismo motor (Engine) sirve para tocar en vivo (sin reservar memoria en el hilo de audio) y
// para renderizar un patrón fuera de tiempo real (inst/Pattern.h).
namespace inst {

enum Kind : int { kDrums = 0, k808, kBass, kGuitar, kSynth, kPiano, kKindCount };

enum Param : int { kTone = 0, kAttack, kDecay, kDrive, kGlide, kTune, kMod, kSpace, kParamCount };

struct ParamInfo {
    const char *key;  // nombre en la sesión
    float min, max, def;
    double step;
    bool log;
};

using Values = std::array<float, kParamCount>;

const ParamInfo &paramInfo(int p);
// Qué ajustes usa cada instrumento (los demás no cambian nada).
bool paramApplies(int kind, int p);
Values defaultValues();
float clampParam(int p, float v);

const char *kindName(int kind);  // "Batería", "808", ...
int presetCount(int kind);
const char *presetName(int kind, int preset);
// El preset con ese nombre (o 0).
int presetIndex(int kind, const char *name);

// La batería tiene 8 piezas (las filas del secuenciador); su "nota" es el número de pieza.
constexpr int kDrumPieces = 8;
const char *drumName(int piece);
// Notas (MIDI) que alcanza cada instrumento melódico y por dónde suele tocarse.
void noteRange(int kind, int &lo, int &hi);
int homePitch(int kind);

struct Sound {
    int kind = kSynth;
    int preset = 0;
    Values values = defaultValues();
    bool operator==(const Sound &o) const { return kind == o.kind && preset == o.preset && values == o.values; }
    bool operator!=(const Sound &o) const { return !(*this == o); }
};
Sound clampSound(Sound s);
// Toca una nota a la vez (con deslizamiento entre notas encimadas): 808, bajos y algunos leads.
bool isMono(const Sound &s);

// El motor: hasta kVoices notas a la vez. Un solo hilo lo usa (el de audio o el que renderiza).
class Engine {
public:
    static constexpr int kVoices = 16;

    explicit Engine(int sampleRate);  // reserva toda la memoria
    ~Engine();
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    // Cambia el sonido sin reservar memoria. Si cambia el instrumento o el preset, lo que sonaba
    // se corta.
    void setSound(const Sound &s);
    const Sound &sound() const { return mSound; }
    // `seed` hace que el ruido de cada golpe sea siempre el mismo (el render es repetible).
    void noteOn(int pitch, float velocity, uint32_t seed);
    void noteOff(int pitch);
    void releaseAll();  // suelta todo (cada nota con su liberación)
    void silence();     // corta todo de inmediato
    // Escribe `n` cuadros estéreo (no suma).
    void render(float *l, float *r, int n);
    bool active() const;  // suena alguna nota o la cola de la reverb

    struct Impl;

private:
    Sound mSound;
    std::unique_ptr<Impl> mImpl;
};

}  // namespace inst
