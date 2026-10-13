#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dsp/Equalizer.h"
#include "io/WavWriter.h"

class Mixer;

// Cola sin bloqueos para un hilo que escribe y otro que lee (muestras float).
class SpscRing {
public:
    explicit SpscRing(size_t capacityPow2);
    size_t write(const float *v, size_t n);  // escribe lo que quepa; devuelve cuántas
    size_t read(float *v, size_t n);
    size_t available() const;
    size_t space() const { return mBuf.size() - available(); }  // solo quien escribe
    void skip(size_t n);  // solo quien lee

private:
    std::vector<float> mBuf;
    size_t mMask;
    std::atomic<uint64_t> mWrite{0}, mRead{0};
};

// Lo que dejó una grabación al terminar.
struct Take {
    std::string path;
    int channels = 0;
    int64_t frames = 0;
    int64_t start = 0;     // posición de la mezcla cuando llegó el primer cuadro (sin compensar)
    int64_t dropped = 0;   // cuadros que no cupieron (el disco no alcanzó)
    std::string error;
};

// La entrada en vivo (micrófono del celular o interfaz USB), a la frecuencia del proyecto:
//   entrada → ganancia → (así se graba) → EQ → escucha en los audífonos y análisis.
// La grabación queda sin EQ (la pista nueva recibe el EQ que se escuchó, y se puede cambiar).
// La plataforma llama a push() desde su hilo de entrada; el mezclador suma la escucha en su
// hilo de salida (mixMonitor). Un hilo propio pasa la grabación al disco.
class LiveInput {
public:
    static constexpr float kGainMin = -24.0f, kGainMax = 24.0f;

    LiveInput(int sampleRate, Mixer &mixer);
    ~LiveInput();
    LiveInput(const LiveInput &) = delete;
    LiveInput &operator=(const LiveInput &) = delete;

    // ---------- Hilo de la interfaz
    void setGainDb(float db);
    void setMonitor(bool on) { mMonitor.store(on); }
    void setMonitorGain(float gain) { mMonitorGain.store(gain); }  // lineal
    bool monitorOn() const { return mMonitor.load(); }
    // Se está escuchando la entrada (encendida y abierta).
    bool monitoring() const { return mMonitor.load(std::memory_order_relaxed) && mActive.load(std::memory_order_relaxed); }
    Equalizer &eq() { return mEq; }
    const Equalizer &eq() const { return mEq; }
    // La plataforma abrió (1 o 2 canales) o cerró la entrada.
    void setActive(bool on, int channels);
    bool active() const { return mActive.load(); }
    int channels() const { return mChannels.load(); }
    // Picos (lineales) desde la lectura anterior, después de la ganancia. Mono: L = R.
    void readPeaks(float *lr);

    // Graba en `path` (WAV float a la frecuencia del proyecto, con los canales de la entrada).
    bool startRecording(const std::string &path, std::string &error);
    bool recording() const { return mRecording.load(); }
    Take stopRecording();  // espera a que todo quede en el disco
    double recordedSeconds() const;
    // Lo último de la toma (`seconds`) en `cols` columnas de 0 a 1 (para dibujarla mientras se graba).
    std::vector<float> recentWave(int cols, double seconds) const;

    // ---------- Hilo de entrada
    void push(const float *in, int frames, int channels);

    // ---------- Hilo de salida (lo llama el mezclador)
    void mixMonitor(float *l, float *r, int n);

private:
    static constexpr int kBlock = 256;
    static constexpr int kWaveStep = 480;  // un valor de la onda en vivo cada 10 ms

    void writerLoop();

    const int mRate;
    Mixer &mMixer;
    Equalizer mEq;

    std::atomic<bool> mActive{false};
    std::atomic<int> mChannels{1};
    std::atomic<float> mGainTarget{1.0f};
    std::atomic<bool> mMonitor{false};
    std::atomic<float> mMonitorGain{1.0f};
    std::atomic<float> mPeakL{0.0f}, mPeakR{0.0f};
    std::atomic<int> mLastPush{256};

    // Escucha: de la entrada a la salida (estéreo intercalado).
    SpscRing mMonitorRing{1 << 14};
    bool mWasMonitoring = false;  // hilo de salida
    float mMonGainS = 0.0f;       // hilo de salida

    // Grabación
    SpscRing mRecRing{1 << 20};
    std::atomic<bool> mRecording{false};
    std::atomic<bool> mRecStarted{false};
    std::atomic<int64_t> mRecStart{0};
    std::atomic<int64_t> mRecFrames{0};   // ya escritos en el disco
    std::atomic<int64_t> mRecDropped{0};
    std::atomic<bool> mWriterRun{false};
    int mRecChannels = 1;
    WavWriter mWriter;
    std::string mRecPath;
    std::thread mWriterThread;
    mutable std::mutex mWaveMutex;
    std::vector<float> mWave;  // un pico cada kWaveStep cuadros

    // Estado del hilo de entrada
    float mGainS = 1.0f;
    float mL[kBlock], mR[kBlock], mInter[2 * kBlock];
};
