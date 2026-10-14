#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "engine/Ring.h"
#include "io/WavWriter.h"

// Graba la presentación: lo que sale hacia la consola (la pista con la voz en vivo, después del
// master y su limitador), en un WAV float estéreo. El hilo de salida deja cada bloque en una
// cola; un hilo propio lo pasa al disco (el audio nunca espera al disco).
class ShowRecorder {
public:
    explicit ShowRecorder(int sampleRate) : mRate(sampleRate) {}
    ~ShowRecorder();
    ShowRecorder(const ShowRecorder &) = delete;
    ShowRecorder &operator=(const ShowRecorder &) = delete;

    struct Result {
        std::string path, error;
        int64_t frames = 0;
        int64_t dropped = 0;  // cuadros que no cupieron (el disco no alcanzó)
    };

    // ---------- Hilo de la interfaz
    bool start(const std::string &path, std::string &error);
    Result stop();  // espera a que todo quede en el disco
    bool recording() const { return mRecording.load(std::memory_order_acquire); }
    double seconds() const { return static_cast<double>(mFrames.load()) / mRate; }

    // ---------- Hilo de salida
    void push(const float *l, const float *r, int n);

private:
    void writerLoop();

    const int mRate;
    SpscRing mRing{1 << 20};  // ~10 s estéreo
    std::atomic<bool> mRecording{false}, mWriterRun{false};
    std::atomic<int64_t> mFrames{0}, mDropped{0};
    WavWriter mWriter;
    std::string mPath;
    std::thread mThread;
    float mInter[2 * 256];
};
