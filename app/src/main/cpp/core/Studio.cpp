#include "Studio.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>

#include "dsp/SongAnalysis.h"
#include "engine/Exporter.h"
#include "io/Json.h"
#include "io/MappedAudio.h"
#include "project/Edit.h"
#include "project/Session.h"
#include "sep/Separator.h"

namespace {

constexpr float kVolMin = -60.0f, kVolMax = 6.0f, kMasterMin = -70.0f;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

float dbToGain(float db, float offBelow) { return db <= offBelow ? 0.0f : std::pow(10.0f, db / 20.0f); }

float roundTo(float v, float s) { return std::round(v / s) * s; }

std::string fmt(const char *f, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

std::string withoutExtension(const std::string &n) {
    const size_t dot = n.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return n.empty() ? "pista" : n;
    return n.substr(0, dot);
}

// Igual que en la versión anterior: "44.1 kHz → 48 kHz · 16 bits · estéreo".
std::string describe(const ImportInfo &i, const std::string &codec) {
    const std::string khz = i.srcRate % 1000 == 0 ? std::to_string(i.srcRate / 1000) : fmt("%.1f", i.srcRate / 1000.0);
    const std::string rate = khz + " kHz" + (i.srcRate != Studio::kRate ? " → 48 kHz" : "");
    std::string ch;
    if (i.dualMono) ch = "estéreo idéntico → mono";
    else if (i.srcChannels == 1) ch = "mono";
    else if (i.srcChannels == 2) ch = "estéreo";
    else ch = std::to_string(i.srcChannels) + " canales → estéreo";
    if (!codec.empty()) return codec + " · " + rate + " · " + ch;
    const std::string bits = std::to_string(i.srcBits) + (i.srcFloat ? " bits float" : " bits");
    return rate + " · " + bits + " · " + ch;
}

// Número con su signo tipográfico ("−14.0"), como los muestra la pantalla.
std::string signed1(double v) {
    std::string t = fmt("%.1f", v);
    if (!t.empty() && t[0] == '-') t = "\u2212" + t.substr(1);
    return t;
}

std::string formatText(int bits, int rate) {
    return (bits == 32 ? std::string("32 bits float") : std::to_string(bits) + " bits") + " · " +
           std::to_string(rate) + " Hz";
}

// Curva de los fundidos, igual que el mezclador.
double fadeGain(const Region &r, double k) {
    double g = 1.0;
    auto s2 = [](double x) {
        const double s = std::sin(std::clamp(x, 0.0, 1.0) * 1.57079632679489661923);
        return s * s;
    };
    if (r.fadeIn > 0 && k < r.fadeIn) g *= s2(k / static_cast<double>(r.fadeIn));
    const double rest = static_cast<double>(r.length) - k;
    if (r.fadeOut > 0 && rest < r.fadeOut) g *= s2(rest / static_cast<double>(r.fadeOut));
    return g;
}

// Centro (en escala logarítmica) del hueco más grande entre 20 Hz, las bandas y 20 kHz,
// redondeado a 2 cifras (p. ej. 3162 → 3200).
float freeFrequency(const std::vector<EqBandModel> &bands) {
    if (bands.empty()) return 1000.0f;
    std::vector<double> pts = {std::log10(20.0), std::log10(20000.0)};
    for (const EqBandModel &b : bands) pts.push_back(std::log10(static_cast<double>(b.freq)));
    std::sort(pts.begin(), pts.end());
    size_t best = 0;
    for (size_t i = 1; i + 1 < pts.size(); ++i)
        if (pts[i + 1] - pts[i] > pts[best + 1] - pts[best]) best = i;
    const double f = std::pow(10.0, (pts[best] + pts[best + 1]) / 2);
    const double scale = std::pow(10.0, std::floor(std::log10(f)) - 1);
    return static_cast<float>(std::round(f / scale) * scale);
}

bool fileExists(const std::string &p) {
    struct stat st {};
    return ::stat(p.c_str(), &st) == 0;
}

int64_t fileSize(const std::string &p) {
    struct stat st {};
    return ::stat(p.c_str(), &st) == 0 ? static_cast<int64_t>(st.st_size) : 0;
}

}  // namespace

Studio::Studio(DecodeFunction decoder) : mDecoder(std::move(decoder)) {
    mMixer.setLiveInput(&mInput);
    mMixer.setInstrumentLive(&mInstLive);
    mMixer.setShowRecorder(&mShow);
    mProject.input.name = "Micrófono";
    applyInputLocked();
}

Studio::~Studio() {
    mCancelExport.store(true);  // un trabajo largo (separar la voz) termina pronto
    mJobs.waitIdle(60000);
    if (mVoiceFd >= 0) ::close(mVoiceFd);
    stopBackground();
    if (mInput.recording()) mInput.stopRecording();  // el archivo se limpia al abrir la próxima vez
    mMixer.setShowRecorder(nullptr);
    if (mShow.recording()) mShow.stop();
    mMixer.setLiveInput(nullptr);
    mInstJobs.waitIdle(30000);
    mMixer.setInstrumentLive(nullptr);
}

// ---------------------------------------------------------------- ciclo de vida

void Studio::open(const std::string &filesDir, const std::string &cacheDir) {
    std::lock_guard<std::mutex> once(mOpenMutex);
    {
        std::lock_guard<std::mutex> lk(mModel);
        if (filesDir == mFilesDir) return;
    }
    mJobs.waitIdle(120000);  // un trabajo del proyecto anterior termina antes de cambiar
    mInstJobs.waitIdle(60000);
    if (!mFilesDir.empty()) saveNow();
    {
        std::lock_guard<std::mutex> lk(mModel);
        removeAllLocked(false);
        mProject = ProjectModel();
        mProject.input.name = "Micrófono";
        applyMasterLocked();
        applyInputLocked();
        mUndo.clear();
        mRedo.clear();
        mGesture = Gesture();
        mAutoGesture = AutoGesture();
        mTouch = AutoTouch();
        mFilesDir = filesDir;
        mAudioDir = filesDir + "/audio";
        mCacheDir = cacheDir.empty() ? filesDir : cacheDir;
        mSessionPath = filesDir + "/sesion.json";
        ::mkdir(mAudioDir.c_str(), 0700);
    }
    {
        std::string e;
        auto a = MappedAudio::open(showPath(), e);
        mShowSeconds.store(a ? static_cast<double>(a->frames()) / kRate : 0.0);
    }
    {
        std::lock_guard<std::mutex> lk(mSgMutex);
        mSgCache.clear();
    }
    {
        std::lock_guard<std::mutex> lk(mLoudMutex);
        mSongState = 0;
        mSongReport = LoudnessReport();
    }
    resetLoudness();
    mDirty.store(false);
    bumpAll();
    std::string session;
    {
        std::lock_guard<std::mutex> lk(mModel);
        session = mSessionPath;
    }
    if (fileExists(session)) {
        mRestoring.store(true);
        if (!beginJob("Abriendo la mezcla anterior…")) mRestoring.store(false);
        else mJobs.post([this] { jobRestore(); });
    } else {
        loadDemo();  // la primera vez, como el prototipo
    }
}

void Studio::startBackground() {
    if (mBgRun.exchange(true)) return;
    mBgThread = std::thread([this] { housekeeping(); });
}

void Studio::stopBackground() {
    if (!mBgRun.exchange(false)) return;
    if (mBgThread.joinable()) mBgThread.join();
}

void Studio::housekeeping() {
    while (mBgRun.load()) {
        mMixer.prefetch();
        {
            // El volumen percibido se sigue midiendo aunque la pantalla no esté mirando.
            std::lock_guard<std::mutex> lk(mLoudMutex);
            pumpLoudnessLocked();
        }
        if (mDirty.load() && !mRestoring.load() && nowMs() - mDirtyMs.load() >= 400) save();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Studio::markDirty() {
    mChanges.fetch_add(1, std::memory_order_relaxed);
    if (mRestoring.load()) return;
    mDirtyMs.store(nowMs());
    mDirty.store(true);
}

bool Studio::saveNow() {
    if (mRestoring.load()) return false;
    return save();
}

bool Studio::save() {
    std::lock_guard<std::mutex> saving(mSaveMutex);
    std::string path, text;
    {
        std::lock_guard<std::mutex> lk(mModel);
        if (mSessionPath.empty()) return false;
        path = mSessionPath;
        mDirty.store(false);
        text = sessionToJson(mProject).dump();
    }
    const std::string tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool closed = std::fclose(f) == 0;
    if (!ok || !closed || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        mDirty.store(true);  // se intenta de nuevo
        return false;
    }
    return true;
}

void Studio::clearProject() {
    mJobs.waitIdle(120000);
    mInstJobs.waitIdle(60000);
    {
        std::lock_guard<std::mutex> lk(mModel);
        removeAllLocked(true);
        const int next = mProject.nextId;
        TrackModel input = std::move(mProject.input);  // los ajustes de la entrada se quedan
        const float gain = mProject.inputGainDb, monitor = mProject.monitorDb;
        const vfx::Values voice = mProject.voice;
        mProject = ProjectModel();
        mProject.nextId = next;
        mProject.input = std::move(input);
        mProject.inputGainDb = gain;
        mProject.monitorDb = monitor;
        mProject.voice = voice;
        mRec = NoteRec();
        mIdeas.clear();
        mMixer.setLoop(0, 0);
        applySongToVoiceLocked();
        applyMasterLocked();
        mUndo.clear();
        mRedo.clear();
        mGesture = Gesture();
        mAutoGesture = AutoGesture();
        mTouch = AutoTouch();
    }
    {
        std::lock_guard<std::mutex> lk(mSgMutex);
        mSgCache.clear();
    }
    bumpAll();
    markDirty();
    setMessage("");
}

bool Studio::waitIdle(int timeoutMs) {
    return mJobs.waitIdle(timeoutMs) && mBackground.waitIdle(timeoutMs) && mInstJobs.waitIdle(timeoutMs);
}

void Studio::notify(const std::string &message) { setMessage(message); }

void Studio::bumpAll() {
    for (auto &v : mVer) v.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------- estado

void Studio::versions(int32_t *out) const {
    for (int i = 0; i < kVersionCount; ++i) out[i] = mVer[i].load(std::memory_order_relaxed);
}

std::string Studio::tracksJson() const {
    std::lock_guard<std::mutex> lk(mModel);
    Json a = Json::array();
    for (const TrackModel &t : mProject.tracks) {
        Json o = Json::object();
        o.set("id", t.id);
        o.set("slot", t.slot);
        o.set("nombre", t.name);
        o.set("canales", t.channels);
        o.set("cuadros", t.frames);
        o.set("info", t.info);
        o.set("instrumento", t.isInst ? t.part.sound.kind : -1);
        a.push(std::move(o));
    }
    return a.dump();
}

bool Studio::trackParams(int id, float *out) const {
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = findLocked(id);
    if (!t) return false;
    out[0] = t->volDb;
    out[1] = static_cast<float>(t->pan);
    out[2] = t->mute ? 1.0f : 0.0f;
    out[3] = t->solo ? 1.0f : 0.0f;
    out[4] = t->phase ? 1.0f : 0.0f;
    out[5] = static_cast<float>(t->autoMode);
    return true;
}

std::vector<float> Studio::eqState(int id) const {
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = eqTargetLocked(id);
    if (!t) return {};
    std::vector<float> v = {t->eqBypass ? 1.0f : 0.0f, t->eqAuto ? 1.0f : 0.0f, static_cast<float>(t->bands.size())};
    for (const EqBandModel &b : t->bands)
        v.insert(v.end(), {static_cast<float>(b.type), b.freq, b.gain, b.q, b.on ? 1.0f : 0.0f});
    return v;
}

std::vector<Region> Studio::regions(int id) const {
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = findLocked(id);
    return t ? t->regions : std::vector<Region>();
}

void Studio::masterState(float *out) const {
    std::lock_guard<std::mutex> lk(mModel);
    const MasterModel &m = mProject.master;
    out[0] = m.volDb;
    out[1] = m.mute ? 1.0f : 0.0f;
    out[2] = m.limiter ? 1.0f : 0.0f;
    out[3] = static_cast<float>(m.notch);
    out[4] = static_cast<float>(m.exportBits);
    out[5] = static_cast<float>(m.exportRate);
    out[6] = static_cast<float>(m.loudTarget);
    out[7] = m.exportNormalize ? 1.0f : 0.0f;
}

std::string Studio::statusJson() const {
    std::lock_guard<std::mutex> lk(mStatus);
    Json o = Json::object();
    o.set("mensaje", mMessage);
    o.set("ocupada", mBusy);
    o.set("texto", mBusyText);
    o.set("exportando", mExporting);
    o.set("exportaciones", mExportCount);
    o.set("ultimaOk", mLastExportOk);
    return o.dump();
}

bool Studio::canUndo() const {
    std::lock_guard<std::mutex> lk(mModel);
    return !mUndo.empty();
}

bool Studio::canRedo() const {
    std::lock_guard<std::mutex> lk(mModel);
    return !mRedo.empty();
}

int Studio::trackCount() const {
    std::lock_guard<std::mutex> lk(mModel);
    return static_cast<int>(mProject.tracks.size());
}

std::string Studio::baseStatus() const {
    int n;
    int64_t bytes = 0;
    {
        std::lock_guard<std::mutex> lk(mModel);
        n = static_cast<int>(mProject.tracks.size());
        for (const TrackModel &t : mProject.tracks) bytes += fileSize(mAudioDir + "/" + t.file);
    }
    if (n == 0) return "Sin pistas. Agrega audio, graba o carga las pistas de prueba.";
    return std::to_string(n) + " pista(s) · 48000 Hz · ≈ " +
           std::to_string(static_cast<long long>(std::llround(bytes / 1048576.0))) + " MB en disco";
}

void Studio::setMessage(const std::string &message) {
    const std::string base = baseStatus();
    {
        std::lock_guard<std::mutex> lk(mStatus);
        mMessage = message.empty() ? base : message + " · " + base;
    }
    bump(kVerStatus);
}

bool Studio::beginJob(const std::string &text, bool exporting) {
    if (mInput.recording()) {
        setMessage("Termina la grabación primero");
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(mStatus);
        if (mBusy) return false;
        mBusy = true;
        mBusyText = text;
        mExporting = exporting;
    }
    mProgress.store(-1.0f);
    bump(kVerStatus);
    return true;
}

void Studio::setJobText(const std::string &text) {
    {
        std::lock_guard<std::mutex> lk(mStatus);
        mBusyText = text;
    }
    bump(kVerStatus);
}

void Studio::endJob(const std::string &message) {
    mProgress.store(-1.0f);
    {
        std::lock_guard<std::mutex> lk(mStatus);
        mBusy = false;
        mBusyText.clear();
        mExporting = false;
    }
    setMessage(message);
}

// ---------------------------------------------------------------- modelo (con mModel tomado)

TrackModel *Studio::findLocked(int id) {
    for (TrackModel &t : mProject.tracks)
        if (t.id == id) return &t;
    return nullptr;
}

const TrackModel *Studio::findLocked(int id) const {
    for (const TrackModel &t : mProject.tracks)
        if (t.id == id) return &t;
    return nullptr;
}

void Studio::applyTrackLocked(TrackModel &t) {
    mMixer.setTrackGain(t.slot, dbToGain(t.volDb, kVolMin + 0.1f));
    mMixer.setTrackPan(t.slot, static_cast<float>(t.pan) / 100.0f);
    mMixer.setTrackMute(t.slot, t.mute);
    mMixer.setTrackSolo(t.slot, t.solo);
    mMixer.setTrackInvert(t.slot, t.phase);
    applyEqLocked(t);
    applyFxLocked(t);
    for (int k = 0; k < autom::kParamCount; ++k) applyAutoLocked(t, k);
}

void Studio::applyFxLocked(TrackModel &t) {
    // Los efectos se crean (y reservan memoria) solo cuando alguno se prende.
    TrackFx *f = mMixer.trackFx(t.slot, fx::anyOn(t.fx));
    if (!f) return;
    int key;
    bool minor;
    songKeyLocked(key, minor);
    f->setSongKey(key, minor);
    for (int k = 0; k < fx::kCount; ++k) f->set(k, t.fx[static_cast<size_t>(k)]);
}

void Studio::songKeyLocked(int &key, bool &minor) const {
    // La de la canción preparada o, si se está creando música (o se eligió a mano), la de la música.
    const SongModel &s = mProject.song;
    key = s.ready() ? s.key : -1;
    minor = s.minor;
    if (mProject.music.keySet || hasInstrumentsLocked()) keyLocked(key, minor);
}

TrackModel *Studio::eqTargetLocked(int id) { return id == kInputId ? &mProject.input : findLocked(id); }

const TrackModel *Studio::eqTargetLocked(int id) const { return id == kInputId ? &mProject.input : findLocked(id); }

Equalizer *Studio::eqOf(const TrackModel &t) { return t.id == kInputId ? &mInput.eq() : mMixer.trackEq(t.slot); }

const Equalizer *Studio::eqOf(const TrackModel &t) const {
    return t.id == kInputId ? &mInput.eq() : mMixer.trackEq(t.slot);
}

int Studio::eqVersion(const TrackModel &t) const { return t.id == kInputId ? kVerInput : verEq(t.slot); }

void Studio::applyInputLocked() {
    mProject.inputGainDb = std::clamp(roundTo(mProject.inputGainDb, 0.5f), LiveInput::kGainMin, LiveInput::kGainMax);
    mProject.monitorDb = std::clamp(roundTo(mProject.monitorDb, 0.5f), kVolMin, kVolMax);
    mInput.setGainDb(mProject.inputGainDb);
    mInput.setMonitorGain(dbToGain(mProject.monitorDb, kVolMin + 0.1f));
    applyEqLocked(mProject.input);
    for (int p = 0; p < vfx::kCount; ++p) mInput.voice().set(p, mProject.voice[static_cast<size_t>(p)]);
    applySongToVoiceLocked();
}

void Studio::applySongToVoiceLocked() {
    // La escala "de la canción" del auto-tune: la de la canción preparada o, si se está creando
    // música (o se eligió a mano), la de la música. Igual el tempo del delay.
    int key;
    bool minor;
    songKeyLocked(key, minor);
    mInput.voice().setSongKey(key, minor);
    for (const TrackModel &t : mProject.tracks)
        if (TrackFx *f = mMixer.trackFx(t.slot, false)) f->setSongKey(key, minor);
    const SongModel &s = mProject.song;
    const bool tempo = s.ready() || mProject.music.bpmSet || hasInstrumentsLocked();
    mInput.voice().setTempo(tempo ? static_cast<float>(bpmLocked()) : 0.0f);
    applyMusicLocked();
}

void Studio::applyEqLocked(TrackModel &t) {
    Equalizer *eq = eqOf(t);
    if (!eq) return;
    bool used[kMaxBands] = {};
    for (const EqBandModel &b : t.bands) {
        eq->setBand(b.slot, true, b.on, b.type, b.freq, b.gain, b.q);
        used[b.slot] = true;
    }
    for (int s = 0; s < kMaxBands; ++s)
        if (!used[s]) eq->setBand(s, false, true, kPeaking, 1000.0f, 0.0f, 1.0f);
    eq->setBypass(t.eqBypass);
    eq->setAutoGain(t.eqAuto);
}

void Studio::applyRegionsLocked(TrackModel &t, const std::vector<Region> &list) {
    mMixer.setRegions(t.slot, list);
    t.regions = mMixer.regions(t.slot);  // lo que el mezclador aceptó (ya ajustado)
    bump(verRegions(t.slot));
    // Las partes con voz siguen los pedazos del instrumental: la pantalla las vuelve a leer.
    if (t.id != 0 && t.id == mProject.song.instrumentalId) bump(kVerSong);
}

void Studio::applyMasterLocked() {
    const MasterModel &m = mProject.master;
    mMixer.setMasterGain(m.mute ? 0.0f : dbToGain(m.volDb, kMasterMin + 0.5f));
    mMixer.setLimiter(m.limiter);
    mMixer.setNotch(m.notch);
}

std::string Studio::addTrackLocked(TrackModel t, std::unique_ptr<MappedAudio> audio) {
    if (!audio) return "No se pudo abrir la pista";
    if (audio->sampleRate() != kRate) return "La pista no está a 48 kHz";
    const int slot = mMixer.freeSlot();
    if (slot < 0) return "límite de 12 pistas";
    t.channels = audio->channels();
    t.frames = audio->frames();
    if (!mMixer.setTrack(slot, std::move(audio))) return "No se pudo agregar la pista";
    t.slot = slot;
    t.volDb = std::clamp(roundTo(t.volDb, 0.5f), kVolMin, kVolMax);
    t.pan = std::clamp(t.pan, -100, 100);
    if (t.autoMode < kAutoOff || t.autoMode > kAutoWrite) t.autoMode = kAutoRead;
    for (int k = 0; k < fx::kCount; ++k) t.fx[static_cast<size_t>(k)] = fx::clampValue(k, t.fx[static_cast<size_t>(k)]);
    for (int p = 0; p < autom::kParamCount; ++p) {
        std::vector<autom::Point> pts = t.autoPts[p].get();
        autom::normalize(p, pts);
        t.autoPts[p] = std::move(pts);
    }
    int k = 0;
    for (EqBandModel &b : t.bands) b.slot = k++;
    applyTrackLocked(t);
    if (t.hasRegions) {
        applyRegionsLocked(t, t.regions);
    } else {
        t.regions = mMixer.regions(slot);
        bump(verRegions(slot));
    }
    t.hasRegions = true;
    bump(verParams(slot));
    bump(verEq(slot));
    bump(verAuto(slot));
    bump(verFx(slot));
    bump(verInst(slot));
    mProject.nextId = std::max(mProject.nextId, t.id + 1);
    mProject.tracks.push_back(std::move(t));
    bump(kVerTracks);
    return "";
}

void Studio::removeAllLocked(bool deleteFiles) {
    for (const TrackModel &t : mProject.tracks) {
        mMixer.takeTrack(t.slot);
        bump(verParams(t.slot));
        bump(verEq(t.slot));
        bump(verRegions(t.slot));
        bump(verAuto(t.slot));
        bump(verFx(t.slot));
        bump(verInst(t.slot));
        if (deleteFiles) std::remove((mAudioDir + "/" + t.file).c_str());
    }
    mProject.tracks.clear();
    mProject.song = SongModel();
    applySongToVoiceLocked();
    mGesture.active = false;
    mAutoGesture.active = false;
    mTouch = AutoTouch();
    mMixer.setAnalysisSource(-1);
    bump(kVerTracks);
    bump(kVerSong);
}

// ---------------------------------------------------------------- pistas

std::string Studio::newFileName(const std::string &prefix) {
    static std::mutex m;
    static std::mt19937_64 rng(std::random_device{}() ^ static_cast<uint64_t>(nowMs()));
    std::lock_guard<std::mutex> lk(m);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s-%016llx%016llx.wav", prefix.c_str(), static_cast<unsigned long long>(rng()),
                  static_cast<unsigned long long>(rng()));
    return buf;
}

bool Studio::importFiles(const std::vector<int> &fds, const std::vector<std::string> &names) {
    if (fds.empty() || !beginJob("Importando…")) {
        for (int fd : fds)
            if (fd >= 0) ::close(fd);
        return false;
    }
    mJobs.post([this, fds, names] { jobImport(fds, names); });
    return true;
}

std::string Studio::importOne(int fd, const std::string &name, const std::string &dst, ImportInfo &info,
                              std::string &codec) {
    if (fd < 0) return "no se pudo abrir";
    struct stat st {};
    bool seekable = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && ::lseek(fd, 0, SEEK_SET) == 0;
    std::string tmp;
    if (!seekable) {
        // Algunas carpetas (p. ej. en la nube) entregan el archivo como un flujo: se copia primero.
        std::string cache;
        {
            std::lock_guard<std::mutex> lk(mModel);
            cache = mCacheDir;
        }
        tmp = cache + "/" + newFileName("entrada");
        const int out = ::open(tmp.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        bool ok = out >= 0;
        std::vector<char> buf(1 << 16);
        while (ok) {
            const ssize_t n = ::read(fd, buf.data(), buf.size());
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                ok = n == 0;
                break;
            }
            ok = ::write(out, buf.data(), static_cast<size_t>(n)) == n;
        }
        if (out >= 0) ::close(out);
        ::close(fd);
        fd = ok ? ::open(tmp.c_str(), O_RDONLY) : -1;
        if (fd < 0 || ::fstat(fd, &st) != 0) {
            if (fd >= 0) ::close(fd);
            std::remove(tmp.c_str());
            return "no se pudo leer el archivo";
        }
    }
    char head[12] = {};
    const bool wav = ::pread(fd, head, 12, 0) == 12 && std::memcmp(head, "RIFF", 4) == 0 &&
                     std::memcmp(head + 8, "WAVE", 4) == 0;
    std::string err;
    if (wav || !mDecoder) {
        FILE *f = ::fdopen(fd, "rb");
        if (!f) {
            ::close(fd);
            err = "no se pudo abrir";
        } else {
            err = importWav(f, dst, kRate, info, &mProgress);  // cierra f
        }
    } else {
        setJobText("Decodificando " + name + "…");
        mProgress.store(0.0f);
        ImportSink sink(dst, kRate);
        err = mDecoder(fd, static_cast<int64_t>(st.st_size), sink, codec, &mProgress);
        ::close(fd);
        if (err.empty()) err = sink.finish(info);
        else sink.abort();
    }
    if (!tmp.empty()) std::remove(tmp.c_str());
    return err;
}

void Studio::jobImport(std::vector<int> fds, std::vector<std::string> names) {
    std::vector<std::string> errors;
    int added = 0;
    std::string audioDir;
    {
        std::lock_guard<std::mutex> lk(mModel);
        audioDir = mAudioDir;
    }
    for (size_t k = 0; k < fds.size(); ++k) {
        const std::string name = k < names.size() ? names[k] : "archivo";
        if (trackCount() >= kMaxTracks) {
            errors.push_back("límite de 12 pistas");
            for (size_t r = k; r < fds.size(); ++r)
                if (fds[r] >= 0) ::close(fds[r]);
            break;
        }
        setJobText("Importando " + name + "…");
        mProgress.store(0.0f);
        const std::string file = newFileName("pista");
        const std::string path = audioDir + "/" + file;
        ImportInfo info;
        std::string codec;
        std::string err = importOne(fds[k], name, path, info, codec);
        if (err.empty()) {
            std::string e2;
            auto audio = MappedAudio::open(path, e2);
            TrackModel t;
            t.name = withoutExtension(name);
            t.file = file;
            t.info = describe(info, codec);
            std::vector<float> peaks;
            if (audio) peaks = computePeaks(*audio, kPeakBucket);
            std::lock_guard<std::mutex> lk(mModel);
            t.id = mProject.nextId++;
            t.peaks = std::move(peaks);
            err = audio ? addTrackLocked(std::move(t), std::move(audio)) : e2;
            if (err.empty()) markDirty();
        }
        if (!err.empty()) {
            std::remove(path.c_str());
            errors.push_back(name + ": " + err);
            continue;
        }
        ++added;
    }
    std::string msg;
    if (errors.empty()) {
        msg = std::to_string(added) + " pista(s) agregada(s)";
    } else {
        msg = added > 0 ? std::to_string(added) + " agregada(s); " : "";
        msg += "problemas: ";
        for (size_t i = 0; i < errors.size(); ++i) msg += (i ? "; " : "") + errors[i];
        msg += ". Suelen funcionar WAV, MP3, AAC/M4A, FLAC, OGG y Opus";
    }
    endJob(msg);
}

bool Studio::loadDemo() {
    if (!beginJob("Creando pistas de prueba…")) return false;
    mJobs.post([this] { jobDemo(); });
    return true;
}

void Studio::jobDemo() {
    // Nombre, paneo y volumen como en el prototipo.
    struct Demo {
        const char *name;
        int pan;
        float vol;
    };
    static const Demo demos[kDemoTracks] = {{"Bajo", 0, 0}, {"Voz", 0, 0}, {"Guitarra", 60, -2}, {"Teclado", -60, -3}};
    std::string audioDir;
    {
        std::lock_guard<std::mutex> lk(mModel);
        audioDir = mAudioDir;
    }
    int n = 0;
    for (int i = 0; i < kDemoTracks; ++i) {
        if (trackCount() >= kMaxTracks) break;
        const std::string file = newFileName("prueba");
        const std::string path = audioDir + "/" + file;
        if (!writeDemoTrack(i, path, kRate).empty()) continue;
        std::string err;
        auto audio = MappedAudio::open(path, err);
        if (!audio) {
            std::remove(path.c_str());
            continue;
        }
        TrackModel t;
        t.name = demos[i].name;
        t.file = file;
        t.info = "prueba · mono · 12 s";
        t.pan = demos[i].pan;
        t.volDb = demos[i].vol;
        t.peaks = computePeaks(*audio, kPeakBucket);
        std::lock_guard<std::mutex> lk(mModel);
        t.id = mProject.nextId++;
        err = addTrackLocked(std::move(t), std::move(audio));
        if (!err.empty()) {
            std::remove(path.c_str());
            continue;
        }
        markDirty();
        ++n;
    }
    endJob(n > 0 ? "Pistas de prueba cargadas" : "Límite de 12 pistas");
}

void Studio::jobRestore() {
    std::string path, audioDir;
    {
        std::lock_guard<std::mutex> lk(mModel);
        path = mSessionPath;
        audioDir = mAudioDir;
    }
    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        text = ss.str();
    }
    std::string err;
    const Json j = Json::parse(text, err);
    ProjectModel pm;
    if (!err.empty() || !sessionFromJson(j, pm)) {
        // Sesión dañada: como la primera vez.
        mRestoring.store(false);
        endJob("");
        loadDemo();
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mModel);
        mProject.master = pm.master;
        mProject.master.volDb = std::clamp(roundTo(pm.master.volDb, 0.5f), kMasterMin, kVolMax);
        mProject.nextId = pm.nextId;
        applyMasterLocked();
        mProject.input = std::move(pm.input);
        mProject.input.id = kInputId;
        mProject.input.name = "Micrófono";
        int k = 0;
        for (EqBandModel &b : mProject.input.bands) b.slot = k++;
        mProject.inputGainDb = pm.inputGainDb;
        mProject.monitorDb = pm.monitorDb;
        mProject.voice = pm.voice;
        applyInputLocked();
    }
    bump(kVerMaster);
    bump(kVerInput);
    int missing = 0;
    std::set<std::string> used;
    {
        // El tempo en uso (para los instrumentos), con la música y la canción de la sesión.
        std::lock_guard<std::mutex> lk(mModel);
        mProject.music = pm.music;
    }
    double bpm;
    {
        std::lock_guard<std::mutex> lk(mModel);
        const MusicModel &m = pm.music;
        bpm = !m.bpmSet && pm.song.ready() && pm.song.bpm >= 40.0f && pm.song.bpm <= 240.0f ? pm.song.bpm : m.bpm;
    }
    for (TrackModel &t : pm.tracks) {
        const std::string file = audioDir + "/" + t.file;
        std::string e;
        auto audio = MappedAudio::open(file, e);
        bool rerendered = false;
        if (!audio && t.isInst) {
            // Un instrumento sin su archivo se vuelve a renderizar (lo que toca está en la sesión).
            std::vector<float> peaks;
            std::string f2;
            if (renderInstrument(t.part, bpm, audioDir, f2, audio, peaks).empty()) {
                rerendered = true;
                t.file = f2;
                t.regions.clear();
                t.hasRegions = false;
            }
        }
        if (!audio) {
            ++missing;
            continue;
        }
        t.peaks = computePeaks(*audio, kPeakBucket);
        t.renderedBpm = bpm;
        // Si la app se cerró antes de que terminara de prepararse, el archivo es de antes: se
        // vuelve a renderizar (lo que toca está en la sesión).
        const bool stale = t.isInst && !rerendered && t.renderedHash != inst::renderHash(t.part, bpm);
        if (rerendered) t.renderedHash = inst::renderHash(t.part, bpm);
        const int id = t.id;
        used.insert(t.file);
        std::lock_guard<std::mutex> lk(mModel);
        if (!addTrackLocked(std::move(t), std::move(audio)).empty()) ++missing;
        else if (stale)
            if (TrackModel *tm = findLocked(id)) scheduleRenderLocked(*tm);
    }
    // Archivos que ya nadie usa (de pistas quitadas o importaciones que no terminaron).
    if (DIR *d = ::opendir(audioDir.c_str())) {
        while (dirent *e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n == "." || n == ".." || used.count(n)) continue;
            std::remove((audioDir + "/" + n).c_str());
        }
        ::closedir(d);
    }
    {
        std::lock_guard<std::mutex> lk(mModel);
        if (pm.song.ready() && findLocked(pm.song.instrumentalId)) {
            mProject.song = std::move(pm.song);
            if (mProject.song.vocalId && !findLocked(mProject.song.vocalId)) mProject.song.vocalId = 0;
        }
        applySongToVoiceLocked();
    }
    bump(kVerSong);
    mRestoring.store(false);
    endJob(missing > 0 ? std::to_string(missing) + " pista(s) no se encontraron" : "Mezcla recuperada");
}

