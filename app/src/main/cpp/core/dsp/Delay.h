#pragma once

#include <vector>

// Línea de retardo con lectura fraccionaria. Lo escrito antes de restart() se lee como
// silencio (así al arrancar no suenan ecos viejos, sin tener que borrar la memoria).
class DelayLine {
public:
    void allocate(int maxDelay);  // hilo de la interfaz, antes de usarla
    void restart() { mValid = 0; }
    int maxDelay() const { return mSize - 4; }
    // Lo escrito hace `d` muestras (d ≥ 1).
    inline float read(int d) const { return d <= mValid ? mBuf[static_cast<size_t>((mPos - d) & mMask)] : 0.0f; }
    inline float readFrac(float d) const {
        const int i = static_cast<int>(d);
        const float f = d - static_cast<float>(i);
        const float a = read(i);
        return a + f * (read(i + 1) - a);
    }
    inline void write(float x) {
        mBuf[static_cast<size_t>(mPos)] = x;
        mPos = (mPos + 1) & mMask;
        if (mValid < mSize) ++mValid;
    }

private:
    std::vector<float> mBuf;
    int mSize = 0, mMask = 0, mPos = 0, mValid = 0;
};

struct DelaySettings {
    float timeMs = 350.0f;
    float feedback = 0.3f;    // 0 .. 0.9
    float toneHz = 5000.0f;   // brillo de los ecos
    bool pingPong = false;    // los ecos van de un lado al otro
};

// Eco estéreo: cada repetición pasa por un pasa-bajos (brillo) y un pasa-altos suave (que los
// graves no se acumulen). Cambiar el tiempo suena como una cinta (sin clics).
class StereoDelay {
public:
    void allocate(int sampleRate, float maxMs);  // hilo de la interfaz
    void restart();
    // Escribe solo el eco (sin la señal original) en outL/outR. `r` puede ser nullptr (mono).
    void process(const float *l, const float *r, int n, float *outL, float *outR, const DelaySettings &s);

private:
    int mRate = 48000;
    DelayLine mLine[2];
    float mDelay = -1.0f;  // retraso suavizado, en muestras
    float mLp[2] = {0.0f, 0.0f}, mLow[2] = {0.0f, 0.0f};
};
