#pragma once

#include <atomic>

#include "dsp/PitchShifter.h"
#include "dsp/PitchTracker.h"

// Escalas del auto-tune.
enum TuneScale : int { kScaleMajor = 0, kScaleMinor, kScaleChromatic, kScaleCount };

struct TuneSettings {
    int key = 9;                 // tónica 0..11 (0 = Do)
    int scale = kScaleMinor;
    float amount = 1.0f;         // cuánto corrige (0..1)
    float speedMs = 20.0f;       // tiempo para llegar a la nota (0 = al instante: efecto robot)
    bool correct = true;         // false = solo mide (afinador): la corrección queda en cero
};

// Auto-tune en vivo: sigue el tono de la voz, elige la nota de la escala más cercana (con un
// poco de histéresis para no saltar entre dos notas) y corrige con el cambiador de tono.
// La corrección llega a la nota con la velocidad elegida: lenta suena natural (deja pasar el
// vibrato), al instante suena "robot" (como en el trap). Cada estimación del tono describe unos
// milisegundos atrás: con la pendiente de las últimas se predice el tono del audio que sale en
// ese momento (así el vibrato y los deslices quedan bien corregidos, no con retraso).
class AutoTune {
public:
    explicit AutoTune(int sampleRate);
    // Rango de la voz (no reserva memoria: se puede cambiar en el hilo de audio).
    void setRange(float minHz, float maxHz) { mTracker.setRange(minHz, maxHz); }
    void reset();

    // ---------- Hilo de audio: mono, en el lugar. `quiet` = la compuerta está cerrada.
    void process(float *x, int n, const TuneSettings &s, bool quiet);
    float latencySamples() const { return mLatency.load(std::memory_order_relaxed); }  // cualquier hilo

    // ---------- Lo que muestra la pantalla (cualquier hilo).
    struct Info {
        float inputMidi = 0.0f;   // la nota que se canta (con decimales); 0 = no hay nota
        int note = -1;            // la nota de la escala a la que va
        float outputMidi = 0.0f;  // como sale
    };
    Info info() const;

    // La nota de la escala más cercana a `midi` (o `current` si está casi igual de cerca).
    static int nearestNote(float midi, int key, int scale, int current);
    static bool inScale(int note, int key, int scale);

private:
    static constexpr int kSub = 32;  // la corrección se recalcula cada 32 muestras
    static constexpr int kHist = 4;  // estimaciones para la pendiente

    float predicted(double when, float fallback) const;

    int mRate;
    PitchTracker mTracker;
    PitchShifter mShifter;
    int mNote = -1;
    float mCorr = 0.0f;      // corrección aplicada (semitonos)
    double mClock = 0.0;     // muestras procesadas
    double mHistT[kHist] = {};
    float mHistM[kHist] = {};
    int mHistN = 0;
    float mTmp[kSub];
    std::atomic<float> mInMidi{0.0f}, mOutMidi{0.0f}, mLatency{0.0f};
    std::atomic<int> mInfoNote{-1};
};