bool Studio::removeTrack(int id) {
    std::string file;
    {
        std::lock_guard<std::mutex> lk(mModel);
        auto it = std::find_if(mProject.tracks.begin(), mProject.tracks.end(), [id](const TrackModel &t) { return t.id == id; });
        if (it == mProject.tracks.end()) return false;
        const int slot = it->slot;
        if (mMixer.analysisSource() == slot) mMixer.setAnalysisSource(-1);
        mMixer.takeTrack(slot);  // con un fundido corto si está sonando
        file = mAudioDir + "/" + it->file;
        mProject.tracks.erase(it);
        if (id == mProject.song.instrumentalId) mProject.song = SongModel();
        if (id == mProject.song.vocalId) mProject.song.vocalId = 0;
        if (id == mProject.song.sourceId) mProject.song.sourceId = 0;
        applySongToVoiceLocked();
        bump(kVerSong);
        if (mGesture.active && mGesture.id == id) mGesture.active = false;
        if (mAutoGesture.active && mAutoGesture.id == id) mAutoGesture.active = false;
        if (mTouch.active && mTouch.id == id) mTouch = AutoTouch();
        if (mProject.tracks.empty()) mMixer.stop();
        if (mRec.id == id) mRec = NoteRec();
        if (mLoopId == id) {
            mLoopId = 0;
            mMixer.setLoop(0, 0);
        }
        mIdeas.erase(id);
        bump(verInst(slot));
        bump(kVerMusic);
        bump(kVerTracks);
        bump(verParams(slot));
        bump(verEq(slot));
        bump(verRegions(slot));
        bump(verAuto(slot));
        bump(verFx(slot));
    }
    std::remove(file.c_str());
    {
        std::lock_guard<std::mutex> lk(mSgMutex);
        mSgCache.erase(id);
    }
    markDirty();
    setMessage("");
    return true;
}

