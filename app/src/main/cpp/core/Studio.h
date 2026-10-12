#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Worker.h"
#include "dsp/AnalysisView.h"
#include "dsp/Analyzer.h"
#include "engine/Decoder.h"
#include "engine/Importer.h"
#include "engine/Meters.h"
#include "engine/Mixer.h"
#include "project/Model.h"

// El corazón de la app. Todo lo técnico vive aquí: el proyecto (pistas, EQ, pedazos, master),
// la mezcla, importar y decodificar, exportar, la sesión, la edición con deshacer/rehacer,
// los medidores y el análisis. La pantalla (Kotlin + Compose) solo dibuja lo que lee de aquí
// y manda lo que la persona hace.
//
// Hilos: la pantalla llama desde su hilo; los trabajos largos (importar, exportar, abrir la
// sesión) corren en un hilo de trabajos; los espectrogramas en otro; el audio lo pide la
// plataforma con mixer().process(); un hilo de mantenimiento trae del disco lo que va a sonar
// y guarda la sesión poco después de cada cambio.
//
// Las pistas se nombran por su id (no cambia mientras exista). Cada cosa que puede cambiar
// tiene una versión: la pantalla compara versions() para saber qué volver a leer.
class Studio {
public:
    static constexpr int kRate = 48000;
    static constexpr int kPeakBucket = 256;
    static constexpr int kSpectrogramCols = 1200, kSpectrogramRows = 256;
    static constexpr int kMaxHistory = 100;

    enum Version { kVerTracks = 0, kVerMaster, kVerHistory, kVerStatus, kVerBackground, kVerFirstSlot };
    static constexpr int kVersionCount = kVerFirstSlot + 3 * kMaxTracks;
    static constexpr int verParams(int slot) { return kVerFirstSlot + 3 * slot; }
    static constexpr int verEq(int slot) { return kVerFirstSlot + 3 * slot + 1; }
    static constexpr int verRegions(int slot) { return kVerFirstSlot + 3 * slot + 2; }

    enum EditMode { kEditMove = 1, kEditTrimStart, kEditTrimEnd, kEditFadeIn, kEditFadeOut };

    explicit Studio(DecodeFunction decoder = nullptr);
    ~Studio();
    Studio(const Studio &) = delete;
    Studio &operator=(const Studio &) = delete;

    Mixer &mixer() { return mMixer; }

    // ---------- Ciclo de vida
    // Abre el proyecto de la carpeta `filesDir` (la sesión y la carpeta "audio"). La primera
    // vez, sin sesión, carga las pistas de prueba (como el prototipo). Si ya está abierta, no
    // hace nada; si es otra, guarda la actual y cambia.
    void open(const std::string &filesDir, const std::string &cacheDir);
    void startBackground();
    void stopBackground();
    bool saveNow();
    void clearProject();  // quita todas las pistas (y sus archivos) y deja el master como nuevo
    bool waitIdle(int timeoutMs);  // espera a que terminen los trabajos (pruebas)
    void notify(const std::string &message);  // mensaje de estado que viene de la plataforma

    // ---------- Lecturas para la pantalla
    void versions(int32_t *out) const;  // kVersionCount valores
    // [{"id","slot","nombre","canales","cuadros","info"}, ...] en el orden de la pantalla.
    std::string tracksJson() const;
    // volumen (dB), paneo, mute, solo, fase. false si la pista no existe.
    bool trackParams(int id, float *out) const;
    // [bypass, auto-ganancia, n, y por banda (en orden): tipo, frecuencia, ganancia, Q, activa].
    std::vector<float> eqState(int id) const;
    std::vector<Region> regions(int id) const;
    // volumen master (dB), mute, limitador, notch, bits y frecuencia de exportación.
    void masterState(float *out) const;
    // {"mensaje","ocupada","texto","exportando","exportaciones","ultimaOk"}
    std::string statusJson() const;
    float progress() const { return mProgress.load(); }  // -1 = sin barra
    bool canUndo() const;
    bool canRedo() const;
    int trackCount() const;

    // ---------- Pistas
    // Importa en orden (WAV directo; lo demás con el decodificador de la plataforma). Cierra
    // los descriptores. false si hay otro trabajo en curso.
    bool importFiles(const std::vector<int> &fds, const std::vector<std::string> &names);
    bool loadDemo();
    bool removeTrack(int id);
    void setVolume(int id, float db);
    void setPan(int id, float pan);
    void setMute(int id, bool on);
    void setSolo(int id, bool on);
    void setPhase(int id, bool on);

    // ---------- EQ (las bandas se nombran por su posición en el orden de la pista)
    int addEqBand(int id);  // en el hueco de frecuencias más grande; devuelve su posición o -1
    void removeEqBand(int id, int pos);
    void setEqBand(int id, int pos, int type, float freq, float gain, float q, bool on);
    void setEqBypass(int id, bool on);
    void setEqAutoGain(int id, bool on);
    void resetEq(int id);
    float eqTrimDb(int id) const;
    void eqResponse(int id, int pos, const float *freqs, float *outDb, int n) const;  // pos -1 = todo

    // ---------- Master y exportar
    void setMasterDb(float db);
    void setMasterMute(bool on);
    void setLimiter(bool on);
    void setNotch(int hz);
    void setExportFormat(int bits, int rate);
    int64_t exportBytes() const;
    // Exporta en el hilo de trabajos y cierra `fd`. `name` es para el mensaje final.
    bool exportMix(int fd, const std::string &name);
    void cancelExport() { mCancelExport.store(true); }

