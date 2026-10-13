#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "dsp/Equalizer.h"
#include "dsp/Limiter.h"
#include "io/MappedAudio.h"

class LiveInput;

constexpr int kMaxTracks = 12;
// Medidores: [0] pico master L, [1] pico master R, [2] reducción del limitador (dB, ≥ 0),
// luego pico L/R de cada pista: [3 + 2*slot], [4 + 2*slot]. Los picos son lineales.
constexpr int kMeterCount = 3 + 2 * kMaxTracks;

// Edición: una pista suena como una lista de pedazos (regiones) de su archivo, cada uno en
// su lugar de la línea de tiempo. Todo en cuadros a la frecuencia del proyecto.
struct Region {
    int64_t start = 0;    // dónde empieza en la línea de tiempo
    int64_t src = 0;      // desde qué cuadro del archivo
    int64_t length = 0;
    int64_t fadeIn = 0, fadeOut = 0;
    int64_t end() const { return start + length; }
    bool operator==(const Region &o) const {
        return start == o.start && src == o.src && length == o.length && fadeIn == o.fadeIn && fadeOut == o.fadeOut;
    }
    bool operator!=(const Region &o) const { return !(*this == o); }
};
constexpr int kMaxRegions = 256;
// Fundido mínimo en un borde cortado (donde el audio no sigue en otro pedazo), para que no truene.
constexpr int64_t kEdgeFade = 96;  // 2 ms

// Mezcla las pistas como el prototipo web ("Mesa de Audio"):
// pista: polaridad → EQ → auto-ganancia → volumen → paneo (StereoPannerNode) → mute/solo → suma
// master: volumen → limitador → recorte suave → salida.
// Los cambios de parámetros se suavizan igual que setTargetAtTime en Web Audio.
class Mixer {
public:
    explicit Mixer(int sampleRate);
    ~Mixer();

    // ---------- Hilo de la interfaz ----------
    int sampleRate() const { return mSampleRate; }
    int freeSlot() const;
    bool setTrack(int slot, std::unique_ptr<MappedAudio> audio);
    // Quita la pista (con un fundido corto si está sonando) y la devuelve cuando el hilo
    // de audio ya no la usa.
    std::unique_ptr<MappedAudio> takeTrack(int slot);
    bool hasTrack(int slot) const;
    // Reemplaza los pedazos de la pista (se ajustan al archivo). Al agregar una pista queda un
    // solo pedazo con el archivo completo desde 0.
    bool setRegions(int slot, const std::vector<Region> &regions);
    std::vector<Region> regions(int slot) const;

    void setTrackGain(int slot, float gain);  // lineal
    void setTrackPan(int slot, float pan);    // -1 (L) .. 1 (R)
    void setTrackMute(int slot, bool on);
    void setTrackSolo(int slot, bool on);
    void setTrackInvert(int slot, bool on);
    // EQ de la pista (sus métodos de interfaz son seguros desde este hilo).
    Equalizer *trackEq(int slot) { return slot >= 0 && slot < kMaxTracks ? &mSlots[slot].eq : nullptr; }
    const Equalizer *trackEq(int slot) const { return slot >= 0 && slot < kMaxTracks ? &mSlots[slot].eq : nullptr; }
    void setMasterGain(float gain);  // lineal (0 = mute)
    void setLimiter(bool on);
    // Filtro anti-zumbido en el bus (fundamental y 3 armónicos, Q 30): 0 = apagado, 50 o 60 Hz.
    void setNotch(int hz) { mNotchHz.store(hz == 50 || hz == 60 ? hz : 0); }
    int notch() const { return mNotchHz.load(); }

    // Análisis: qué se manda al analizador. -1 = la mezcla (bus, antes del volumen master);
    // 0..11 = esa pista después de volumen y paneo (antes de mute/solo), como el prototipo;
    // kTapInput = la entrada en vivo (después de su EQ; la escribe su hilo con writeInputTap).
    static constexpr int kTapInput = -2;
    void setAnalysisSource(int slot) { mTapSource.store(slot); }
    int analysisSource() const { return mTapSource.load(); }
    static constexpr int kTapSize = 32768;
    // Copia los últimos `n` cuadros (n <= kTapSize) y devuelve cuántos cuadros se han escrito
    // en total (sirve para saber si llegó audio nuevo).
    uint64_t readTap(float *l, float *r, int n) const;

    // Exportación: copia pistas y ajustes a otro mezclador, que renderiza fuera de tiempo real.
    bool cloneInto(Mixer &dst, std::string &error) const;
    // Prepara este mezclador para renderizar desde el inicio sin rampas de entrada.
    void prepareOffline();
    std::string trackPath(int slot) const;