void Studio::setVolume(int id, float db) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t) return;
    const float before = staticValueLocked(*t, autom::kVolume);
    setStaticLocked(*t, autom::kVolume, db);
    // Lo que se graba va con más detalle que el valor fijo (0.1 dB en vez de 0.5).
    touchValueLocked(*t, autom::kVolume, before, autom::clampValue(autom::kVolume, db));
}

void Studio::setPan(int id, float pan) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t) return;
    const float before = staticValueLocked(*t, autom::kPan);
    setStaticLocked(*t, autom::kPan, pan);
    touchValueLocked(*t, autom::kPan, before, static_cast<float>(t->pan));
}

float Studio::staticValueLocked(const TrackModel &t, int param) const {
    return param == autom::kPan ? static_cast<float>(t.pan) : t.volDb;
}

void Studio::setStaticLocked(TrackModel &t, int param, float value) {
    if (param == autom::kPan) {
        t.pan = static_cast<int>(std::lround(std::clamp(std::isfinite(value) ? value : 0.0f, -100.0f, 100.0f)));
        mMixer.setTrackPan(t.slot, static_cast<float>(t.pan) / 100.0f);
    } else {
        t.volDb = std::clamp(roundTo(std::isfinite(value) ? value : 0.0f, 0.5f), kVolMin, kVolMax);
        mMixer.setTrackGain(t.slot, dbToGain(t.volDb, kVolMin + 0.1f));
    }
    bump(verParams(t.slot));
    markDirty();
}

void Studio::setMute(int id, bool on) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t) return;
    t->mute = on;
    mMixer.setTrackMute(t->slot, on);
    bump(verParams(t->slot));
    markDirty();
}

void Studio::setSolo(int id, bool on) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t) return;
    t->solo = on;
    mMixer.setTrackSolo(t->slot, on);
    bump(verParams(t->slot));
    markDirty();
}

void Studio::setPhase(int id, bool on) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t) return;
    t->phase = on;
    mMixer.setTrackInvert(t->slot, on);
    bump(verParams(t->slot));
    markDirty();
}

// ---------------------------------------------------------------- EQ

int Studio::addEqBand(int id) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = eqTargetLocked(id);
    if (!t || static_cast<int>(t->bands.size()) >= kMaxBands) return -1;
    int slot = 0;
    while (std::any_of(t->bands.begin(), t->bands.end(), [slot](const EqBandModel &b) { return b.slot == slot; })) ++slot;
    EqBandModel b;
    b.slot = slot;
    b.type = kPeaking;
    b.freq = freeFrequency(t->bands);
    b.gain = 0.0f;
    b.q = 1.4f;
    b.on = true;
    t->bands.push_back(b);
    applyEqLocked(*t);
    bump(eqVersion(*t));
    markDirty();
    return static_cast<int>(t->bands.size()) - 1;
}

void Studio::removeEqBand(int id, int pos) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = eqTargetLocked(id);
    if (!t || pos < 0 || pos >= static_cast<int>(t->bands.size())) return;
    t->bands.erase(t->bands.begin() + pos);
    applyEqLocked(*t);
    bump(eqVersion(*t));
    markDirty();
}

void Studio::setEqBand(int id, int pos, int type, float freq, float gain, float q, bool on) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = eqTargetLocked(id);
    if (!t || pos < 0 || pos >= static_cast<int>(t->bands.size())) return;
    EqBandModel &b = t->bands[pos];
    b.type = std::clamp(type, 0, kFilterTypes - 1);
    b.freq = roundTo(std::clamp(freq, 20.0f, 20000.0f), 0.1f);
    b.gain = roundTo(std::clamp(gain, -15.0f, 15.0f), 0.5f);
    b.q = roundTo(std::clamp(q, 0.1f, 18.0f), 0.01f);
    b.on = on;
    if (Equalizer *eq = eqOf(*t)) eq->setBand(b.slot, true, b.on, b.type, b.freq, b.gain, b.q);
    bump(eqVersion(*t));
    markDirty();
}

void Studio::setEqBypass(int id, bool on) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = eqTargetLocked(id);
    if (!t) return;
    t->eqBypass = on;
    if (Equalizer *eq = eqOf(*t)) eq->setBypass(on);
    bump(eqVersion(*t));
    markDirty();
}

void Studio::setEqAutoGain(int id, bool on) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = eqTargetLocked(id);
    if (!t) return;
    t->eqAuto = on;
    if (Equalizer *eq = eqOf(*t)) eq->setAutoGain(on);
    bump(eqVersion(*t));
    markDirty();
}

void Studio::resetEq(int id) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = eqTargetLocked(id);
    if (!t) return;
    t->bands.clear();
    applyEqLocked(*t);
    bump(eqVersion(*t));
    markDirty();
}

float Studio::eqTrimDb(int id) const {
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = eqTargetLocked(id);
    if (!t) return 0.0f;
    const Equalizer *eq = eqOf(*t);
    return eq ? eq->trimDb() : 0.0f;
}

void Studio::eqResponse(int id, int pos, const float *freqs, float *outDb, int n) const {
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = eqTargetLocked(id);
    const Equalizer *eq = t ? eqOf(*t) : nullptr;
    if (!eq || (pos >= static_cast<int>(t->bands.size()))) {
        std::fill(outDb, outDb + n, 0.0f);
        return;
    }
    eq->response(pos < 0 ? -1 : t->bands[pos].slot, freqs, outDb, n);
}

// ---------------------------------------------------------------- efectos

void Studio::setFx(int id, int param, float value) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || param < 0 || param >= fx::kCount) return;
    t->fx[static_cast<size_t>(param)] = fx::clampValue(param, value);
    applyFxLocked(*t);
    bump(verFx(t->slot));
    markDirty();
}

std::vector<float> Studio::fxState(int id) const {
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = findLocked(id);
    return t ? std::vector<float>(t->fx.begin(), t->fx.end()) : std::vector<float>();
}

std::vector<float> Studio::fxInfo() {
    std::vector<float> v;
    for (int k = 0; k < fx::kCount; ++k) {
        const fx::Info &i = fx::info(k);
        v.insert(v.end(), {i.min, i.max, i.def, i.log ? 1.0f : 0.0f});
    }
    return v;
}

std::string Studio::fxPresetsJson() {
    Json a = Json::array();
    for (int i = 0; i < fx::presetCount(); ++i) a.push(std::string(fx::preset(i).name));
    return a.dump();
}

bool Studio::applyFxPreset(int id, int index) {
    if (index < 0 || index >= fx::presetCount()) return false;
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t) return false;
    t->fx = fx::preset(index).values;
    applyFxLocked(*t);
    bump(verFx(t->slot));
    markDirty();
    return true;
}

// ---------------------------------------------------------------- master y exportar

void Studio::setMasterDb(float db) {
    std::lock_guard<std::mutex> lk(mModel);
    mProject.master.volDb = std::clamp(roundTo(db, 0.5f), kMasterMin, kVolMax);
    applyMasterLocked();
    bump(kVerMaster);
    markDirty();
}

void Studio::setMasterMute(bool on) {
    std::lock_guard<std::mutex> lk(mModel);
    mProject.master.mute = on;
    applyMasterLocked();
    bump(kVerMaster);
    markDirty();
}

void Studio::setLimiter(bool on) {
    std::lock_guard<std::mutex> lk(mModel);
    mProject.master.limiter = on;
    applyMasterLocked();
    bump(kVerMaster);
    markDirty();
}

