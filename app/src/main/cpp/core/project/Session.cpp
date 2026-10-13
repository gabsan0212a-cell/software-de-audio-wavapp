#include "project/Session.h"

#include <algorithm>
#include <cmath>

#include "dsp/Equalizer.h"

namespace {
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
        tracks.push(std::move(jt));
    }
    Json master = Json::object();
    master.set("volDb", step(p.master.volDb, 0.5));
    master.set("mute", p.master.mute);
    master.set("limitador", p.master.limiter);
    master.set("notch", p.master.notch);
    Json exp = Json::object();
    exp.set("bits", p.master.exportBits);
    exp.set("rate", p.master.exportRate);

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
    j.set("entrada", std::move(in));
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
        if (t.file.empty()) continue;
        p.nextId = std::max(p.nextId, t.id + 1);
        p.tracks.push_back(std::move(t));
    }
    const Json &in = j["entrada"];
    p.inputGainDb = static_cast<float>(in["gananciaDb"].asDouble(0));
    p.monitorDb = static_cast<float>(in["escuchaDb"].asDouble(0));
    eqFromJson(in["eq"], p.input);
    return true;
}
