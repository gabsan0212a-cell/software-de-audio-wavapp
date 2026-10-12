#pragma once

#include <cstdint>

#include "engine/Mixer.h"

// Balística de los medidores, igual que el prototipo: lo que se ve en pantalla.
// - Pistas: caen 45 dB por segundo; master: 36 dB por segundo.
// - Marca de pico del master: se queda 1.2 s y luego baja 15 dB por segundo.
// - Pico máximo y clip (≥ 0.999) del master: se quedan hasta que se borran.
class MeterBallistics {
public:
    // Salida de read(): [2*espacio] L y [2*espacio + 1] R de cada pista (dB), y luego:
    static constexpr int kMasterL = 2 * kMaxTracks, kMasterR = kMasterL + 1;
    static constexpr int kTickL = kMasterL + 2, kTickR = kMasterL + 3;
    static constexpr int kMaxL = kMasterL + 4, kMaxR = kMasterL + 5;
    static constexpr int kClipL = kMasterL + 6, kClipR = kMasterL + 7;  // 0 o 1
    static constexpr int kReduction = kMasterL + 8;                      // dB del limitador
    static constexpr int kMoving = kMasterL + 9;                         // 1 si algo sigue bajando
    static constexpr int kCount = kMasterL + 10;

    MeterBallistics() { reset(); }
    // `raw`: picos lineales de Mixer::readMeters (desde la lectura anterior).
    void update(const float *raw, int64_t nowMs);
    void read(float *out) const;
    bool moving() const;
    void clearClip();  // borra los clips y el pico máximo
    void reset();

private:
    float mTrack[kMaxTracks][2];
    float mL, mR, mTickL, mTickR, mMaxL, mMaxR, mReduction;
    bool mClipL, mClipR;
    int64_t mTickTimeL, mTickTimeR, mLast;
};
