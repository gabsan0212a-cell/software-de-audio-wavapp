// Crear música: pistas de instrumento (inst/), tempo, tonalidad, tocar en vivo y metrónomo.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "Studio.h"
#include "io/Json.h"
#include "io/WavWriter.h"

namespace {

constexpr float kVolMin = -60.0f;
constexpr int64_t kTouchLatency = Studio::kRate * 3 / 100;  // ~30 ms: del dedo a que la app se entera

float dbToGain(float db) { return db <= kVolMin + 0.1f ? 0.0f : std::pow(10.0f, db / 20.0f); }

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

// ---------------------------------------------------------------- tempo y tonalidad

double Studio::bpmLocked() const {
    const MusicModel &m = mProject.music;
    const SongModel &s = mProject.song;
    if (!m.bpmSet && s.ready() && s.bpm >= 40.0f && s.bpm <= 240.0f) return s.bpm;
    return m.bpm;
}

void Studio::keyLocked(int &key, bool &minor) const {
    const MusicModel &m = mProject.music;
    const SongModel &s = mProject.song;
    if (!m.keySet && s.ready() && s.key >= 0) {
        key = s.key;
        minor = s.minor;
        return;
    }
    key = m.key;
    minor = m.minor;
}

bool Studio::hasInstrumentsLocked() const {
    return std::any_of(mProject.tracks.begin(), mProject.tracks.end(), [](const TrackModel &t) { return t.isInst; });
}

void Studio::applyMusicLocked() {
    const double bpm = bpmLocked();
    mMixer.setMetronome(mProject.music.metronome ? kRate * 60.0 / bpm : 0.0);
    for (TrackModel &t : mProject.tracks)
        if (t.isInst && std::fabs(t.renderedBpm - bpm) > 1e-6) scheduleRenderLocked(t);
    bump(kVerMusic);
}

void Studio::musicState(float *out) const {
    std::lock_guard<std::mutex> lk(mModel);
    int key;
    bool minor;
    keyLocked(key, minor);
    const MusicModel &m = mProject.music;
    const SongModel &s = mProject.song;
    out[0] = static_cast<float>(bpmLocked());
    out[1] = static_cast<float>(key);
    out[2] = minor ? 1.0f : 0.0f;
    out[3] = m.metronome ? 1.0f : 0.0f;
    out[4] = !m.bpmSet && s.ready() && s.bpm >= 40.0f ? 1.0f : 0.0f;
    out[5] = !m.keySet && s.ready() && s.key >= 0 ? 1.0f : 0.0f;
    out[6] = static_cast<float>(mRenderQueued.size() + static_cast<size_t>(mPendingAdds));
    int tk;
    bool tm;
    songKeyLocked(tk, tm);
    out[7] = static_cast<float>(tk);
    out[8] = tm ? 1.0f : 0.0f;
}

void Studio::setTempo(float bpm) {
    if (!std::isfinite(bpm)) return;
    std::lock_guard<std::mutex> lk(mModel);
    mProject.music.bpm = std::clamp(std::round(bpm), 40.0f, 240.0f);
    mProject.music.bpmSet = true;
    applySongToVoiceLocked();  // el delay de la voz va a tiempo
    markDirty();
}

void Studio::setKey(int key, bool minor) {
    std::lock_guard<std::mutex> lk(mModel);
    mProject.music.key = ((key % 12) + 12) % 12;
    mProject.music.minor = minor;
    mProject.music.keySet = true;
    applySongToVoiceLocked();  // la escala del auto-tune
    for (const TrackModel &t : mProject.tracks)
        if (t.isInst) bump(verInst(t.slot));  // la cuadrícula marca otra escala
    markDirty();
}

void Studio::setMetronome(bool on) {
    std::lock_guard<std::mutex> lk(mModel);
    mProject.music.metronome = on;
    applyMusicLocked();
    markDirty();
}

// ---------------------------------------------------------------- información

std::string Studio::instrumentsJson() {
    Json tipos = Json::array();
    for (int k = 0; k < inst::kKindCount; ++k) {
        Json o = Json::object();
        o.set("nombre", std::string(inst::kindName(k)));
        Json pr = Json::array();
        for (int p = 0; p < inst::presetCount(k); ++p) pr.push(std::string(inst::presetName(k, p)));
        o.set("presets", std::move(pr));
        Json aj = Json::array();
        for (int p = 0; p < inst::kParamCount; ++p)
            if (inst::paramApplies(k, p)) aj.push(p);
        o.set("ajustes", std::move(aj));
        o.set("inicio", inst::homePitch(k));
        tipos.push(std::move(o));
    }
    Json piezas = Json::array();
    for (int i = 0; i < inst::kDrumPieces; ++i) piezas.push(std::string(inst::drumName(i)));
    Json o = Json::object();
    Json estilos = Json::array();
    for (int i = 0; i < inst::styleCount(); ++i) {
        const inst::Style &st = inst::style(i);
        Json e = Json::object();
        e.set("nombre", std::string(st.name));
        e.set("detalle", std::string(st.detail));
        e.set("bpm", st.bpm);
        e.set("tonica", st.key);
        e.set("menor", st.minor);
        Json partes = Json::array();
        for (const inst::StylePart &sp : st.parts) partes.push(sp.kind);
        e.set("partes", std::move(partes));
        estilos.push(std::move(e));
    }
    o.set("tipos", std::move(tipos));
    o.set("piezas", std::move(piezas));
    o.set("estilos", std::move(estilos));
    o.set("pasoPulsos", inst::kStepTicks);
    o.set("pasosCompas", inst::kStepsPerBar);
    o.set("maxCompases", inst::kMaxBars);
    o.set("maxVeces", inst::kMaxRepeats);
    return o.dump();
}

std::vector<float> Studio::instParamInfo() {
    std::vector<float> v;
    for (int k = 0; k < inst::kParamCount; ++k) {
        const inst::ParamInfo &i = inst::paramInfo(k);
        v.insert(v.end(), {i.min, i.max, i.def, i.log ? 1.0f : 0.0f});
    }
    return v;
}

std::string Studio::instrumentJson(int id) const {
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = findLocked(id);
    if (!t || !t->isInst) return "";
    const inst::Part &p = t->part;
    Json o = Json::object();
    o.set("tipo", p.sound.kind);
    o.set("preset", p.sound.preset);
    o.set("compases", p.pattern.bars);
    o.set("veces", p.pattern.repeats);
    o.set("swing", static_cast<double>(p.pattern.swing));
    Json aj = Json::array();
    for (float v : p.sound.values) aj.push(static_cast<double>(v));
    o.set("ajustes", std::move(aj));
    Json notas = Json::array();
    for (const inst::Note &n : p.pattern.notes) notas.push(n.tick).push(n.len).push(n.pitch).push(n.vel);
    o.set("notas", std::move(notas));
    o.set("preparando", mRenderQueued.count(id) > 0);
    return o.dump();
}

std::string Studio::instRowsJson(int id, bool scaleOnly) const {
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = findLocked(id);
    if (!t || !t->isInst) return "[]";
    int key;
    bool minor;
    keyLocked(key, minor);
    const int kind = t->part.sound.kind;
    Json a = Json::array();
    for (int p : inst::rows(kind, key, minor, scaleOnly)) {
        Json r = Json::array();
        if (kind == inst::kDrums) {
            r.push(p).push(std::string(inst::drumName(p))).push(0).push(1);
        } else {
            r.push(p).push(inst::noteName(p)).push(((p - key) % 12 + 12) % 12 == 0 ? 1 : 0).push(inst::inScale(p, key, minor) ? 1 : 0);
        }
        a.push(std::move(r));
    }
    return a.dump();
}

// ---------------------------------------------------------------- render

std::string Studio::renderInstrument(const inst::Part &part, double bpm, const std::string &audioDir, std::string &file,
                                     std::unique_ptr<MappedAudio> &audio, std::vector<float> &peaks) {
    const std::vector<float> pcm = inst::render(part, bpm, kRate);
    if (pcm.empty()) return "no se pudo preparar el instrumento";
    file = newFileName("instrumento");
    const std::string path = audioDir + "/" + file;
    WavWriter w;
    if (!w.open(path, 2, kRate) || !w.write(pcm.data(), pcm.size() / 2) || !w.close()) {
        std::remove(path.c_str());
        return "no se pudo guardar el instrumento (¿falta espacio?)";
    }
    std::string err;
    audio = MappedAudio::open(path, err);
    if (!audio) {
        std::remove(path.c_str());
        return err;
    }
    peaks = computePeaks(*audio, kPeakBucket);
    return "";
}

void Studio::scheduleRenderLocked(TrackModel &t) {
    ++t.renderGen;
    bump(verInst(t.slot));
    if (!mRenderQueued.insert(t.id).second) return;  // ya hay uno en camino: tomará lo último
    const int id = t.id;
    mInstJobs.post([this, id] { jobRenderInstrument(id); });
}

void Studio::jobRenderInstrument(int id) {
    inst::Part part;
    double bpm;
    uint32_t gen;
    std::string audioDir;
    {
        std::lock_guard<std::mutex> lk(mModel);
        mRenderQueued.erase(id);
        const TrackModel *t = findLocked(id);
        if (!t || !t->isInst) return;
        part = t->part;
        gen = t->renderGen;
        bpm = bpmLocked();
        audioDir = mAudioDir;
    }
    std::string file;
    std::unique_ptr<MappedAudio> audio;
    std::vector<float> peaks;
    const std::string err = renderInstrument(part, bpm, audioDir, file, audio, peaks);
    if (!err.empty()) {
        setMessage("No se preparó el instrumento: " + err);
        return;
    }
    std::string oldFile;
    std::unique_ptr<MappedAudio> old;
    {
        std::lock_guard<std::mutex> lk(mModel);
        TrackModel *t = findLocked(id);
        if (!t || t->renderGen != gen) {  // ya no existe o cambió otra vez (viene otro render)
            audio.reset();
            std::remove((audioDir + "/" + file).c_str());
            bump(verInst(t ? t->slot : 0));
            return;
        }
        // Si los pedazos eran el archivo completo, siguen siéndolo; si se editaron, se quedan.
        const bool whole = t->regions.size() == 1 && t->regions[0] == Region{0, 0, t->frames, 0, 0};
        const int64_t frames = audio->frames();
        const std::vector<Region> regs = whole ? std::vector<Region>{Region{0, 0, frames, 0, 0}} : t->regions;
        old = mMixer.replaceAudio(t->slot, std::move(audio), regs);
        oldFile = t->file;
        t->file = file;
        t->frames = frames;
        t->channels = 2;
        t->peaks = std::move(peaks);
        t->renderedBpm = bpm;
        t->renderedHash = inst::renderHash(part, bpm);
        t->regions = mMixer.regions(t->slot);
        if (mCreating && mLoopId == id) applyLoopLocked();  // la vuelta pudo cambiar de largo
        bump(verRegions(t->slot));
        bump(verInst(t->slot));
        bump(kVerTracks);
        bump(kVerMusic);
    }
    old.reset();
    if (!oldFile.empty() && oldFile != file) std::remove((audioDir + "/" + oldFile).c_str());
    {
        std::lock_guard<std::mutex> lk(mSgMutex);
        mSgCache.erase(id);
    }
    bump(kVerBackground);
    markDirty();
}

// ---------------------------------------------------------------- instrumentos

int Studio::addInstrument(int kind, int preset) {
    if (kind < 0 || kind >= inst::kKindCount) return -1;
    TrackModel t;
    bool full;
    {
        std::lock_guard<std::mutex> lk(mModel);
        full = static_cast<int>(mProject.tracks.size()) + mPendingAdds >= kMaxTracks;
    }
    if (full) {
        setMessage("Límite de 12 pistas");
        return -1;
    }
    {
        std::lock_guard<std::mutex> lk(mModel);
        t.id = mProject.nextId++;
        t.isInst = true;
        inst::Sound s;
        s.kind = kind;
        s.preset = preset;
        t.part.sound = inst::clampSound(s);
        int key;
        bool minor;
        keyLocked(key, minor);
        t.part.pattern = inst::starter(t.part.sound, key, minor, 4, 0);
        t.part.pattern.repeats = 4;
        mIdeas[t.id] = 0;
        t.name = instNameLocked(kind, {});
        t.info = std::string("instrumento · ") + inst::presetName(kind, t.part.sound.preset);
        t.fx = fx::defaults();
        ++mPendingAdds;
        bump(kVerMusic);
    }
    const int id = t.id;
    mInstJobs.post([this, tm = std::move(t)]() mutable { jobAddInstrument(std::move(tm)); });
    return id;
}

std::string Studio::instNameLocked(int kind, const std::vector<TrackModel> &extra) const {
    const std::string base = inst::kindName(kind);
    int n = 1;
    for (const auto *list : {&mProject.tracks, &extra})
        for (const TrackModel &o : *list)
            if (o.isInst && o.part.sound.kind == kind) ++n;
    return n == 1 ? base : base + " " + std::to_string(n);
}

int Studio::addStyle(int index) {
    if (index < 0 || index >= inst::styleCount()) return -1;
    const inst::Style &st = inst::style(index);
    const int count = static_cast<int>(st.parts.size());
    std::vector<TrackModel> add;
    bool full = false;
    {
        std::lock_guard<std::mutex> lk(mModel);
        if (static_cast<int>(mProject.tracks.size()) + mPendingAdds + count > kMaxTracks) {
            full = true;
        } else {
            // Con una canción preparada se sigue su tempo y su tonalidad (así encaja con ella).
            if (!mProject.song.ready()) {
                MusicModel &m = mProject.music;
                m.bpm = static_cast<float>(st.bpm);
                m.bpmSet = true;
                m.key = st.key;
                m.minor = st.minor;
                m.keySet = true;
                applySongToVoiceLocked();  // y los instrumentos que ya estaban, al tempo nuevo
                for (const TrackModel &t : mProject.tracks)
                    if (t.isInst) bump(verInst(t.slot));
            }
            int key;
            bool minor;
            keyLocked(key, minor);
            for (const inst::StylePart &sp : st.parts) {
                TrackModel t;
                t.id = mProject.nextId++;
                t.isInst = true;
                inst::Sound s;
                s.kind = sp.kind;
                s.preset = inst::presetIndex(sp.kind, sp.preset);
                t.part.sound = inst::clampSound(s);
                t.part.pattern = inst::starter(t.part.sound, key, minor, 4, st.variant);
                t.part.pattern.swing = st.swing;
                t.part.pattern.repeats = 4;
                mIdeas[t.id] = st.variant;
                t.name = instNameLocked(sp.kind, add);
                t.info = std::string("instrumento · ") + inst::presetName(sp.kind, t.part.sound.preset);
                t.fx = fx::defaults();
                t.volDb = sp.volDb;
                add.push_back(std::move(t));
            }
            mPendingAdds += count;
            bump(kVerMusic);
            markDirty();
        }
    }
    if (full) {
        setMessage("No caben los " + std::to_string(count) + " instrumentos de " + st.name + " (máximo 12 pistas)");
        return -1;
    }
    const int first = add.front().id;
    for (TrackModel &t : add) mInstJobs.post([this, tm = std::move(t)]() mutable { jobAddInstrument(std::move(tm)); });
    return first;
}

void Studio::jobAddInstrument(TrackModel t) {
    double bpm;
    std::string audioDir;
    {
        std::lock_guard<std::mutex> lk(mModel);
        bpm = bpmLocked();
        audioDir = mAudioDir;
    }
    std::string file;
    std::unique_ptr<MappedAudio> audio;
    std::vector<float> peaks;
    std::string err = renderInstrument(t.part, bpm, audioDir, file, audio, peaks);
    const std::string name = t.name;
    {
        std::lock_guard<std::mutex> lk(mModel);
        --mPendingAdds;
        if (err.empty()) {
            t.file = file;
            t.peaks = std::move(peaks);
            t.renderedBpm = bpm;
            t.renderedHash = inst::renderHash(t.part, bpm);
            err = addTrackLocked(std::move(t), std::move(audio));
            if (!err.empty()) std::remove((audioDir + "/" + file).c_str());
            else applySongToVoiceLocked();  // con instrumentos, el auto-tune sigue su tonalidad
        }
        bump(kVerMusic);
    }
    if (!err.empty()) {
        setMessage("No se agregó el instrumento: " + err);
        return;
    }
    markDirty();
    setMessage(name + " agregado");
}

void Studio::changePatternLocked(TrackModel &t, inst::Pattern p, const std::string &key) {
    inst::normalize(p, t.part.sound.kind);
    if (p == t.part.pattern) return;
    const int64_t now = nowMs();
    const bool together = !key.empty() && key == mLastKey && now - mLastKeyMs < 1500;
    mLastKey = key;
    mLastKeyMs = now;
    if (!together) pushUndoLocked(snapshotLocked());
    t.part.pattern = std::move(p);
    scheduleRenderLocked(t);
    markDirty();
}

int Studio::instToggle(int id, int tick, int pitch, int lenTicks, bool chord) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || !t->isInst || tick < 0) return -1;
    inst::Pattern p = t->part.pattern;
    const int idx = inst::findNote(p, tick, pitch);
    int result;
    if (idx >= 0) {
        p.notes.erase(p.notes.begin() + idx);
        result = 0;
    } else {
        std::vector<int> add = {pitch};
        if (chord && t->part.sound.kind != inst::kDrums) {
            int key;
            bool minor;
            keyLocked(key, minor);
            add = inst::chord(pitch, key, minor);
        }
        const int len = std::clamp(lenTicks, 1, inst::kMaxBars * inst::kBarTicks);
        for (int q : add) {
            const int other = inst::findNote(p, tick, q);
            if (other >= 0) p.notes.erase(p.notes.begin() + other);
            p.notes.push_back({tick, len, q, 100});
        }
        result = 1;
    }
    changePatternLocked(*t, std::move(p), "");
    return result;
}

