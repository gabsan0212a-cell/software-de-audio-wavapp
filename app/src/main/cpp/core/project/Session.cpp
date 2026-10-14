#include "project/Session.h"

#include <algorithm>
#include <cmath>

#include "dsp/Equalizer.h"

namespace {

constexpr int kVoiceVersion = 2;  // ajustes de la voz en vivo (ver la migración al leer)
// Los valores se guardan redondeados a su paso (así el archivo queda limpio: 1.4 y no 1.39999998).
double step(double v, double s) { return std::round(v / s) * s; }

Json eqToJson(const TrackModel &t) {
    Json bands = Json::array();
    for (const EqBandModel &b : t.bands) {
        Json jb = Json::object();
        jb.set("tipo", b.type);
        jb.set("f", step(b.freq, 0.1));
        jb.set("g", step(b.gain, 0.5));
        jb.set("q", step(b.q, 0.01));
        jb.set("on", b.on);
        bands.push(std::move(jb));
    }
    Json eq = Json::object();
    eq.set("bypass", t.eqBypass);
    eq.set("auto", t.eqAuto);
    eq.set("bandas", std::move(bands));
    return eq;
}

void eqFromJson(const Json &eq, TrackModel &t) {
    t.eqBypass = eq["bypass"].asBool(false);
    t.eqAuto = eq["auto"].asBool(true);
    const Json &bs = eq["bandas"];
    for (size_t k = 0; k < bs.size() && k < static_cast<size_t>(kMaxBands); ++k) {
        const Json &jb = bs[k];
        EqBandModel b;
        b.slot = static_cast<int>(k);
        b.type = std::clamp(static_cast<int>(jb["tipo"].asInt(0)), 0, kFilterTypes - 1);
        b.freq = static_cast<float>(jb["f"].asDouble(1000));
        b.gain = static_cast<float>(jb["g"].asDouble(0));
        b.q = static_cast<float>(jb["q"].asDouble(1.4));
        b.on = jb["on"].asBool(true);
        t.bands.push_back(b);
    }
}

const char *const kAutoKeys[autom::kParamCount] = {"vol", "pan"};

// {"modo": 1, "vol": [[cuadro, dB], ...], "pan": [[cuadro, paneo], ...]}
Json autoToJson(const TrackModel &t) {
    Json a = Json::object();
    a.set("modo", t.autoMode);
    for (int k = 0; k < autom::kParamCount; ++k) {
        Json pts = Json::array();
        for (const autom::Point &p : t.autoPts[k].get()) {
            Json jp = Json::array();
            jp.push(p.frame).push(step(p.value, k == autom::kPan ? 1.0 : 0.1));
            pts.push(std::move(jp));
        }
        a.set(kAutoKeys[k], std::move(pts));
    }
    return a;
}

void autoFromJson(const Json &a, TrackModel &t) {
    const int mode = static_cast<int>(a["modo"].asInt(1));
    t.autoMode = mode >= 0 && mode <= 2 ? mode : 1;
    for (int k = 0; k < autom::kParamCount; ++k) {
        const Json &pts = a[kAutoKeys[k]];
        std::vector<autom::Point> v;
        for (size_t i = 0; i < pts.size() && i < static_cast<size_t>(autom::kMaxPoints); ++i) {
            const Json &jp = pts[i];
            if (jp.size() < 2) continue;
            v.push_back({jp[0].asInt(), static_cast<float>(jp[1].asDouble(0))});
        }
        autom::normalize(k, v);
        t.autoPts[k] = std::move(v);
    }
}

}  // namespace

