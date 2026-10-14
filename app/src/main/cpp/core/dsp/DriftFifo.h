#pragma once

#include <cstdint>
#include <vector>

// Une dos relojes de audio sin cortes: junta lo que llega de la entrada (a su ritmo, en ráfagas
// y con su propio reloj, p. ej. un micrófono USB) y entrega exactamente lo que pide la salida.
// Para que la reserva no crezca ni se vacíe, lee un poquito más rápido o más lento (hasta ±0.3 %,
// unos 5 cents: no se oye) según lo que quedó guardado en el peor momento de los últimos 100 ms;
// así el retraso se queda en lo mínimo que aguantan las ráfagas. Interpola con Catmull-Rom.
// Si de todos modos falta audio (la entrada se trabó), baja a silencio con un fundido corto y
// vuelve a juntar; si sobra mucho (la app se trabó), salta con un fundido cruzado. Nunca
// chasquea. Un solo hilo (el de la salida).
class DriftFifo {
public:
    DriftFifo(int channels, int capacityFrames);

    void reset();
    // Lo que llegó: `frames` cuadros intercalados. Si no cabe, se tira lo más viejo.
    void push(const float *x, int frames);
    // Entrega `frames` cuadros intercalados. Devuelve cuántos tienen audio (el resto es silencio).
    int pull(float *out, int frames);

    // Cuánto se guarda de reserva, como mínimo, por encima de lo que se va a leer (cuadros). Si
    // llega a faltar audio, crece solo (hasta kMaxSafety): aprende lo que aguanta este aparato.
    void setSafety(int frames) { mSafety = frames; }
    int safety() const { return mSafety; }
    static constexpr int kMaxSafety = 512;
    int level() const { return static_cast<int>(static_cast<double>(mFill) - mPos); }
    double ratio() const { return mRatio; }
    int64_t underruns() const { return mUnderruns; }
    int64_t skips() const { return mSkips; }

    static constexpr double kMaxAdjust = 0.003;  // ±0.3 % de velocidad como máximo

private:
    float sample(int64_t frame, int c) const { return mBuf[static_cast<size_t>(frame) * mCh + c]; }
    void interp(double pos, float *dst) const;
    void compact();

    int mCh, mCap;
    std::vector<float> mBuf;
    int64_t mFill = 0;     // cuadros válidos desde el principio de mBuf
    double mPos = 1.0;     // lectura (cuadros, con decimales); siempre deja uno atrás
    double mRatio = 1.0, mRatioTarget = 1.0;
    bool mRunning = false; // false = juntando reserva (sale silencio)
    float mGain = 0.0f;    // fundido de entrada/salida
    // Salto con fundido cruzado
    double mOldPos = 0.0;
    int mXfade = 0;
    // Control
    int mSafety = 64;
    int mMaxPush = 0;      // la ráfaga más grande que ha llegado (para juntar al arrancar)
    int64_t mWindow = 0;   // cuadros entregados en esta ventana
    double mMinLevel = 1e18;
    int64_t mUnderruns = 0, mSkips = 0;
};
