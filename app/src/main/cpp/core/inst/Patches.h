#pragma once

// Los presets por dentro (solo los usan Instruments.cpp y Engine.cpp).
namespace inst {

// ---------- Batería: cada pieza se arma con un tono (con caída de afinación) y ruido filtrado.
enum DrumType : int { dKick = 0, dSnare, dClap, dHat, dTom, dCymbal, dRim, dShaker };

struct DrumPatch {
    int type;
    float freq;          // Hz: tono base (en hi-hats y platillos, el más grave del "metal")
    float sweep;         // semitonos que cae la afinación al inicio
    float sweepMs;
    float decayMs;       // el tono
    float noise;         // cantidad de ruido (0..1)
    float noiseDecayMs;
    float noiseHz;       // centro del ruido
    float click;         // golpe inicial (0..1)
    float drive;         // saturación (0..1)
    float gain;
    float pan;           // -1 .. 1
};

// ---------- Efectos dentro del instrumento: movimiento y ambiente (los ajustes "Movimiento" y
// "Ambiente" los suben o los bajan).
enum ModType : int { modNone = 0, modChorus, modTremolo, modVibrato };

struct FxPatch {
    int mod;         // el tipo de movimiento (sin tipo, "Movimiento" agrega chorus)
    float modDepth;  // 0..1
    float modHz;     // 0 = la velocidad de cada tipo
    float space;     // 0..1: cuánta reverb
    float spaceS;    // T60 de la reverb (0 = 1.8 s)
};

struct Kit {
    const char *name;
    DrumPatch piece[8];
    float lowpassHz;  // 0 = sin filtro (lo-fi: más oscuro)
    FxPatch fx;       // la sala
};

// ---------- 808: un seno con golpe de afinación, larga caída y saturación.
struct BoomPatch {
    const char *name;
    float punchSemis, punchMs;  // el golpe al inicio
    float decayS;               // T60
    float drive;                // 0..1
    float click;
    float toneHz;               // filtro final
    float glideMs;              // deslizamiento entre notas encimadas (0 = vuelve a golpear)
    float harm;                 // segundo armónico (se oye en bocinas chicas)
    float gain;
};

// ---------- Cuerda pulsada (Karplus-Strong): guitarras y bajo eléctrico.
struct StringPatch {
    float t60;       // s que suena una nota de 110 Hz (las graves suenan más)
    float bright;    // 0..1 (el filtro de la cuerda)
    float pickPos;   // dónde se pulsa (0..0.5 del largo)
    float pickTone;  // 0..1 brillo del ataque
    float muteT60;   // s al soltar la nota
    float strumMs;   // rasgueo: separación entre cuerdas de un acorde
    float body;      // resonancia de la caja (acústica)
    float drive;     // 0..1 distorsión del amplificador
    float cabHz;     // filtro del amplificador (0 = sin)
    float gain;
};

// ---------- Sintetizador: osciladores + filtro (resta), FM de dos operadores u órgano.
enum SynthMode : int { mSub = 0, mFm, mOrgan };
enum Wave : int { wSaw = 0, wSquare, wTri, wSine, wPulse };

struct SynthPatch {
    int mode;
    int wave1, wave2;
    float mix2;       // nivel del oscilador 2
    float semi2;      // semitonos del oscilador 2
    float detune;     // cents entre las copias del oscilador 1 (unísono)
    int unison;       // 1..5 copias
    float spread;     // estéreo de las copias (0..1)
    float sub;        // seno una octava abajo
    float noise;
    float cutoff;     // Hz
    float reso;       // 0..1
    float envAmt;     // octavas que abre el filtro su envolvente
    float a, d, s, r;      // volumen (s, nivel)
    float fa, fd, fs, fr;  // filtro
    float fmRatio, fmIndex, fmDecay, tine;  // FM (piano eléctrico, campanas)
    float vibrato;    // cents
    float drive;
    float gain;
    bool mono;
    float glideMs;
};

// ---------- Piano: cada nota es una suma de parciales (un poco desafinados hacia arriba, como
// las cuerdas de verdad) que se apagan solos, los agudos antes; una segunda cuerda desafinada
// apenas da el batimiento y la cola larga; el martillo suma su golpe.
struct PianoPatch {
    float t60;        // s que suena un Do4 (los graves más, los agudos menos)
    float bright;     // 0..1: cuántos armónicos saca el martillo
    float strike;     // dónde golpea el martillo (fracción de la cuerda)
    float detune;     // cents entre las cuerdas de una nota
    float unison;     // 0..1: nivel de la segunda cuerda
    float thump;      // 0..1: el golpe del martillo y la mecánica
    float releaseS;   // al soltar (el apagador)
    float inharm;     // qué tan estiradas (metálicas) suenan
    float lowpassHz;  // 0 = sin filtro
    float gain;
};

// Un preset de un instrumento melódico: qué motor usa y sus datos.
enum Voicing : int { vSynth = 0, vString, vBoom, vPiano };
struct MelodicPreset {
    const char *name;
    int voicing;
    SynthPatch synth;
    StringPatch str;
    BoomPatch boom;
    PianoPatch piano;
    FxPatch fx;
};

const Kit &kit(int preset);
int kitCount();
const MelodicPreset &melodic(int kind, int preset);
int melodicCount(int kind);

}  // namespace inst
