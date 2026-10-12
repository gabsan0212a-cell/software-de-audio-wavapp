#pragma once

#include <oboe/Oboe.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Analyzer.h"
#include "Exporter.h"
#include "Importer.h"
#include "Mixer.h"

// Une el mezclador con la salida de audio (Oboe) y administra las pistas.
// El proyecto trabaja a 48 kHz; si el celular usa otra frecuencia, Oboe convierte.
class AudioEngine : public oboe::AudioStreamDataCallback,
                    public oboe::AudioStreamErrorCallback {
public:
    static constexpr int kSampleRate = 48000;

    ~AudioEngine() override;

    bool start();
    void stop();
    int32_t deviceRate();
    // [frecuencia real, ráfaga, búfer en cuadros, cortes (xruns), exclusivo 1/0]; ceros si está cerrado.
    void outputInfo(int32_t out[5]);
    Mixer &mixer() { return mMixer; }

    // Devuelven el espacio (0..11) de la pista nueva, o -1 con `error`.
    int importTrack(int fd, const std::string &dstPath, ImportInfo &info, std::string &error);
    int addTrackFile(const std::string &path, std::string &error);
    int addDemoTrack(int index, const std::string &path, std::string &error);
    void removeTrack(int slot);
    float importProgress() const { return mProgress.load(); }
    int trackChannels(int slot) const;
    int64_t trackFrames(int slot) const;
    // Forma de onda para dibujar: [columna][canal][min, max].
    std::vector<float> overview(int slot, int columns) const;

    // Exportar (en un hilo de trabajo). Cierra fd.
    ExportResult exportMix(int fd, const ExportFormat &fmt);
    float exportProgress() const { return mExportProgress.load(); }
    void cancelExport() { mExportCancel.store(true); }

    // Análisis (desde el hilo de la pantalla).
    Analyzer &analyzer() { return mAnalyzer; }
    std::vector<uint8_t> spectrogram(int slot, int cols, int rows, std::string &error);

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream, void *audioData,
                                          int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override;

private:
    bool openStream();
    int addMapped(std::unique_ptr<MappedAudio> audio, std::string &error);

    Mixer mMixer{kSampleRate};
    std::mutex mStreamMutex;
    std::shared_ptr<oboe::AudioStream> mStream;
    bool mWanted = false;  // la app quiere el audio abierto

    std::atomic<bool> mPrefetchRun{false};
    std::thread mPrefetchThread;

    mutable std::mutex mTracksMutex;
    std::vector<float> mPeaks[kMaxTracks];
    int mPeakChannels[kMaxTracks] = {};
    int64_t mFrames[kMaxTracks] = {};
    std::atomic<float> mProgress{0.0f};
    std::atomic<float> mExportProgress{0.0f};
    std::atomic<bool> mExportCancel{false};
    Analyzer mAnalyzer{kSampleRate};
    static constexpr int kPeakBucket = 256;
};
