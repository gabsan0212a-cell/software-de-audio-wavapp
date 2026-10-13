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
#include "engine/LiveInput.h"
#include "engine/Meters.h"
#include "engine/Mixer.h"
#include "project/Model.h"

// El corazón de la app. Todo lo técnico vive aquí: el proyecto (pistas, EQ, pedazos,
// automatización, master),
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
    // Por cada espacio del mezclador: parámetros, EQ, pedazos y automatización.
    static constexpr int kVersPerSlot = 4;
    // Al final: la entrada en vivo (ganancia, escucha y su EQ).
    static constexpr int kVerInput = kVerFirstSlot + kVersPerSlot * kMaxTracks;
    static constexpr int kVersionCount = kVerInput + 1;
    // El EQ de la entrada en vivo se maneja con las mismas funciones del EQ, con este id.
    static constexpr int kInputId = 0;
    // inputState(): [abierta, canales, ganancia dB, escucha, volumen de escucha dB,
    // retraso de ida y vuelta (ms), grabando, segundos grabados]
    static constexpr int kInputStateCount = 8;
    static constexpr int verParams(int slot) { return kVerFirstSlot + kVersPerSlot * slot; }
    static constexpr int verEq(int slot) { return kVerFirstSlot + kVersPerSlot * slot + 1; }
    static constexpr int verRegions(int slot) { return kVerFirstSlot + kVersPerSlot * slot + 2; }
    static constexpr int verAuto(int slot) { return kVerFirstSlot + kVersPerSlot * slot + 3; }
    // trackParams(): volumen (dB), paneo, mute, solo, fase, modo de automatización.
    static constexpr int kTrackParamCount = 6;

    enum EditMode { kEditMove = 1, kEditTrimStart, kEditTrimEnd, kEditFadeIn, kEditFadeOut };
    // Automatización: apagada (suenan los valores fijos), leer, grabar movimientos (mientras
    // suena, lo que se mueve en el deslizador queda grabado; detenido, cambia el punto del cursor).
    enum AutoMode { kAutoOff = 0, kAutoRead = 1, kAutoWrite = 2 };
    // automationNow(): por espacio [estado, volumen dB, paneo]. Estado: 1 = el volumen sigue la
    // automatización, 2 = el paneo la sigue, 4 = se graba el volumen, 8 = se graba el paneo.
    static constexpr int kAutoNowCount = 3;
    static constexpr int64_t kAutoGlide = kRate / 4;  // al soltar, regresa a lo que había en 250 ms

    explicit Studio(DecodeFunction decoder = nullptr);
    ~Studio();
    Studio(const Studio &) = delete;
    Studio &operator=(const Studio &) = delete;

    Mixer &mixer() { return mMixer; }
    LiveInput &input() { return mInput; }

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
    // kTrackParamCount valores. false si la pista no existe.
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

    // ---------- Entrada en vivo y grabación
    // La plataforma abrió (con 1 o 2 canales) o cerró la entrada.
    void inputOpened(int channels);
    void inputClosed();
    // Retrasos medidos por la plataforma (cuadros; -1 = no se sabe).
    void setLatencies(int inputFrames, int outputFrames);
    void setInputGain(float db);
    void setMonitor(bool on);
    void setMonitorDb(float db);
    void inputState(float *out) const;  // kInputStateCount valores
    // Graba desde el cursor mientras suena la mezcla (si hay pistas). false si no se puede
    // (el motivo queda en el mensaje).
    bool recordStart();
    // Termina: la toma queda como pista nueva en su lugar (compensando el retraso), con el EQ
    // que se estaba escuchando. Devuelve su id o -1.
    int recordStop();
    void recordCancel();
    bool recording() const { return mInput.recording(); }
    std::vector<float> recordingWave(int cols, double seconds) const { return mInput.recentWave(cols, seconds); }

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

    // ---------- Automatización de volumen y paneo (param: autom::kVolume o autom::kPan)
    void setAutoMode(int id, int mode);
    // Para dibujar el carril: [n, posición del valor fijo, valor fijo, posición guía (0 dB o el
    // centro), y por punto: cuadro, posición (0 abajo .. 1 arriba), valor].
    std::vector<double> autoLane(int id, int param) const;
    // En cada cuadro de pantalla: avanza lo que se esté grabando y da kAutoNowCount valores por
    // espacio del mezclador (lo que marcan los deslizadores ahora).
    void automationNow(float *out);
    // Desde el carril: agrega un punto (si la automatización estaba apagada, la prende). Devuelve
    // su índice o -1.
    int autoAdd(int id, int param, int64_t frame, float pos);
    bool autoRemove(int id, int param, int index);
    void autoClear(int id, int param);
    // Arrastre de un punto: cuadros y posición desde que empezó. Todo el arrastre es un paso de
    // deshacer. El punto no pasa a sus vecinos.
    bool autoBegin(int id, int param, int index);
    void autoDrag(int64_t deltaFrames, float deltaPos);
    void autoEnd();
    // El dedo está (o ya no) sobre el deslizador de volumen o paneo de la pista.
    void autoTouch(int id, int param, bool down);