int Studio::instRoll(int id, int step, int pitch) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || !t->isInst || step < 0) return -1;
    inst::Pattern p = t->part.pattern;
    const int from = step * inst::kStepTicks, to = from + inst::kStepTicks;
    int count = 0;
    p.notes.erase(std::remove_if(p.notes.begin(), p.notes.end(),
                                 [&](const inst::Note &n) {
                                     const bool in = n.pitch == pitch && n.tick >= from && n.tick < to;
                                     if (in) ++count;
                                     return in;
                                 }),
                  p.notes.end());
    const int next = count % 4 + 1;  // 1 → 2 → 3 → 4 → 1
    const int len = inst::kStepTicks / next;
    for (int k = 0; k < next; ++k) p.notes.push_back({from + k * len, len, pitch, k == 0 ? 100 : 78});
    changePatternLocked(*t, std::move(p), "");
    return next;
}

bool Studio::instSetNote(int id, int index, int tick, int len, int pitch, int vel) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || !t->isInst || index < 0 || index >= static_cast<int>(t->part.pattern.notes.size())) return false;
    inst::Pattern p = t->part.pattern;
    p.notes[static_cast<size_t>(index)] = {tick, len, pitch, vel};
    changePatternLocked(*t, std::move(p), "nota-" + std::to_string(id) + "-" + std::to_string(index));
    return true;
}