    // ---------- Transporte
    void play();
    void pause();
    void playPause();
    void stop();
    void seek(double seconds);
    bool isPlaying() const { return mMixer.isPlaying(); }
    double position() const { return static_cast<double>(mMixer.position()) / kRate; }
    double duration() const { return static_cast<double>(mMixer.length()) / kRate; }

    // ---------- Medidores y análisis (desde el hilo de la pantalla)
    void meters(int64_t nowMs, float *out);  // MeterBallistics::kCount valores
    void clearClip() { mMeters.clearClip(); }
    void setAnalysisSource(int id);  // -1 = la mezcla
    bool analysisUpdate(int64_t nowMs, bool classic, float *stats);  // AnalysisView::kStatCount
    int goniometer(float *out, int max) const { return mView.goniometer(out, max); }
    void spectrumColumns(float *out, int cols) const;
    // Espectrograma de la pista (antes del EQ), kSpectrogramCols × kSpectrogramRows. La primera
    // vez se calcula aparte y devuelve false; cuando está listo cambia kVerBackground.
    bool spectrogram(int id, std::vector<uint8_t> &out);

    // ---------- Forma de onda
    // Del pedazo `index`, solo la parte [from, to) de la línea de tiempo, en `cols` columnas:
    // [columna][canal][min, max], ya con sus fundidos.
    std::vector<float> regionWave(int id, int index, int64_t from, int64_t to, int cols) const;

    // ---------- Edición (cada acción se puede deshacer)
    bool editBegin(int id, int index, int mode);
    // Arrastre: `delta` cuadros desde el inicio; el imán pega a 0, al cursor y a los bordes de
    // los demás pedazos que estén a `snap` cuadros o menos (0 = sin imán).
    void editDrag(int64_t delta, int64_t snap);
    void editEnd();
    // Corta en el cursor. Con una pista: el pedazo `index`; devuelve el índice del pedazo nuevo
    // (la parte derecha) o -1. Con id = -1: todos los pedazos bajo el cursor; devuelve cuántos.
    int cut(int id, int index);
    bool deleteRegion(int id, int index);
    void nudgeRegion(int id, int index, int ms);
    void setRegionFade(int id, int index, bool fadeIn, int64_t frames);
    void restoreRegions(int id);  // el archivo completo desde 0
    bool undo();
    bool redo();

private:
    using Snapshot = std::vector<std::pair<int, std::vector<Region>>>;

    struct Gesture {
        bool active = false;
        int id = 0, index = 0, mode = 0;
        Region base;
        std::vector<int64_t> points;
        Snapshot before;
    };

    // Con mModel tomado:
    TrackModel *findLocked(int id);
    const TrackModel *findLocked(int id) const;
    void applyTrackLocked(TrackModel &t);
    void applyEqLocked(TrackModel &t);
    void applyRegionsLocked(TrackModel &t, const std::vector<Region> &list);
    void applyMasterLocked();
    Snapshot snapshotLocked() const;
    void restoreLocked(const Snapshot &s);
    void pushUndoLocked(Snapshot before);
    void changeRegionsLocked(TrackModel &t, std::vector<Region> list, const std::string &key);
    std::vector<int64_t> snapPointsLocked(int id, int index) const;
    std::string addTrackLocked(TrackModel t, std::unique_ptr<MappedAudio> audio);
    void removeAllLocked(bool deleteFiles);

    // Trabajos
    bool beginJob(const std::string &text, bool exporting = false);
    void setJobText(const std::string &text);
    void endJob(const std::string &message);
    void jobImport(std::vector<int> fds, std::vector<std::string> names);
    void jobDemo();
    void jobRestore();
    std::string importOne(int fd, const std::string &name, const std::string &dst, ImportInfo &info,
                          std::string &codec);
    std::string newFileName(const std::string &prefix);

    void setMessage(const std::string &message);
    std::string baseStatus() const;
    void bump(int v) { mVer[v].fetch_add(1, std::memory_order_relaxed); }
    void bumpAll();
    void markDirty();
    bool save();
    void housekeeping();

    const DecodeFunction mDecoder;
    Mixer mMixer{kRate};

    mutable std::mutex mModel;
    ProjectModel mProject;
    std::string mFilesDir, mAudioDir, mCacheDir, mSessionPath;
    std::deque<Snapshot> mUndo, mRedo;
    Gesture mGesture;
    std::string mLastKey;
    int64_t mLastKeyMs = 0;

    mutable std::mutex mStatus;
    std::string mMessage;
    bool mBusy = false, mExporting = false, mLastExportOk = false;
    std::string mBusyText;
    int mExportCount = 0;
    std::atomic<float> mProgress{-1.0f};
    std::atomic<bool> mCancelExport{false};

    std::atomic<int32_t> mVer[kVersionCount] = {};
    std::atomic<bool> mDirty{false}, mRestoring{false};
    std::atomic<int64_t> mDirtyMs{0};
    std::mutex mSaveMutex;

    // Hilo de la pantalla
    Analyzer mAnalyzer{kRate};
    AnalysisView mView;
    MeterBallistics mMeters;

    std::mutex mSgMutex;
    std::map<int, std::vector<uint8_t>> mSgCache;
    std::set<int> mSgPending;

    std::mutex mOpenMutex;
    std::atomic<bool> mBgRun{false};
    std::thread mBgThread;

    // Al final: se destruyen primero (esperan a sus trabajos antes de que se vaya lo demás).
    Worker mBackground;
    Worker mJobs;
};
