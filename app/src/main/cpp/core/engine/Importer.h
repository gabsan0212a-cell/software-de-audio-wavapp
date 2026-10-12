#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "dsp/Resampler.h"
#include "engine/Decoder.h"
#include "io/MappedAudio.h"
#include "io/WavWriter.h"

struct ImportInfo {
    int srcRate = 0;
    int srcChannels = 0;
    int srcBits = 0;
    bool srcFloat = false;
    int channels = 0;      // canales guardados (1 o 2)
    int64_t frames = 0;    // cuadros guardados (a la frecuencia del proyecto)
    bool dualMono = false; // era estéreo con L = R: se guardó en mono, como en el prototipo
};

// Convierte audio que llega por partes (de un WAV o de un decodificador) al formato interno:
// WAV float 32 a la frecuencia del proyecto, 1 o 2 canales (con más de 2 se usan los dos
// primeros; estéreo con L = R se guarda en mono, como en el prototipo).
class ImportSink : public PcmSink {
public:
    ImportSink(std::string dstPath, int projectRate);
    ~ImportSink() override;  // si no se terminó, borra lo escrito
    std::string begin(int sampleRate, int channels) override;
    std::string write(const float *interleaved, size_t frames) override;
    // Termina y deja el archivo en dstPath. Llena canales, cuadros, dualMono y lo de origen
    // (frecuencia y canales; bits y float los pone quien lee).
    std::string finish(ImportInfo &info);
    void abort();

private:
    std::string fail(const std::string &e);

    std::string mDst, mTmp;
    int mRate;
    int mSrcRate = 0, mSrcCh = 0, mOutCh = 0;
    bool mDual = false, mOpen = false;
    WavWriter mWriter;
    std::unique_ptr<Resampler> mResampler;
    std::vector<float> mSel, mOut;
};

// Convierte un WAV al formato interno. Cierra `src`. `progress` (opcional) va de 0 a 1.
// Devuelve "" o un mensaje de error.
std::string importWav(FILE *src, const std::string &dstPath, int sampleRate, ImportInfo &info,
                      std::atomic<float> *progress = nullptr);

// Pistas de prueba del prototipo (12 s, mono): 0 Bajo, 1 Voz, 2 Guitarra, 3 Teclado.
constexpr int kDemoTracks = 4;
std::vector<float> synthDemo(int index, int sampleRate);
std::string writeDemoTrack(int index, const std::string &dstPath, int sampleRate);

// Picos para dibujar la forma de onda: por bloque de `framesPerBucket` y por canal, min y max.
// Distribución: [bucket][canal][min, max].
std::vector<float> computePeaks(const MappedAudio &audio, int framesPerBucket);