bool Studio::instRemoveNote(int id, int index) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || !t->isInst || index < 0 || index >= static_cast<int>(t->part.pattern.notes.size())) return false;
    inst::Pattern p = t->part.pattern;
    p.notes.erase(p.notes.begin() + index);
    changePatternLocked(*t, std::move(p), "");
    return true;
}

void Studio::instClear(int id) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || !t->isInst) return;
    inst::Pattern p = t->part.pattern;
    p.notes.clear();
    changePatternLocked(*t, std::move(p), "");
}

void Studio::instSetPattern(int id, int bars, int repeats, float swing) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || !t->isInst) return;
    inst::Pattern p = t->part.pattern;
    p.bars = bars;
    p.repeats = repeats;
    p.swing = swing;
    changePatternLocked(*t, std::move(p), "patron-" + std::to_string(id));
}

void Studio::instSetPreset(int id, int preset) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || !t->isInst) return;
    inst::Sound s = t->part.sound;
    s.preset = preset;
    s = inst::clampSound(s);
    if (s == t->part.sound) return;
    t->part.sound = s;
    t->info = std::string("instrumento · ") + inst::presetName(s.kind, s.preset);
    bump(kVerTracks);
    scheduleRenderLocked(*t);
    markDirty();
}

void Studio::instSetParam(int id, int param, float value) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || !t->isInst || param < 0 || param >= inst::kParamCount) return;
    const float v = inst::clampParam(param, value);
    if (t->part.sound.values[static_cast<size_t>(param)] == v) return;
    t->part.sound.values[static_cast<size_t>(param)] = v;
    scheduleRenderLocked(*t);
    markDirty();
}