void Studio::setNotch(int hz) {
    std::lock_guard<std::mutex> lk(mModel);
    mProject.master.notch = hz == 50 || hz == 60 ? hz : 0;
    applyMasterLocked();
    bump(kVerMaster);
    markDirty();
}

void Studio::setExportFormat(int bits, int rate) {
    std::lock_guard<std::mutex> lk(mModel);
    mProject.master.exportBits = bits == 16 || bits == 32 ? bits : 24;
    mProject.master.exportRate = rate == 44100 ? 44100 : 48000;
    bump(kVerMaster);
    markDirty();
}

int64_t Studio::exportBytes() const {
    ExportFormat f;
    {
        std::lock_guard<std::mutex> lk(mModel);
        f.bits = mProject.master.exportBits;
        f.isFloat = f.bits == 32;
        f.sampleRate = mProject.master.exportRate;
    }
    return ::exportBytes(mMixer.length(), f);
}

bool Studio::exportMix(int fd, const std::string &name) {
    if (trackCount() == 0 || !beginJob("Exportando la mezcla…", true)) {
        if (fd >= 0) ::close(fd);
        return false;
    }
    mProgress.store(0.0f);
    mCancelExport.store(false);
    ExportFormat f;
    {
        std::lock_guard<std::mutex> lk(mModel);
        f.bits = mProject.master.exportBits;
        f.isFloat = f.bits == 32;
        f.sampleRate = mProject.master.exportRate;
        if (mProject.master.exportNormalize) f.targetLufs = static_cast<float>(mProject.master.loudTarget);
    }
    if (f.targetLufs < 0.0f) setJobText("Midiendo y exportando la mezcla…");
    mJobs.post([this, fd, name, f] {
        mInstJobs.waitIdle(60000);  // los instrumentos que se estaban preparando, ya listos
        const ExportResult r = ::exportMix(mMixer, fd, f, &mProgress, &mCancelExport);
        const bool ok = r.error.empty();
        std::string msg;
        if (ok) {
            const long long s = r.frames / f.sampleRate;
            char buf[160];
            std::snprintf(buf, sizeof buf, " (%s · %lld:%02lld · %.1f MB", formatText(f.bits, f.sampleRate).c_str(),
                          s / 60, s % 60, static_cast<double>(r.bytes) / 1048576.0);
            msg = "Mezcla exportada: " + name + buf;
            if (r.lufs > kLoudnessNone) msg += " · " + signed1(r.lufs) + " LUFS";
            if (r.truePeak > kLoudnessNone) msg += " · pico real " + signed1(r.truePeak) + " dBTP";
            msg += ")";
        } else {
            msg = "No se exportó: " + r.error;
        }
        {
            std::lock_guard<std::mutex> lk(mStatus);
            ++mExportCount;
            mLastExportOk = ok;
        }
        endJob(msg);
    });
    return true;
}

// ---------------------------------------------------------------- transporte

void Studio::play() {
    if (trackCount() == 0) return;
    // Desde el inicio de la canción: medición nueva del volumen percibido.
    if (!mMixer.isPlaying() && mMixer.position() == 0) resetLoudness();
    mMixer.play();
}

void Studio::pause() { mMixer.pause(); }

void Studio::playPause() {
    if (mMixer.isPlaying()) mMixer.pause();
    else play();
}

void Studio::stop() { mMixer.stop(); }

void Studio::seek(double seconds) {
    const double s = std::clamp(seconds, 0.0, duration());
    mMixer.seek(static_cast<int64_t>(std::llround(s * kRate)));
}

// ---------------------------------------------------------------- medidores y análisis

void Studio::meters(int64_t now, float *out) {
    float raw[kMeterCount], in[2];
    mMixer.readMeters(raw);
    mInput.readPeaks(in);
    mMeters.update(raw, now, mInput.active() ? in : nullptr);
    mMeters.read(out);
}

void Studio::setAnalysisSource(int id) {
    int slot = -1;
    if (id == kInputId) {
        slot = Mixer::kTapInput;
    } else if (id > 0) {
        std::lock_guard<std::mutex> lk(mModel);
        if (const TrackModel *t = findLocked(id)) slot = t->slot;
    }
    if (mMixer.analysisSource() != slot) {
        mMixer.setAnalysisSource(slot);
        mAnalyzer.reset(mMixer);  // que no se cuele lo de la fuente anterior
    }
}

bool Studio::analysisUpdate(int64_t now, bool classic, float *stats) {
    // La entrada se analiza aunque la mezcla no suene.
    const bool live = mMixer.analysisSource() == Mixer::kTapInput && mInput.active();
    const bool changed = mView.update(mAnalyzer, mMixer, mMixer.isPlaying() || live, now, classic);
    mView.stats(stats);
    return changed;
}

void Studio::spectrumColumns(float *out, int cols) const {
    if (!mView.active()) {
        std::fill(out, out + cols, -200.0f);
        return;
    }
    AnalysisView::spectrumColumns(mAnalyzer, out, cols);
}

bool Studio::spectrogram(int id, std::vector<uint8_t> &out) {
    std::string path;
    {
        std::lock_guard<std::mutex> lk(mModel);
        const TrackModel *t = findLocked(id);
        if (!t) return false;
        path = mAudioDir + "/" + t->file;
    }
    {
        std::lock_guard<std::mutex> lk(mSgMutex);
        auto it = mSgCache.find(id);
        if (it != mSgCache.end()) {
            out = it->second;
            return true;
        }
        if (!mSgPending.insert(id).second) return false;  // ya se está calculando
    }
    mBackground.post([this, id, path] {
        std::string err;
        std::vector<uint8_t> data;
        if (auto a = MappedAudio::open(path, err)) data = ::spectrogram(*a, kSpectrogramCols, kSpectrogramRows, nullptr);
        bool exists;
        {
            std::lock_guard<std::mutex> lk(mModel);
            exists = findLocked(id) != nullptr;
        }
        {
            std::lock_guard<std::mutex> lk(mSgMutex);
            mSgPending.erase(id);
            if (exists && !data.empty()) mSgCache[id] = std::move(data);
        }
        bump(kVerBackground);
    });
    return false;
}

// ---------------------------------------------------------------- entrada en vivo y grabación

void Studio::inputOpened(int channels) {
    mInput.setActive(true, channels);
    bump(kVerInput);
}

void Studio::inputClosed() {
    if (mInput.recording()) recordStop();  // lo grabado no se pierde
    mInput.setActive(false, mInput.channels());
    bump(kVerInput);
}

void Studio::setLatencies(int inputFrames, int outputFrames) {
    mLatIn.store(inputFrames);
    mLatOut.store(outputFrames);
}

void Studio::setInputGain(float db) {
    {
        std::lock_guard<std::mutex> lk(mModel);
        mProject.inputGainDb = db;
        applyInputLocked();
    }
    bump(kVerInput);
    markDirty();
}

void Studio::setMonitor(bool on) {
    mInput.setMonitor(on);
    bump(kVerInput);
}

void Studio::setMonitorDb(float db) {
    {
        std::lock_guard<std::mutex> lk(mModel);
        mProject.monitorDb = db;
        applyInputLocked();
    }
    bump(kVerInput);
    markDirty();
}

void Studio::inputState(float *out) const {
    float gain, monitorDb;
    {
        std::lock_guard<std::mutex> lk(mModel);
        gain = mProject.inputGainDb;
        monitorDb = mProject.monitorDb;
    }
    const int li = mLatIn.load(), lo = mLatOut.load();
    out[0] = mInput.active() ? 1.0f : 0.0f;
    out[1] = static_cast<float>(mInput.channels());
    out[2] = gain;
    out[3] = mInput.monitorOn() ? 1.0f : 0.0f;
    out[4] = monitorDb;
    out[5] = li >= 0 && lo >= 0 ? (li + lo) * 1000.0f / kRate : -1.0f;
    out[6] = mInput.recording() ? 1.0f : 0.0f;
    out[7] = static_cast<float>(mInput.recordedSeconds());
}

// ---------------------------------------------------------------- presentación

std::string Studio::showPath() const { return mFilesDir + "/presentacion.wav"; }

bool Studio::showStart() {
    if (mShow.recording()) return true;
    if (mFilesDir.empty()) return false;
    std::string err;
    if (!mShow.start(showPath(), err)) {
        setMessage("No se pudo grabar la presentación: " + err);
        return false;
    }
    mShowSeconds.store(0.0);
    bump(kVerInput);
    return true;
}

bool Studio::showStop() {
    if (!mShow.recording()) return false;
    const ShowRecorder::Result r = mShow.stop();
    mShowSeconds.store(r.error.empty() ? static_cast<double>(r.frames) / kRate : 0.0);
    if (!r.error.empty()) {
        std::remove(r.path.c_str());
        setMessage("No se pudo grabar la presentación: " + r.error);
    } else {
        const long long s = r.frames / kRate;
        char buf[96];
        std::snprintf(buf, sizeof buf, "Presentación grabada (%lld:%02lld)", s / 60, s % 60);
        std::string msg = buf;
        if (r.dropped > 0) msg += ": el disco no alcanzó en algunos momentos";
        setMessage(msg);
    }
    bump(kVerInput);
    return r.error.empty();
}

void Studio::showState(float *out) const {
    out[0] = mShow.recording() ? 1.0f : 0.0f;
    out[1] = static_cast<float>(mShow.seconds());
    const double saved = mShowSeconds.load();
    out[2] = !mShow.recording() && saved > 0.0 ? 1.0f : 0.0f;
    out[3] = static_cast<float>(saved);
}

bool Studio::saveShow(int fd, const std::string &name) {
    if (mShow.recording() || mShowSeconds.load() <= 0.0 || !beginJob("Guardando la presentación…", true)) {
        if (fd >= 0) ::close(fd);
        return false;
    }
    mProgress.store(0.0f);
    mCancelExport.store(false);
    ExportFormat f;
    {
        std::lock_guard<std::mutex> lk(mModel);
        f.bits = mProject.master.exportBits;
        f.isFloat = f.bits == 32;
        f.sampleRate = mProject.master.exportRate;
    }
    const std::string path = showPath();
    mJobs.post([this, fd, name, f, path] {
        const ExportResult r = ::exportRecording(path, fd, f, &mProgress, &mCancelExport);
        const bool ok = r.error.empty();
        std::string msg;
        if (ok) {
            const long long s = r.frames / f.sampleRate;
            char buf[160];
            std::snprintf(buf, sizeof buf, " (%s · %lld:%02lld · %.1f MB", formatText(f.bits, f.sampleRate).c_str(),
                          s / 60, s % 60, static_cast<double>(r.bytes) / 1048576.0);
            msg = "Presentación guardada: " + name + buf;
            if (r.lufs > kLoudnessNone) msg += " · " + signed1(r.lufs) + " LUFS";
            msg += ")";
        } else {
            msg = "No se guardó la presentación: " + r.error;
        }
        {
            std::lock_guard<std::mutex> lk(mStatus);
            ++mExportCount;
            mLastExportOk = ok;
        }
        endJob(msg);
    });
    return true;
}

// ---------------------------------------------------------------- voz en vivo

void Studio::setVoiceFx(int param, float value) {
    if (param < 0 || param >= vfx::kCount) return;
    {
        std::lock_guard<std::mutex> lk(mModel);
        mProject.voice[static_cast<size_t>(param)] = vfx::clampValue(param, value);
        mInput.voice().set(param, mProject.voice[static_cast<size_t>(param)]);
    }
    bump(kVerInput);
    markDirty();
}

std::vector<float> Studio::voiceFxState() const {
    std::lock_guard<std::mutex> lk(mModel);
    return std::vector<float>(mProject.voice.begin(), mProject.voice.end());
}

std::vector<float> Studio::voiceFxInfo() {
    std::vector<float> v;
    for (int k = 0; k < vfx::kCount; ++k) {
        const vfx::Info &i = vfx::info(k);
        v.insert(v.end(), {i.min, i.max, i.def, i.log ? 1.0f : 0.0f});
    }
    return v;
}

std::string Studio::voicePresetsJson() {
    Json a = Json::array();
    for (int i = 0; i < vfx::presetCount(); ++i) a.push(std::string(vfx::preset(i).name));
    return a.dump();
}

bool Studio::applyVoicePreset(int index) {
    if (index < 0 || index >= vfx::presetCount()) return false;
    {
        std::lock_guard<std::mutex> lk(mModel);
        const vfx::Values &v = vfx::preset(index).values;
        for (int p = 0; p < vfx::kCount; ++p) {
            // Lo que es de la persona y de la canción se queda como estaba.
            if (p == vfx::kTuneKey || p == vfx::kTuneScale || p == vfx::kRange || p == vfx::kLevel) continue;
            mProject.voice[static_cast<size_t>(p)] = v[static_cast<size_t>(p)];
            mInput.voice().set(p, v[static_cast<size_t>(p)]);
        }
    }
    bump(kVerInput);
    markDirty();
    return true;
}

bool Studio::recordStart() {
    if (mInput.recording()) return true;
    if (!mInput.active()) {
        setMessage("Activa la entrada para grabar");
        return false;
    }
    bool busy;
    {
        std::lock_guard<std::mutex> lk(mStatus);
        busy = mBusy;
    }
    if (busy) {
        setMessage("Espera a que termine el trabajo en curso");
        return false;
    }
    std::string path;
    bool full;
    {
        std::lock_guard<std::mutex> lk(mModel);
        full = mProject.tracks.size() >= static_cast<size_t>(kMaxTracks);
        if (!full && !mAudioDir.empty()) path = mAudioDir + "/" + newFileName("grabacion");
        mRecWithMix = !mProject.tracks.empty();
        mRecFrom = mMixer.position();
    }
    if (full || path.empty()) {
        setMessage(full ? "Límite de 12 pistas: quita una para grabar" : "No hay proyecto abierto");
        return false;
    }
    std::string err;
    mMixer.setFreeRun(true);
    if (!mInput.startRecording(path, err)) {
        mMixer.setFreeRun(false);
        setMessage("No se pudo grabar: " + err);
        return false;
    }
    mMixer.play();
    bump(kVerInput);
    setMessage("Grabando…");
    return true;
}

