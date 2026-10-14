#pragma once

#include <cstdint>
#include <vector>

// Cambia el tono de una voz sin cambiar su duración, con muy poco retraso (pensado para el
// auto-tune: correcciones de hasta un par de semitonos). Lee una línea de retardo a otra
// velocidad, como una cinta; cuando la lectura se acerca demasiado a lo que entra (sube el
// tono) o se aleja (baja), salta exactamente un período de la voz hacia atrás o adelante con
// un fundido. Como la voz se repite en cada período, el salto no se oye. El largo exacto del
// salto se ajusta comparando la forma de onda. En silencio recupera el retraso mínimo.
class PitchShifter {
public:
    explicit PitchShifter(int sampleRate);
    void reset();

    // ---------- Hilo de audio
    // `ratio`: frecuencia de salida / de entrada (dentro del bloque va de la anterior a esta).
    // `period`: período de la voz en muestras (0 = no hay nota). `quiet`: es silencio.
    void process(const float *in, float *out, int n, float ratio, float period, bool quiet);
    // Retraso de la salida (muestras): la próxima muestra que sale es la que entró hace esto.
    float delay() const { return static_cast<float>(static_cast<double>(mWrite) - mRead); }

    static constexpr int kMinDelay = 48;  // ~1 ms: antes de esto salta hacia atrás

private:
    float at(double pos) const;
    float sample(int64_t i) const { return mBuf[static_cast<size_t>(i & mMask)]; }
    double bestJump(double jump, bool back) const;
    void startSplice(double jump, bool back, bool voiced, float ratio);

    int mRate;
    std::vector<float> mBuf;
    int64_t mMask;
    int64_t mWrite = 0;   // cuántas muestras se han escrito
    double mRead = 0.0;   // posición de lectura (absoluta, con decimales)
    float mRatio = 1.0f;  // la del bloque anterior
    // Salto en curso: se lee de las dos posiciones y se pasa de una a otra.
    double mRead2 = 0.0;
    int mFade = 0, mFadeLen = 0;
    bool mFadePower = false;  // fundido de igual potencia (para lo que no es nota)
};
