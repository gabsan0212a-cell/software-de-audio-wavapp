#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "inst/Instruments.h"

// Para tocar en vivo (pads y teclado): lo que se toca suena al instante por la salida, antes del
// master. La pantalla manda las notas desde su hilo; el hilo de audio las toma de una cola sin
// esperar y las toca con su propio motor (que no reserva memoria).
namespace inst {

class Live {
public:
    explicit Live(int sampleRate);

    // ---------- Cualquier hilo menos el de audio
    // `gain`: el volumen de la pista (lineal).
    void play(const Sound &s, int pitch, float velocity, float gain);
    void release(int pitch);
    void releaseAll();
    void stop();  // corta todo
    // Está sonando algo (o hay notas en camino): la salida no se duerme.
    bool busy() const;

    // ---------- Hilo de audio: suma lo que suena a l/r. false si no había nada.
    bool mix(float *l, float *r, int n);

private:
    enum Type : int { kOn = 0, kOff, kOffAll, kStop };
    struct Event {
        int type = kOn;
        int pitch = 0;
        float vel = 0, gain = 1;
        uint32_t seed = 1;
        Sound sound;
    };
    void push(const Event &e);

    static constexpr int kQueue = 128;
    static constexpr int kBlock = 256;
    Event mQueue[kQueue];
    std::atomic<uint32_t> mWrite{0}, mRead{0};
    std::mutex mPush;  // varios hilos pueden mandar notas; el de audio nunca espera
    uint32_t mSeed = 0x2545F491u;

    // Hilo de audio
    Engine mEngine;
    float mGain = 1.0f, mGainS = 1.0f;
    float mL[kBlock], mR[kBlock];
    std::atomic<bool> mActive{false};
};

}  // namespace inst