int Studio::recordStop() {
    if (!mInput.recording()) return -1;
    const Take take = mInput.stopRecording();
    mMixer.pause();
    mMixer.setFreeRun(false);
    bump(kVerInput);
    auto fail = [&](const std::string &m) {
        std::remove(take.path.c_str());
        setMessage(m);
        return -1;
    };
    if (!take.error.empty()) return fail("No se guardó la grabación: " + take.error);
    if (take.frames < kRate / 10) return fail("Grabación muy corta: no se guardó");
    std::string err;
    auto audio = MappedAudio::open(take.path, err);
    if (!audio) return fail("No se guardó la grabación: " + err);
    std::vector<float> peaks = computePeaks(*audio, kPeakBucket);
    // Lo que entra por el micrófono llega tarde respecto a lo que se oye: el retraso de la
    // salida más el de la entrada. Si no se conocen, ~10 ms cada uno.
    const int li = mLatIn.load(), lo = mLatOut.load();
    const int64_t latency = (li >= 0 ? li : kRate / 100) + (lo >= 0 ? lo : kRate / 100);
    const size_t slash = take.path.find_last_of('/');
    const std::string file = slash == std::string::npos ? take.path : take.path.substr(slash + 1);
    int id = -1;
    std::string name;
    {
        std::lock_guard<std::mutex> lk(mModel);
        // Sin pistas no hay con qué alinear: la toma empieza donde estaba el cursor.
        int64_t start = mRecWithMix ? take.start - latency : mRecFrom, src = 0;
        if (start < 0) {
            src = std::min(-start, take.frames - 1);
            start = 0;
        }
        int n = 1;
        for (const TrackModel &t : mProject.tracks)
            if (t.name.rfind("Grabación", 0) == 0) ++n;
        TrackModel t;
        t.id = mProject.nextId++;
        t.name = "Grabación " + std::to_string(n);
        t.file = file;
        const int secs = static_cast<int>(take.frames / kRate);
        t.info = std::string("grabación · ") + (take.channels == 2 ? "estéreo" : "mono") + " · 48 kHz · " +
                 std::to_string(secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") + std::to_string(secs % 60);
        // Suena como se escuchó: la pista recibe el EQ de la entrada (y se puede cambiar).
        t.bands = mProject.input.bands;
        t.eqBypass = mProject.input.eqBypass;
        t.eqAuto = mProject.input.eqAuto;
        t.regions = {Region{start, src, take.frames - src, 0, 0}};
        t.hasRegions = true;
        t.peaks = std::move(peaks);
        id = t.id;
        name = t.name;
        err = addTrackLocked(std::move(t), std::move(audio));
    }
    if (!err.empty()) return fail("No se guardó la grabación: " + err);
    markDirty();
    std::string msg = name + " agregada";
    if (take.dropped > 0)
        msg += " (se perdieron " + std::to_string(take.dropped * 1000 / kRate) + " ms: el celular no alcanzó a guardar)";
    setMessage(msg);
    return id;
}

void Studio::recordCancel() {
    if (!mInput.recording()) return;
    const Take take = mInput.stopRecording();
    mMixer.pause();
    mMixer.setFreeRun(false);
    std::remove(take.path.c_str());
    bump(kVerInput);
    setMessage("Grabación descartada");
}

// ---------------------------------------------------------------- forma de onda

std::vector<float> Studio::regionWave(int id, int index, int64_t from, int64_t to, int cols) const {
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = findLocked(id);
    if (!t || cols <= 0 || index < 0 || index >= static_cast<int>(t->regions.size()) || t->peaks.empty()) return {};
    const Region &r = t->regions[index];
    from = std::max(from, r.start);
    to = std::min(to, r.end());
    if (to <= from) return {};
    const int ch = t->channels;
    const std::vector<float> &p = t->peaks;
    const int64_t buckets = static_cast<int64_t>(p.size()) / (ch * 2);
    const int64_t src0 = r.src + (from - r.start), src1 = r.src + (to - r.start);
    const int64_t first = std::clamp<int64_t>(src0 / kPeakBucket, 0, buckets);
    const int64_t last = std::clamp<int64_t>((src1 + kPeakBucket - 1) / kPeakBucket, first, buckets);
    const int64_t span = last - first;
    std::vector<float> out(static_cast<size_t>(cols) * ch * 2, 0.0f);
    if (span <= 0) return out;
    const double perCol = static_cast<double>(to - from) / cols;
    for (int col = 0; col < cols; ++col) {
        int64_t b0 = first + span * col / cols, b1 = first + span * (col + 1) / cols;
        if (b1 <= b0) b1 = std::min(last, b0 + 1);
        const float g = static_cast<float>(fadeGain(r, static_cast<double>(from - r.start) + (col + 0.5) * perCol));
        for (int c = 0; c < ch; ++c) {
            float mn = 0.0f, mx = 0.0f;
            for (int64_t b = b0; b < b1; ++b) {
                mn = std::min(mn, p[(b * ch + c) * 2]);
                mx = std::max(mx, p[(b * ch + c) * 2 + 1]);
            }
            out[(static_cast<size_t>(col) * ch + c) * 2] = mn * g;
            out[(static_cast<size_t>(col) * ch + c) * 2 + 1] = mx * g;
        }
    }
    return out;
}

// ---------------------------------------------------------------- edición e historial

Studio::Snapshot Studio::snapshotLocked() const {
    Snapshot s;
    for (const TrackModel &t : mProject.tracks) {
        TrackSnap ts;
        ts.id = t.id;
        ts.regions = t.regions;
        for (int k = 0; k < autom::kParamCount; ++k) ts.autos[k] = t.autoPts[k];
        if (t.isInst) ts.pattern = t.part.pattern;
        s.push_back(std::move(ts));
    }
    return s;
}

void Studio::restoreLocked(const Snapshot &s) {
    for (const TrackSnap &ts : s) {
        TrackModel *t = findLocked(ts.id);
        if (!t) continue;
        if (t->regions != ts.regions) applyRegionsLocked(*t, ts.regions);
        if (t->isInst && t->part.pattern != ts.pattern) {
            t->part.pattern = ts.pattern;
            scheduleRenderLocked(*t);
        }
        for (int k = 0; k < autom::kParamCount; ++k) {
            if (t->autoPts[k] == ts.autos[k]) continue;
            t->autoPts[k] = ts.autos[k];
            applyAutoLocked(*t, k);
        }
    }
}

void Studio::pushUndoLocked(Snapshot before) {
    mUndo.push_back(std::move(before));
    while (static_cast<int>(mUndo.size()) > kMaxHistory) mUndo.pop_front();
    mRedo.clear();
    bump(kVerHistory);
}

void Studio::changeRegionsLocked(TrackModel &t, std::vector<Region> list, const std::string &key) {
    if (list == t.regions) return;
    const int64_t now = nowMs();
    const bool together = !key.empty() && key == mLastKey && now - mLastKeyMs < 1500;
    mLastKey = key;
    mLastKeyMs = now;
    // Durante un arrastre, el estado de antes ya se guardó al empezar; cambios seguidos de lo
    // mismo (un deslizador, los toques de mover) cuentan como uno solo.
    if (!mGesture.active && !together) pushUndoLocked(snapshotLocked());
    applyRegionsLocked(t, list);
    markDirty();
}

std::vector<int64_t> Studio::snapPointsLocked(int id, int index) const {
    std::vector<int64_t> pts = {0, mMixer.position()};
    for (const TrackModel &t : mProject.tracks) {
        for (size_t i = 0; i < t.regions.size(); ++i) {
            if (t.id == id && static_cast<int>(i) == index) continue;
            pts.push_back(t.regions[i].start);
            pts.push_back(t.regions[i].end());
        }
    }
    return pts;
}

bool Studio::editBegin(int id, int index, int mode) {
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = findLocked(id);
    if (!t || index < 0 || index >= static_cast<int>(t->regions.size()) || mode < kEditMove || mode > kEditFadeOut)
        return false;
    mGesture.active = true;
    mGesture.id = id;
    mGesture.index = index;
    mGesture.mode = mode;
    mGesture.base = t->regions[index];
    mGesture.points = snapPointsLocked(id, index);
    mGesture.before = snapshotLocked();
    mLastKey.clear();
    return true;
}

void Studio::editDrag(int64_t delta, int64_t snap) {
    std::lock_guard<std::mutex> lk(mModel);
    if (!mGesture.active) return;
    TrackModel *t = findLocked(mGesture.id);
    if (!t || mGesture.index >= static_cast<int>(t->regions.size())) return;
    const Region &b = mGesture.base;
    Region r = b;
    switch (mGesture.mode) {
        case kEditMove: {
            const int64_t n = b.start + delta;
            r = edit::move(b, n + edit::snapOffset({n, n + b.length}, mGesture.points, snap));
            break;
        }
        case kEditTrimStart: {
            const int64_t n = b.start + delta;
            r = edit::trimStart(b, n + edit::snapOffset({n}, mGesture.points, snap));
            break;
        }
        case kEditTrimEnd: {
            const int64_t n = b.end() + delta;
            r = edit::trimEnd(b, n + edit::snapOffset({n}, mGesture.points, snap), t->frames);
            break;
        }
        case kEditFadeIn: r = edit::setFadeIn(b, b.fadeIn + delta); break;
        case kEditFadeOut: r = edit::setFadeOut(b, b.fadeOut - delta); break;
        default: break;
    }
    std::vector<Region> list = t->regions;
    list[mGesture.index] = r;
    changeRegionsLocked(*t, std::move(list), "");
}

void Studio::editEnd() {
    std::lock_guard<std::mutex> lk(mModel);
    if (!mGesture.active) return;
    mGesture.active = false;
    if (mGesture.before != snapshotLocked()) pushUndoLocked(std::move(mGesture.before));
    mGesture.before.clear();
}

int Studio::cut(int id, int index) {
    std::lock_guard<std::mutex> lk(mModel);
    const int64_t at = mMixer.position();
    Snapshot before = snapshotLocked();
    int result = 0;
    if (id >= 0) {
        TrackModel *t = findLocked(id);
        if (!t || index < 0 || index >= static_cast<int>(t->regions.size()) ||
            static_cast<int>(t->regions.size()) >= kMaxRegions)
            return -1;
        Region a, b;
        if (!edit::cut(t->regions[index], at, a, b)) return -1;
        std::vector<Region> list = t->regions;
        list[index] = a;
        list.push_back(b);
        applyRegionsLocked(*t, list);
        result = static_cast<int>(t->regions.size()) - 1;
    } else {
        for (TrackModel &t : mProject.tracks) {
            std::vector<Region> list = t.regions;
            const size_t n = list.size();
            for (size_t i = 0; i < n && list.size() < static_cast<size_t>(kMaxRegions); ++i) {
                Region a, b;
                if (list[i].start < at && list[i].end() > at && edit::cut(list[i], at, a, b)) {
                    list[i] = a;
                    list.push_back(b);
                    ++result;
                }
            }
            if (list != t.regions) applyRegionsLocked(t, list);
        }
        if (result == 0) return 0;
    }
    mLastKey.clear();
    pushUndoLocked(std::move(before));
    markDirty();
    return result;
}

bool Studio::deleteRegion(int id, int index) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || index < 0 || index >= static_cast<int>(t->regions.size())) return false;
    std::vector<Region> list = t->regions;
    list.erase(list.begin() + index);
    mLastKey.clear();
    changeRegionsLocked(*t, std::move(list), "");
    return true;
}

void Studio::nudgeRegion(int id, int index, int ms) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || index < 0 || index >= static_cast<int>(t->regions.size())) return;
    std::vector<Region> list = t->regions;
    list[index] = edit::move(list[index], list[index].start + static_cast<int64_t>(ms) * (kRate / 1000));
    changeRegionsLocked(*t, std::move(list), "empujar-" + std::to_string(id) + "-" + std::to_string(index));
}

void Studio::setRegionFade(int id, int index, bool fadeIn, int64_t frames) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || index < 0 || index >= static_cast<int>(t->regions.size())) return;
    std::vector<Region> list = t->regions;
    list[index] = fadeIn ? edit::setFadeIn(list[index], frames) : edit::setFadeOut(list[index], frames);
    changeRegionsLocked(*t, std::move(list),
                        std::string(fadeIn ? "fi-" : "fo-") + std::to_string(id) + "-" + std::to_string(index));
}

void Studio::restoreRegions(int id) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t) return;
    Region whole;
    whole.length = t->frames;
    mLastKey.clear();
    changeRegionsLocked(*t, {whole}, "");
}

bool Studio::undo() {
    std::lock_guard<std::mutex> lk(mModel);
    if (mUndo.empty() || mGesture.active || mAutoGesture.active || mTouch.active) return false;
    mRedo.push_back(snapshotLocked());
    restoreLocked(mUndo.back());
    mUndo.pop_back();
    mLastKey.clear();
    bump(kVerHistory);
    markDirty();
    return true;
}

bool Studio::redo() {
    std::lock_guard<std::mutex> lk(mModel);
    if (mRedo.empty() || mGesture.active || mAutoGesture.active || mTouch.active) return false;
    mUndo.push_back(snapshotLocked());
    restoreLocked(mRedo.back());
    mRedo.pop_back();
    mLastKey.clear();
    bump(kVerHistory);
    markDirty();
    return true;
}

// ---------------------------------------------------------------- automatización

namespace {
// Al simplificar lo grabado (escala del carril): el cursor avanza a saltos (lo que pide la salida
// de audio cada vez), así que lo grabado trae un poco de temblor que no vale la pena guardar.
constexpr float kAutoTolerance = 0.008f;
constexpr int64_t kCursorNear = Studio::kRate / 100;  // 10 ms: "el punto del cursor"
constexpr int64_t kStillMs = 250;                     // el dedo quieto este tiempo: el valor se quedó
}  // namespace

bool Studio::automatedLocked(const TrackModel &t, int param) const {
    return t.autoMode != kAutoOff && !t.autoPts[param].empty();
}

void Studio::applyAutoLocked(TrackModel &t, int param) {
    std::vector<autom::PosPoint> pts;
    if (t.autoMode != kAutoOff) pts = autom::toPosPoints(param, t.autoPts[param].get());
    mMixer.setAutomation(t.slot, param, std::move(pts));
    bump(verAuto(t.slot));
}

