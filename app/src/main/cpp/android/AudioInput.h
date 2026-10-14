#pragma once

#include <oboe/Oboe.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

class LiveInput;

// Entrada de audio de Android (Oboe): el micrófono del celular, el de los audífonos o una
// interfaz USB. Entrega lo que entra al núcleo (LiveInput) a 48 kHz; si el aparato usa otra
// frecuencia, Oboe convierte.
//
// Se lee "a la par" de la salida (full duplex): la entrada no tiene hilo propio; en cada ráfaga
// de la salida se toma lo que llegó, se procesa y suena en esa misma ráfaga. Es el menor retraso
// posible para cantar en vivo. Se guarda una reserva de una ráfaga para no quedarse sin muestras
// (más si hace falta) y se tira lo que se junte de más (así el retraso no crece).
class AudioInput : public oboe::AudioStreamErrorCallback {
public:
    static constexpr int kSampleRate = 48000;

    explicit AudioInput(LiveInput &live) : mLive(live) {}
    ~AudioInput() override;

    // deviceId 0 = el que elija el sistema. Devuelve los canales abiertos (1 o 2) o 0 si falló.
    int start(int deviceId, int channels);
    void stop();
    int latencyFrames();  // -1 si no se sabe
    // Se desconectó sola (p. ej. se quitó la interfaz USB). Se lee una vez.
    bool lost() { return mLost.exchange(false); }
    bool open() const { return mOpen.load(); }

    // ---------- Hilo de la salida: lee (sin esperar) lo que llegó y lo pasa al núcleo.
    void pull(int frames);
    // [veces que faltaron muestras, cuadros tirados por retraso de más]
    void stats(int64_t out[2]) const;

    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override;

private:
    bool openStream(int deviceId, int channels, oboe::InputPreset preset);

    static constexpr int kMaxPull = 4096;  // cuadros por lectura
    static constexpr int kMaxCushion = 4;  // ráfagas de reserva como máximo

    LiveInput &mLive;
    std::mutex mMutex;
    std::shared_ptr<oboe::AudioStream> mStream;
    std::atomic<bool> mLost{false}, mOpen{false};
    std::vector<float> mBuf;  // kMaxPull cuadros estéreo
    bool mPriming = true;     // juntando la reserva (hilo de la salida)
    int mCushion = 1;         // ráfagas de reserva (crece si se queda sin muestras)
    std::atomic<int64_t> mUnderruns{0}, mDropped{0};
};