    // Entrada en vivo: su escucha se suma a la salida (después del master).
    void setLiveInput(LiveInput *in) { mLive.store(in); }
    // Mientras se graba, la canción sigue corriendo después de su final (y aunque no haya pistas).
    void setFreeRun(bool on) { mFreeRun.store(on); }
    // La salida tiene algo que dar: la mezcla corre o se está escuchando la entrada.
    bool needsOutput() const;
    // Lo llama el hilo de la entrada (solo si el análisis es de la entrada).
    void writeInputTap(const float *l, const float *r, int n);

    void play();
    void pause();
    void stop();
    void seek(int64_t frame);
    bool isPlaying() const { return mPlaying.load(); }
    // Está sonando algo (reproduciendo o terminando la rampa de pausa).
    bool isRolling() const { return mRolling.load(std::memory_order_relaxed); }
    int64_t position() const;
    int64_t length() const { return mLength.load(); }
    void readMeters(float *out);  // picos desde la lectura anterior

    // Pide al sistema que lea del disco lo que viene. Llamar cada ~100 ms desde otro hilo.
    void prefetch();

    // ---------- Hilo de audio ----------
    void process(float *out, int frames, int channels);

private:
    static constexpr int kBlock = 64;

    // Pedazo listo para el hilo de audio: con los fundidos ya resueltos.
    struct Piece {
        int64_t start, src, length, fadeIn, fadeOut;
    };
    struct PieceSet {
        std::vector<Region> regions;  // como se pidieron (ya ajustadas)
        std::vector<Piece> pieces;
        int64_t end = 0;
    };

    struct Slot {
        std::atomic<const MappedAudio *> audio{nullptr};
        std::atomic<const PieceSet *> pieces{nullptr};
        std::unique_ptr<PieceSet> ownedPieces;  // solo hilo de la interfaz (con mMutex)
        std::unique_ptr<MappedAudio> owned;  // solo hilo de la interfaz (con mMutex)
        std::atomic<uint32_t> gen{0};
        std::atomic<float> gain{1.0f}, pan{0.0f};
        std::atomic<bool> mute{false}, solo{false}, invert{false}, removing{false};
        std::atomic<float> peakL{0.0f}, peakR{0.0f};
        Equalizer eq;
        // Estado del hilo de audio
        uint32_t seenGen = 0;
        float gainS = 1.0f, panS = 0.0f, onS = 0.0f, invS = 1.0f;
    };

    void renderBlock(int n);
    void readInput(const MappedAudio &a, const PieceSet &ps, int n);
    static std::unique_ptr<PieceSet> makePieces(std::vector<Region> regions, int64_t fileFrames);
    void publishPieces(Slot &s, std::unique_ptr<PieceSet> ps);  // con mMutex
    void applyNotch(int n);
    void updateLength();  // con mMutex
    bool waitForAudioThread();

    const int mSampleRate;
    float mCoefGain, mCoefPan, mCoefOn, mCoefInv, mCoefMaster;
    float mTauPanFrames;

    Slot mSlots[kMaxTracks];
    mutable std::mutex mMutex;

    std::atomic<bool> mPlaying{false};
    std::atomic<int64_t> mSeekTarget{-1};
    std::atomic<int64_t> mPosition{0};
    std::atomic<int64_t> mLength{0};
    std::atomic<float> mMasterGain{1.0f};
    std::atomic<bool> mLimiterOn{true};
    std::atomic<bool> mRolling{false};
    std::atomic<uint64_t> mCallbacks{0};  // impar = callback en curso
    std::atomic<float> mPeakL{0.0f}, mPeakR{0.0f}, mReduction{0.0f};
    std::atomic<int> mNotchHz{0};
    std::atomic<int> mTapSource{-1};
    std::atomic<LiveInput *> mLive{nullptr};
    std::atomic<bool> mFreeRun{false};
    std::unique_ptr<std::atomic<float>[]> mTapL, mTapR;
    std::atomic<uint64_t> mTapWrite{0};

    // Estado del hilo de audio
    int64_t mPos = 0;
    float mTransport = 0.0f;  // rampa anti-clic de play/pausa/salto
    float mMasterS = 1.0f;
    bool mLimiterWasOn = true;
    Limiter mLimiter;
    float mMixL[kBlock], mMixR[kBlock], mRamp[kBlock], mInL[kBlock], mInR[kBlock];
    float mBlkTapL[kBlock], mBlkTapR[kBlock];
    bool mOffline = false;
    int mNotchSeen = 0;
    Biquad mNotchC[4];
    bool mNotchOn[4] = {false, false, false, false};
    double mNotchZ[4][2][2] = {};
};