void Studio::changeAutoLocked(TrackModel &t, int param, std::vector<autom::Point> pts, bool undo) {
    autom::normalize(param, pts);
    if (pts == t.autoPts[param].get()) return;
    if (undo) pushUndoLocked(snapshotLocked());
    mLastKey.clear();
    t.autoPts[param] = std::move(pts);
    applyAutoLocked(t, param);
    markDirty();
}

float Studio::lanePosToValue(int param, float pos) const {
    float v = autom::clampValue(param, autom::fromPos(param, pos));
    // Un poco de imán en el centro del paneo y en 0 dB: es difícil atinarles con el dedo.
    if (param == autom::kPan && std::fabs(v) <= 2.0f) v = 0.0f;
    if (param == autom::kVolume && std::fabs(v) <= 0.3f) v = 0.0f;
    return v;
}

void Studio::setAutoMode(int id, int mode) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || mode < kAutoOff || mode > kAutoWrite || t->autoMode == mode) return;
    if (mTouch.active && mTouch.id == id) endTouchLocked();
    t->autoMode = mode;
    for (int k = 0; k < autom::kParamCount; ++k) applyAutoLocked(*t, k);
    bump(verParams(t->slot));
    markDirty();
}

std::vector<double> Studio::autoLane(int id, int param) const {
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = findLocked(id);
    if (!t || param < 0 || param >= autom::kParamCount) return {};
    const std::vector<autom::Point> &pts = t->autoPts[param].get();
    const float fixed = staticValueLocked(*t, param);
    std::vector<double> v = {static_cast<double>(pts.size()), autom::toPos(param, fixed), fixed,
                             param == autom::kVolume ? autom::toPos(param, 0.0f) : 0.5};
    v.reserve(4 + 3 * pts.size());
    for (const autom::Point &p : pts)
        v.insert(v.end(), {static_cast<double>(p.frame), autom::toPos(param, p.value), p.value});
    return v;
}

void Studio::automationNow(float *out) {
    std::lock_guard<std::mutex> lk(mModel);
    advanceTouchLocked(false);
    std::fill(out, out + kAutoNowCount * kMaxTracks, 0.0f);
    const int64_t pos = mMixer.position();
    for (const TrackModel &t : mProject.tracks) {
        if (t.slot < 0 || t.slot >= kMaxTracks) continue;
        float *o = out + kAutoNowCount * t.slot;
        int flags = 0;
        for (int k = 0; k < autom::kParamCount; ++k) {
            const bool touching = mTouch.active && mTouch.id == t.id && mTouch.param == k;
            float v = staticValueLocked(t, k);
            if (automatedLocked(t, k) && !touching) {
                flags |= 1 << k;
                v = autom::valueAt(k, t.autoPts[k].get(), pos, v);
            }
            if (touching && mTouch.writing && !mTouch.frozen) flags |= 4 << k;
            o[1 + k] = v;
        }
        o[0] = static_cast<float>(flags);
    }
}

int Studio::autoAdd(int id, int param, int64_t frame, float pos) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || param < 0 || param >= autom::kParamCount || mAutoGesture.active) return -1;
    if (mTouch.active && mTouch.id == id && mTouch.param == param) return -1;
    std::vector<autom::Point> pts = t->autoPts[param].get();
    const autom::Point p{std::max<int64_t>(0, frame), lanePosToValue(param, pos)};
    auto it = std::lower_bound(pts.begin(), pts.end(), p.frame,
                               [](const autom::Point &a, int64_t f) { return a.frame < f; });
    if (it != pts.end() && it->frame == p.frame) {
        *it = p;
    } else {
        if (static_cast<int>(pts.size()) >= autom::kMaxPoints) return -1;
        it = pts.insert(it, p);
    }
    const int index = static_cast<int>(it - pts.begin());
    if (t->autoMode == kAutoOff) {  // dibujar algo es para oírlo
        t->autoMode = kAutoRead;
        applyAutoLocked(*t, 1 - param);
        bump(verParams(t->slot));
    }
    changeAutoLocked(*t, param, std::move(pts), true);
    applyAutoLocked(*t, param);
    return index;
}

bool Studio::autoRemove(int id, int param, int index) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || param < 0 || param >= autom::kParamCount || mAutoGesture.active) return false;
    if (mTouch.active && mTouch.id == id && mTouch.param == param) return false;
    std::vector<autom::Point> pts = t->autoPts[param].get();
    if (index < 0 || index >= static_cast<int>(pts.size())) return false;
    pts.erase(pts.begin() + index);
    changeAutoLocked(*t, param, std::move(pts), true);
    return true;
}

void Studio::autoClear(int id, int param) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || param < 0 || param >= autom::kParamCount || mAutoGesture.active) return;
    if (mTouch.active && mTouch.id == id && mTouch.param == param) return;
    changeAutoLocked(*t, param, {}, true);
}

bool Studio::autoBegin(int id, int param, int index) {
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = findLocked(id);
    if (!t || param < 0 || param >= autom::kParamCount || index < 0 ||
        index >= static_cast<int>(t->autoPts[param].size()))
        return false;
    if (mTouch.active && mTouch.id == id && mTouch.param == param) return false;
    mAutoGesture.active = true;
    mAutoGesture.id = id;
    mAutoGesture.param = param;
    mAutoGesture.index = index;
    mAutoGesture.base = t->autoPts[param].get()[index];
    mAutoGesture.before = snapshotLocked();
    mLastKey.clear();
    return true;
}

void Studio::autoDrag(int64_t deltaFrames, float deltaPos) {
    std::lock_guard<std::mutex> lk(mModel);
    if (!mAutoGesture.active) return;
    TrackModel *t = findLocked(mAutoGesture.id);
    const int k = mAutoGesture.param, i = mAutoGesture.index;
    if (!t || i >= static_cast<int>(t->autoPts[k].size())) return;
    std::vector<autom::Point> pts = t->autoPts[k].get();
    // Entre sus vecinos: el orden no cambia y el índice sigue siendo el mismo.
    const int64_t lo = i > 0 ? pts[i - 1].frame + 1 : 0;
    const int64_t hi = i + 1 < static_cast<int>(pts.size()) ? pts[i + 1].frame - 1 : INT64_MAX;
    const autom::Point &b = mAutoGesture.base;
    pts[i].frame = std::clamp(b.frame + deltaFrames, lo, std::max(lo, hi));
    pts[i].value = lanePosToValue(k, autom::toPos(k, b.value) + (std::isfinite(deltaPos) ? deltaPos : 0.0f));
    changeAutoLocked(*t, k, std::move(pts), false);
}

void Studio::autoEnd() {
    std::lock_guard<std::mutex> lk(mModel);
    if (!mAutoGesture.active) return;
    mAutoGesture.active = false;
    if (mAutoGesture.before != snapshotLocked()) pushUndoLocked(std::move(mAutoGesture.before));
    mAutoGesture.before.clear();
}

void Studio::autoTouch(int id, int param, bool down) {
    std::string msg;
    {
        std::lock_guard<std::mutex> lk(mModel);
        if (param < 0 || param >= autom::kParamCount) return;
        if (!down) {
            if (mTouch.active && mTouch.id == id && mTouch.param == param) msg = endTouchLocked();
        } else {
            if (mTouch.active) endTouchLocked();
            TrackModel *t = findLocked(id);
            if (!t) return;
            // El deslizador arranca donde va la automatización: tocarlo no hace saltar el sonido.
            if (automatedLocked(*t, param))
                setStaticLocked(*t, param, autom::valueAt(param, t->autoPts[param].get(), mMixer.position(), 0.0f));
            beginTouchLocked(*t, param, staticValueLocked(*t, param));
        }
    }
    if (!msg.empty()) setMessage(msg);
}

void Studio::beginTouchLocked(TrackModel &t, int param, float base) {
    mTouch = AutoTouch();
    mTouch.active = true;
    mTouch.id = t.id;
    mTouch.param = param;
    mTouch.base = mTouch.value = base;
    mTouch.old = t.autoPts[param];
    mTouch.before = snapshotLocked();
    // Mientras el dedo está encima suena lo que marca (no la automatización).
    mMixer.setAutomationHold(t.slot, param, true);
    advanceTouchLocked(false);
}

void Studio::touchValueLocked(TrackModel &t, int param, float before, float value) {
    if (mTouch.active && mTouch.id == t.id && mTouch.param == param) {
        mTouch.value = value;
        mTouch.changed = true;
        advanceTouchLocked(true);
        return;
    }
    // Sin el dedo encima (un toque suelto en el riel, el lector de pantalla): como tocar, mover y
    // soltar. Solo importa al grabar movimientos.
    if (t.autoMode != kAutoWrite || value == before) return;
    if (mTouch.active) endTouchLocked();
    beginTouchLocked(t, param, before);
    mTouch.value = value;
    mTouch.changed = true;
    advanceTouchLocked(true);
    endTouchLocked();
}

void Studio::advanceTouchLocked(bool moved) {
    if (!mTouch.active || mTouch.frozen) return;
    const TrackModel *t = findLocked(mTouch.id);
    if (!t || t->autoMode != kAutoWrite) return;
    const bool playing = mMixer.isPlaying();
    const int64_t pos = mMixer.position();
    std::vector<autom::Point> &w = mTouch.written;
    if (!mTouch.writing) {
        if (!playing) return;
        // Empieza la toma desde lo que sonaba ahí (así lo de antes no cambia).
        mTouch.writing = true;
        w = {{pos, autom::valueAt(mTouch.param, mTouch.old.get(), pos, mTouch.base)}};
        mTouch.last = pos;
        mTouch.changedMs = nowMs();
        moved = true;
    } else if (!playing || pos < mTouch.last) {  // se detuvo, llegó al final o saltó atrás
        mTouch.frozen = true;
        return;
    }
    mTouch.last = std::max(mTouch.last, pos);
    if (!moved || mTouch.value == w.back().value) return;
    // El cursor avanza por pedazos (lo que pide la salida de audio cada vez): un cambio va en la
    // posición donde se vio; varios en el mismo pedazo, gana el último.
    const int64_t now = nowMs();
    if (pos > w.back().frame) {
        // El dedo estuvo quieto un rato: el valor se quedó hasta ahora (no una rampa larga). Se
        // mide en tiempo, no en cuadros: hay salidas que avanzan a saltos grandes.
        if (now - mTouch.changedMs >= kStillMs && pos - 1 > w.back().frame) w.push_back({pos - 1, w.back().value});
        w.push_back({pos, mTouch.value});
    } else if (w.size() == 1) {
        w.push_back({pos + 1, mTouch.value});
    } else {
        w.back().value = mTouch.value;
    }
    mTouch.changedMs = now;
}

std::string Studio::endTouchLocked() {
    advanceTouchLocked(false);
    AutoTouch tc = std::move(mTouch);
    mTouch = AutoTouch();
    TrackModel *t = findLocked(tc.id);
    if (!tc.active || !t) return "";
    const int k = tc.param;
    mMixer.setAutomationHold(t->slot, k, false);
    const char *what = k == autom::kVolume ? "el volumen" : "el paneo";
    if (t->autoMode != kAutoWrite) {
        if (tc.changed && automatedLocked(*t, k))
            return t->name + ": " + what + " sigue su automatización (para cambiarla, activa Grabar en A)";
        return "";
    }
    const std::vector<autom::Point> &old = tc.old.get();
    std::vector<autom::Point> lane;
    if (tc.writing) {
        if (tc.last > tc.written.back().frame) tc.written.push_back({tc.last, tc.written.back().value});
        std::vector<autom::Point> w = autom::simplify(k, tc.written, kAutoTolerance);
        const int64_t from = w.front().frame, to = w.back().frame;
        for (const autom::Point &p : old)
            if (p.frame < from) lane.push_back(p);
        lane.insert(lane.end(), w.begin(), w.end());
        // Lo que había después se respeta: el valor regresa a él poco a poco. Si el carril estaba
        // vacío, lo último se queda.
        if (!old.empty()) {
            const int64_t back = to + kAutoGlide;
            lane.push_back({back, autom::valueAt(k, old, back, tc.base)});
            for (const autom::Point &p : old)
                if (p.frame > back) lane.push_back(p);
        }
        for (float tol = kAutoTolerance * 2; static_cast<int>(lane.size()) > autom::kMaxPoints; tol *= 2)
            lane = autom::simplify(k, lane, tol);
    } else if (tc.changed && !old.empty()) {
        // Detenido: cambia el punto del cursor (o pone uno ahí).
        const int64_t at = mMixer.position();
        lane = old;
        auto near = std::find_if(lane.begin(), lane.end(),
                                 [at](const autom::Point &p) { return std::llabs(p.frame - at) <= kCursorNear; });
        if (near != lane.end()) near->value = tc.value;
        else lane.push_back({at, tc.value});
    } else {
        return "";
    }
    autom::normalize(k, lane);
    if (lane == old) return "";
    pushUndoLocked(std::move(tc.before));
    mLastKey.clear();
    t->autoPts[k] = std::move(lane);
    applyAutoLocked(*t, k);
    markDirty();
    return "";
}

// ---------------------------------------------------------------- volumen percibido (LUFS)

void Studio::pumpLoudnessLocked() {
    LoudnessBlock b[64];
    for (;;) {
        const int n = mMixer.readLoudness(b, 64);
        for (int i = 0; i < n; ++i)
            if (b[i].gen == mLoudGen) mLoud.add(b[i]);
        if (n < 64) break;
    }
}

void Studio::loudVerdict(float lufs, int target, float *out) {
    if (lufs <= kLoudnessNone) {
        out[0] = kLoudNoData;
        out[1] = 0.0f;
        return;
    }
    const float diff = lufs - static_cast<float>(target);
    out[0] = diff > 1.0f ? kLoudHigh : diff < -1.0f ? kLoudLow : kLoudOk;
    out[1] = diff;
}

void Studio::loudness(float *out) {
    int target;
    {
        std::lock_guard<std::mutex> lk(mModel);
        target = mProject.master.loudTarget;
    }
    std::lock_guard<std::mutex> lk(mLoudMutex);
    pumpLoudnessLocked();
    out[0] = mLoud.momentary();
    out[1] = mLoud.shortTerm();
    out[2] = mLoud.integrated();
    out[3] = mLoud.range();
    out[4] = mLoud.truePeakDb();
    out[5] = mLoud.maxMomentary();
    out[6] = mLoud.maxShortTerm();
    out[7] = static_cast<float>(mLoud.seconds());
    out[8] = static_cast<float>(target);
    loudVerdict(out[2], target, out + 9);
}

