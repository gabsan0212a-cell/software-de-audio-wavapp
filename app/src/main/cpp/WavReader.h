#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Lector de WAV por partes: PCM entero (8/16/24/32 bits, también dentro de contenedores
// más grandes) y float (32/64). Tolera los errores de encabezado que dejan algunas
// grabadoras (blockAlign mal escrito o en 0, tamaño de "data" en 0).
class WavReader {
public:
    WavReader() = default;
    WavReader(const WavReader &) = delete;
    WavReader &operator=(const WavReader &) = delete;
    ~WavReader();

    // Toma posesión de f (lo cierra al terminar). Devuelve "" o un mensaje de error.
    std::string open(FILE *f);
    // Lee hasta maxFrames cuadros intercalados (todos los canales) como float -1..1.
    // Devuelve los cuadros leídos; 0 al final.
    size_t read(float *dst, size_t maxFrames);

    int channels() const { return mChannels; }
    int sampleRate() const { return mSampleRate; }
    int bits() const { return mBits; }  // bits útiles (p. ej. 24 aunque vengan en 32)
    bool isFloat() const { return mIsFloat; }
    int64_t frames() const { return mFrames; }
    int64_t dataOffset() const { return mDataOffset; }

private:
    FILE *mFile = nullptr;
    int mChannels = 0;
    int mSampleRate = 0;
    int mBits = 0;
    bool mIsFloat = false;
    int mContainer = 0;  // bytes por muestra en el archivo
    int mBlockAlign = 0;
    int64_t mFrames = 0;
    int64_t mFramesLeft = 0;
    int64_t mDataOffset = 0;
    std::vector<uint8_t> mBuf;
};

// Audio completo en memoria (para pruebas y archivos cortos).
struct Clip {
    std::vector<float> data;
    int32_t channels = 0;
    int32_t sampleRate = 0;
    int32_t bits = 0;
    bool isFloat = false;
    int64_t frames = 0;
};

// Lee todo el archivo. Cierra el FILE*. Devuelve "" o un mensaje de error.
std::string readWav(FILE *f, Clip &out);