bool Studio::instIdea(int id) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *t = findLocked(id);
    if (!t || !t->isInst) return false;
    int key;
    bool minor;
    keyLocked(key, minor);
    const int variant = ++mIdeas[id];
    inst::Pattern p = inst::starter(t->part.sound, key, minor, t->part.pattern.bars, variant);
    p.repeats = t->part.pattern.repeats;
    changePatternLocked(*t, std::move(p), "");
    return true;
}

// ---------------------------------------------------------------- tocar en vivo

int64_t Studio::instFileFrameLocked(const TrackModel &t, int64_t frame) const {
    for (const Region &r : t.regions)
        if (frame >= r.start && frame < r.end()) return r.src + (frame - r.start);
    return -1;
}

void Studio::instPlay(int id, int pitch, int vel, bool down) {
    std::lock_guard<std::mutex> lk(mModel);
    TrackModel *tp = findLocked(id);
    if (!tp || !tp->isInst) return;
    TrackModel &t = *tp;
    const float v = std::clamp(vel, 1, 127) / 127.0f;
    if (down) mInstLive.play(t.part.sound, pitch, v, t.mute ? 0.0f : dbToGain(t.volDb));
    else mInstLive.release(pitch);
    if (!mRec.on || mRec.id != id || !mMixer.isPlaying()) return;

    // Grabando: la nota entra al patrón en el paso más cercano a lo que se estaba oyendo.
    const int lo = mLatOut.load();
    const int64_t heard = mMixer.position() - (lo >= 0 ? lo : kRate / 100) - kTouchLatency;
    const int64_t f = instFileFrameLocked(t, std::max<int64_t>(0, heard));
    if (f < 0) return;
    const double bpm = bpmLocked();
    const double stepFrames = inst::kStepTicks * inst::framesPerTick(bpm, kRate);
    const int steps = t.part.pattern.bars * inst::kStepsPerBar;
    const double loop = inst::patternFrames(t.part.pattern, bpm, kRate);
    const bool drums = t.part.sound.kind == inst::kDrums;
    if (down) {
        if (!drums) {
            mRec.down[pitch] = f;
            return;
        }
    } else if (drums || !mRec.down.count(pitch)) {
        return;
    }
    int64_t start = f, lenSteps = 1;
    if (!drums) {
        start = mRec.down[pitch];
        mRec.down.erase(pitch);
        lenSteps = std::max<int64_t>(1, std::llround(static_cast<double>(f - start) / stepFrames));
    }
    const double in = std::fmod(static_cast<double>(start), loop);
    const int step = static_cast<int>(std::llround(in / stepFrames)) % steps;
    inst::Pattern p = t.part.pattern;
    const int tick = step * inst::kStepTicks;
    const int other = inst::findNote(p, tick, pitch);
    if (other >= 0 && p.notes[static_cast<size_t>(other)].tick == tick) p.notes.erase(p.notes.begin() + other);
    p.notes.push_back({tick, static_cast<int>(std::min<int64_t>(lenSteps, steps)) * inst::kStepTicks, pitch,
                       std::clamp(vel, 1, 127)});
    inst::normalize(p, t.part.sound.kind);
    t.part.pattern = std::move(p);  // (el paso de deshacer se guardó al empezar a grabar)
    scheduleRenderLocked(t);
    markDirty();
}