void Studio::loudnessHistory(float *momentary, float *shortTerm, int n) {
    std::lock_guard<std::mutex> lk(mLoudMutex);
    pumpLoudnessLocked();
    mLoud.history(momentary, shortTerm, n);
}

void Studio::resetLoudness() {
    std::lock_guard<std::mutex> lk(mLoudMutex);
    mLoudGen = mMixer.requestLoudnessReset();
    mLoud.reset();
}

void Studio::setLoudnessTarget(int lufs) {
    std::lock_guard<std::mutex> lk(mModel);
    mProject.master.loudTarget = lufs == -16 || lufs == -23 ? lufs : -14;
    bump(kVerMaster);
    markDirty();
}

void Studio::setExportNormalize(bool on) {
    std::lock_guard<std::mutex> lk(mModel);
    mProject.master.exportNormalize = on;
    bump(kVerMaster);
    markDirty();
}

bool Studio::measureSong() {
    if (trackCount() == 0 || !beginJob("Midiendo el volumen de la canción…")) return false;
    mProgress.store(0.0f);
    mCancelExport.store(false);
    const int64_t changes = mChanges.load();
    {
        std::lock_guard<std::mutex> lk(mLoudMutex);
        mSongState = 1;
    }
    mJobs.post([this, changes] {
        mInstJobs.waitIdle(60000);
        LoudnessReport r = measureMix(mMixer, 0.0f, false, &mProgress, 0.0f, 1.0f, &mCancelExport);
        const bool ok = r.error.empty();
        std::string msg;
        if (ok && r.integrated > kLoudnessNone) {
            msg = "Canción completa: " + signed1(r.integrated) + " LUFS integrado";
            if (r.range > kLoudnessNone) msg += " · rango " + fmt("%.1f", r.range) + " LU";
            if (r.truePeak > kLoudnessNone) msg += " · pico real " + signed1(r.truePeak) + " dBTP";
        } else {
            msg = ok ? "La canción está en silencio" : "No se midió: " + r.error;
        }
        {
            std::lock_guard<std::mutex> lk(mLoudMutex);
            mSongReport = std::move(r);
            mSongState = ok ? 2 : 0;
            mSongChanges = changes;
        }
        bump(kVerBackground);
        endJob(msg);
    });
    return true;
}

void Studio::songLoudness(float *out) const {
    int target;
    {
        std::lock_guard<std::mutex> lk(mModel);
        target = mProject.master.loudTarget;
    }
    std::lock_guard<std::mutex> lk(mLoudMutex);
    const LoudnessReport &r = mSongReport;
    const bool ready = mSongState == 2;
    out[0] = static_cast<float>(ready && mSongChanges != mChanges.load() ? 3 : mSongState);
    out[1] = ready ? r.integrated : kLoudnessNone;
    out[2] = ready ? r.range : kLoudnessNone;
    out[3] = ready ? r.truePeak : kLoudnessNone;
    out[4] = ready ? r.maxMomentary : kLoudnessNone;
    out[5] = ready ? r.maxShortTerm : kLoudnessNone;
    out[6] = ready ? static_cast<float>(r.seconds) : 0.0f;
    loudVerdict(out[1], target, out + 7);
}

std::vector<float> Studio::songLoudnessCurve(int cols) const {
    std::lock_guard<std::mutex> lk(mLoudMutex);
    const std::vector<float> &v = mSongReport.shortTerm;
    if (mSongState != 2 || v.empty() || cols <= 0) return {};
    std::vector<float> out(static_cast<size_t>(cols), kLoudnessNone);
    const size_t n = v.size();
    for (int c = 0; c < cols; ++c) {
        size_t a = n * static_cast<size_t>(c) / static_cast<size_t>(cols);
        size_t b = n * static_cast<size_t>(c + 1) / static_cast<size_t>(cols);
        if (b <= a) b = std::min(n, a + 1);
        for (size_t i = a; i < b; ++i) out[static_cast<size_t>(c)] = std::max(out[static_cast<size_t>(c)], v[i]);
    }
    return out;
}

// ---------------------------------------------------------------- separar la voz (preparar para cantar)

namespace {

const char *const kNoteNames[12] = {"Do", "Do#", "Re", "Mi♭", "Mi", "Fa", "Fa#", "Sol", "La♭", "La", "Si♭", "Si"};

std::string keyName(int key, bool minor) {
    return key < 0 || key > 11 ? "" : std::string(kNoteNames[key]) + (minor ? " menor" : " mayor");
}

std::string durationText(double s) {
    if (s < 60) return "menos de un minuto";
    const int m = static_cast<int>(std::lround(s / 60));
    if (m < 60) return "unos " + std::to_string(m) + " min";
    return "más de " + std::to_string(m / 60) + " h " + std::to_string(m % 60) + " min";
}

// Borra lo que haya en la carpeta (no las subcarpetas) y la carpeta.
void removeDir(const std::string &dir) {
    if (DIR *d = ::opendir(dir.c_str())) {
        while (dirent *e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n != "." && n != "..") std::remove((dir + "/" + n).c_str());
        }
        ::closedir(d);
    }
    ::rmdir(dir.c_str());
}

// Solo revisa la cabecera (no carga los pesos).
bool voiceHeader(const ModelFile &f, std::string &name, int &chunk, int &overlap, int &rate) {
    const Json &h = f.header();
    const Json &c = h["config"];
    if (h["modelo"].asString() != "mel_band_roformer" || c["chunk_size"].asInt(0) <= 0) return false;
    name = h["nombre"].asString("modelo");
    chunk = static_cast<int>(c["chunk_size"].asInt());
    overlap = static_cast<int>(c["num_overlap"].asInt(2));
    rate = static_cast<int>(c["sample_rate"].asInt(44100));
    return rate > 0 && overlap > 0;
}

}  // namespace

bool Studio::setVoiceModel(const std::string &path) {
    std::string err, name;
    int chunk = 0, overlap = 2, rate = 44100;
    auto f = ModelFile::open(path, err);
    if (!f || !voiceHeader(*f, name, chunk, overlap, rate)) return false;
    {
        std::lock_guard<std::mutex> lk(mVoiceMutex);
        if (mVoiceFd >= 0) ::close(mVoiceFd);
        mVoiceFd = -1;
        mVoicePath = path;
        mVoiceName = name;
        mVoiceChunk = chunk;
        mVoiceOverlap = overlap;
        mVoiceRate = rate;
    }
    bump(kVerSong);
    return true;
}

bool Studio::setVoiceModelFd(int fd, int64_t offset, int64_t length) {
    const int own = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (own < 0) return false;
    std::string err, name;
    int chunk = 0, overlap = 2, rate = 44100;
    auto f = ModelFile::openFd(own, offset, length, err);
    if (!f || !voiceHeader(*f, name, chunk, overlap, rate)) {
        ::close(own);
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(mVoiceMutex);
        if (mVoiceFd >= 0) ::close(mVoiceFd);
        mVoiceFd = own;
        mVoiceOffset = offset;
        mVoiceLength = length;
        mVoicePath.clear();
        mVoiceName = name;
        mVoiceChunk = chunk;
        mVoiceOverlap = overlap;
        mVoiceRate = rate;
    }
    bump(kVerSong);
    return true;
}

bool Studio::hasVoiceModel() const {
    std::lock_guard<std::mutex> lk(mVoiceMutex);
    return !mVoiceName.empty();
}

std::unique_ptr<ModelFile> Studio::openVoiceModel(std::string &error) const {
    std::lock_guard<std::mutex> lk(mVoiceMutex);
    if (!mVoicePath.empty()) return ModelFile::open(mVoicePath, error);
    if (mVoiceFd >= 0) return ModelFile::openFd(mVoiceFd, mVoiceOffset, mVoiceLength, error);
    error = "no está en esta versión";
    return nullptr;
}

std::string Studio::separationKey(const std::string &file, int64_t frames, int quality) const {
    std::lock_guard<std::mutex> lk(mVoiceMutex);
    return file + "|" + std::to_string(frames) + "|" + mVoiceName + "|" + std::to_string(quality);
}

void Studio::separationSaved(int id, int quality, int32_t *out) const {
    out[0] = out[1] = 0;
    std::string file;
    int64_t frames = 0;
    {
        std::lock_guard<std::mutex> lk(mModel);
        const TrackModel *t = findLocked(id);
        if (!t) return;
        file = t->file;
        frames = t->frames;
    }
    int chunk, overlap, rate;
    {
        std::lock_guard<std::mutex> lk(mVoiceMutex);
        if (mVoiceName.empty()) return;
        chunk = mVoiceChunk;
        overlap = mVoiceOverlap * (quality == kSepMax ? 2 : 1);
        rate = mVoiceRate;
    }
    // La separación trabaja a la frecuencia del modelo.
    const int64_t length = Resampler::outputFrames(frames, kRate, rate);
    out[1] = Separator::chunkCount(length, chunk, overlap);
    out[0] = Separator::savedChunks(mCacheDir + "/separar", separationKey(file, frames, quality), length, chunk, overlap);
}

bool Studio::separate(int id, int quality) {
    quality = quality == kSepMax ? kSepMax : kSepHigh;
    std::string file, name;
    bool room;
    {
        std::lock_guard<std::mutex> lk(mModel);
        const TrackModel *t = findLocked(id);
        if (!t) return false;
        file = t->file;
        name = t->name;
        room = mProject.tracks.size() + 2 <= static_cast<size_t>(kMaxTracks);
    }
    if (!hasVoiceModel()) {
        setMessage("Esta versión no trae el modelo para quitar la voz");
        return false;
    }
    if (!room) {
        setMessage("Para quitar la voz hacen falta dos lugares libres (máximo 12 pistas)");
        return false;
    }
    if (!beginJob("Preparando la canción…")) return false;
    mCancelExport.store(false);
    mSepTrack.store(id);
    mSepDone.store(0);
    mSepTotal.store(0);
    mSepEta.store(-1.0f);
    mSepPhase.store(1);
    bump(kVerSong);
    mJobs.post([this, id, file, name, quality] { jobSeparate(id, file, name, quality); });
    return true;
}

void Studio::separationState(float *out) const {
    out[0] = static_cast<float>(mSepPhase.load());
    out[1] = mSepPhase.load() ? std::max(0.0f, mProgress.load()) : 0.0f;
    out[2] = static_cast<float>(mSepDone.load());
    out[3] = static_cast<float>(mSepTotal.load());
    out[4] = mSepEta.load();
    out[5] = static_cast<float>(mSepTrack.load());
}

