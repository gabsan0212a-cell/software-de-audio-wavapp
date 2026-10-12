#pragma once

#include <cstdint>
#include <memory>
#include <string>

// Pista ya convertida al formato interno (WAV float 32, 1 o 2 canales) y mapeada en memoria:
// no ocupa RAM de la app; el sistema trae del disco solo lo que se va leyendo.
class MappedAudio {
public:
    // Devuelve nullptr y llena `error` si no se puede abrir.
    static std::unique_ptr<MappedAudio> open(const std::string &path, std::string &error);
    ~MappedAudio();
    MappedAudio(const MappedAudio &) = delete;
    MappedAudio &operator=(const MappedAudio &) = delete;

    const float *data() const { return mData; }  // intercalado
    int channels() const { return mChannels; }
    int sampleRate() const { return mSampleRate; }
    int64_t frames() const { return mFrames; }
    const std::string &path() const { return mPath; }

    // Pide al sistema que vaya leyendo del disco [frame, frame + count).
    void prefetch(int64_t frame, int64_t count) const;

private:
    MappedAudio() = default;
    void *mMap = nullptr;
    size_t mMapBytes = 0;
    const float *mData = nullptr;
    int mChannels = 0;
    int mSampleRate = 0;
    int64_t mFrames = 0;
    std::string mPath;
};