void Studio::instRecord(int id, bool on) {
    std::lock_guard<std::mutex> lk(mModel);
    if (on) {
        const TrackModel *t = findLocked(id);
        if (!t || !t->isInst) return;
        pushUndoLocked(snapshotLocked());  // todo lo grabado se deshace junto
        mRec = NoteRec();
        mRec.on = true;
        mRec.id = id;
    } else {
        mRec = NoteRec();
    }
    bump(kVerMusic);
}

bool Studio::instRecording() const {
    std::lock_guard<std::mutex> lk(mModel);
    return mRec.on;
}

double Studio::instPosition(int id) const {
    if (!mMixer.isRolling()) return -1.0;
    std::lock_guard<std::mutex> lk(mModel);
    const TrackModel *t = findLocked(id);
    if (!t || !t->isInst) return -1.0;
    // Lo que se está oyendo (la salida va un poco atrás de lo que calcula el mezclador).
    const int lo = mLatOut.load();
    const int64_t f = instFileFrameLocked(*t, std::max<int64_t>(0, mMixer.position() - (lo >= 0 ? lo : 0)));
    if (f < 0) return -1.0;
    const double bpm = bpmLocked();
    const double loop = inst::patternFrames(t->part.pattern, bpm, kRate);
    if (f >= loop * t->part.pattern.repeats) return -1.0;  // ya es la cola
    const double stepFrames = inst::kStepTicks * inst::framesPerTick(bpm, kRate);
    return std::fmod(static_cast<double>(f), loop) / stepFrames;
}

void Studio::setCreating(bool on, int loopId) {
    std::lock_guard<std::mutex> lk(mModel);
    mCreating = on;
    mLoopId = on ? loopId : 0;
    mMixer.setKeepAwake(on);
    if (!on) {
        mInstLive.releaseAll();
        if (mRec.on) mRec = NoteRec();
    }
    applyLoopLocked();
}

void Studio::applyLoopLocked() {
    const TrackModel *t = mLoopId > 0 ? findLocked(mLoopId) : nullptr;
    if (!t || !t->isInst || t->regions.empty()) {
        mMixer.setLoop(0, 0);
        return;
    }
    // Se repite una vuelta del patrón. Desde la segunda (si hay), así lo que queda sonando de una
    // vuelta (la cola de un 808 o un platillo) sigue igual al volver.
    const double loop = inst::patternFrames(t->part.pattern, bpmLocked(), kRate);
    const Region &r = t->regions.front();
    const int64_t offset = r.start - r.src;  // dónde queda el inicio del archivo
    const double from = t->part.pattern.repeats >= 2 ? loop : 0.0;
    mMixer.setLoop(offset + static_cast<int64_t>(std::llround(from)), offset + static_cast<int64_t>(std::llround(from + loop)));
}
