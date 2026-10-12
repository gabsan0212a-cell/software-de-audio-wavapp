#pragma once

#include <cstdint>
#include <vector>

#include "dsp/Analyzer.h"

class Mixer;

// Lo que muestra el panel de análisis, ya calculado (la pantalla solo lo dibuja), con las
// mismas reglas que el prototipo:
// - analiza ~30 veces por segundo mientras suena; sin sonar, todo vuelve a reposo;
// - correlación suavizada (0.85), paneo por nivel RMS, veredicto de fase y desbalance;
// - el zumbido se actualiza 4 veces por segundo;
// - goniómetro "por fuentes" (rayos) o clásico (nube M/S con ganancia automática);
// - espectro reducido a columnas para dibujar.
class AnalysisView {
public:
    enum Verdict { kNone = 0, kOutOfPhase, kVeryWide, kStereo, kMono };

    // Valores de stats(): activo, fuerte, correlación, paneo, veredicto, desbalance (-1 L, 0, 1 R),
    // malo (0/1), zumbido Hz, armónicos, dB sobre el piso, ganancia automática del goniómetro (dB).
    static constexpr int kStatCount = 11;
    // Goniómetro: clásico = puntos (x, y) en -1..1 (1 = 45 % del ancho o alto desde el centro);
    // por fuentes = rayos (ángulo en radianes, largo 0..1, opacidad 0..1).
    static constexpr int kMaxGonio = 2 * Analyzer::kTime;
    static constexpr float kMaxAngle = 1.30899694f;  // 75°

    // Devuelve true si cambió lo que hay que dibujar.
    bool update(Analyzer &a, const Mixer &m, bool playing, int64_t nowMs, bool classic);
    void reset(Analyzer &a, const Mixer &m);

    void stats(float *out) const;
    // Copia el goniómetro a `out` (hasta `max` valores) y devuelve cuántos puntos o rayos hay.
    int goniometer(float *out, int max) const;
    // Espectro de 20 Hz a 20 kHz en `cols` columnas logarítmicas: el máximo (dB) de cada una.
    static void spectrumColumns(const Analyzer &a, float *out, int cols);

    bool active() const { return mActive; }
    float correlation() const { return mCorrelation; }
    float pan() const { return mPan; }
    bool loud() const { return mLoud; }
    Verdict verdict() const;
    int imbalance() const;
    bool bad() const;
    const HumInfo &hum() const { return mHum; }

private:
    void classicPoints(const Analyzer &a);
    void sourceRays(const Analyzer &a);

    bool mActive = false, mLoud = false, mClassic = false;
    float mCorrelation = 1.0f, mPan = 0.0f;
    HumInfo mHum;
    int64_t mLastMs = -1, mLastHumMs = -1;
    float mPeakVector = 0.05f, mPeakSources = -60.0f;
    std::vector<float> mGonio;
    int mGonioCount = 0;
};