Json sessionToJson(const ProjectModel &p) {
    Json tracks = Json::array();
    for (const TrackModel &t : p.tracks) {
        Json eq = eqToJson(t);

        Json regions = Json::array();
        for (const Region &r : t.regions) {
            Json jr = Json::array();
            jr.push(r.start).push(r.src).push(r.length).push(r.fadeIn).push(r.fadeOut);
            regions.push(std::move(jr));
        }

        Json jt = Json::object();
        jt.set("id", t.id);
        jt.set("nombre", t.name);
        jt.set("archivo", t.file);
        jt.set("info", t.info);
        jt.set("volDb", step(t.volDb, 0.5));
        jt.set("pan", t.pan);
        jt.set("mute", t.mute);
        jt.set("solo", t.solo);
        jt.set("fase", t.phase);
        jt.set("eq", std::move(eq));
        jt.set("pedazos", std::move(regions));
        jt.set("auto", autoToJson(t));
        Json fxj = Json::object();
        for (int k = 0; k < fx::kCount; ++k) fxj.set(fx::info(k).key, static_cast<double>(t.fx[static_cast<size_t>(k)]));
        jt.set("efectos", std::move(fxj));
        tracks.push(std::move(jt));
    }
    Json master = Json::object();
    master.set("volDb", step(p.master.volDb, 0.5));
    master.set("mute", p.master.mute);
    master.set("limitador", p.master.limiter);
    master.set("notch", p.master.notch);
    master.set("objetivoLufs", p.master.loudTarget);
    Json exp = Json::object();
    exp.set("bits", p.master.exportBits);
    exp.set("rate", p.master.exportRate);
    exp.set("ajustarLufs", p.master.exportNormalize);

    Json j = Json::object();
    j.set("version", 1);
    j.set("siguienteId", p.nextId);
    j.set("master", std::move(master));
    j.set("exportar", std::move(exp));
    j.set("pistas", std::move(tracks));
    Json in = Json::object();
    in.set("gananciaDb", step(p.inputGainDb, 0.5));
    in.set("escuchaDb", step(p.monitorDb, 0.5));
    in.set("eq", eqToJson(p.input));
    Json voz = Json::object();
    for (int k = 0; k < vfx::kCount; ++k) voz.set(vfx::info(k).key, static_cast<double>(p.voice[static_cast<size_t>(k)]));
    voz.set("version", kVoiceVersion);
    in.set("voz", std::move(voz));
    j.set("entrada", std::move(in));
    if (p.song.ready()) {
        const SongModel &c = p.song;
        Json cj = Json::object();
        cj.set("fuente", c.sourceId);
        cj.set("instrumental", c.instrumentalId);
        cj.set("voz", c.vocalId);
        cj.set("tonica", c.key);
        cj.set("menor", c.minor);
        cj.set("claridad", step(c.keyScore, 0.01f));
        cj.set("bpm", step(c.bpm, 0.01f));
        auto lista = [](const std::vector<float> &v, float paso) {
            Json a = Json::array();
            for (float x : v) a.push(step(x, paso));
            return a;
        };
        cj.set("pulsos", lista(c.beats, 0.001f));
        cj.set("inicios", lista(c.vocalStart, 0.01f));
        cj.set("finales", lista(c.vocalEnd, 0.01f));
        cj.set("envolvente", lista(c.envelope, 0.01f));
        cj.set("notas", lista(std::vector<float>(c.notes.begin(), c.notes.end()), 0.001f));
        j.set("cancion", std::move(cj));
    }
    return j;
}