void Studio::jobSeparate(int id, std::string file, std::string name, int quality) {
    auto finish = [&](const std::string &msg) {
        mSepPhase.store(0);
        bump(kVerSong);
        endJob(msg);
    };
    const std::string tmpIn = mCacheDir + "/separar-entrada.wav";
    const std::string cacheDir = mCacheDir + "/separar";
    std::string err;
    std::unique_ptr<ModelFile> mf = openVoiceModel(err);
    std::unique_ptr<MelRoformer> model;
    try {
        if (mf) model = MelRoformer::load(*mf, err);
    } catch (const std::bad_alloc &) {
        err = "no hay memoria suficiente";
    }
    if (!model) return finish("No se pudo abrir el modelo de voz: " + err);
    const int rate = model->config().sampleRate;
    const int overlap = model->config().overlap * (quality == kSepMax ? 2 : 1);
    auto src = MappedAudio::open(mAudioDir + "/" + file, err);
    if (!src) return finish("No se pudo leer la pista: " + err);
    const int64_t frames = src->frames();
    const int ch = src->channels();

    // 1) La pista a la frecuencia del modelo y en estéreo (archivo temporal).
    {
        WavWriter w;
        if (!w.open(tmpIn, 2, rate)) return finish("No hay espacio para preparar la canción");
        Resampler rs(2, kRate, rate);
        std::vector<float> in, out;
        constexpr int64_t kBlock = 65536;
        bool ok = true;
        for (int64_t f = 0; f < frames && ok; f += kBlock) {
            if (mCancelExport.load()) {
                w.close();
                std::remove(tmpIn.c_str());
                return finish("Cancelado");
            }
            const int64_t n = std::min(kBlock, frames - f);
            in.resize(static_cast<size_t>(n) * 2);
            const float *d = src->data() + f * ch;
            for (int64_t i = 0; i < n; ++i) {
                in[static_cast<size_t>(2 * i)] = d[i * ch];
                in[static_cast<size_t>(2 * i + 1)] = d[i * ch + (ch == 2 ? 1 : 0)];
            }
            out.clear();
            rs.process(in.data(), static_cast<size_t>(n), out);
            if (!out.empty()) ok = w.write(out.data(), out.size() / 2);
            mProgress.store(0.02f * static_cast<float>(f + n) / static_cast<float>(frames));
        }
        out.clear();
        rs.finish(out);
        if (ok && !out.empty()) ok = w.write(out.data(), out.size() / 2);
        if (!w.close() || !ok) {
            std::remove(tmpIn.c_str());
            return finish("No hay espacio para preparar la canción");
        }
    }
    auto input = MappedAudio::open(tmpIn, err);
    if (!input) return finish("No se pudo preparar la canción: " + err);

    // 2) Quitar la voz trozo por trozo. La voz vuelve a 48 kHz y el instrumental es lo que queda
    //    de la pista original sin esa voz (así no pierde nada de su calidad).
    mSepPhase.store(2);
    bump(kVerSong);
    ::mkdir(cacheDir.c_str(), 0700);
    const std::string vocFile = newFileName("voz"), instFile = newFileName("instrumental");
    const std::string vocPath = mAudioDir + "/" + vocFile, instPath = mAudioDir + "/" + instFile;
    WavWriter wv, wi;
    if (!wv.open(vocPath, ch, kRate) || !wi.open(instPath, ch, kRate)) {
        wv.close();
        wi.close();
        std::remove(vocPath.c_str());
        std::remove(instPath.c_str());
        std::remove(tmpIn.c_str());
        return finish("No hay espacio para guardar el resultado");
    }
    Resampler up(2, rate, kRate);
    int64_t written = 0;
    std::vector<float> outV, outI, inter, up48;
    auto emit = [&](const std::vector<float> &v48) -> bool {  // voz intercalada en estéreo, a 48 kHz
        const int64_t n = std::min<int64_t>(static_cast<int64_t>(v48.size() / 2), frames - written);
        if (n <= 0) return true;
        outV.resize(static_cast<size_t>(n * ch));
        outI.resize(static_cast<size_t>(n * ch));
        const float *s = src->data() + written * ch;
        for (int64_t i = 0; i < n; ++i) {
            const float vl = v48[static_cast<size_t>(2 * i)], vr = v48[static_cast<size_t>(2 * i + 1)];
            if (ch == 2) {
                outV[static_cast<size_t>(2 * i)] = vl;
                outV[static_cast<size_t>(2 * i + 1)] = vr;
                outI[static_cast<size_t>(2 * i)] = s[2 * i] - vl;
                outI[static_cast<size_t>(2 * i + 1)] = s[2 * i + 1] - vr;
            } else {
                const float v = 0.5f * (vl + vr);
                outV[static_cast<size_t>(i)] = v;
                outI[static_cast<size_t>(i)] = s[i] - v;
            }
        }
        written += n;
        return wv.write(outV.data(), static_cast<size_t>(n)) && wi.write(outI.data(), static_cast<size_t>(n));
    };
    Separator::Options opt;
    opt.overlap = overlap;
    opt.cacheDir = cacheDir;
    opt.cacheKey = separationKey(file, frames, quality);
    opt.cancel = &mCancelExport;
    auto last = std::chrono::steady_clock::now();
    double computeSecs = 0;
    int computed = 0, lastReused = 0;
    opt.progress = [&](int done, int total, int reused) {
        const auto now = std::chrono::steady_clock::now();
        if (reused == lastReused) {  // calculado ahora (no leído de lo guardado)
            computeSecs += std::chrono::duration<double>(now - last).count();
            ++computed;
        }
        lastReused = reused;
        last = now;
        const float eta = computed > 0 ? static_cast<float>((total - done) * computeSecs / computed) : -1.0f;
        mSepDone.store(done);
        mSepTotal.store(total);
        mSepEta.store(done == total ? 0.0f : eta);
        mProgress.store(0.02f + 0.93f * static_cast<float>(done) / static_cast<float>(total));
        std::string text = "Quitando la voz · trozo " + std::to_string(done) + " de " + std::to_string(total);
        if (eta >= 0 && done < total) text += " · faltan " + durationText(eta);
        setJobText(text);
        bump(kVerSong);
    };
    mSepTotal.store(Separator::chunkCount(input->frames(), model->chunk(), overlap));
    std::string e;
    {
        ThreadPool pool;
        e = Separator::run(*model, pool, input->frames(),
            [&](int64_t from, int n, float *l, float *r) {
                const float *d = input->data() + from * 2;
                for (int i = 0; i < n; ++i) {
                    l[i] = d[2 * i];
                    r[i] = d[2 * i + 1];
                }
            },
            [&](const float *l, const float *r, int n) {
                inter.resize(static_cast<size_t>(n) * 2);
                for (int i = 0; i < n; ++i) {
                    inter[static_cast<size_t>(2 * i)] = l[i];
                    inter[static_cast<size_t>(2 * i + 1)] = r[i];
                }
                up48.clear();
                up.process(inter.data(), static_cast<size_t>(n), up48);
                return emit(up48);
            }, opt);
    }
    if (e.empty()) {
        up48.clear();
        up.finish(up48);
        if (!emit(up48)) e = "no hay espacio";
        // Si al volver a 48 kHz faltó algún cuadro, la voz termina en silencio.
        if (e.empty() && written < frames && !emit(std::vector<float>(static_cast<size_t>(frames - written) * 2, 0.0f)))
            e = "no hay espacio";
    }
    const bool closed = wv.close() & wi.close();
    model.reset();
    mf.reset();
    input.reset();
    std::remove(tmpIn.c_str());
    if (!e.empty() || !closed || written != frames) {
        std::remove(vocPath.c_str());
        std::remove(instPath.c_str());
        if (e == "Cancelado") return finish("Cancelado: lo que ya se calculó queda guardado para seguir después");
        return finish("No se pudo quitar la voz: " + (e.empty() ? std::string("no hay espacio") : e));
    }

    // 3) Las pistas nuevas, debajo de la original (con sus mismos cortes y volumen).
    mSepPhase.store(3);
    setJobText("Armando las pistas…");
    bump(kVerSong);
    auto va = MappedAudio::open(vocPath, err);
    auto ia = MappedAudio::open(instPath, err);
    if (!va || !ia) {
        std::remove(vocPath.c_str());
        std::remove(instPath.c_str());
        return finish("No se pudo abrir el resultado: " + err);
    }
    std::vector<float> vPeaks = computePeaks(*va, kPeakBucket), iPeaks = computePeaks(*ia, kPeakBucket);
    int instId = 0, vocId = 0;
    std::string addErr;
    {
        std::lock_guard<std::mutex> lk(mModel);
        const TrackModel *orig = findLocked(id);
        const std::string info = std::string(ch == 2 ? "estéreo" : "mono") + " · 48 kHz · sin la voz (IA)";
        auto make = [&](const std::string &suffix, const std::string &f, std::vector<float> peaks) {
            TrackModel t;
            t.id = mProject.nextId++;
            t.name = name + " · " + suffix;
            t.file = f;
            if (orig) {
                t.volDb = orig->volDb;
                t.pan = orig->pan;
                t.regions = orig->regions;
                t.hasRegions = true;
            }
            t.peaks = std::move(peaks);
            return t;
        };
        TrackModel ti = make("instrumental", instFile, std::move(iPeaks));
        ti.info = info;
        TrackModel tv = make("voz original", vocFile, std::move(vPeaks));
        tv.info = std::string(ch == 2 ? "estéreo" : "mono") + " · 48 kHz · la voz de la canción (IA)";
        tv.mute = true;  // guía: se prende si hace falta
        instId = ti.id;
        vocId = tv.id;
        addErr = addTrackLocked(std::move(ti), std::move(ia));
        if (addErr.empty()) addErr = addTrackLocked(std::move(tv), std::move(va));
        if (addErr.empty()) {
            // Debajo de la original, que queda en silencio.
            auto pos = std::find_if(mProject.tracks.begin(), mProject.tracks.end(), [id](const TrackModel &t) { return t.id == id; });
            if (pos != mProject.tracks.end()) {
                pos->mute = true;
                applyTrackLocked(*pos);
                bump(verParams(pos->slot));
                std::rotate(pos + 1, mProject.tracks.end() - 2, mProject.tracks.end());
            }
            mProject.song = SongModel();
            mProject.song.sourceId = id;
            mProject.song.instrumentalId = instId;
            mProject.song.vocalId = vocId;
            applySongToVoiceLocked();
            bump(kVerTracks);
        }
    }
    if (!addErr.empty()) {
        removeTrack(instId);
        std::remove(vocPath.c_str());
        std::remove(instPath.c_str());
        return finish("No se pudieron agregar las pistas: " + addErr);
    }
    // Se guarda ya: si la app se cierra ahora, las pistas nuevas no se pierden.
    markDirty();
    save();
    removeDir(cacheDir);  // ya no hacen falta los trozos guardados

    // 4) Lo que se sabe de la canción: tonalidad, tempo y dónde canta la voz original.
    mSepPhase.store(4);
    setJobText("Analizando la canción…");
    bump(kVerSong);
    SongInfo info;
    auto ia2 = MappedAudio::open(instPath, err);
    auto va2 = MappedAudio::open(vocPath, err);
    const bool analyzed = analyzeSong(ia2.get(), va2.get(), info, &mCancelExport,
                                      [&](float p) { mProgress.store(0.95f + 0.05f * p); });
    if (analyzed) {
        std::lock_guard<std::mutex> lk(mModel);
        SongModel &s = mProject.song;
        if (s.instrumentalId == instId) {
            s.key = info.key;
            s.minor = info.minor;
            s.keyScore = info.keyScore;
            s.bpm = info.bpm;
            s.beats = std::move(info.beats);
            s.vocalStart = std::move(info.vocalStart);
            s.vocalEnd = std::move(info.vocalEnd);
            s.envelope = std::move(info.envelope);
            s.notes = info.notes;
            applySongToVoiceLocked();
        }
    }
    markDirty();
    save();
    std::string msg = "Listo: " + name + " sin la voz";
    if (analyzed && info.key >= 0) msg += " · " + keyName(info.key, info.minor);
    if (analyzed && info.bpm > 0) msg += " · " + std::to_string(static_cast<int>(std::lround(info.bpm))) + " BPM";
    if (!analyzed) msg += " (análisis cancelado)";
    finish(msg);
}

std::string Studio::songJson() const {
    std::lock_guard<std::mutex> lk(mModel);
    const SongModel &s = mProject.song;
    Json o = Json::object();
    o.set("lista", s.ready());
    if (!s.ready()) return o.dump();
    o.set("fuente", s.sourceId);
    o.set("instrumental", s.instrumentalId);
    o.set("voz", s.vocalId);
    o.set("tonica", s.key);
    o.set("menor", s.minor);
    o.set("claridad", s.keyScore);
    o.set("bpm", s.bpm);
    o.set("pulsos", static_cast<int>(s.beats.size()));
    o.set("primerPulso", s.beats.empty() ? -1.0f : s.beats.front());
    Json escala = Json::array();
    if (s.key >= 0)
        for (int n : song::scaleNotes(s.key, s.minor)) escala.push(n);
    o.set("escala", std::move(escala));
    Json notas = Json::array();
    for (float n : s.notes) notas.push(n);
    o.set("notas", std::move(notas));
    Json partes = Json::array();
    for (const auto &[a, b] : songPartsLocked()) {
        Json p = Json::array();
        p.push(static_cast<float>(a));
        p.push(static_cast<float>(b));
        partes.push(std::move(p));
    }
    o.set("partes", std::move(partes));
    return o.dump();
}

std::vector<std::pair<double, double>> Studio::songPartsLocked() const {
    std::vector<std::pair<double, double>> parts;
    const SongModel &s = mProject.song;
    const TrackModel *t = s.ready() ? findLocked(s.instrumentalId) : nullptr;
    if (!t) return parts;
    // El análisis está en segundos del archivo: cada parte, por cada pedazo que la toca.
    const size_t n = std::min(s.vocalStart.size(), s.vocalEnd.size());
    for (size_t i = 0; i < n; ++i) {
        const double a = static_cast<double>(s.vocalStart[i]) * kRate, b = static_cast<double>(s.vocalEnd[i]) * kRate;
        for (const Region &r : t->regions) {
            const double lo = std::max(a, static_cast<double>(r.src));
            const double hi = std::min(b, static_cast<double>(r.src + r.length));
            if (hi > lo) parts.emplace_back((r.start + (lo - r.src)) / kRate, (r.start + (hi - r.src)) / kRate);
        }
    }
    std::sort(parts.begin(), parts.end());
    // Las que se tocan (un corte en medio de una parte), juntas.
    std::vector<std::pair<double, double>> out;
    for (const auto &p : parts) {
        if (!out.empty() && p.first <= out.back().second + 0.05) out.back().second = std::max(out.back().second, p.second);
        else out.push_back(p);
    }
    return out;
}

void Studio::songCue(float *out) const {
    std::fill(out, out + kCueCount, 0.0f);
    std::vector<std::pair<double, double>> parts;
    {
        std::lock_guard<std::mutex> lk(mModel);
        parts = songPartsLocked();
    }
    out[3] = static_cast<float>(parts.size());
    if (parts.empty()) return;
    const double t = position();
    size_t i = 0;  // la parte en la que va, o la siguiente
    while (i < parts.size() && parts[i].second <= t) ++i;
    if (i == parts.size()) {
        out[0] = kCueDone;
        out[2] = static_cast<float>(parts.size());
        out[4] = 1.0f;
        return;
    }
    const auto [a, b] = parts[i];
    out[2] = static_cast<float>(i);
    if (t >= a) {
        out[0] = kCueSinging;
        out[1] = static_cast<float>(b - t);
        out[4] = static_cast<float>(std::clamp((t - a) / std::max(1e-3, b - a), 0.0, 1.0));
    } else {
        const double from = i > 0 ? parts[i - 1].second : 0.0;
        out[0] = kCueWaiting;
        out[1] = static_cast<float>(a - t);
        out[4] = static_cast<float>(std::clamp((t - from) / std::max(1e-3, a - from), 0.0, 1.0));
    }
}

std::vector<float> Studio::songEnvelope(int cols) const {
    std::lock_guard<std::mutex> lk(mModel);
    const SongModel &s = mProject.song;
    const std::vector<float> &env = s.envelope;
    std::vector<float> out(static_cast<size_t>(std::max(cols, 0)), 0.0f);
    const TrackModel *t = s.ready() ? findLocked(s.instrumentalId) : nullptr;
    if (env.empty() || cols <= 0 || !t) return out;
    // Del principio al fin de la mezcla: en cada columna, lo que suena ahí del instrumental
    // (siguiendo sus pedazos) y el máximo de la voz original en esa parte del archivo.
    const double total = std::max(1.0, static_cast<double>(mMixer.length()));  // cuadros
    for (int c = 0; c < cols; ++c) {
        const double t0 = total * c / cols, t1 = total * (c + 1) / cols;
        float m = 0.0f;
        for (const Region &r : t->regions) {
            const double lo = std::max(t0, static_cast<double>(r.start)), hi = std::min(t1, static_cast<double>(r.end()));
            if (hi <= lo) continue;
            const double f0 = (r.src + (lo - r.start)) / kRate, f1 = (r.src + (hi - r.start)) / kRate;  // segundos del archivo
            const size_t a = static_cast<size_t>(f0 * song::kEnvelopeRate);
            const size_t b = std::max(a + 1, static_cast<size_t>(std::ceil(f1 * song::kEnvelopeRate)));
            for (size_t i = a; i < b && i < env.size(); ++i) m = std::max(m, env[i]);
        }
        out[static_cast<size_t>(c)] = m;
    }
    return out;
}