private:
    // Lo que se guarda para deshacer: pedazos y automatización de cada pista.
    struct TrackSnap {
        int id = 0;
        std::vector<Region> regions;
        autom::Lane autos[autom::kParamCount];
        bool operator==(const TrackSnap &o) const {
            return id == o.id && regions == o.regions && autos[0] == o.autos[0] && autos[1] == o.autos[1];
        }
        bool operator!=(const TrackSnap &o) const { return !(*this == o); }
    };
    using Snapshot = std::vector<TrackSnap>;

    struct Gesture {
        bool active = false;
        int id = 0, index = 0, mode = 0;
        Region base;
        std::vector<int64_t> points;
        Snapshot before;
    };
    struct AutoGesture {
        bool active = false;
        int id = 0, param = 0, index = 0;
        autom::Point base;
        Snapshot before;
    };
    // El dedo sobre un deslizador (para grabar movimientos).
    struct AutoTouch {
        bool active = false;
        int id = 0, param = 0;
        bool writing = false;  // grabando mientras suena
        bool frozen = false;   // la canción se detuvo o volvió atrás: ya no avanza
        bool changed = false;  // el deslizador se movió
        float base = 0.0f;     // valor fijo al empezar (si el carril estaba vacío)
        float value = 0.0f;    // lo que marca el dedo
        int64_t last = 0;      // hasta dónde llegó el cursor
        int64_t changedMs = 0;  // cuándo cambió el valor por última vez (reloj)
        autom::Lane old;
        std::vector<autom::Point> written;
        Snapshot before;
    };

    // Con mModel tomado:
    TrackModel *eqTargetLocked(int id);  // una pista o la entrada (kInputId)
    const TrackModel *eqTargetLocked(int id) const;
    Equalizer *eqOf(const TrackModel &t);
    const Equalizer *eqOf(const TrackModel &t) const;
    int eqVersion(const TrackModel &t) const;
    void applyInputLocked();
    TrackModel *findLocked(int id);
    const TrackModel *findLocked(int id) const;
    void applyTrackLocked(TrackModel &t);
    void applyEqLocked(TrackModel &t);
    void applyRegionsLocked(TrackModel &t, const std::vector<Region> &list);
    void applyAutoLocked(TrackModel &t, int param);
    bool automatedLocked(const TrackModel &t, int param) const;
    float staticValueLocked(const TrackModel &t, int param) const;
    void setStaticLocked(TrackModel &t, int param, float value);
    // Pone los puntos (ajustados). Con `undo`, es un paso de deshacer.
    void changeAutoLocked(TrackModel &t, int param, std::vector<autom::Point> pts, bool undo);
    float lanePosToValue(int param, float pos) const;
    void beginTouchLocked(TrackModel &t, int param, float base);
    void touchValueLocked(TrackModel &t, int param, float before, float value);
    void advanceTouchLocked(bool moved);  // moved: llegó un valor nuevo del deslizador
    std::string endTouchLocked();  // devuelve un aviso para mostrar (después de soltar mModel)
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
    LiveInput mInput{kRate, mMixer};
    std::atomic<int> mLatIn{-1}, mLatOut{-1};
    bool mRecWithMix = false;  // con mModel: había pistas al empezar a grabar
    int64_t mRecFrom = 0;      // con mModel: dónde estaba el cursor al empezar a grabar

    mutable std::mutex mModel;
    ProjectModel mProject;
    std::string mFilesDir, mAudioDir, mCacheDir, mSessionPath;
    std::deque<Snapshot> mUndo, mRedo;
    Gesture mGesture;
    AutoGesture mAutoGesture;
    AutoTouch mTouch;
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