bool sessionFromJson(const Json &j, ProjectModel &p) {
    if (!j.isObject()) return false;
    p = ProjectModel();
    p.nextId = static_cast<int>(j["siguienteId"].asInt(1));
    const Json &m = j["master"];
    p.master.volDb = static_cast<float>(m["volDb"].asDouble(0));
    p.master.mute = m["mute"].asBool(false);
    p.master.limiter = m["limitador"].asBool(true);
    const int notch = static_cast<int>(m["notch"].asInt(0));
    p.master.notch = notch == 50 || notch == 60 ? notch : 0;
    const Json &e = j["exportar"];
    const int bits = static_cast<int>(e["bits"].asInt(24));
    p.master.exportBits = bits == 16 || bits == 32 ? bits : 24;
    p.master.exportRate = e["rate"].asInt(48000) == 44100 ? 44100 : 48000;
    const int target = static_cast<int>(m["objetivoLufs"].asInt(-14));
    p.master.loudTarget = target == -16 || target == -23 ? target : -14;
    p.master.exportNormalize = e["ajustarLufs"].asBool(false);

    const Json &ts = j["pistas"];
    for (size_t i = 0; i < ts.size(); ++i) {
        const Json &o = ts[i];
        TrackModel t;
        t.id = static_cast<int>(o["id"].asInt(p.nextId));
        t.name = o["nombre"].asString("pista");
        t.file = o["archivo"].asString();
        t.info = o["info"].asString();
        t.volDb = static_cast<float>(o["volDb"].asDouble(0));
        t.pan = static_cast<int>(o["pan"].asInt(0));
        t.mute = o["mute"].asBool(false);
        t.solo = o["solo"].asBool(false);
        t.phase = o["fase"].asBool(false);
        eqFromJson(o["eq"], t);
        if (o.has("pedazos")) {
            t.hasRegions = true;
            const Json &rs = o["pedazos"];
            for (size_t k = 0; k < rs.size(); ++k) {
                const Json &a = rs[k];
                if (a.size() < 3) continue;
                t.regions.push_back({a[0].asInt(), a[1].asInt(), a[2].asInt(), a[3].asInt(), a[4].asInt()});
            }
        }
        autoFromJson(o["auto"], t);
        const Json &fxj = o["efectos"];
        for (int k = 0; k < fx::kCount; ++k)
            t.fx[static_cast<size_t>(k)] =
                fx::clampValue(k, static_cast<float>(fxj[fx::info(k).key].asDouble(fx::info(k).def)));
        if (t.file.empty()) continue;
        p.nextId = std::max(p.nextId, t.id + 1);
        p.tracks.push_back(std::move(t));
    }
    const Json &in = j["entrada"];
    p.inputGainDb = static_cast<float>(in["gananciaDb"].asDouble(0));
    p.monitorDb = static_cast<float>(in["escuchaDb"].asDouble(0));
    eqFromJson(in["eq"], p.input);
    const Json &voz = in["voz"];
    for (int k = 0; k < vfx::kCount; ++k)
        p.voice[static_cast<size_t>(k)] = vfx::clampValue(k, static_cast<float>(voz[vfx::info(k).key].asDouble(vfx::info(k).def)));
    // Antes de la versión 2 la afinación por omisión (20 ms) y la de "Paraíso" (12 ms) casi no se
    // notaban: se pasan a las nuevas (5 y 4 ms). Lo que se haya puesto a mano se queda.
    if (voz.isObject() && voz["version"].asInt(1) < 2) {
        float &speed = p.voice[static_cast<size_t>(vfx::kTuneSpeed)];
        if (speed == 20.0f) speed = vfx::info(vfx::kTuneSpeed).def;
        else if (speed == 12.0f) speed = 4.0f;
    }
    const Json &cj = j["cancion"];
    if (cj.isObject() && cj["instrumental"].asInt(0) > 0) {
        SongModel &c = p.song;
        c.sourceId = static_cast<int>(cj["fuente"].asInt(0));
        c.instrumentalId = static_cast<int>(cj["instrumental"].asInt(0));
        c.vocalId = static_cast<int>(cj["voz"].asInt(0));
        const int key = static_cast<int>(cj["tonica"].asInt(-1));
        c.key = key >= 0 && key < 12 ? key : -1;
        c.minor = cj["menor"].asBool(false);
        c.keyScore = static_cast<float>(cj["claridad"].asDouble(0));
        c.bpm = std::max(0.0f, static_cast<float>(cj["bpm"].asDouble(0)));
        auto lista = [](const Json &a, std::vector<float> &v) {
            for (size_t k = 0; k < a.size(); ++k) v.push_back(static_cast<float>(a[k].asDouble(0)));
        };
        lista(cj["pulsos"], c.beats);
        lista(cj["inicios"], c.vocalStart);
        lista(cj["finales"], c.vocalEnd);
        if (c.vocalEnd.size() != c.vocalStart.size()) c.vocalStart.clear(), c.vocalEnd.clear();
        lista(cj["envolvente"], c.envelope);
        const Json &notas = cj["notas"];
        for (size_t k = 0; k < 12 && k < notas.size(); ++k) c.notes[k] = static_cast<float>(notas[k].asDouble(0));
    }
    return true;
}
