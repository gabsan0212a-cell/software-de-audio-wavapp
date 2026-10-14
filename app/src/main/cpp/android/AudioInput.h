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
// Normalmente se lee "a la par" de la salida (full duplex): la entrada no tiene hilo propio; en
// cada ráfaga de la salida se vacía lo que llegó y pasa directo al núcleo, se procesa y suena en
// esa misma ráfaga. Es el menor retraso posible para cantar en vivo. La reserva de la escucha
// (DriftFifo, en el núcleo) une los dos relojes (p. ej. un micrófono USB y la salida del celular)
// sin huecos ni saltos. Si en un aparato así no llega nada, se vuelve a abrir con su propio hilo.
class AudioInput : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    static constexpr int kSampleRate = 48000;

    explicit AudioInput(LiveInput &live) : mLive(live) {}
    ~AudioInput() override;

    // deviceId 0 = el que elija el sistema. `pull`: a la par de la salida (si no, con hilo
    // propio). Devuelve los canales abiertos (1 o 2) o 0 si falló.
    int start(int deviceId, int channels, bool pull = true);
    void stop();
    int latencyFrames();  // -1 si no se sabe
    // Se desconectó sola (p. ej. se quitó la interfaz USB). Se lee una vez.
    bool lost() { return mLost.exchange(false); }
    bool open() const { return mOpen.load(); }
    bool pulling() const { return mOpen.load() && mPull.load(); }
    // A la par no llegó nada en 2 s: hay que abrirla con hilo propio (lo hace la pantalla).
    bool stuck() const { return mStuck.load(); }
    int deviceId() const { return mDevice; }
    int channelsWanted() const { return mChannelsWanted; }

    // ---------- Hilo de la salida: vacía lo que llegó y pasa al núcleo `frames` cuadros.
    void pull(int frames);
    // [modo (0 cerrada, 1 a la par, 2 con hilo propio), segundos leídos, veces que faltó audio en
    //  la escucha, saltos (sobraba mucho), margen de la reserva (cuadros), ráfaga, capacidad]
    static constexpr int kInfoCount = 7;
    void info(int32_t *out);

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override;

private:
    bool openStream(int deviceId, int channels, oboe::InputPreset preset, bool pull);

    static constexpr int kCapacity = 4096;    // lo que se le pide a la entrada (85 ms)
    static constexpr int kReadFrames = 2048;  // de a cuánto se lee

    LiveInput &mLive;
    std::mutex mMutex;
    std::shared_ptr<oboe::AudioStream> mStream;
    std::atomic<bool> mLost{false}, mOpen{false}, mPull{true}, mStuck{false};
    int mDevice = 0, mChannelsWanted = 1;

    // Hilo de la salida (modo a la par)
    std::vector<float> mTmp;
    int mChannels = 1;
    int64_t mOutFrames = 0;  // cuadros que pidió la salida desde que se abrió

    std::atomic<int64_t> mPulled{0};
};
