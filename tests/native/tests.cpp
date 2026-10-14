// Pruebas del núcleo nativo (sin Oboe ni Android). Se compilan y corren en Linux:
//   tests/native/run.sh
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Studio.h"
#include "dsp/AnalysisView.h"
#include "dsp/Analyzer.h"
#include "dsp/AutoTune.h"
#include "dsp/Delay.h"
#include "dsp/DriftFifo.h"
#include "dsp/Dynamics.h"
#include "dsp/Equalizer.h"
#include "dsp/Limiter.h"
#include "dsp/Loudness.h"
#include "dsp/Pitch.h"
#include "dsp/PitchShifter.h"
#include "dsp/PitchTracker.h"
#include "dsp/Resampler.h"
#include "dsp/Reverb.h"
#include "dsp/SongAnalysis.h"
#include "dsp/VoiceDynamics.h"
#include "engine/Automation.h"
#include "engine/Effects.h"
#include "engine/Exporter.h"
#include "engine/Importer.h"
#include "engine/LiveInput.h"
#include "engine/Meters.h"
#include "engine/Mixer.h"
#include "engine/ShowRecorder.h"
#include "engine/VoiceFx.h"
#include "io/Json.h"
#include "io/MappedAudio.h"
#include "io/WavReader.h"
#include "io/WavWriter.h"
#include "project/Edit.h"
#include "project/Session.h"
#include "sep/Half.h"
#include "sep/MelRoformer.h"
#include "sep/ModelFile.h"
#include "sep/Separator.h"
#include "sep/Stft.h"
#include "sep/ThreadPool.h"

// ---------------------------------------------------------------- mini framework
static int gChecks = 0, gFailures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++gChecks;                                                               \
        if (!(cond)) {                                                           \
            ++gFailures;                                                         \
            std::printf("  FALLA %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                        \
    } while (0)
#define CHECK_NEAR(a, b, tol)                                                                      \
    do {                                                                                           \
        ++gChecks;                                                                                 \
        const double _a = (a), _b = (b);                                                           \
        if (!(std::fabs(_a - _b) <= (tol))) {                                                      \
            ++gFailures;                                                                           \
            std::printf("  FALLA %s:%d: %s = %.9g, esperado %.9g ± %g\n", __FILE__, __LINE__, #a, \
                        _a, _b, static_cast<double>(tol));                                         \
        }                                                                                          \
    } while (0)

struct TestCase {
    const char *name;
    void (*fn)();
};
static std::vector<TestCase> &tests() {
    static std::vector<TestCase> t;
    return t;
}
struct Registrar {
    Registrar(const char *n, void (*f)()) { tests().push_back({n, f}); }
};
#define TEST(name)                                  \
    static void name();                             \
    static Registrar reg_##name(#name, name);       \
    static void name()

// ---------------------------------------------------------------- utilidades
static std::string gDir;

static std::string tmpPath(const std::string &name) { return gDir + "/" + name; }

static void put(std::vector<uint8_t> &v, uint32_t x, int bytes) {
    for (int i = 0; i < bytes; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

struct WavSpec {
    int format = 1;        // 1 PCM, 3 float
    int channels = 2;
    int rate = 48000;
    int bits = 16;         // bits en el campo bitsPerSample
    int container = 0;     // bytes por muestra (0 = bits/8)
    int validBits = 0;     // extensible
    bool extensible = false;
    int blockAlign = -1;   // -1 = correcto
    int64_t byteRate = -1; // -1 = correcto
    bool dataSizeZero = false;
    bool listBefore = true;
    bool junkAfter = false;
};

// Escribe un WAV con muestras gen(cuadro, canal) en -1..1.
static void makeWav(const std::string &path, const WavSpec &s, int64_t frames,
                    const std::function<double(int64_t, int)> &gen) {
    const int cont = s.container ? s.container : s.bits / 8;
    std::vector<uint8_t> data;
    for (int64_t f = 0; f < frames; ++f) {
        for (int c = 0; c < s.channels; ++c) {
            const double v = std::max(-1.0, std::min(1.0, gen(f, c)));
            if (s.format == 3) {
                if (cont == 4) {
                    float x = static_cast<float>(v);
                    uint32_t u;
                    std::memcpy(&u, &x, 4);
                    put(data, u, 4);
                } else {
                    double x = v;
                    uint64_t u;
                    std::memcpy(&u, &x, 8);
                    put(data, static_cast<uint32_t>(u), 4);
                    put(data, static_cast<uint32_t>(u >> 32), 4);
                }
            } else if (cont == 1) {
                data.push_back(static_cast<uint8_t>(std::lround(v * 127) + 128));
            } else {
                // entero con signo alineado a la izquierda en `cont` bytes con `bits` útiles
                const int useful = s.validBits ? s.validBits : s.bits;
                const double scale = std::ldexp(1.0, useful - 1) - 1;
                const int64_t q = std::llround(v * scale);
                const uint64_t u = static_cast<uint64_t>(q) << (cont * 8 - useful);
                put(data, static_cast<uint32_t>(u & 0xFFFFFFFFull), cont);
            }
        }
    }
    const int ba = s.blockAlign >= 0 ? s.blockAlign : s.channels * cont;
    const uint32_t br = s.byteRate >= 0 ? static_cast<uint32_t>(s.byteRate) : s.rate * s.channels * cont;
    std::vector<uint8_t> fmt;
    put(fmt, s.extensible ? 0xFFFE : s.format, 2);
    put(fmt, s.channels, 2);
    put(fmt, s.rate, 4);
    put(fmt, br, 4);
    put(fmt, ba, 2);
    put(fmt, s.bits, 2);
    if (s.extensible) {
        put(fmt, 22, 2);
        put(fmt, s.validBits ? s.validBits : s.bits, 2);
        put(fmt, 3, 4);
        put(fmt, s.format, 2);
        for (int i = 0; i < 14; ++i) fmt.push_back(0);
    }
    std::vector<uint8_t> body = {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '};
    put(body, static_cast<uint32_t>(fmt.size()), 4);
    body.insert(body.end(), fmt.begin(), fmt.end());
    if (s.listBefore) {
        const char l[] = {'L', 'I', 'S', 'T', 3, 0, 0, 0, 'a', 'b', 'c', 0};
        body.insert(body.end(), l, l + 12);
    }
    body.insert(body.end(), {'d', 'a', 't', 'a'});
    put(body, s.dataSizeZero ? 0 : static_cast<uint32_t>(data.size()), 4);
    body.insert(body.end(), data.begin(), data.end());
    if (data.size() & 1) body.push_back(0);
    if (s.junkAfter) {
        const char l[] = {'L', 'I', 'S', 'T', 8, 0, 0, 0, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f};
        body.insert(body.end(), l, l + 16);
    }
    std::vector<uint8_t> file = {'R', 'I', 'F', 'F'};
    put(file, static_cast<uint32_t>(body.size()), 4);
    file.insert(file.end(), body.begin(), body.end());
    FILE *f = std::fopen(path.c_str(), "wb");
    std::fwrite(file.data(), 1, file.size(), f);
    std::fclose(f);
}

static double sine(double freq, double rate, int64_t f, double amp = 0.5) {
    return amp * std::sin(2 * M_PI * freq * static_cast<double>(f) / rate);
}

// Escribe una pista interna (float 32) con valores constantes por canal.
static std::unique_ptr<MappedAudio> constTrack(const std::string &name, int channels, int64_t frames,
                                               float l, float r = 0.0f) {
    const std::string p = tmpPath(name);
    WavWriter w;
    w.open(p, channels, 48000);
    std::vector<float> buf(static_cast<size_t>(frames) * channels);
    for (int64_t f = 0; f < frames; ++f) {
        buf[f * channels] = l;
        if (channels == 2) buf[f * channels + 1] = r;
    }
    w.write(buf.data(), static_cast<size_t>(frames));
    w.close();
    std::string err;
    auto a = MappedAudio::open(p, err);
    if (!a) std::printf("  no se pudo abrir %s: %s\n", p.c_str(), err.c_str());
    return a;
}

// Escribe una pista interna con muestras gen(cuadro, canal).
static std::unique_ptr<MappedAudio> genTrack(const std::string &name, int channels, int64_t frames,
                                             const std::function<float(int64_t, int)> &gen) {
    const std::string p = tmpPath(name);
    WavWriter w;
    w.open(p, channels, 48000);
    std::vector<float> buf(static_cast<size_t>(frames) * channels);
    for (int64_t f = 0; f < frames; ++f)
        for (int c = 0; c < channels; ++c) buf[f * channels + c] = gen(f, c);
    w.write(buf.data(), static_cast<size_t>(frames));
    w.close();
    std::string err;
    return MappedAudio::open(p, err);
}

// Corre el mezclador y devuelve el pico de salida (L, R) de los últimos `medir` cuadros.
static std::pair<float, float> peakRun(Mixer &m, int frames, int medir) {
    std::vector<float> out(192 * 2);
    float pl = 0, pr = 0;
    int done = 0;
    while (done < frames) {
        const int n = std::min(192, frames - done);
        m.process(out.data(), n, 2);
        for (int i = 0; i < n; ++i) {
            if (done + i >= frames - medir) {
                pl = std::max(pl, std::fabs(out[2 * i]));
                pr = std::max(pr, std::fabs(out[2 * i + 1]));
            }
        }
        done += n;
    }
    return {pl, pr};
}

// Corre el mezclador `frames` cuadros y devuelve el último cuadro L/R.
static std::pair<float, float> run(Mixer &m, int frames, int block = 192) {
    std::vector<float> out(static_cast<size_t>(block) * 2);
    float l = 0, r = 0;
    while (frames > 0) {
        const int n = std::min(block, frames);
        m.process(out.data(), n, 2);
        l = out[(n - 1) * 2];
        r = out[(n - 1) * 2 + 1];
        frames -= n;
    }
    return {l, r};
}

// ---------------------------------------------------------------- lector WAV
TEST(lectorFormatos) {
    struct Case {
        const char *name;
        WavSpec spec;
        int expCh, expBits;
        bool isFloat;
        double tol;
    };
    std::vector<Case> cases;
    { WavSpec s; cases.push_back({"p16", s, 2, 16, false, 1e-4}); }
    { WavSpec s; s.bits = 24; s.extensible = true; cases.push_back({"p24ext", s, 2, 24, false, 1e-6}); }
    { WavSpec s; s.bits = 32; s.channels = 1; s.rate = 44100; s.dataSizeZero = true; cases.push_back({"p32", s, 1, 32, false, 1e-6}); }
    { WavSpec s; s.format = 3; s.bits = 32; s.extensible = true; s.rate = 96000; cases.push_back({"f32", s, 2, 32, true, 1e-6}); }
    { WavSpec s; s.format = 3; s.bits = 64; cases.push_back({"f64", s, 2, 64, true, 1e-6}); }
    { WavSpec s; s.bits = 8; s.channels = 1; s.rate = 22050; cases.push_back({"p8", s, 1, 8, false, 1e-2}); }
    { WavSpec s; s.bits = 32; s.validBits = 24; s.container = 4; s.extensible = true; cases.push_back({"p24in32", s, 2, 24, false, 1e-6}); }
    { WavSpec s; s.blockAlign = 0; s.byteRate = 0; cases.push_back({"ba0", s, 2, 16, false, 1e-4}); }
    { WavSpec s; s.blockAlign = 2; s.junkAfter = true; cases.push_back({"celular", s, 2, 16, false, 1e-4}); }
    for (auto &c : cases) {
        const std::string p = tmpPath(std::string(c.name) + ".wav");
        makeWav(p, c.spec, 1000, [&](int64_t f, int ch) { return (ch ? -1 : 1) * sine(440, c.spec.rate, f); });
        Clip clip;
        const std::string err = readWav(std::fopen(p.c_str(), "rb"), clip);
        if (!err.empty()) std::printf("  %s: %s\n", c.name, err.c_str());
        CHECK(err.empty());
        CHECK(clip.channels == c.expCh);
        CHECK(clip.bits == c.expBits);
        CHECK(clip.isFloat == c.isFloat);
        CHECK(clip.frames == 1000);
        CHECK(clip.sampleRate == c.spec.rate);
        double maxErr = 0;
        for (int64_t f = 0; f < clip.frames; ++f)
            for (int ch = 0; ch < clip.channels; ++ch)
                maxErr = std::max(maxErr, std::fabs(clip.data[f * clip.channels + ch] -
                                                    (ch ? -1 : 1) * sine(440, c.spec.rate, f)));
        if (maxErr > c.tol) std::printf("  %s: error máximo %g\n", c.name, maxErr);
        CHECK(maxErr <= c.tol);
    }
}

TEST(lectorCanalesMalEscritos) {
    // blockAlign = 2 y byteRate de mono: en realidad es mono (los canales estaban mal).
    WavSpec s;
    s.blockAlign = 2;
    s.byteRate = 48000 * 2;
    const std::string p = tmpPath("canales.wav");
    // Se escriben 2000 muestras de 16 bits seguidas: como mono son 2000 cuadros.
    makeWav(p, s, 1000, [&](int64_t f, int ch) { return sine(440, 48000, 2 * f + ch); });
    Clip clip;
    CHECK(readWav(std::fopen(p.c_str(), "rb"), clip).empty());
    CHECK(clip.channels == 1);
    CHECK(clip.frames == 2000);
}

TEST(lectorErrores) {
    const std::string p = tmpPath("no.wav");
    FILE *f = std::fopen(p.c_str(), "wb");
    std::fputs("hola, esto no es un wav", f);
    std::fclose(f);
    Clip clip;
    CHECK(readWav(std::fopen(p.c_str(), "rb"), clip) == "No es un archivo WAV");
    WavSpec s;
    s.format = 2;  // ADPCM
    s.bits = 16;
    const std::string q = tmpPath("adpcm.wav");
    makeWav(q, s, 10, [](int64_t, int) { return 0.0; });
    const std::string err = readWav(std::fopen(q.c_str(), "rb"), clip);
    CHECK(err.find("formato=2") != std::string::npos);
    CHECK(readWav(nullptr, clip) == "No se pudo abrir el archivo");
}

// ---------------------------------------------------------------- remuestreo
// Remuestrea un seno y mide la amplitud de la salida (en la parte central) y el error
// contra el seno ideal a la nueva frecuencia.
struct SineResult {
    double amp, errDb;
    int64_t frames;
};
static SineResult resampleSine(int inRate, int outRate, double freq, int chunk = 0) {
    const int64_t inFrames = inRate;  // 1 s
    std::vector<float> in(static_cast<size_t>(inFrames) * 2);
    for (int64_t f = 0; f < inFrames; ++f) in[2 * f] = in[2 * f + 1] = static_cast<float>(sine(freq, inRate, f));
    Resampler rs(2, inRate, outRate);
    std::vector<float> out;
    if (chunk <= 0) {
        rs.process(in.data(), static_cast<size_t>(inFrames), out);
    } else {
        std::mt19937 rng(1);
        int64_t pos = 0;
        while (pos < inFrames) {
            const int64_t n = std::min<int64_t>(inFrames - pos, 1 + rng() % chunk);
            rs.process(in.data() + pos * 2, static_cast<size_t>(n), out);
            pos += n;
        }
    }
    rs.finish(out);
    SineResult r{};
    r.frames = static_cast<int64_t>(out.size() / 2);
    double peak = 0, err2 = 0, ref2 = 0;
    for (int64_t f = outRate / 10; f < r.frames - outRate / 10; ++f) {
        const double ideal = sine(freq, outRate, f);
        peak = std::max(peak, std::fabs(static_cast<double>(out[2 * f])));
        err2 += (out[2 * f] - ideal) * (out[2 * f] - ideal);
        ref2 += ideal * ideal;
        if (out[2 * f] != out[2 * f + 1]) peak = -1;  // los canales deben ser idénticos
    }
    r.amp = peak;
    r.errDb = 10 * std::log10(err2 / ref2 + 1e-30);
    return r;
}

TEST(remuestreoCalidad) {
    SineResult r = resampleSine(44100, 48000, 1000);
    CHECK(r.frames == 48000);
    CHECK_NEAR(r.amp, 0.5, 0.0005);
    std::printf("  44.1k→48k, 1 kHz: error %.1f dB\n", r.errDb);
    CHECK(r.errDb < -90);

    r = resampleSine(44100, 48000, 18000);
    std::printf("  44.1k→48k, 18 kHz: amplitud %.5f, error %.1f dB\n", r.amp, r.errDb);
    CHECK_NEAR(r.amp, 0.5, 0.005);  // plano en la banda útil (±0.1 dB)

    r = resampleSine(48000, 44100, 1000);
    CHECK(r.frames == 44100);
    CHECK(r.errDb < -90);

    r = resampleSine(22050, 48000, 5000);
    CHECK(r.errDb < -90);

    r = resampleSine(96000, 48000, 10000);
    CHECK(r.frames == 48000);
    CHECK(r.errDb < -90);

    // Frecuencia rara: fases interpoladas.
    r = resampleSine(47999, 48000, 1000);
    std::printf("  47.999k→48k (interpolado), 1 kHz: error %.1f dB\n", r.errDb);
    CHECK(r.errDb < -85);
}

TEST(remuestreoRechazaAliasing) {
    // 30 kHz a 96k se volvería 18 kHz a 48k: el filtro debe quitarlo.
    const int inRate = 96000, outRate = 48000;
    std::vector<float> in(inRate);
    for (int f = 0; f < inRate; ++f) in[f] = static_cast<float>(sine(30000, inRate, f));
    Resampler rs(1, inRate, outRate);
    std::vector<float> out;
    rs.process(in.data(), in.size(), out);
    rs.finish(out);
    double peak = 0;
    for (size_t i = 4800; i + 4800 < out.size(); ++i) peak = std::max(peak, std::fabs(static_cast<double>(out[i])));
    const double db = 20 * std::log10(peak / 0.5 + 1e-30);
    std::printf("  96k→48k, 30 kHz: queda %.1f dB\n", db);
    CHECK(db < -90);

    // 23 kHz a 48k (por encima del Nyquist de 44.1k).
    std::vector<float> in2(48000);
    for (int f = 0; f < 48000; ++f) in2[f] = static_cast<float>(sine(23000, 48000, f));
    Resampler rs2(1, 48000, 44100);
    out.clear();
    rs2.process(in2.data(), in2.size(), out);
    rs2.finish(out);
    peak = 0;
    for (size_t i = 4410; i + 4410 < out.size(); ++i) peak = std::max(peak, std::fabs(static_cast<double>(out[i])));
    std::printf("  48k→44.1k, 23 kHz: queda %.1f dB\n", 20 * std::log10(peak / 0.5 + 1e-30));
    CHECK(20 * std::log10(peak / 0.5 + 1e-30) < -80);
}

TEST(remuestreoPorPartesIgualQueDeUnaVez) {
    SineResult a = resampleSine(44100, 48000, 3000, 0);
    SineResult b = resampleSine(44100, 48000, 3000, 777);
    CHECK(a.frames == b.frames);
    CHECK_NEAR(a.errDb, b.errDb, 1e-6);
}

TEST(remuestreoLongitudes) {
    CHECK(Resampler::outputFrames(44100, 44100, 48000) == 48000);
    CHECK(Resampler::outputFrames(1, 44100, 48000) == 2);
    CHECK(Resampler::outputFrames(48000, 48000, 44100) == 44100);
    CHECK(Resampler::outputFrames(3, 96000, 48000) == 2);
}

// ---------------------------------------------------------------- importar
TEST(importarDelCelular) {
    // Como el WAV del celular: estéreo 16 bits, bloque = 2, a 44.1 kHz.
    WavSpec s;
    s.rate = 44100;
    s.blockAlign = 2;
    const std::string src = tmpPath("cel.wav"), dst = tmpPath("cel-int.wav");
    makeWav(src, s, 44100, [](int64_t f, int ch) { return ch ? sine(500, 44100, f, 0.25) : sine(1000, 44100, f); });
    ImportInfo info;
    std::atomic<float> progress{0};
    const std::string err = importWav(std::fopen(src.c_str(), "rb"), dst, 48000, info, &progress);
    CHECK(err.empty());
    CHECK(info.srcRate == 44100);
    CHECK(info.srcChannels == 2);
    CHECK(info.srcBits == 16);
    CHECK(info.channels == 2);
    CHECK(!info.dualMono);
    CHECK(info.frames == 48000);
    CHECK_NEAR(progress.load(), 1.0, 1e-6);
    std::string e;
    auto a = MappedAudio::open(dst, e);
    CHECK(a != nullptr);
    if (!a) return;
    CHECK(a->channels() == 2);
    CHECK(a->sampleRate() == 48000);
    CHECK(a->frames() == 48000);
    double err2 = 0;
    for (int64_t f = 4800; f < 43200; ++f) {
        err2 = std::max(err2, std::fabs(a->data()[2 * f] - sine(1000, 48000, f)));
        err2 = std::max(err2, std::fabs(a->data()[2 * f + 1] - sine(500, 48000, f, 0.25)));
    }
    std::printf("  error máximo tras importar: %g\n", err2);
    CHECK(err2 < 1e-3);  // ~ ruido de 16 bits
    CHECK(access((dst + ".tmp").c_str(), F_OK) != 0);  // sin temporales
}

TEST(importarEstereoIdenticoQuedaMono) {
    WavSpec s;  // 48k, 16 bits, L = R
    const std::string src = tmpPath("dual.wav"), dst = tmpPath("dual-int.wav");
    makeWav(src, s, 4800, [](int64_t f, int) { return sine(440, 48000, f); });
    ImportInfo info;
    CHECK(importWav(std::fopen(src.c_str(), "rb"), dst, 48000, info).empty());
    CHECK(info.dualMono);
    CHECK(info.channels == 1);
    CHECK(info.frames == 4800);
    std::string e;
    auto a = MappedAudio::open(dst, e);
    CHECK(a && a->channels() == 1 && a->frames() == 4800);
}

TEST(importarErrores) {
    const std::string src = tmpPath("malo.wav"), dst = tmpPath("malo-int.wav");
    FILE *f = std::fopen(src.c_str(), "wb");
    std::fputs("RIFF....WAVEnada", f);
    std::fclose(f);
    ImportInfo info;
    CHECK(!importWav(std::fopen(src.c_str(), "rb"), dst, 48000, info).empty());
    CHECK(access(dst.c_str(), F_OK) != 0);
    // Carpeta que no existe: no se puede escribir.
    WavSpec s;
    const std::string ok = tmpPath("ok.wav");
    makeWav(ok, s, 100, [](int64_t, int) { return 0.1; });
    CHECK(!importWav(std::fopen(ok.c_str(), "rb"), tmpPath("no/existe.wav"), 48000, info).empty());
}

TEST(pistasDePrueba) {
    for (int i = 0; i < kDemoTracks; ++i) {
        const std::string p = tmpPath("demo" + std::to_string(i) + ".wav");
        CHECK(writeDemoTrack(i, p, 48000).empty());
        std::string e;
        auto a = MappedAudio::open(p, e);
        CHECK(a && a->channels() == 1 && a->frames() == 48000 * 12);
        if (!a) continue;
        double peak = 0;
        for (int64_t f = 0; f < a->frames(); ++f) peak = std::max(peak, std::fabs(static_cast<double>(a->data()[f])));
        CHECK(peak > 0.03 && peak < 0.5);
        CHECK(a->data()[0] == 0.0f);  // fundido de entrada
    }
}

TEST(picosParaLaOnda) {
    auto a = constTrack("picos.wav", 2, 1000, 0.5f, -0.25f);
    std::vector<float> p = computePeaks(*a, 256);
    CHECK(p.size() == 4 * 2 * 2);
    CHECK(p[0] == 0.0f && p[1] == 0.5f);    // L: min 0 (incluye el 0), max 0.5
    CHECK(p[2] == -0.25f && p[3] == 0.0f);  // R
}

// ---------------------------------------------------------------- mezclador
TEST(paneoComoWebAudio) {
    Mixer m(48000);
    m.setLimiter(false);
    CHECK(m.setTrack(0, constTrack("mono.wav", 1, 96000, 0.5f)));
    m.play();
    auto [l, r] = run(m, 9600);
    CHECK_NEAR(l, 0.5 * std::cos(M_PI / 4), 1e-4);  // mono al centro: -3 dB por lado
    CHECK_NEAR(r, 0.5 * std::cos(M_PI / 4), 1e-4);
    m.setTrackPan(0, 1.0f);
    std::tie(l, r) = run(m, 9600);
    CHECK_NEAR(l, 0.0, 1e-4);
    CHECK_NEAR(r, 0.5, 1e-4);
    m.takeTrack(0);

    CHECK(m.setTrack(1, constTrack("est.wav", 2, 96000, 0.25f, 0.5f)));
    m.seek(0);
    m.play();
    std::tie(l, r) = run(m, 9600);
    CHECK_NEAR(l, 0.25, 1e-4);  // estéreo al centro: sin cambio
    CHECK_NEAR(r, 0.5, 1e-4);
    m.setTrackPan(1, -1.0f);
    std::tie(l, r) = run(m, 9600);
    CHECK_NEAR(l, 0.75, 1e-4);  // todo a la izquierda: L + R
    CHECK_NEAR(r, 0.0, 1e-4);
    m.setTrackPan(1, 0.5f);
    std::tie(l, r) = run(m, 9600);
    CHECK_NEAR(l, 0.25 * std::cos(0.5 * M_PI / 2), 1e-4);
    CHECK_NEAR(r, 0.5 + 0.25 * std::sin(0.5 * M_PI / 2), 1e-4);
}

TEST(volumenMuteSoloPolaridad) {
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(0, constTrack("a.wav", 2, 480000, 0.2f, 0.2f));
    m.setTrack(1, constTrack("b.wav", 2, 480000, 0.1f, 0.1f));
    m.play();
    auto [l, r] = run(m, 9600);
    CHECK_NEAR(l, 0.3, 1e-4);
    m.setTrackGain(0, 0.5f);
    std::tie(l, r) = run(m, 9600);
    CHECK_NEAR(l, 0.2, 1e-4);
    m.setTrackMute(1, true);
    std::tie(l, r) = run(m, 9600);
    CHECK_NEAR(l, 0.1, 1e-4);
    m.setTrackMute(1, false);
    m.setTrackSolo(1, true);  // solo la pista 1 suena
    std::tie(l, r) = run(m, 9600);
    CHECK_NEAR(l, 0.1, 1e-4);
    m.setTrackSolo(0, true);  // dos en solo: suenan las dos
    std::tie(l, r) = run(m, 9600);
    CHECK_NEAR(l, 0.2, 1e-4);
    m.setTrackSolo(0, false);
    m.setTrackSolo(1, false);
    m.setTrackInvert(1, true);
    std::tie(l, r) = run(m, 9600);
    CHECK_NEAR(l, 0.0, 1e-4);  // 0.1 - 0.1
    m.setMasterGain(0.5f);
    m.setTrackInvert(1, false);
    std::tie(l, r) = run(m, 9600);
    CHECK_NEAR(l, 0.1, 1e-4);
    m.setMasterGain(0.0f);
    std::tie(l, r) = run(m, 9600);
    CHECK_NEAR(l, 0.0, 1e-5);
}

TEST(transporte) {
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(0, constTrack("t1.wav", 1, 48000, 0.1f));   // 1 s
    m.setTrack(3, constTrack("t2.wav", 1, 96000, 0.1f));   // 2 s
    CHECK(m.length() == 96000);
    CHECK(m.freeSlot() == 1);
    CHECK(m.position() == 0);
    run(m, 4800);
    CHECK(m.position() == 0);  // sin play no avanza
    m.play();
    run(m, 4800);
    CHECK(m.position() == 4800);
    m.pause();
    run(m, 4800);
    const int64_t p = m.position();
    CHECK(p > 4800 && p <= 4800 + 256 + 192);  // solo la rampa de salida
    run(m, 4800);
    CHECK(m.position() == p);
    m.seek(60000);
    CHECK(m.position() == 60000);
    run(m, 192);
    CHECK(m.position() == 60000);
    m.play();
    run(m, 9600);
    CHECK(m.position() == 69600);
    // salto mientras suena: primero baja la rampa, luego salta
    m.seek(1000);
    run(m, 4800);
    CHECK(m.position() > 1000 && m.position() < 1000 + 4800);
    m.stop();
    run(m, 1920);
    CHECK(m.position() == 0);
    CHECK(!m.isPlaying());
    // llega al final: se detiene y vuelve al inicio
    m.seek(90000);
    m.play();
    run(m, 9600);
    CHECK(!m.isPlaying());
    CHECK(m.position() == 0);
    // sin pistas no reproduce
    m.takeTrack(0);
    m.takeTrack(3);
    CHECK(m.length() == 0);
    m.play();
    CHECK(!m.isPlaying());
}

TEST(saltoJustoAlFinalNoDetiene) {
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(0, constTrack("fin.wav", 1, 48000, 0.1f));  // 1 s
    m.seek(46000);
    m.play();
    run(m, 1792, 64);  // quedan 208 cuadros: menos que la rampa del salto (256)
    CHECK(m.isPlaying());
    m.seek(0);
    run(m, 512, 64);
    CHECK(m.isPlaying());  // siguió desde el inicio
    CHECK(m.position() > 0 && m.position() < 1000);
    // Sin salto pendiente, al llegar al final se detiene y vuelve al inicio.
    m.seek(47000);
    run(m, 2048, 64);
    CHECK(!m.isPlaying() && m.position() == 0);
}

TEST(pistaMasCortaSeCalla) {
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(0, constTrack("c1.wav", 1, 4800, 0.2f));
    m.setTrack(1, constTrack("c2.wav", 1, 48000, 0.2f));
    m.setTrackPan(0, 1.0f);
    m.setTrackPan(1, 1.0f);
    m.play();
    auto [l, r] = run(m, 3840);
    CHECK_NEAR(r, 0.4, 1e-3);
    std::tie(l, r) = run(m, 3840);
    CHECK_NEAR(r, 0.2, 1e-3);
}

TEST(medidores) {
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(2, constTrack("med.wav", 2, 48000, 0.5f, 0.25f));
    m.play();
    run(m, 9600);
    float meters[kMeterCount];
    m.readMeters(meters);
    CHECK_NEAR(meters[0], 0.5, 1e-3);
    CHECK_NEAR(meters[1], 0.25, 1e-3);
    CHECK_NEAR(meters[3 + 4], 0.5, 1e-3);
    CHECK_NEAR(meters[4 + 4], 0.25, 1e-3);
    CHECK(meters[3] == 0.0f);
    m.readMeters(meters);  // se reinician al leer
    CHECK(meters[0] == 0.0f);
}

TEST(limitador) {
    Limiter lim;
    lim.setup(48000);
    std::printf("  compensación del limitador: +%.2f dB, salida a 0 dBFS: %.4f\n", lim.makeupDb(), lim.saturate(1.0f));
    CHECK(lim.makeupDb() > 0.0f && lim.makeupDb() < 3.0f);
    CHECK_NEAR(lim.saturate(0.5f), 0.5, 1e-6);  // por debajo del umbral no toca
    CHECK(lim.saturate(2.0f) < 1.0f);

    Mixer m(48000);  // limitador encendido por defecto
    m.setTrack(0, constTrack("fuerte.wav", 2, 96000, 2.0f, -2.0f));  // +6 dBFS
    m.play();
    float maxOut = 0;
    std::vector<float> out(384);
    for (int i = 0; i < 400; ++i) {
        m.process(out.data(), 192, 2);
        for (float v : out) maxOut = std::max(maxOut, std::fabs(v));
    }
    CHECK(maxOut <= 0.98f + 1e-6f);  // nunca pasa del techo del recorte suave
    float meters[kMeterCount];
    m.readMeters(meters);
    CHECK(meters[2] > 3.0f);  // está reduciendo

    // Señal baja: pasa con la compensación.
    Mixer q(48000);
    q.setTrack(0, constTrack("bajo.wav", 2, 96000, 0.1f, 0.1f));
    q.play();
    auto [l, r] = run(q, 48000);
    CHECK_NEAR(l, 0.1 * std::pow(10.0, lim.makeupDb() / 20), 1e-4);

    CHECK_NEAR(softClip(0.5f), 0.5, 1e-7);
    CHECK_NEAR(softClip(5.0f), 0.98, 1e-6);
    CHECK_NEAR(softClip(-5.0f), -0.98, 1e-6);
    CHECK(softClip(0.9f) < 0.9f && softClip(0.9f) > 0.85f);
}

TEST(salidaMonoYMultiCanal) {
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(0, constTrack("mc.wav", 2, 48000, 0.2f, 0.4f));
    m.play();
    std::vector<float> out(192 * 4);
    for (int i = 0; i < 50; ++i) m.process(out.data(), 192, 4);
    CHECK_NEAR(out[191 * 4], 0.2, 1e-4);
    CHECK_NEAR(out[191 * 4 + 1], 0.4, 1e-4);
    CHECK(out[191 * 4 + 2] == 0.0f && out[191 * 4 + 3] == 0.0f);
    for (int i = 0; i < 5; ++i) m.process(out.data(), 192, 1);
    CHECK_NEAR(out[191], 0.3, 1e-4);
}

// ---------------------------------------------------------------- EQ
TEST(eqCurvasComoWebAudio) {
    const double sr = 48000;
    Biquad c = designBiquad(kPeaking, 1000, 6, 1.4, sr);
    CHECK_NEAR(biquadMagnitudeDb(c, 1000, sr), 6.0, 0.01);
    CHECK_NEAR(biquadMagnitudeDb(c, 20, sr), 0.0, 0.05);
    CHECK_NEAR(biquadMagnitudeDb(c, 15000, sr), 0.0, 0.1);
    c = designBiquad(kPeaking, 1000, -9, 4, sr);
    CHECK_NEAR(biquadMagnitudeDb(c, 1000, sr), -9.0, 0.01);

    c = designBiquad(kLowShelf, 100, 6, 1, sr);
    CHECK_NEAR(biquadMagnitudeDb(c, 20, sr), 6.0, 0.4);
    CHECK_NEAR(biquadMagnitudeDb(c, 100, sr), 3.0, 0.05);  // a la mitad en su frecuencia
    CHECK_NEAR(biquadMagnitudeDb(c, 10000, sr), 0.0, 0.05);
    c = designBiquad(kHighShelf, 8000, -6, 1, sr);
    CHECK_NEAR(biquadMagnitudeDb(c, 20000, sr), -6.0, 0.5);
    CHECK_NEAR(biquadMagnitudeDb(c, 8000, sr), -3.0, 0.05);
    CHECK_NEAR(biquadMagnitudeDb(c, 100, sr), 0.0, 0.05);

    c = designBiquad(kHighPass, 1000, 0, std::sqrt(0.5), sr);
    CHECK_NEAR(biquadMagnitudeDb(c, 1000, sr), -3.01, 0.02);
    CHECK(biquadMagnitudeDb(c, 100, sr) < -38);  // 12 dB por octava
    CHECK_NEAR(biquadMagnitudeDb(c, 10000, sr), 0.0, 0.05);
    c = designBiquad(kLowPass, 1000, 0, std::sqrt(0.5), sr);
    CHECK_NEAR(biquadMagnitudeDb(c, 1000, sr), -3.01, 0.02);
    CHECK(biquadMagnitudeDb(c, 10000, sr) < -38);
    c = designBiquad(kNotch, 60, 0, 30, sr);
    CHECK(biquadMagnitudeDb(c, 60, sr) < -60);
    CHECK_NEAR(biquadMagnitudeDb(c, 120, sr), 0.0, 0.1);
    CHECK(filterHasGain(kPeaking) && filterHasGain(kHighShelf) && !filterHasGain(kHighPass));
}

TEST(eqAutoGananciaYRespuesta) {
    Equalizer e;
    e.setSampleRate(48000);
    CHECK(e.trimDb() == 0.0f);
    e.setBand(3, true, true, kPeaking, 1000, 6, 1.4f);
    CHECK_NEAR(e.trimDb(), -6.0, 0.05);
    float f[3] = {100, 1000, 10000}, db[3];
    e.response(-1, f, db, 3);
    CHECK_NEAR(db[1], 6.0, 0.01);
    e.setBand(5, true, true, kPeaking, 100, -12, 1.4f);
    e.response(-1, f, db, 3);
    CHECK(db[0] < -11.5);  // se suman
    CHECK_NEAR(e.trimDb(), -6.0, 0.1);
    e.response(5, f, db, 3);  // una sola banda
    CHECK_NEAR(db[1], 0.0, 0.2);
    e.setBand(3, true, false, kPeaking, 1000, 6, 1.4f);  // apagada
    CHECK_NEAR(e.trimDb(), 0.0, 1e-6);
    e.setBand(3, true, true, kPeaking, 1000, 6, 1.4f);
    e.setAutoGain(false);
    CHECK(e.trimDb() == 0.0f);
    e.setAutoGain(true);
    e.setBypass(true);
    CHECK(e.trimDb() == 0.0f);
    e.response(-1, f, db, 3);
    CHECK(db[0] == 0.0f && db[1] == 0.0f);
    e.clear();
    e.response(-1, f, db, 3);
    CHECK(db[1] == 0.0f && e.trimDb() == 0.0f);
}

TEST(eqSuenaComoLaCurva) {
    Mixer m(48000);
    m.setLimiter(false);
    // Tono de 1 kHz a 0.25 en estéreo y en mono.
    m.setTrack(0, genTrack("eq1k.wav", 2, 480000, [](int64_t f, int) {
                   return static_cast<float>(0.25 * std::sin(2 * M_PI * 1000 * f / 48000.0));
               }));
    m.play();
    auto [l, r] = peakRun(m, 24000, 4800);
    CHECK_NEAR(l, 0.25, 0.002);
    Equalizer *eq = m.trackEq(0);
    eq->setAutoGain(false);
    eq->setBand(0, true, true, kPeaking, 1000, 6, 1.4f);
    std::tie(l, r) = peakRun(m, 24000, 4800);
    CHECK_NEAR(l, 0.25 * std::pow(10, 6 / 20.0), 0.004);
    CHECK_NEAR(r, 0.25 * std::pow(10, 6 / 20.0), 0.004);
    eq->setAutoGain(true);  // la compensación lo regresa al nivel original
    std::tie(l, r) = peakRun(m, 24000, 4800);
    CHECK_NEAR(l, 0.25, 0.003);
    eq->setBand(0, true, true, kHighPass, 8000, 0, 0.7071f);  // pasa-altos: el tono casi desaparece
    std::tie(l, r) = peakRun(m, 24000, 4800);
    CHECK(l < 0.25 * 0.02);
    eq->setBypass(true);
    std::tie(l, r) = peakRun(m, 24000, 4800);
    CHECK_NEAR(l, 0.25, 0.002);
}

TEST(eqSinBandasNoTocaElAudio) {
    // Sin bandas (o en bypass) el resultado es idéntico bit a bit a no tener EQ.
    auto mk = [](const char *n) {
        return genTrack(n, 1, 48000, [](int64_t f, int) {
            return static_cast<float>(0.3 * std::sin(2 * M_PI * 440 * f / 48000.0));
        });
    };
    Mixer a(48000), b(48000);
    a.setLimiter(false);
    b.setLimiter(false);
    a.setTrack(0, mk("nb1.wav"));
    b.setTrack(0, mk("nb2.wav"));
    b.trackEq(0)->setBand(2, true, true, kPeaking, 440, 12, 2);
    b.trackEq(0)->setBypass(true);
    a.play();
    b.play();
    std::vector<float> oa(384), ob(384);
    bool igual = true;
    for (int i = 0; i < 100; ++i) {
        a.process(oa.data(), 192, 2);
        b.process(ob.data(), 192, 2);
        igual = igual && oa == ob;
    }
    CHECK(igual);
}

// ---------------------------------------------------------------- exportar
static int crear(const std::string &p) { return ::open(p.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644); }

static std::unique_ptr<MappedAudio> seno(const std::string &n, int ch, int64_t frames, double hz, double amp) {
    return genTrack(n, ch, frames, [=](int64_t f, int c) {
        return static_cast<float>((c ? -amp : amp) * std::sin(2 * M_PI * hz * f / 48000.0));
    });
}

// Render de referencia: otra copia del mezclador procesada completa en bloques de 4096.
static std::vector<float> referencia(const Mixer &live) {
    Mixer m(48000);
    std::string e;
    live.cloneInto(m, e);
    m.prepareOffline();
    std::vector<float> out(static_cast<size_t>(m.length()) * 2), blk(4096 * 2);
    int64_t done = 0;
    while (done < m.length()) {
        const int n = static_cast<int>(std::min<int64_t>(4096, m.length() - done));
        m.process(blk.data(), n, 2);
        std::copy(blk.begin(), blk.begin() + n * 2, out.begin() + done * 2);
        done += n;
    }
    return out;
}

static Mixer *mezclaDePrueba() {
    auto *m = new Mixer(48000);
    m->setTrack(0, seno("ex1.wav", 2, 48000, 440, 0.3));
    m->setTrack(5, seno("ex2.wav", 1, 30000, 1000, 0.4));
    m->setTrackPan(5, -0.4f);
    m->setTrackGain(5, 0.7f);
    m->trackEq(5)->setBand(0, true, true, kPeaking, 1000, 4, 1.4f);
    m->trackEq(0)->setBand(3, true, true, kHighPass, 100, 0, 0.7f);
    m->setMasterGain(0.9f);
    return m;
}

TEST(exportarFloatEsIgualALaMezcla) {
    std::unique_ptr<Mixer> live(mezclaDePrueba());
    live->play();  // que esté sonando en vivo no debe cambiar la exportación
    run(*live, 1000);
    const std::string p = tmpPath("mezcla-f32.wav");
    std::atomic<float> prog{0};
    ExportFormat f;
    f.bits = 32;
    f.isFloat = true;
    ExportResult r = exportMix(*live, crear(p), f, &prog, nullptr);
    CHECK(r.error.empty());
    CHECK(r.frames == 48000);
    CHECK_NEAR(prog.load(), 1.0, 1e-6);
    Clip c;
    CHECK(readWav(std::fopen(p.c_str(), "rb"), c).empty());
    CHECK(c.channels == 2 && c.sampleRate == 48000 && c.isFloat && c.bits == 32 && c.frames == 48000);
    const std::vector<float> ref = referencia(*live);
    CHECK(c.data == ref);
    // Sin rampa de entrada: la primera muestra ya trae señal completa (no un fundido).
    float maxIni = 0;
    for (int i = 0; i < 200; ++i) maxIni = std::max(maxIni, std::fabs(c.data[i]));
    CHECK(maxIni > 0.1f);
    struct stat st {};
    stat(p.c_str(), &st);
    CHECK(st.st_size == r.bytes);
    CHECK(r.bytes == exportBytes(48000, f));
}

TEST(exportar24y16Bits) {
    std::unique_ptr<Mixer> live(mezclaDePrueba());
    const std::vector<float> ref = referencia(*live);
    for (int bits : {24, 16}) {
        const std::string p = tmpPath("mezcla-" + std::to_string(bits) + ".wav");
        ExportFormat f;
        f.bits = bits;
        ExportResult r = exportMix(*live, crear(p), f, nullptr, nullptr);
        CHECK(r.error.empty());
        Clip c;
        CHECK(readWav(std::fopen(p.c_str(), "rb"), c).empty());
        CHECK(c.bits == bits && !c.isFloat && c.frames == 48000 && c.channels == 2);
        const double lsb = std::ldexp(1.0, -(bits - 1));
        double maxErr = 0, sumErr = 0;
        for (size_t i = 0; i < ref.size(); ++i) {
            maxErr = std::max(maxErr, std::fabs(c.data[i] - (double)ref[i]));
            sumErr += c.data[i] - (double)ref[i];
        }
        std::printf("  %d bits: error máximo %.2f LSB, sesgo %.3f LSB\n", bits, maxErr / lsb, sumErr / ref.size() / lsb);
        CHECK(maxErr <= 1.5 * lsb);                    // redondeo + dither de 1 LSB
        CHECK(std::fabs(sumErr / ref.size()) < 0.05 * lsb);  // el dither no mete corriente directa
    }
}

TEST(exportarA44100) {
    std::unique_ptr<Mixer> live(mezclaDePrueba());
    const std::string p = tmpPath("mezcla-441.wav");
    ExportFormat f;
    f.sampleRate = 44100;
    ExportResult r = exportMix(*live, crear(p), f, nullptr, nullptr);
    CHECK(r.error.empty());
    CHECK(r.frames == Resampler::outputFrames(48000, 48000, 44100));
    Clip c;
    CHECK(readWav(std::fopen(p.c_str(), "rb"), c).empty());
    CHECK(c.sampleRate == 44100 && c.frames == r.frames && c.bits == 24);
}

TEST(exportarErroresYCancelar) {
    Mixer vacio(48000);
    ExportFormat f;
    CHECK(exportMix(vacio, crear(tmpPath("vacio.wav")), f, nullptr, nullptr).error == "No hay pistas para exportar");
    CHECK(!exportMix(vacio, -1, f, nullptr, nullptr).error.empty());
    std::unique_ptr<Mixer> live(mezclaDePrueba());
    std::atomic<bool> cancelar{true};
    CHECK(exportMix(*live, crear(tmpPath("cancelada.wav")), f, nullptr, &cancelar).error == "Exportación cancelada");
}

TEST(exportarRespetaSoloYMute) {
    Mixer live(48000);
    live.setLimiter(false);
    live.setTrack(0, constTrack("s1.wav", 2, 4800, 0.25f, 0.25f));
    live.setTrack(1, constTrack("s2.wav", 2, 4800, 0.5f, 0.5f));
    live.setTrackSolo(1, true);
    const std::string p = tmpPath("solo.wav");
    ExportFormat f;
    f.bits = 32;
    CHECK(exportMix(live, crear(p), f, nullptr, nullptr).error.empty());
    Clip c;
    readWav(std::fopen(p.c_str(), "rb"), c);
    CHECK_NEAR(c.data[0], 0.5, 1e-6);  // solo la pista 2, desde la primera muestra
    CHECK_NEAR(c.data[2 * 4000], 0.5, 1e-6);
    live.setTrackSolo(1, false);
    live.setTrackMute(1, true);
    CHECK(exportMix(live, crear(p), f, nullptr, nullptr).error.empty());
    readWav(std::fopen(p.c_str(), "rb"), c);
    CHECK_NEAR(c.data[2 * 4000], 0.25, 1e-6);
}

// ---------------------------------------------------------------- edición (pedazos)
static bool igualDesde(const std::vector<float> &a, int64_t da, const std::vector<float> &b, int64_t db, int64_t n) {
    for (int64_t i = 0; i < 2 * n; ++i)
        if (a[2 * da + i] != b[2 * db + i]) return false;
    return true;
}

TEST(edicionMoverRecortarYCortar) {
    Mixer m(48000);
    m.setLimiter(false);
    CHECK(!m.setRegions(0, {}));  // espacio vacío
    m.setTrack(0, genTrack("ed.wav", 2, 48000, [](int64_t f, int c) {
        return static_cast<float>((c ? 0.3 : 0.5) * std::sin(2 * M_PI * 440 * f / 48000.0));
    }));
    std::vector<Region> r = m.regions(0);
    CHECK(r.size() == 1 && r[0].start == 0 && r[0].src == 0 && r[0].length == 48000);
    const std::vector<float> base = referencia(m);
    CHECK(base.size() == 96000);

    // Cortar en dos sin mover nada suena exactamente igual.
    CHECK(m.setRegions(0, {{0, 0, 20000, 0, 0}, {20000, 20000, 28000, 0, 0}}));
    CHECK(referencia(m) == base);
    CHECK(m.length() == 48000);

    // Mover: silencio antes y el audio corrido, sin cambios.
    m.setRegions(0, {{12000, 0, 48000, 0, 0}});
    CHECK(m.length() == 60000);
    std::vector<float> o = referencia(m);
    CHECK(std::all_of(o.begin(), o.begin() + 24000, [](float x) { return x == 0.0f; }));
    CHECK(igualDesde(o, 12000, base, 0, 48000));

    // Recortar: suena solo ese pedazo; los bordes cortados llevan un fundido de 2 ms.
    m.setRegions(0, {{0, 10000, 5000, 0, 0}});
    CHECK(m.length() == 5000);
    o = referencia(m);
    CHECK(igualDesde(o, kEdgeFade, base, 10000 + kEdgeFade, 5000 - 2 * kEdgeFade));
    CHECK(std::fabs(o[0]) < 0.01f && std::fabs(o[2 * 4999]) < 0.01f);
    CHECK(std::fabs(base[2 * 10000]) > 0.1f || std::fabs(base[2 * 14999]) > 0.1f);

    // Los dos pedazos de un corte, separados: cada borde cortado lleva su fundido.
    m.setRegions(0, {{0, 0, 20000, 0, 0}, {30000, 20000, 28000, 0, 0}});
    o = referencia(m);
    CHECK(m.length() == 58000);
    CHECK(igualDesde(o, 0, base, 0, 20000 - kEdgeFade));
    CHECK(igualDesde(o, 30000 + kEdgeFade, base, 20000 + kEdgeFade, 28000 - kEdgeFade));

    // Sin pedazos la pista no suena ni cuenta para la duración.
    m.setRegions(0, {});
    CHECK(m.length() == 0 && m.regions(0).empty());
}

TEST(edicionFundidos) {
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(0, constTrack("fd.wav", 2, 48000, 0.5f, 0.5f));
    m.setRegions(0, {{0, 0, 48000, 4800, 9600}});
    std::vector<float> o = referencia(m);
    CHECK_NEAR(o[2 * 2400], 0.25, 1e-3);        // a la mitad del fundido de entrada: sin²(45°)
    CHECK_NEAR(o[2 * 24000], 0.5, 1e-6);        // en medio, completo
    CHECK_NEAR(o[2 * (48000 - 4800)], 0.25, 1e-3);
    CHECK(o[0] < 0.001f && o[2 * 47999] < 0.001f);
    bool sube = true, baja = true;
    for (int i = 1; i < 4800; ++i) sube = sube && o[2 * i] >= o[2 * (i - 1)];
    for (int i = 48000 - 9600 + 1; i < 48000; ++i) baja = baja && o[2 * i] <= o[2 * (i - 1)];
    CHECK(sube && baja);

    // Cruce: el final de uno y el principio del otro, encimados con fundidos, suman 1.
    m.setRegions(0, {{0, 0, 24000, 0, 4800}, {19200, 19200, 28800, 4800, 0}});
    o = referencia(m);
    float err = 0;
    for (int i = 0; i < 48000; ++i) err = std::max(err, std::fabs(o[2 * i] - 0.5f));
    CHECK(err < 1e-5f);

    // Encimados sin fundidos: suenan los dos.
    m.setRegions(0, {{0, 0, 48000, 0, 0}, {0, 0, 48000, 0, 0}});
    o = referencia(m);
    CHECK_NEAR(o[2 * 1000], 1.0, 1e-6);
}

TEST(edicionAjustaLoImposible) {
    Mixer m(48000);
    m.setTrack(2, constTrack("aj.wav", 1, 48000, 0.1f));
    m.setRegions(2, {{-5, 47000, 99999, 99999, 99999}, {100, 60000, 10, 0, 0}, {100, 5, 0, 0, 0}});
    std::vector<Region> r = m.regions(2);
    CHECK(r.size() == 1);
    CHECK(r[0].start == 0 && r[0].src == 47000 && r[0].length == 1000);
    CHECK(r[0].fadeIn + r[0].fadeOut == 1000);
    std::vector<Region> muchas(kMaxRegions + 10, Region{0, 0, 100, 0, 0});
    m.setRegions(2, muchas);
    CHECK(static_cast<int>(m.regions(2).size()) == kMaxRegions);
    // Al quitar la pista y poner otra en su lugar, vuelve a ser el archivo completo.
    m.takeTrack(2);
    m.setTrack(2, constTrack("aj2.wav", 1, 4800, 0.1f));
    r = m.regions(2);
    CHECK(r.size() == 1 && r[0].length == 4800 && m.length() == 4800);
}

TEST(edicionSeExporta) {
    std::unique_ptr<Mixer> live(mezclaDePrueba());
    live->setRegions(0, {{0, 0, 10000, 0, 1000}, {30000, 10000, 20000, 500, 0}});
    CHECK(live->length() == 50000);
    const std::string p = tmpPath("edicion-f32.wav");
    ExportFormat f;
    f.bits = 32;
    ExportResult r = exportMix(*live, crear(p), f, nullptr, nullptr);
    CHECK(r.error.empty() && r.frames == 50000);
    Clip c;
    CHECK(readWav(std::fopen(p.c_str(), "rb"), c).empty());
    CHECK(c.data == referencia(*live));
}

// ---------------------------------------------------------------- análisis
TEST(fftBasica) {
    FFT fft(1024);
    std::vector<double> re(1024), im(1024, 0.0);
    for (int i = 0; i < 1024; ++i) re[i] = std::cos(2 * M_PI * 37 * i / 1024);
    fft.forward(re.data(), im.data());
    CHECK_NEAR(std::hypot(re[37], im[37]), 512, 1e-6);
    CHECK_NEAR(std::hypot(re[36], im[36]), 0, 1e-6);
    CHECK_NEAR(std::hypot(re[0], im[0]), 0, 1e-6);
}

static void senal(std::vector<float> &l, std::vector<float> &r, const std::function<std::pair<double, double>(int)> &g) {
    l.resize(Analyzer::kSpecSize);
    r.resize(Analyzer::kSpecSize);
    for (int i = 0; i < Analyzer::kSpecSize; ++i) {
        auto [a, b] = g(i);
        l[i] = static_cast<float>(a);
        r[i] = static_cast<float>(b);
    }
}

TEST(analizadorEspectroComoWebAudio) {
    Analyzer an(48000);
    std::vector<float> l, r;
    senal(l, r, [](int i) {
        const double v = 0.5 * std::sin(2 * M_PI * 1000 * i / 48000.0);
        return std::make_pair(v, v);
    });
    for (int k = 0; k < 60; ++k) an.updateFrom(l.data(), r.data());
    const float *db = an.spectrumDb();
    int pk = 0;
    for (int k = 1; k < Analyzer::kSpecBins; ++k)
        if (db[k] > db[pk]) pk = k;
    std::printf("  pico en %.1f Hz, %.2f dB\n", pk * an.binHz(), db[pk]);
    CHECK(std::fabs(pk * an.binHz() - 1000) < 3);
    CHECK_NEAR(db[pk], 20 * std::log10(0.5 * 0.42 / 2), 1.2);  // amplitud × ganancia Blackman / 2
    CHECK(db[static_cast<int>(5000 / an.binHz())] < -100);
    CHECK_NEAR(an.correlation, 1.0, 1e-6);
    CHECK_NEAR(an.peakL, 0.5, 1e-3);
    CHECK_NEAR(an.rmsL, 0.5 / std::sqrt(2.0), 2e-3);
    CHECK(an.hum.freq == 0);
}

TEST(analizadorFaseYPaneo) {
    Analyzer an(48000);
    std::vector<float> l, r;
    senal(l, r, [](int i) {
        const double v = 0.4 * std::sin(2 * M_PI * 300 * i / 48000.0);
        return std::make_pair(v, -v);  // polaridad invertida
    });
    an.updateFrom(l.data(), r.data());
    CHECK_NEAR(an.correlation, -1.0, 1e-6);
    senal(l, r, [](int i) { return std::make_pair(0.4 * std::sin(2 * M_PI * 300 * i / 48000.0), 0.0); });
    an.reset();
    for (int k = 0; k < 20; ++k) an.updateFrom(l.data(), r.data());
    CHECK(an.rmsR == 0.0f && an.rmsL > 0.2f);
    // En el goniómetro "por fuentes" todo queda a la izquierda.
    const int b = static_cast<int>(std::lround(300.0 / (48000.0 / Analyzer::kFanSize)));
    CHECK_NEAR(an.fanPan()[b], -1.0, 1e-6);
}

TEST(analizadorZumbido) {
    Analyzer an(48000);
    std::vector<float> l, r;
    uint32_t seed = 1;
    auto ruido = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return (seed / 4294967296.0 - 0.5) * 2e-4;
    };
    senal(l, r, [&](int i) {
        double v = 0.2 * std::sin(2 * M_PI * 440 * i / 48000.0);  // música
        for (int k = 1; k <= 4; ++k) v += 0.01 / k * std::sin(2 * M_PI * 60 * k * i / 48000.0);
        v += ruido();
        return std::make_pair(v, v);
    });
    for (int k = 0; k < 30; ++k) an.updateFrom(l.data(), r.data());
    std::printf("  zumbido: %d Hz, %d armónicos, +%.0f dB\n", an.hum.freq, an.hum.count, an.hum.db);
    CHECK(an.hum.freq == 60);
    CHECK(an.hum.count >= 3);
}

TEST(analizadorNoConfundeUnBajoConZumbido) {
    // Un bajo en La (55 Hz) con armónicos está cerca de 50 Hz, pero no es zumbido.
    Analyzer an(48000);
    std::vector<float> l, r;
    senal(l, r, [](int i) {
        double v = 0;
        for (int k = 1; k <= 4; ++k) v += 0.2 / k * std::sin(2 * M_PI * 55 * k * i / 48000.0);
        return std::make_pair(v, v);
    });
    for (int k = 0; k < 30; ++k) an.updateFrom(l.data(), r.data());
    CHECK(an.hum.freq == 0);
}

TEST(analizadorIgnoraLaFuenteAnterior) {
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(0, seno("fa.wav", 1, 480000, 1000, 0.5));  // suena fuerte en la mezcla
    m.setTrack(1, seno("fb.wav", 1, 480000, 300, 0.001)); // casi nada
    m.play();
    run(m, 48000);
    Analyzer an(48000);
    m.setAnalysisSource(1);
    an.reset(m);
    run(m, 2048);  // muy poco audio nuevo: el resto de la ventana es de antes
    an.update(m);
    const int b1k = static_cast<int>(std::lround(1000 / an.binHz()));
    CHECK(an.spectrumDb()[b1k] < -100);  // el tono de 1 kHz de la mezcla no se cuela
}

TEST(notchQuitaElZumbido) {
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(0, seno("hum.wav", 2, 480000, 60, 0.3));
    m.play();
    auto [l0, r0] = peakRun(m, 24000, 4800);
    CHECK_NEAR(l0, 0.3, 0.003);
    m.setNotch(60);
    CHECK(m.notch() == 60);
    auto [l1, r1] = peakRun(m, 48000, 4800);
    std::printf("  notch 60 Hz: queda %.1f dB\n", 20 * std::log10(l1 / 0.3));
    CHECK(l1 < 0.3 * 0.03);
    m.setNotch(33);  // valor inválido: apagado
    CHECK(m.notch() == 0);
}

TEST(tomaDeAnalisis) {
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(2, constTrack("tap.wav", 2, 96000, 0.4f, 0.2f));
    m.setMasterGain(0.5f);
    m.play();
    run(m, 9600);
    std::vector<float> l(256), r(256);
    uint64_t w = m.readTap(l.data(), r.data(), 256);
    CHECK(w >= 9600);
    CHECK_NEAR(l[255], 0.4, 1e-4);  // la mezcla antes del volumen master
    CHECK_NEAR(r[255], 0.2, 1e-4);
    m.setTrackMute(2, true);
    m.setAnalysisSource(2);
    run(m, 9600);
    m.readTap(l.data(), r.data(), 256);
    CHECK_NEAR(l[255], 0.4, 1e-4);  // la pista se analiza aunque esté en mute
    m.setAnalysisSource(7);  // espacio vacío: silencio
    run(m, 9600);
    m.readTap(l.data(), r.data(), 256);
    CHECK(l[255] == 0.0f);
    m.pause();
    run(m, 4800);
    const uint64_t w2 = m.readTap(l.data(), r.data(), 256);
    run(m, 4800);
    CHECK(m.readTap(l.data(), r.data(), 256) == w2);  // detenido no llega audio nuevo
}

TEST(espectrograma) {
    auto a = seno("sg.wav", 1, 96000, 1000, 0.5);
    float hi = 0;
    std::vector<uint8_t> sg = spectrogram(*a, 100, 64, &hi);
    CHECK(sg.size() == 6400);
    CHECK_NEAR(hi, 20000, 1e-3);
    const int fila1k = static_cast<int>(std::log(1000 / 20.0) / std::log(hi / 20.0) * 64);
    const int fila100 = static_cast<int>(std::log(100 / 20.0) / std::log(hi / 20.0) * 64);
    int v1k = 0, v100 = 0;
    for (int c = 10; c < 90; ++c) {
        v1k = std::max<int>(v1k, sg[c * 64 + fila1k]);
        v100 = std::max<int>(v100, sg[c * 64 + fila100]);
    }
    std::printf("  espectrograma: 1 kHz=%d, 100 Hz=%d\n", v1k, v100);
    CHECK(v1k > 200);
    CHECK(v100 < 20);
}

TEST(concurrencia) {
    // Un hilo hace de audio; otro agrega y quita pistas y mueve controles sin parar.
    Mixer m(48000);
    std::vector<std::string> paths;
    for (int i = 0; i < 3; ++i) {
        const std::string p = tmpPath("conc" + std::to_string(i) + ".wav");
        WavWriter w;
        w.open(p, 2, 48000);
        std::vector<float> buf(48000 * 2, 0.1f);
        w.write(buf.data(), 48000);
        w.close();
        paths.push_back(p);
    }
    std::atomic<bool> done{false};
    std::thread audio([&] {
        std::vector<float> out(256 * 2);
        while (!done.load()) m.process(out.data(), 256, 2);
    });
    std::mt19937 rng(42);
    for (int it = 0; it < 200; ++it) {
        const int slot = static_cast<int>(rng() % kMaxTracks);
        if (m.hasTrack(slot)) {
            m.takeTrack(slot);
        } else {
            std::string e;
            m.setTrack(slot, MappedAudio::open(paths[rng() % paths.size()], e));
        }
        m.setTrackGain(slot, (rng() % 100) / 50.0f);
        m.setTrackPan(slot, ((rng() % 200) - 100) / 100.0f);
        m.setTrackSolo(slot, rng() % 5 == 0);
        m.trackEq(slot)->setBand(static_cast<int>(rng() % kMaxBands), rng() % 4 != 0, rng() % 5 != 0,
                                 static_cast<int>(rng() % kFilterTypes), 20.0f + rng() % 18000,
                                 static_cast<float>(rng() % 31) - 15, 0.2f + (rng() % 100) / 10.0f);
        if (rng() % 9 == 0) m.trackEq(slot)->setBypass(rng() % 2 == 0);
        if (rng() % 3 == 0) {
            std::vector<Region> rs;
            for (int k = static_cast<int>(rng() % 5); k > 0; --k)
                rs.push_back({static_cast<int64_t>(rng() % 60000), static_cast<int64_t>(rng() % 48000),
                              static_cast<int64_t>(rng() % 48000), static_cast<int64_t>(rng() % 5000),
                              static_cast<int64_t>(rng() % 5000)});
            m.setRegions(slot, rs);
            (void)m.regions(slot);
        }
        if (rng() % 11 == 0) m.setNotch(rng() % 2 ? 50 : 0);
        m.setAnalysisSource(static_cast<int>(rng() % (kMaxTracks + 1)) - 1);
        {
            std::vector<float> tl(1024), tr(1024);
            m.readTap(tl.data(), tr.data(), 1024);
        }
        if (rng() % 3 == 0) m.seek(rng() % 48000);
        if (rng() % 4 == 0) m.play();
        if (rng() % 7 == 0) m.pause();
        float meters[kMeterCount];
        m.readMeters(meters);
        m.prefetch();
        (void)m.position();
    }
    done.store(true);
    audio.join();
    for (int i = 0; i < kMaxTracks; ++i) m.takeTrack(i);
    CHECK(m.length() == 0);
}

// ---------------------------------------------------------------- JSON
TEST(jsonLeerYEscribir) {
    std::string e;
    const Json j = Json::parse(
        "{\"a\": 1, \"b\": [true, false, null, -2.5, \"x\\\"y\\\\z\\n\"], "
        "\"c\": {\"d\": \"ñandú \\u00e9 \\ud83c\\udfb5\"}}",
        e);
    CHECK(e.empty());
    CHECK(j["a"].asInt() == 1);
    CHECK(j["b"].size() == 5 && j["b"][0].asBool() && !j["b"][1].asBool(true) && j["b"][2].isNull());
    CHECK_NEAR(j["b"][3].asDouble(), -2.5, 0);
    CHECK(j["b"][4].asString() == "x\"y\\z\n");
    CHECK(j["c"]["d"].asString() == "ñandú é \xF0\x9F\x8E\xB5");
    // Lo que falta o es de otro tipo da el valor por omisión.
    CHECK(j["no"].asInt(7) == 7 && j["a"].asString("z") == "z" && j["b"][99].isNull() && j["a"]["x"].isNull());
    // Ida y vuelta.
    const Json k = Json::parse(j.dump(), e);
    CHECK(e.empty() && k.dump() == j.dump());
    Json n = Json::object();
    n.set("f", static_cast<int64_t>(576000)).set("q", 1.4).set("big", static_cast<int64_t>(1) << 40);
    n.set("t", "a\x01" "b");
    CHECK(n.dump() == "{\"f\":576000,\"q\":1.4,\"big\":1099511627776,\"t\":\"a\\u0001b\"}");
    for (const char *mal : {"", "{", "[1,]", "{\"a\" 1}", "tru", "\"sin cerrar", "[1] x", "{\"a\":01e}", "[\"\\ud800\"]"}) {
        Json::parse(mal, e);
        CHECK(!e.empty());
    }
}

// ---------------------------------------------------------------- medidores
TEST(medidoresBalistica) {
    MeterBallistics mb;
    float raw[kMeterCount] = {};
    float out[MeterBallistics::kCount];
    raw[0] = 1.0f;
    raw[1] = 0.5f;
    raw[2] = 2.5f;
    raw[3] = 0.1f;  // pista 0, L
    mb.update(raw, 1000);
    mb.read(out);
    CHECK_NEAR(out[MeterBallistics::kMasterL], 0, 1e-4);
    CHECK_NEAR(out[MeterBallistics::kMasterR], 20 * std::log10(0.5), 1e-4);
    CHECK_NEAR(out[0], -20, 1e-4);
    CHECK(out[1] == -120.0f);
    CHECK(out[MeterBallistics::kClipL] == 1.0f && out[MeterBallistics::kClipR] == 0.0f);
    CHECK_NEAR(out[MeterBallistics::kReduction], 2.5, 0);
    CHECK(out[MeterBallistics::kMoving] == 1.0f);
    std::fill(raw, raw + kMeterCount, 0.0f);
    mb.update(raw, 1100);  // 0.1 s: el master cae 3.6 dB y la pista 4.5 dB
    mb.read(out);
    CHECK_NEAR(out[MeterBallistics::kMasterL], -3.6, 1e-3);
    CHECK_NEAR(out[0], -24.5, 1e-3);
    CHECK_NEAR(out[MeterBallistics::kTickL], 0, 1e-4);  // la marca se queda 1.2 s
    mb.update(raw, 2300);  // pasado 1.2 s: baja 15 dB/s (el paso se limita a 0.2 s)
    mb.read(out);
    CHECK_NEAR(out[MeterBallistics::kTickL], -3, 1e-3);
    CHECK_NEAR(out[MeterBallistics::kMaxL], 0, 1e-4);  // el pico máximo se queda
    mb.clearClip();
    mb.read(out);
    CHECK(out[MeterBallistics::kClipL] == 0.0f && out[MeterBallistics::kMaxL] == -120.0f);
    for (int64_t t = 2400; t < 9000; t += 100) mb.update(raw, t);
    mb.read(out);
    CHECK(out[MeterBallistics::kMoving] == 0.0f);
}

// ---------------------------------------------------------------- vista del análisis
TEST(analisisVeredictoYGoniometro) {
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(0, genTrack("av.wav", 2, 48000 * 4, [](int64_t f, int c) {
        const float s = static_cast<float>(0.3 * std::sin(2 * M_PI * 440 * f / 48000.0));
        return c ? -s : s;  // fuera de fase
    }));
    Analyzer a(48000);
    AnalysisView v;
    float st[AnalysisView::kStatCount];
    std::vector<float> g(AnalysisView::kMaxGonio);
    m.play();
    int64_t t = 0;
    for (int i = 0; i < 20; ++i) {
        run(m, 2048);
        t += 40;
        v.update(a, m, true, t, false);
    }
    v.stats(st);
    CHECK(st[0] == 1.0f && st[1] == 1.0f);
    CHECK(st[2] < -0.9f);
    CHECK(st[4] == AnalysisView::kOutOfPhase && st[6] == 1.0f && st[5] == 0.0f);
    // Por fuentes: con L = -R el tono queda al centro (rayo vertical) y largo.
    int n = v.goniometer(g.data(), static_cast<int>(g.size()));
    bool vertical = false;
    for (int i = 0; i < n; ++i) vertical = vertical || (std::fabs(g[3 * i]) < 0.1f && g[3 * i + 1] > 0.5f);
    CHECK(n >= 1 && vertical);
    // Clásico: todo sobre el eje de lado (y ≈ 0) y la escala automática llega al borde.
    for (int i = 0; i < 5; ++i) {
        run(m, 2048);
        t += 40;
        v.update(a, m, true, t, true);
    }
    n = v.goniometer(g.data(), static_cast<int>(g.size()));
    float mx = 0, my = 0;
    for (int i = 0; i < n; ++i) {
        mx = std::max(mx, std::fabs(g[2 * i]));
        my = std::max(my, std::fabs(g[2 * i + 1]));
    }
    CHECK(n == Analyzer::kTime);
    CHECK(mx > 0.9f && mx <= 1.0001f && my < 0.01f);
    // Espectro en columnas: el máximo cae en la columna de 440 Hz.
    std::vector<float> cols(100);
    AnalysisView::spectrumColumns(a, cols.data(), 100);
    const int c = static_cast<int>(std::max_element(cols.begin(), cols.end()) - cols.begin());
    const double f0 = 20 * std::pow(1000.0, c / 100.0), f1 = 20 * std::pow(1000.0, (c + 1) / 100.0);
    CHECK(f0 <= 445 && f1 >= 435);
    // Sin sonar: todo a reposo.
    CHECK(v.update(a, m, false, t + 100, true));
    v.stats(st);
    CHECK(st[0] == 0.0f && st[4] == AnalysisView::kNone && st[2] == 1.0f && v.goniometer(g.data(), 10) == 0);

    // Una pista mono toda a la izquierda: desbalance hacia L.
    Mixer m2(48000);
    m2.setLimiter(false);
    m2.setTrack(0, seno("av2.wav", 1, 48000 * 2, 300, 0.3));
    m2.setTrackPan(0, -1.0f);
    m2.play();
    Analyzer a2(48000);
    AnalysisView v2;
    for (int i = 0; i < 10; ++i) {
        run(m2, 2048);
        v2.update(a2, m2, true, 40 * (i + 1), false);
    }
    v2.stats(st);
    CHECK(st[4] == AnalysisView::kMono && st[5] == -1.0f && st[6] == 1.0f);
    CHECK_NEAR(st[3], -1.0, 1e-3);
}

// ---------------------------------------------------------------- edición (funciones puras)
TEST(edicionReglas) {
    const Region r{48000, 0, 96000, 0, 0};
    CHECK(edit::move(r, -5).start == 0);
    Region t = edit::trimStart(r, 40000);  // no antes del principio del archivo
    CHECK(t.start == 48000 && t.src == 0 && t.length == 96000);
    t = edit::trimStart(r, 60000);
    CHECK(t.start == 60000 && t.src == 12000 && t.length == 84000 && t.end() == r.end());
    t = edit::trimStart(r, 200000);
    CHECK(t.length == edit::kMinLength && t.end() == r.end());
    t = edit::trimEnd(r, 999999, 100000);  // no después del final del archivo
    CHECK(t.length == 100000);
    t = edit::trimEnd(r, 0, 100000);
    CHECK(t.length == edit::kMinLength);
    Region f = edit::setFadeOut(edit::setFadeIn(r, 50000), 80000);  // uno no empuja al otro
    CHECK(f.fadeIn == 50000 && f.fadeOut == 46000);
    t = edit::trimEnd(f, r.start + 30000, 100000);
    CHECK(t.length == 30000 && t.fadeIn == 30000 && t.fadeOut == 0);
    Region a, b;
    CHECK(!edit::cut(r, r.start + 100, a, b));
    CHECK(edit::cut(f, 100000, a, b));
    CHECK(a.start == 48000 && a.length == 52000 && a.fadeIn == 50000 && a.fadeOut == 0);
    CHECK(b.start == 100000 && b.src == 52000 && b.length == 44000 && b.fadeIn == 0 && b.fadeOut == 44000);
    CHECK(edit::snapOffset({1000, 5000}, {0, 5100, 9000}, 200) == 100);
    CHECK(edit::snapOffset({1000}, {0}, 500) == 0);
    CHECK(edit::snapOffset({1000}, {1200}, 0) == 0);  // imán apagado
}

// ---------------------------------------------------------------- estudio (el núcleo completo)
static std::string nuevaCarpeta(const std::string &n) {
    const std::string d = tmpPath(n);
    ::mkdir(d.c_str(), 0700);
    return d;
}

static Json pistas(const Studio &s) {
    std::string e;
    return Json::parse(s.tracksJson(), e);
}

static Json estado(const Studio &s) {
    std::string e;
    return Json::parse(s.statusJson(), e);
}

static int idDe(const Studio &s, int i) { return static_cast<int>(pistas(s)[i]["id"].asInt(-1)); }

TEST(estudioPrimeraVez) {
    const std::string dir = nuevaCarpeta("estudio1");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    const Json p = pistas(s);
    CHECK(p.size() == 4);
    const char *nombres[] = {"Bajo", "Voz", "Guitarra", "Teclado"};
    for (int i = 0; i < 4 && i < static_cast<int>(p.size()); ++i) {
        CHECK(p[i]["nombre"].asString() == nombres[i]);
        CHECK(p[i]["cuadros"].asInt() == 12 * 48000 && p[i]["canales"].asInt() == 1);
        CHECK(p[i]["info"].asString() == "prueba · mono · 12 s");
    }
    float v[Studio::kTrackParamCount];
    CHECK(s.trackParams(idDe(s, 2), v) && v[0] == -2.0f && v[1] == 60.0f && v[5] == Studio::kAutoRead);
    CHECK_NEAR(s.duration(), 12.0, 1e-9);
    const Json st = estado(s);
    CHECK(!st["ocupada"].asBool(true));
    CHECK(st["mensaje"].asString().rfind("Pistas de prueba cargadas · 4 pista(s) · 48000 Hz", 0) == 0);
    CHECK(s.saveNow());
    struct stat sb {};
    CHECK(::stat((dir + "/sesion.json").c_str(), &sb) == 0);
    // Abrir la misma carpeta otra vez no hace nada.
    s.open(dir, dir);
    CHECK(s.trackCount() == 4);
}

TEST(estudioSesionIdaYVuelta) {
    const std::string dir = nuevaCarpeta("estudio2");
    Studio s1;
    s1.open(dir, dir);
    CHECK(s1.waitIdle(30000));
    const int id = idDe(s1, 0);
    s1.setVolume(id, 3.26f);
    s1.setPan(id, -33.4f);
    s1.setMute(id, true);
    s1.setPhase(id, true);
    CHECK(s1.addEqBand(id) == 0);
    CHECK(s1.addEqBand(id) == 1);
    s1.setEqBand(id, 1, kLowShelf, 123.456f, 4.3f, 0.556f, false);
    s1.setEqAutoGain(id, false);
    s1.seek(6.0);
    CHECK(s1.cut(id, 0) == 1);
    s1.setRegionFade(id, 1, false, 4800);
    s1.setMasterDb(-3.24f);
    s1.setLimiter(false);
    s1.setNotch(50);
    s1.setExportFormat(16, 44100);
    float p1[Studio::kTrackParamCount], m1[Studio::kMasterStateCount];
    CHECK(s1.trackParams(id, p1));
    CHECK(p1[0] == 3.5f && p1[1] == -33.0f && p1[2] == 1.0f && p1[3] == 0.0f && p1[4] == 1.0f);
    const std::vector<float> eq1 = s1.eqState(id);
    CHECK(eq1.size() == 3 + 2 * 5 && eq1[1] == 0.0f && eq1[2] == 2.0f);
    CHECK(eq1[8] == kLowShelf && std::fabs(eq1[9] - 123.5f) < 1e-3f && eq1[10] == 4.5f &&
          std::fabs(eq1[11] - 0.56f) < 1e-4f && eq1[12] == 0.0f);
    s1.masterState(m1);
    CHECK(m1[0] == -3.0f && m1[1] == 0.0f && m1[2] == 0.0f && m1[3] == 50.0f && m1[4] == 16.0f && m1[5] == 44100.0f);
    CHECK(s1.saveNow());

    Studio s2;
    s2.open(dir, dir);
    CHECK(s2.waitIdle(30000));
    CHECK(s2.tracksJson() == s1.tracksJson());
    float p2[Studio::kTrackParamCount], m2[Studio::kMasterStateCount];
    CHECK(s2.trackParams(id, p2));
    CHECK(std::equal(p1, p1 + Studio::kTrackParamCount, p2));
    CHECK(s2.eqState(id) == eq1);
    CHECK(s2.regions(id) == s1.regions(id));
    CHECK(s2.regions(id).size() == 2 && s2.regions(id)[1].fadeOut == 4800);
    s2.masterState(m2);
    CHECK(std::equal(m1, m1 + 6, m2));
    CHECK(s2.mixer().notch() == 50);
    CHECK(estado(s2)["mensaje"].asString().rfind("Mezcla recuperada", 0) == 0);
    CHECK(!s2.canUndo());  // el historial no se guarda
}

TEST(estudioLeeSesionVieja) {
    // Como la guardaba la versión anterior (Kotlin): floats largos y "pedazos" solo si se editó.
    const std::string dir = nuevaCarpeta("estudio3");
    ::mkdir((dir + "/audio").c_str(), 0700);
    CHECK(writeDemoTrack(0, dir + "/audio/uno.wav", 48000).empty());
    CHECK(writeDemoTrack(1, dir + "/audio/dos.wav", 48000).empty());
    CHECK(writeDemoTrack(2, dir + "/audio/huerfano.wav", 48000).empty());
    const char *json = R"({"version":1,"siguienteId":9,"master":{"volDb":-1.5,"mute":true,"limitador":true,"notch":60},
      "exportar":{"bits":32,"rate":48000},"pistas":[
      {"id":3,"nombre":"Uno","archivo":"uno.wav","info":"44.1 kHz → 48 kHz · 16 bits · estéreo","volDb":-6,"pan":20,
       "mute":false,"solo":true,"fase":false,"eq":{"bypass":true,"auto":true,"bandas":[{"tipo":0,"f":1000,"g":3.5,
       "q":1.399999976158142,"on":true}]},"pedazos":[[48000,0,96000,480,0],[200000,96000,48000,0,0]]},
      {"id":7,"nombre":"Dos","archivo":"dos.wav","info":"prueba · mono · 12 s","volDb":0,"pan":0,"mute":false,
       "solo":false,"fase":false,"eq":{"bypass":false,"auto":true,"bandas":[]}},
      {"id":8,"nombre":"Falta","archivo":"no-existe.wav","info":"","volDb":0,"pan":0}]})";
    FILE *f = std::fopen((dir + "/sesion.json").c_str(), "wb");
    std::fputs(json, f);
    std::fclose(f);
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    const Json p = pistas(s);
    CHECK(p.size() == 2 && p[0]["id"].asInt() == 3 && p[1]["id"].asInt() == 7 && p[0]["nombre"].asString() == "Uno");
    CHECK(p[0]["info"].asString() == "44.1 kHz → 48 kHz · 16 bits · estéreo");
    const std::vector<Region> r = s.regions(3);
    CHECK(r.size() == 2 && r[0].start == 48000 && r[0].fadeIn == 480 && r[1].src == 96000);
    CHECK(s.regions(7).size() == 1 && s.regions(7)[0].length == 12 * 48000);  // sin "pedazos": completa
    const std::vector<float> eq = s.eqState(3);
    CHECK(eq.size() == 8 && eq[0] == 1.0f && std::fabs(eq[6] - 1.4f) < 1e-6f && eq[5] == 3.5f);
    float m[Studio::kMasterStateCount];
    s.masterState(m);
    CHECK(m[0] == -1.5f && m[1] == 1.0f && m[3] == 60.0f && m[4] == 32.0f);
    CHECK_NEAR(s.duration(), 12.0, 1e-9);  // la pista "Dos" completa es la más larga
    struct stat sb {};
    CHECK(::stat((dir + "/audio/huerfano.wav").c_str(), &sb) != 0);  // se borró lo que nadie usa
    CHECK(::stat((dir + "/audio/uno.wav").c_str(), &sb) == 0);
    CHECK(estado(s)["mensaje"].asString().rfind("1 pista(s) no se encontraron", 0) == 0);
    // La pista siguiente usa un id nuevo.
    CHECK(s.loadDemo() && s.waitIdle(30000));
    CHECK(idDe(s, 2) == 9);
}

TEST(estudioSesionDanadaCargaPruebas) {
    const std::string dir = nuevaCarpeta("estudio4");
    FILE *f = std::fopen((dir + "/sesion.json").c_str(), "wb");
    std::fputs("{esto no es json", f);
    std::fclose(f);
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    CHECK(s.trackCount() == 4);
}

// Decodificador de prueba: entiende archivos que empiezan con "FALSO" (1 s a 32 kHz, estéreo L = R).
static std::string decodificadorFalso(int fd, int64_t, PcmSink &sink, std::string &codec, std::atomic<float> *prog) {
    char head[5] = {};
    if (::pread(fd, head, 5, 0) != 5 || std::memcmp(head, "FALSO", 5) != 0) return "no tiene audio que se pueda leer";
    std::string e = sink.begin(32000, 2);
    if (!e.empty()) return e;
    std::vector<float> buf(2 * 3200);
    for (int b = 0; b < 10; ++b) {
        for (int i = 0; i < 3200; ++i) buf[2 * i] = buf[2 * i + 1] = static_cast<float>(0.25 * std::sin(0.01 * (b * 3200 + i)));
        e = sink.write(buf.data(), 3200);
        if (!e.empty()) return e;
        if (prog) prog->store((b + 1) / 10.0f);
    }
    codec = "FALSO";
    return "";
}

static int abrir(const std::string &p) { return ::open(p.c_str(), O_RDONLY); }

TEST(estudioImporta) {
    const std::string dir = nuevaCarpeta("estudio5");
    Studio s(decodificadorFalso);
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    s.clearProject();
    CHECK(s.trackCount() == 0);
    // WAV del celular (44.1 kHz, 16 bits, estéreo).
    WavSpec spec;
    spec.rate = 44100;
    const std::string wav = tmpPath("grabacion.wav");
    makeWav(wav, spec, 44100, [](int64_t f, int c) { return sine(c ? 500 : 1000, 44100, f); });
    // Algo comprimido (lo entiende el decodificador de prueba) y algo que no es audio.
    const std::string falso = tmpPath("cancion.falso"), texto = tmpPath("notas.txt");
    FILE *f = std::fopen(falso.c_str(), "wb");
    std::fputs("FALSO...", f);
    std::fclose(f);
    f = std::fopen(texto.c_str(), "wb");
    std::fputs("hola", f);
    std::fclose(f);
    CHECK(s.importFiles({abrir(wav), abrir(falso), abrir(texto)}, {"grabación.wav", "canción.mp3", "notas.txt"}));
    CHECK(!s.importFiles({abrir(wav)}, {"otra.wav"}));  // ya hay un trabajo en curso
    CHECK(s.waitIdle(30000));
    const Json p = pistas(s);
    CHECK(p.size() == 2);
    CHECK(p[0]["nombre"].asString() == "grabación" && p[0]["canales"].asInt() == 2 && p[0]["cuadros"].asInt() == 48000);
    CHECK(p[0]["info"].asString() == "44.1 kHz → 48 kHz · 16 bits · estéreo");
    CHECK(p[1]["nombre"].asString() == "canción" && p[1]["canales"].asInt() == 1 && p[1]["cuadros"].asInt() == 48000);
    CHECK(p[1]["info"].asString() == "FALSO · 32 kHz → 48 kHz · estéreo idéntico → mono");
    const std::string msg = estado(s)["mensaje"].asString();
    CHECK(msg.rfind("2 agregada(s); problemas: notas.txt: no tiene audio que se pueda leer.", 0) == 0);
    CHECK(s.progress() < 0);

    // Un flujo que no se puede recorrer (como algunas carpetas en la nube): se copia primero.
    int tubo[2];
    CHECK(::pipe(tubo) == 0);
    std::thread escritor([&] {
        FILE *src = std::fopen(wav.c_str(), "rb");
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, src)) > 0)
            if (::write(tubo[1], buf, n) != static_cast<ssize_t>(n)) break;
        std::fclose(src);
        ::close(tubo[1]);
    });
    CHECK(s.importFiles({tubo[0]}, {"flujo.wav"}));
    CHECK(s.waitIdle(30000));
    escritor.join();
    CHECK(s.trackCount() == 3 && pistas(s)[2]["cuadros"].asInt() == 48000);

    // Sin decodificador (como en estas pruebas de Linux) lo que no es WAV no entra.
    Studio sin;
    const std::string dir2 = nuevaCarpeta("estudio5b");
    sin.open(dir2, dir2);
    CHECK(sin.waitIdle(30000));
    CHECK(sin.importFiles({abrir(falso)}, {"x.mp3"}) && sin.waitIdle(30000));
    CHECK(estado(sin)["mensaje"].asString().find("x.mp3: No es un archivo WAV") != std::string::npos);

    // Límite de 12 pistas.
    CHECK(sin.loadDemo() && sin.waitIdle(30000));
    CHECK(sin.loadDemo() && sin.waitIdle(30000));
    CHECK(sin.trackCount() == 12);
    CHECK(sin.loadDemo() && sin.waitIdle(30000));
    CHECK(sin.trackCount() == 12 && estado(sin)["mensaje"].asString().rfind("Límite de 12 pistas", 0) == 0);
    CHECK(sin.importFiles({abrir(wav)}, {"una.wav"}) && sin.waitIdle(30000));
    CHECK(estado(sin)["mensaje"].asString().find("límite de 12 pistas") != std::string::npos);
}

TEST(estudioEdicionYDeshacer) {
    const std::string dir = nuevaCarpeta("estudio6");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    const int id = idDe(s, 0), id2 = idDe(s, 1);
    const std::vector<Region> original = s.regions(id);
    s.seek(6.0);
    CHECK(s.cut(id, 0) == 1);
    std::vector<Region> r = s.regions(id);
    CHECK(r.size() == 2 && r[0].length == 288000 && r[1].start == 288000 && r[1].src == 288000);
    CHECK(s.canUndo());
    // Arrastre: cerca del cursor se pega (imán); el arrastre completo es un solo paso de deshacer.
    CHECK(s.editBegin(id, 1, Studio::kEditMove));
    s.editDrag(1500, 2000);
    CHECK(s.regions(id)[1].start == 288000);
    s.editDrag(48000, 2000);
    CHECK(s.regions(id)[1].start == 336000);
    s.editDrag(48000, 0);
    s.editEnd();
    CHECK_NEAR(s.duration(), (336000 + 288000) / 48000.0, 1e-9);
    CHECK(s.undo() && s.regions(id)[1].start == 288000);
    CHECK(s.undo() && s.regions(id) == original);
    CHECK(!s.canUndo() && !s.undo());
    CHECK(s.redo() && s.regions(id).size() == 2);
    CHECK(s.redo() && s.regions(id)[1].start == 336000 && !s.canRedo());
    // Recortes y fundidos con arrastre.
    CHECK(s.editBegin(id, 0, Studio::kEditTrimEnd));
    s.editDrag(-48000, 0);
    s.editEnd();
    CHECK(s.regions(id)[0].length == 240000);
    CHECK(s.editBegin(id, 0, Studio::kEditFadeIn));
    s.editDrag(9600, 0);
    s.editEnd();
    CHECK(s.regions(id)[0].fadeIn == 9600);
    CHECK(s.editBegin(id, 0, Studio::kEditTrimStart));
    s.editDrag(-100, 0);  // no antes del principio del archivo
    s.editEnd();
    CHECK(s.regions(id)[0].start == 0 && s.regions(id)[0].src == 0);
    // Fundidos seguidos con el deslizador: un solo paso.
    s.setRegionFade(id, 1, true, 4800);
    s.setRegionFade(id, 1, true, 9600);
    CHECK(s.regions(id)[1].fadeIn == 9600);
    CHECK(s.undo() && s.regions(id)[1].fadeIn == 0);
    // Empujar tres veces: un solo paso.
    const int64_t antes = s.regions(id)[1].start;
    for (int i = 0; i < 3; ++i) s.nudgeRegion(id, 1, 10);
    CHECK(s.regions(id)[1].start == antes + 3 * 480);
    CHECK(s.undo() && s.regions(id)[1].start == antes);
    // Cortar todas las pistas en el cursor: solo donde hay audio bajo el cursor.
    s.seek(3.0);
    CHECK(s.cut(-1, -1) == 4);
    CHECK(s.regions(id).size() == 3 && s.regions(id2).size() == 2);
    s.seek(13.5);
    CHECK(s.cut(-1, -1) == 0 && s.cut(id, 0) == -1);
    // Borrar y regresar al original.
    CHECK(s.deleteRegion(id2, 0) && s.regions(id2).size() == 1);
    s.restoreRegions(id2);
    CHECK(s.regions(id2).size() == 1 && s.regions(id2)[0].length == 12 * 48000 && s.regions(id2)[0].start == 0);
    CHECK(!s.editBegin(id, 99, Studio::kEditMove) && !s.editBegin(999, 0, Studio::kEditMove));
    // Sin pedazos la pista no suena: la duración la dan las demás.
    while (!s.regions(id).empty()) s.deleteRegion(id, 0);
    CHECK_NEAR(s.duration(), 12.0, 1e-9);
}

TEST(estudioEq) {
    const std::string dir = nuevaCarpeta("estudio7");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    const int id = idDe(s, 0);
    CHECK(s.addEqBand(id) == 0 && s.addEqBand(id) == 1 && s.addEqBand(id) == 2);
    std::vector<float> e = s.eqState(id);
    CHECK(e[2] == 3.0f && e[4] == 1000.0f && e[9] == 140.0f && e[14] == 4500.0f);  // en los huecos más grandes
    s.setEqBand(id, 0, kPeaking, 1000, 6, 1.4f, true);
    const float fr[3] = {100, 1000, 10000};
    float db[3];
    s.eqResponse(id, -1, fr, db, 3);
    CHECK_NEAR(db[1], 6, 0.05);
    CHECK_NEAR(s.eqTrimDb(id), -6, 0.1);
    s.eqResponse(id, 0, fr, db, 3);
    CHECK_NEAR(db[1], 6, 0.05);
    s.removeEqBand(id, 1);
    CHECK(s.addEqBand(id) == 2);
    e = s.eqState(id);
    CHECK(e[2] == 3.0f && e[14] == 140.0f);
    s.setEqBypass(id, true);
    CHECK(s.eqTrimDb(id) == 0.0f);
    s.resetEq(id);
    e = s.eqState(id);
    CHECK(e.size() == 3 && e[2] == 0.0f);
    s.eqResponse(id, -1, fr, db, 3);
    CHECK(db[1] == 0.0f);
    for (int i = 0; i < 12; ++i) CHECK(s.addEqBand(id) == i);
    CHECK(s.addEqBand(id) == -1);
}

TEST(estudioExportaYQuita) {
    const std::string dir = nuevaCarpeta("estudio8");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    const std::string out = tmpPath("estudio-mezcla.wav");
    const int64_t esperado = s.exportBytes();
    CHECK(s.exportMix(crear(out), "Mezcla.wav"));
    CHECK(s.waitIdle(30000));
    const Json st = estado(s);
    CHECK(st["exportaciones"].asInt() == 1 && st["ultimaOk"].asBool() && !st["exportando"].asBool(true));
    CHECK(st["mensaje"].asString().rfind("Mezcla exportada: Mezcla.wav (24 bits · 48000 Hz · 0:12 · ", 0) == 0);
    struct stat sb {};
    CHECK(::stat(out.c_str(), &sb) == 0 && sb.st_size == esperado);
    // Cancelar.
    s.setExportFormat(32, 44100);
    CHECK(s.exportMix(crear(tmpPath("cancelada2.wav")), "x.wav"));
    s.cancelExport();
    CHECK(s.waitIdle(30000));
    CHECK(estado(s)["exportaciones"].asInt() == 2);
    // Quitar una pista: su archivo se borra.
    const int id = idDe(s, 1);
    std::vector<uint8_t> sg;
    CHECK(!s.spectrogram(id, sg));  // se calcula aparte
    CHECK(s.waitIdle(30000) && s.spectrogram(id, sg) && sg.size() == size_t(Studio::kSpectrogramCols) * Studio::kSpectrogramRows);
    int32_t v1[Studio::kVersionCount], v2[Studio::kVersionCount];
    s.versions(v1);
    CHECK(s.removeTrack(id) && !s.removeTrack(id));
    s.versions(v2);
    CHECK(v2[Studio::kVerTracks] != v1[Studio::kVerTracks]);
    CHECK(s.trackCount() == 3 && !s.spectrogram(id, sg));
    CHECK(estado(s)["mensaje"].asString() == "3 pista(s) · 48000 Hz · ≈ 7 MB en disco");
    for (int i = 0; i < 3; ++i) s.removeTrack(idDe(s, 0));
    CHECK(!s.exportMix(crear(tmpPath("vacia.wav")), "v.wav"));  // sin pistas no hay qué exportar
    CHECK(estado(s)["mensaje"].asString() == "Sin pistas. Agrega audio, graba o carga las pistas de prueba.");
}

TEST(estudioOndaConFundidos) {
    const std::string dir = nuevaCarpeta("estudio9");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    const int id = idDe(s, 0);  // bajo: notas desde el inicio
    const std::vector<float> sin = s.regionWave(id, 0, 0, 12 * 48000, 100);
    CHECK(sin.size() == 200);
    s.setRegionFade(id, 0, true, 48000);  // 1 s
    const std::vector<float> con = s.regionWave(id, 0, 0, 12 * 48000, 100);
    const double g = std::pow(std::sin(0.5 * 5760.0 / 48000 * M_PI / 2), 2);  // centro de la columna 0
    CHECK_NEAR(con[1], sin[1] * g, 1e-5);
    CHECK(con[100] == sin[100] && con[101] == sin[101]);  // pasado el fundido, igual
    // Solo una parte: columnas de esa parte.
    const std::vector<float> parte = s.regionWave(id, 0, 6 * 48000, 7 * 48000, 10);
    CHECK(parte.size() == 20);
    CHECK(s.regionWave(id, 5, 0, 100, 10).empty() && s.regionWave(id, 0, 13 * 48000, 14 * 48000, 10).empty());
}

TEST(estudioMedidoresYAnalisis) {
    const std::string dir = nuevaCarpeta("estudio10");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    s.play();
    float out[MeterBallistics::kCount], st[AnalysisView::kStatCount];
    std::vector<float> buf(512 * 2), cols(64);
    bool cambio = false;
    for (int i = 0; i < 40; ++i) {
        for (int k = 0; k < 4; ++k) s.mixer().process(buf.data(), 512, 2);
        cambio = s.analysisUpdate(40 * (i + 1), false, st) || cambio;
    }
    s.meters(2000, out);
    CHECK(out[MeterBallistics::kMasterL] > -40.0f && out[0] > -60.0f);
    CHECK(cambio && st[0] == 1.0f && st[1] == 1.0f);
    s.spectrumColumns(cols.data(), 64);
    CHECK(*std::max_element(cols.begin(), cols.end()) > -80.0f);
    // Analizar una sola pista (la guitarra, a la derecha).
    s.setAnalysisSource(idDe(s, 2));
    for (int i = 0; i < 20; ++i) {
        for (int k = 0; k < 4; ++k) s.mixer().process(buf.data(), 512, 2);
        s.analysisUpdate(2000 + 40 * (i + 1), false, st);
    }
    CHECK(st[3] > 0.3f);
    s.pause();
    for (int k = 0; k < 4; ++k) s.mixer().process(buf.data(), 512, 2);
    CHECK(s.analysisUpdate(5000, false, st) && st[0] == 0.0f);
}

// ---------------------------------------------------------------- entrada en vivo y grabación

// Seno mono de `n` cuadros empezando en el cuadro `desde`.
static std::vector<float> senoMono(double freq, int64_t desde, int n, double amp = 0.5) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = static_cast<float>(sine(freq, 48000, desde + i, amp));
    return v;
}

static float picoDe(const std::vector<float> &v, size_t desde = 0) {
    float m = 0.0f;
    for (size_t i = desde; i < v.size(); ++i) m = std::max(m, std::fabs(v[i]));
    return m;
}

TEST(colaSinBloqueos) {
    SpscRing r(8);
    float a[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, b[10] = {};
    CHECK(r.write(a, 10) == 8 && r.available() == 8 && r.space() == 0);
    CHECK(r.read(b, 3) == 3 && b[0] == 1 && b[2] == 3);
    CHECK(r.write(a + 8, 2) == 2);  // da la vuelta
    r.skip(2);
    CHECK(r.read(b, 10) == 5 && b[0] == 6 && b[2] == 8 && b[3] == 9 && b[4] == 10);
    CHECK(r.available() == 0 && r.space() == 8);
}

TEST(entradaEnVivoEscucha) {
    Mixer m(48000);
    LiveInput in(48000, m);
    m.setLiveInput(&in);
    CHECK(!m.needsOutput());
    in.setActive(true, 1);
    in.setMonitor(true);
    CHECK(in.monitoring() && m.needsOutput());
    std::vector<float> out(192 * 2);
    float pico = 0.0f;
    for (int k = 0; k < 100; ++k) {
        const std::vector<float> x = senoMono(440, k * 192, 192);
        in.push(x.data(), 192, 1);
        m.process(out.data(), 192, 2);
        if (k > 50) pico = std::max(pico, picoDe(out));
    }
    // Se escucha como una pista más: pasa por el master (con el limitador y su compensación),
    // aunque la mezcla esté parada.
    Limiter lim;
    lim.setup(48000);
    CHECK_NEAR(pico, 0.5 * std::pow(10.0, lim.makeupDb() / 20.0), 0.02);
    // Apagada: se va a silencio con un fundido corto.
    in.setMonitor(false);
    CHECK(!m.needsOutput());
    for (int k = 0; k < 20; ++k) {
        const std::vector<float> x = senoMono(440, k * 192, 192);
        in.push(x.data(), 192, 1);
        m.process(out.data(), 192, 2);
    }
    CHECK(picoDe(out) < 1e-4f);
    // Si la entrada se cierra, tampoco se escucha.
    in.setMonitor(true);
    in.setActive(false, 1);
    CHECK(!in.monitoring());
    m.setLiveInput(nullptr);
}

TEST(entradaEnVivoNoAcumulaRetraso) {
    // La entrada llega en ráfagas grandes y la salida pide pedazos chicos: lo de más se tira.
    Mixer m(48000);
    LiveInput in(48000, m);
    m.setLiveInput(&in);
    in.setActive(true, 1);
    in.setMonitor(true);
    std::vector<float> out(64 * 2);
    for (int k = 0; k < 50; ++k) {
        const std::vector<float> x = senoMono(440, 0, 960);
        in.push(x.data(), 960, 1);
        in.push(x.data(), 960, 1);  // el doble de lo que se consume
        for (int j = 0; j < 15; ++j) m.process(out.data(), 64, 2);
    }
    m.process(out.data(), 64, 2);
    CHECK(in.monitoring());
    m.setLiveInput(nullptr);
}

TEST(entradaGananciaMedidoresYEq) {
    const std::string dir = nuevaCarpeta("entrada1");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    s.inputOpened(1);
    s.setInputGain(6.0f);
    LiveInput &in = s.input();
    for (int k = 0; k < 50; ++k) {
        const std::vector<float> x = senoMono(100, k * 256, 256, 0.25);
        in.push(x.data(), 256, 1);
    }
    float med[MeterBallistics::kCount];
    s.meters(1000, med);
    CHECK_NEAR(med[MeterBallistics::kInputL], 20 * std::log10(0.5), 0.3);  // +6 dB
    CHECK(med[MeterBallistics::kInputClip] == 0.0f);
    s.setInputGain(24.0f);
    for (int k = 0; k < 50; ++k) {
        const std::vector<float> x = senoMono(100, k * 256, 256, 0.25);
        in.push(x.data(), 256, 1);
    }
    s.meters(1100, med);
    CHECK(med[MeterBallistics::kInputClip] == 1.0f);
    s.clearClip();
    s.meters(1200, med);
    CHECK(med[MeterBallistics::kInputClip] == 0.0f);
    float st[Studio::kInputStateCount];
    s.inputState(st);
    CHECK(st[0] == 1.0f && st[1] == 1.0f && st[2] == 24.0f && st[3] == 0.0f && st[6] == 0.0f);
    // El EQ de la entrada usa las mismas funciones con el id 0.
    CHECK(s.addEqBand(Studio::kInputId) == 0);
    s.setEqBand(Studio::kInputId, 0, kHighPass, 2000, 0, 0.71f, true);
    std::vector<float> e = s.eqState(Studio::kInputId);
    CHECK(e.size() == 8 && e[2] == 1.0f && e[3] == kHighPass);
    const float fr[2] = {100, 10000};
    float db[2];
    s.eqResponse(Studio::kInputId, -1, fr, db, 2);
    CHECK(db[0] < -30.0f && std::fabs(db[1]) < 0.5f);
    // Con escucha: el grave casi no se oye.
    s.setInputGain(0.0f);
    s.setMonitor(true);
    std::vector<float> out(256 * 2);
    float pico = 0.0f;
    for (int k = 0; k < 80; ++k) {
        const std::vector<float> x = senoMono(100, k * 256, 256, 0.5);
        in.push(x.data(), 256, 1);
        s.mixer().process(out.data(), 256, 2);
        if (k > 40) pico = std::max(pico, picoDe(out));
    }
    CHECK(pico < 0.02f);
    s.inputClosed();
    s.inputState(st);
    CHECK(st[0] == 0.0f);
}

// Simula el audio del celular: cada vuelta pide un pedazo de salida y entrega uno de entrada.
static void correr(Studio &s, int vueltas, int64_t &cuadro, double freq = 440, int n = 192) {
    std::vector<float> out(static_cast<size_t>(n) * 2);
    for (int k = 0; k < vueltas; ++k) {
        s.mixer().process(out.data(), n, 2);
        const std::vector<float> x = senoMono(freq, cuadro, n);
        s.input().push(x.data(), n, 1);
        cuadro += n;
    }
}

TEST(grabacionSinPistas) {
    const std::string dir = nuevaCarpeta("graba1");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    s.clearProject();
    CHECK(!s.recordStart());  // sin entrada abierta no se puede
    s.inputOpened(1);
    CHECK(s.recordStart() && s.recording());
    CHECK(!s.loadDemo());  // no se mezclan trabajos con la grabación
    int64_t cuadro = 0;
    correr(s, 250, cuadro);  // 1 s
    CHECK(s.position() > 0.9);  // corre aunque no haya pistas
    float st[Studio::kInputStateCount];
    s.inputState(st);
    CHECK(st[6] == 1.0f);
    const int id = s.recordStop();
    CHECK(id > 0 && !s.recording() && !s.isPlaying());
    const Json p = pistas(s);
    CHECK(p.size() == 1 && p[0]["nombre"].asString() == "Grabación 1" && p[0]["canales"].asInt() == 1);
    CHECK(p[0]["cuadros"].asInt() == 250 * 192);
    CHECK(p[0]["info"].asString() == "grabación · mono · 48 kHz · 0:01");
    const std::vector<Region> r = s.regions(id);
    CHECK(r.size() == 1 && r[0].start == 0 && r[0].src == 0);  // sin mezcla no se compensa
    // El archivo tiene lo que entró, tal cual.
    std::vector<float> wave = s.regionWave(id, 0, 0, 48000, 10);
    CHECK(wave.size() == 20 && wave[1] > 0.45f && wave[1] < 0.55f);
    CHECK(estado(s)["mensaje"].asString().rfind("Grabación 1 agregada", 0) == 0);
    s.inputClosed();
}

TEST(grabacionAlineadaConLaMezcla) {
    const std::string dir = nuevaCarpeta("graba2");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    s.inputOpened(1);
    // EQ de la entrada: la pista nueva lo recibe.
    s.addEqBand(Studio::kInputId);
    s.setEqBand(Studio::kInputId, 0, kPeaking, 3000, 4, 1, true);
    s.setLatencies(480, 960);
    float st[Studio::kInputStateCount];
    s.inputState(st);
    CHECK_NEAR(st[5], 30.0, 0.01);  // ms de ida y vuelta
    s.seek(2.0);
    CHECK(s.recordStart());
    int64_t cuadro = 0;
    correr(s, 100, cuadro);
    const int id = s.recordStop();
    CHECK(id > 0 && s.trackCount() == 5);
    const std::vector<Region> r = s.regions(id);
    // El primer cuadro llegó con la mezcla en 2 s + un pedazo; se adelanta lo que tarda el audio.
    CHECK(r.size() == 1 && r[0].start == 2 * 48000 + 192 - 1440 && r[0].src == 0 && r[0].length == 100 * 192);
    const std::vector<float> e = s.eqState(id);
    CHECK(e.size() == 8 && e[4] == 3000.0f && e[5] == 4.0f);
    s.inputClosed();
}

TEST(grabacionSigueDespuesDelFinal) {
    const std::string dir = nuevaCarpeta("graba3");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    s.inputOpened(1);
    s.setLatencies(0, 0);
    s.seek(11.9);
    CHECK(s.recordStart());
    int64_t cuadro = 0;
    correr(s, 125, cuadro);  // 0.5 s: pasa del final (12 s)
    CHECK(s.isPlaying() && s.position() > 12.3);
    const int id = s.recordStop();
    CHECK(id > 0);
    CHECK(s.duration() > 12.35);
    const std::vector<Region> r = s.regions(id);
    CHECK(r.size() == 1 && r[0].end() > 12 * 48000 + 18000);
    // Al terminar se puede reproducir normal (se detiene al final).
    s.seek(12.0);
    s.play();
    correr(s, 200, cuadro);
    CHECK(!s.isPlaying());
    s.inputClosed();
}

TEST(grabacionCortaYCancelar) {
    const std::string dir = nuevaCarpeta("graba4");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    s.inputOpened(2);
    auto archivos = [&] {
        int n = 0;
        if (DIR *d = ::opendir((dir + "/audio").c_str())) {
            while (dirent *e = ::readdir(d)) n += e->d_name[0] != '.';
            ::closedir(d);
        }
        return n;
    };
    CHECK(archivos() == 4);
    CHECK(s.recordStart());
    int64_t cuadro = 0;
    correr(s, 10, cuadro);  // 40 ms
    CHECK(s.recordStop() == -1 && s.trackCount() == 4 && archivos() == 4);
    CHECK(estado(s)["mensaje"].asString().rfind("Grabación muy corta", 0) == 0);
    CHECK(s.recordStart());
    correr(s, 100, cuadro);
    s.recordCancel();
    CHECK(!s.recording() && s.trackCount() == 4 && archivos() == 4 && !s.isPlaying());
    // Estéreo: dos canales en la pista nueva.
    CHECK(s.recordStart());
    std::vector<float> out(192 * 2), x(192 * 2, 0.25f);
    for (int k = 0; k < 50; ++k) {
        s.mixer().process(out.data(), 192, 2);
        s.input().push(x.data(), 192, 2);
    }
    const int id = s.recordStop();
    CHECK(id > 0 && pistas(s)[4]["canales"].asInt() == 2);
    // Cerrar la entrada a media grabación guarda lo grabado.
    CHECK(s.recordStart());
    correr(s, 100, cuadro);
    s.inputClosed();
    CHECK(!s.recording() && s.trackCount() == 6);
}

TEST(entradaEnLaSesion) {
    const std::string dir = nuevaCarpeta("entrada2");
    {
        Studio s;
        s.open(dir, dir);
        CHECK(s.waitIdle(30000));
        s.setInputGain(7.3f);
        s.setMonitorDb(-12.0f);
        s.addEqBand(Studio::kInputId);
        s.setEqBand(Studio::kInputId, 0, kLowShelf, 200, -3, 0.71f, true);
        s.setEqAutoGain(Studio::kInputId, false);
        CHECK(s.saveNow());
    }
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    float st[Studio::kInputStateCount];
    s.inputState(st);
    CHECK(st[2] == 7.5f && st[4] == -12.0f && st[3] == 0.0f);  // la escucha empieza apagada
    std::vector<float> e = s.eqState(Studio::kInputId);
    CHECK(e.size() == 8 && e[1] == 0.0f && e[3] == kLowShelf && e[4] == 200.0f && e[5] == -3.0f);
    // Quitar todas las pistas no borra los ajustes de la entrada.
    s.clearProject();
    s.inputState(st);
    e = s.eqState(Studio::kInputId);
    CHECK(st[2] == 7.5f && e.size() == 8);
}

TEST(analisisDeLaEntrada) {
    const std::string dir = nuevaCarpeta("entrada3");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    s.inputOpened(1);
    s.setAnalysisSource(Studio::kInputId);
    CHECK(s.mixer().analysisSource() == Mixer::kTapInput);
    float st[AnalysisView::kStatCount];
    bool activo = false;
    for (int k = 0; k < 40; ++k) {
        for (int j = 0; j < 4; ++j) {
            const std::vector<float> x = senoMono(1000, (k * 4 + j) * 512, 512);
            s.input().push(x.data(), 512, 1);
        }
        s.analysisUpdate(40 * (k + 1), false, st);
        activo = st[0] == 1.0f;
    }
    CHECK(activo && !s.isPlaying());  // se analiza aunque la mezcla no suene
    std::vector<float> cols(200);
    s.spectrumColumns(cols.data(), 200);
    const int pico = static_cast<int>(std::max_element(cols.begin(), cols.end()) - cols.begin());
    const double hz = 20 * std::pow(1000.0, (pico + 0.5) / 200.0);
    CHECK(hz > 900 && hz < 1100);
    s.inputClosed();
    s.setAnalysisSource(-1);
}

TEST(concurrenciaEstudio) {
    // Audio, pantalla, trabajos y mantenimiento a la vez, sin carreras.
    const std::string dir = nuevaCarpeta("estudio11");
    WavSpec spec;
    const std::string wav = tmpPath("conc-estudio.wav");
    makeWav(wav, spec, 9600, [](int64_t f, int c) { return sine(c ? 300 : 700, 48000, f); });
    Studio s;
    s.open(dir, dir);
    s.startBackground();
    std::atomic<bool> done{false};
    std::thread audio([&] {
        std::vector<float> out(256 * 2);
        while (!done.load()) s.mixer().process(out.data(), 256, 2);
    });
    std::mt19937 rng(7);
    float buf[64];
    int32_t ver[Studio::kVersionCount];
    for (int it = 0; it < 300; ++it) {
        const int n = s.trackCount();
        const int id = n > 0 ? idDe(s, static_cast<int>(rng() % n)) : -1;
        switch (rng() % 23) {
            case 16: s.autoAdd(id, rng() % 2, rng() % 600000, (rng() % 100) / 100.0f); break;
            case 17:
                if (s.autoBegin(id, rng() % 2, 0)) {
                    s.autoDrag(static_cast<int64_t>(rng() % 20000) - 10000, ((rng() % 100) - 50) / 100.0f);
                    s.autoEnd();
                }
                break;
            case 18: s.setAutoMode(id, rng() % 3); s.autoTouch(id, 0, true); s.setVolume(id, -(rng() % 30) * 1.0f); break;
            case 19: { float a[Studio::kAutoNowCount * kMaxTracks]; s.automationNow(a); s.autoTouch(id, 0, false); s.autoLane(id, 0); } break;
            case 20: s.applyFxPreset(id, static_cast<int>(rng() % fx::presetCount())); break;
            case 21: s.setFx(id, static_cast<int>(rng() % fx::kCount), (rng() % 100) / 10.0f); s.fxState(id); break;
            case 22: { float l[Studio::kLoudCount]; s.loudness(l); s.loudnessHistory(nullptr, nullptr, 0); if (rng() % 9 == 0) s.resetLoudness(); } break;
            case 0: s.setVolume(id, (rng() % 66) - 60.0f); break;
            case 1: s.setPan(id, (rng() % 200) - 100.0f); break;
            case 2: s.setMute(id, rng() % 2); s.setSolo(id, rng() % 3 == 0); break;
            case 3: s.addEqBand(id); s.setEqBand(id, 0, rng() % 6, 20.0f + rng() % 18000, 3, 1, true); break;
            case 4: s.removeEqBand(id, 0); break;
            case 5: s.seek((rng() % 120) / 10.0); s.cut(rng() % 3 ? id : -1, 0); break;
            case 6:
                if (s.editBegin(id, 0, 1 + static_cast<int>(rng() % 5))) {
                    s.editDrag(static_cast<int64_t>(rng() % 20000) - 10000, 500);
                    s.editEnd();
                }
                break;
            case 7: s.undo(); break;
            case 8: s.redo(); break;
            case 9: s.playPause(); break;
            case 10: s.importFiles({abrir(wav)}, {"c.wav"}); break;
            case 11: if (n > 6) s.removeTrack(id); break;
            case 12: { std::vector<uint8_t> sg; s.spectrogram(id, sg); } break;
            case 13: s.setAnalysisSource(rng() % 2 ? id : -1); s.analysisUpdate(it * 40, rng() % 2, buf); break;
            case 14: { float m[MeterBallistics::kCount]; s.meters(it * 16, m); } break;
            default: s.regionWave(id, 0, 0, 48000, 50); s.statusJson(); s.versions(ver); s.setNotch(rng() % 2 ? 60 : 0); break;
        }
    }
    CHECK(s.waitIdle(60000));
    done.store(true);
    audio.join();
    s.stopBackground();
    CHECK(s.saveNow());
}

TEST(concurrenciaEntrada) {
    // Entrada, salida, pantalla y disco a la vez (TSan).
    const std::string dir = nuevaCarpeta("entrada4");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    s.inputOpened(1);
    std::atomic<bool> done{false};
    std::thread salida([&] {
        std::vector<float> out(192 * 2);
        while (!done.load()) s.mixer().process(out.data(), 192, 2);
    });
    std::thread entrada([&] {
        int64_t f = 0;
        while (!done.load()) {
            const std::vector<float> x = senoMono(220, f, 240);
            s.input().push(x.data(), 240, 1);
            f += 240;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    std::mt19937 rng(11);
    float buf[64];
    for (int it = 0; it < 200; ++it) {
        switch (rng() % 11) {
            case 9: s.setVoiceFx(static_cast<int>(rng() % vfx::kCount), static_cast<float>(rng() % 100) / 50.0f); break;
            case 10: {
                if (rng() % 2) s.applyVoicePreset(static_cast<int>(rng() % vfx::presetCount()));
                float vm[VoiceFx::kMeterCount];
                s.voiceMeters(vm);
                s.voiceFxState();
            } break;
            case 0: s.setMonitor(rng() % 2); break;
            case 1: s.setInputGain((rng() % 48) - 24.0f); s.setMonitorDb(-(rng() % 30) * 1.0f); break;
            case 2: s.addEqBand(Studio::kInputId); s.setEqBand(Studio::kInputId, 0, rng() % 6, 50.0f + rng() % 9000, 3, 1, true); break;
            case 3: s.removeEqBand(Studio::kInputId, 0); break;
            case 4: if (!s.recording()) s.recordStart(); break;
            case 5: if (s.recording() && rng() % 3 == 0) s.recordStop(); break;
            case 6: { float m[MeterBallistics::kCount]; s.meters(it * 16, m); s.inputState(buf); } break;
            case 7: s.setAnalysisSource(rng() % 2 ? Studio::kInputId : -1); s.analysisUpdate(it * 40, false, buf); break;
            default: s.recordingWave(50, 4.0); break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (s.recording()) s.recordStop();
    done.store(true);
    salida.join();
    entrada.join();
    s.inputClosed();
    CHECK(!s.recording());
}

// ---------------------------------------------------------------- automatización
TEST(automatizacionCurvas) {
    using namespace autom;
    // Escala del carril: extremos, 0 dB y paneo.
    CHECK_NEAR(toPos(kVolume, 6.0f), 1.0, 1e-6);
    CHECK_NEAR(toPos(kVolume, -60.0f), 0.0, 1e-6);
    CHECK_NEAR(toPos(kVolume, 0.0f), 1.0 - std::sqrt(6.0 / 66.0), 1e-6);
    CHECK_NEAR(toPos(kPan, -100.0f), 1.0, 1e-6);  // arriba = izquierda
    CHECK_NEAR(toPos(kPan, 0.0f), 0.5, 1e-6);
    for (float v : {-60.0f, -24.5f, -6.0f, 0.0f, 3.5f, 6.0f}) CHECK_NEAR(fromPos(kVolume, toPos(kVolume, v)), v, 1e-4);
    for (float v : {-100.0f, -30.0f, 0.0f, 55.0f, 100.0f}) CHECK_NEAR(fromPos(kPan, toPos(kPan, v)), v, 1e-3);
    CHECK(gainFromPos(0.0f) == 0.0f);  // −60 dB es silencio
    CHECK_NEAR(gainFromPos(toPos(kVolume, -6.0f)), std::pow(10.0, -6.0 / 20), 1e-5);
    CHECK_NEAR(panFromPos(1.0f), -1.0, 1e-6);
    CHECK_NEAR(panFromPos(0.25f), 0.5, 1e-6);
    CHECK(clampValue(kVolume, -3.04f) == -3.0f && clampValue(kVolume, 9.0f) == 6.0f && clampValue(kPan, 33.6f) == 34.0f);
    CHECK(clampValue(kVolume, NAN) == 0.0f);

    // Valor entre puntos: recta en la escala del carril; antes y después se queda.
    const std::vector<Point> pts = {{1000, -6.0f}, {3000, 0.0f}};
    CHECK(valueAt(kVolume, {}, 500, -2.5f) == -2.5f);
    CHECK(valueAt(kVolume, pts, 0, 0) == -6.0f && valueAt(kVolume, pts, 9999, 0) == 0.0f);
    const float mid = (toPos(kVolume, -6.0f) + toPos(kVolume, 0.0f)) / 2;
    CHECK_NEAR(valueAt(kVolume, pts, 2000, 0), fromPos(kVolume, mid), 1e-4);
    const std::vector<PosPoint> pp = toPosPoints(kVolume, pts);
    CHECK_NEAR(posAt(pp, 2000), mid, 1e-6);
    CHECK_NEAR(posAt(pp, 0), pp[0].pos, 1e-6);

    // Ordenar, quitar repetidos (gana el último) y negativos.
    std::vector<Point> v = {{500, 3.33f}, {-20, 1.0f}, {100, 200.0f}, {500, -1.0f}};
    normalize(kVolume, v);
    CHECK(v.size() == 3 && v[0].frame == 0 && v[1].frame == 100 && v[1].value == 6.0f && v[2].value == -1.0f);

    // Simplificar: una recta queda en sus dos puntas; una esquina se queda.
    std::vector<Point> recta, esquina;
    for (int i = 0; i <= 100; ++i) recta.push_back({i * 480, 100.0f - 2.0f * i});
    CHECK(simplify(kPan, recta, 0.004f).size() == 2);
    for (int i = 0; i <= 100; ++i) esquina.push_back({i * 480, i < 50 ? 0.0f : -80.0f});
    const std::vector<Point> e = simplify(kPan, esquina, 0.004f);
    CHECK(e.size() == 4 && e.front().frame == 0 && e.back().frame == 48000);
    CHECK(valueAt(kPan, e, 49 * 480, 1) == 0.0f && valueAt(kPan, e, 50 * 480, 1) == -80.0f);
    // Lane: copiar comparte la lista.
    Lane a(recta), b = a;
    CHECK(a == b && &a.get() == &b.get() && a.size() == 101 && Lane().empty());
}

TEST(automatizacionEnElMezclador) {
    using namespace autom;
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(0, constTrack("auto1.wav", 2, 48000, 0.25f, 0.25f));
    m.setTrack(1, constTrack("auto2.wav", 1, 48000, 0.5f, 0.5f));
    // Pista 1: −6 dB hasta 0.5 s y de golpe −20 dB. Pista 2: paneo de izquierda a derecha.
    m.setAutomation(0, kVolume, toPosPoints(kVolume, {{0, -6.0f}, {24000, -6.0f}, {24001, -20.0f}}));
    m.setAutomation(1, kPan, toPosPoints(kPan, {{0, -100.0f}, {48000, 100.0f}}));
    m.setTrackGain(1, 0.0f);  // la 2 se calla salvo para medir su paneo abajo
    CHECK(m.automation(0, kVolume).size() == 3 && m.automation(0, kPan).empty());
    std::vector<float> out = referencia(m);
    CHECK_NEAR(out[2 * 10000], 0.25 * std::pow(10.0, -6.0 / 20), 1e-5);
    CHECK_NEAR(out[2 * 10000 + 1], 0.25 * std::pow(10.0, -6.0 / 20), 1e-5);
    CHECK_NEAR(out[2 * 40000], 0.25 * std::pow(10.0, -20.0 / 20), 1e-5);
    // El escalón se suaviza (sin clic): a 1 ms del cambio va en camino.
    CHECK(out[2 * 24100] < 0.25 * std::pow(10.0, -6.0 / 20) && out[2 * 24100] > 0.25 * std::pow(10.0, -20.0 / 20));

    // Paneo automatizado (mono): empieza a la izquierda y termina a la derecha.
    m.setTrackGain(0, 0.0f);
    m.setAutomation(0, kVolume, {});
    m.setTrackGain(1, 1.0f);
    out = referencia(m);
    CHECK_NEAR(out[2 * 200], 0.5, 1e-3);
    CHECK_NEAR(out[2 * 200 + 1], 0.0, 1e-2);
    CHECK_NEAR(out[2 * 24000], 0.5 * std::cos(M_PI / 4), 5e-3);  // centro
    CHECK_NEAR(out[2 * 47900 + 1], 0.5, 1e-3);
    CHECK_NEAR(out[2 * 47900], 0.0, 1e-2);

    // En vivo: al empezar a sonar desde un salto, el volumen ya está donde marca la
    // automatización (no sube desde el anterior).
    m.setTrackGain(1, 0.0f);
    m.setAutomation(1, kPan, {});
    m.setTrackGain(0, 1.0f);
    m.setAutomation(0, kVolume, toPosPoints(kVolume, {{0, 0.0f}, {24000, 0.0f}, {24001, -40.0f}}));
    m.seek(26000);
    m.play();
    std::vector<float> blk(512 * 2);
    m.process(blk.data(), 512, 2);
    CHECK_NEAR(blk[2 * 400], 0.25 * 0.01, 1e-4);
    // En espera (el dedo en el deslizador) suena el valor fijo.
    m.setAutomationHold(0, kVolume, true);
    run(m, 7200);
    m.process(blk.data(), 512, 2);
    CHECK_NEAR(blk[2 * 400], 0.25, 1e-4);
    m.setAutomationHold(0, kVolume, false);
    run(m, 7200);
    m.process(blk.data(), 512, 2);
    CHECK_NEAR(blk[2 * 400], 0.25 * 0.01, 1e-4);
    // Quitar la pista borra su automatización.
    m.takeTrack(0);
    m.setTrack(0, constTrack("auto3.wav", 2, 4800, 0.25f, 0.25f));
    CHECK(m.automation(0, kVolume).empty());
}

static std::vector<autom::Point> carril(const Studio &s, int id, int param) {
    const std::vector<double> v = s.autoLane(id, param);
    std::vector<autom::Point> r;
    for (size_t i = 4; i + 2 < v.size(); i += 3) r.push_back({static_cast<int64_t>(v[i]), static_cast<float>(v[i + 2])});
    return r;
}

TEST(estudioAutomatizacionPuntos) {
    using namespace autom;
    const std::string dir = nuevaCarpeta("auto1");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    const int id = idDe(s, 0), slot = static_cast<int>(pistas(s)[0]["slot"].asInt());
    std::vector<double> lane = s.autoLane(id, kVolume);
    CHECK(lane.size() == 4 && lane[0] == 0 && lane[2] == 0.0 && std::fabs(lane[3] - toPos(kVolume, 0)) < 1e-6);
    CHECK(s.autoLane(id, 7).empty() && s.autoLane(999, 0).empty());
    int32_t v0[Studio::kVersionCount], v1[Studio::kVersionCount];
    s.versions(v0);
    // Agregar en orden, con un poco de imán en 0 dB.
    CHECK(s.autoAdd(id, kVolume, 96000, toPos(kVolume, -12.0f)) == 0);
    CHECK(s.autoAdd(id, kVolume, 48000, toPos(kVolume, 0.2f)) == 0);
    CHECK(s.autoAdd(id, kVolume, 144000, toPos(kVolume, -60.0f)) == 2);
    s.versions(v1);
    CHECK(v1[Studio::verAuto(slot)] != v0[Studio::verAuto(slot)] && v1[Studio::kVerHistory] != v0[Studio::kVerHistory]);
    std::vector<Point> c = carril(s, id, kVolume);
    CHECK(c.size() == 3 && c[0].frame == 48000 && c[0].value == 0.0f && c[1].value == -12.0f && c[2].value == -60.0f);
    CHECK(s.mixer().automation(slot, kVolume).size() == 3);
    // Lo que marca el deslizador ahora (cursor a 1.5 s: entre 0 y −12 dB, en la escala del carril).
    float now[Studio::kAutoNowCount * kMaxTracks];
    s.seek(1.5);
    s.automationNow(now);
    CHECK(now[Studio::kAutoNowCount * slot] == 1.0f);
    CHECK_NEAR(now[Studio::kAutoNowCount * slot + 1], valueAt(kVolume, c, 72000, 0), 1e-4);
    CHECK(now[Studio::kAutoNowCount * slot + 2] == 0.0f);  // el paneo no está automatizado
    // Arrastrar un punto: no pasa a sus vecinos y todo el arrastre es un paso.
    CHECK(s.autoBegin(id, kVolume, 1));
    s.autoDrag(1000, 0.0f);
    s.autoDrag(200000, 0.05f);
    s.autoEnd();
    c = carril(s, id, kVolume);
    CHECK(c[1].frame == 143999 && c[1].value == clampValue(kVolume, fromPos(kVolume, toPos(kVolume, -12.0f) + 0.05f)));
    CHECK(!s.autoBegin(id, kVolume, 3));
    CHECK(s.undo() && carril(s, id, kVolume)[1].frame == 96000);
    CHECK(s.redo() && carril(s, id, kVolume)[1].frame == 143999);
    // Quitar y borrar todo, con deshacer.
    CHECK(s.autoRemove(id, kVolume, 0) && carril(s, id, kVolume).size() == 2);
    CHECK(!s.autoRemove(id, kVolume, 5));
    s.autoClear(id, kVolume);
    CHECK(carril(s, id, kVolume).empty() && s.mixer().automation(slot, kVolume).empty());
    CHECK(s.undo() && carril(s, id, kVolume).size() == 2);
    CHECK(s.undo() && carril(s, id, kVolume).size() == 3);
    // Apagada: los puntos se quedan pero suenan los valores fijos.
    s.setAutoMode(id, Studio::kAutoOff);
    float p[Studio::kTrackParamCount];
    CHECK(s.trackParams(id, p) && p[5] == Studio::kAutoOff);
    CHECK(s.mixer().automation(slot, kVolume).empty() && carril(s, id, kVolume).size() == 3);
    s.automationNow(now);
    CHECK(now[Studio::kAutoNowCount * slot] == 0.0f && now[Studio::kAutoNowCount * slot + 1] == 0.0f);
    // Dibujar con la automatización apagada la prende.
    CHECK(s.autoAdd(id, kPan, 0, 1.0f) == 0);
    CHECK(s.trackParams(id, p) && p[5] == Studio::kAutoRead);
    CHECK(carril(s, id, kPan)[0].value == -100.0f && s.mixer().automation(slot, kVolume).size() == 3);
    // Un cuadro que ya tiene punto se reemplaza; el paneo pega al centro.
    CHECK(s.autoAdd(id, kPan, 0, 0.51f) == 0 && carril(s, id, kPan).size() == 1 && carril(s, id, kPan)[0].value == 0.0f);

    // La sesión la guarda.
    CHECK(s.saveNow());
    Studio s2;
    s2.open(dir, dir);
    CHECK(s2.waitIdle(30000));
    CHECK(carril(s2, id, kVolume) == carril(s, id, kVolume) && carril(s2, id, kPan) == carril(s, id, kPan));
    CHECK(s2.trackParams(id, p) && p[5] == Studio::kAutoRead);
    CHECK(s2.mixer().automation(slot, kVolume).size() == 3);
    // Y la exportación suena con ella (el mezclador de exportar la copia).
    Mixer copia(48000);
    std::string err;
    CHECK(s2.mixer().cloneInto(copia, err) && copia.automation(slot, kVolume).size() == 3);
}

// Hace sonar el estudio `segundos` en pedazos de 480 cuadros; `cada` se llama en cada pedazo
// (lo que haría la pantalla en cada cuadro).
static void sonar(Studio &s, double segundos, const std::function<void(double)> &cada = nullptr) {
    std::vector<float> out(480 * 2);
    float now[Studio::kAutoNowCount * kMaxTracks];
    const int n = static_cast<int>(segundos * 100);
    for (int i = 0; i < n; ++i) {
        if (cada) cada(static_cast<double>(i) / n);
        s.mixer().process(out.data(), 480, 2);
        s.automationNow(now);
    }
}

TEST(estudioGrabarMovimientos) {
    using namespace autom;
    const std::string dir = nuevaCarpeta("auto2");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    const int id = idDe(s, 0), id2 = idDe(s, 1);
    const int slot = static_cast<int>(pistas(s)[0]["slot"].asInt());
    float now[Studio::kAutoNowCount * kMaxTracks];
    s.setAutoMode(id, Studio::kAutoWrite);
    s.play();
    sonar(s, 1.0);
    // Primera pasada: de 0 a −20 dB en un segundo, con el carril vacío.
    s.autoTouch(id, kVolume, true);
    s.automationNow(now);
    CHECK(now[Studio::kAutoNowCount * slot] == 4.0f);  // se graba el volumen (y no lo sigue)
    sonar(s, 1.0, [&](double x) { s.setVolume(id, static_cast<float>(-20.0 * x)); });
    s.setVolume(id, -20.0f);
    sonar(s, 0.05);
    s.autoTouch(id, kVolume, false);
    std::vector<Point> c = carril(s, id, kVolume);
    CHECK(c.size() >= 3 && c.size() <= 10);
    CHECK(c.front().value == 0.0f && c.front().frame == 48000);
    CHECK(c.back().value == -20.0f && c.back().frame == 98400);
    // Sigue lo que hizo el dedo (de 0 a −20 dB parejo en 1 s), sin escalones y con pocos puntos.
    for (int64_t f = 48000; f <= 96000; f += 4800) CHECK_NEAR(valueAt(kVolume, c, f, 0), -20.0 * (f - 48000) / 48000.0, 0.8);
    for (size_t i = 1; i < c.size(); ++i) CHECK(c[i].value <= c[i - 1].value);  // va bajando
    CHECK(s.canUndo());
    s.automationNow(now);
    CHECK(now[Studio::kAutoNowCount * slot] == 1.0f);  // ahora la sigue
    CHECK_NEAR(now[Studio::kAutoNowCount * slot + 1], -20.0, 1e-4);  // lo último se queda

    // Segunda pasada, de 1.5 s a 1.7 s con el dedo quieto: luego regresa a lo que había.
    s.seek(1.4);
    sonar(s, 0.1);
    const std::vector<Point> antes = c;
    s.autoTouch(id, kVolume, true);
    const float tocado = s.trackParams(id, now) ? now[0] : 99;  // el deslizador tomó el valor de la automatización
    CHECK_NEAR(tocado, valueAt(kVolume, antes, 72000, 0), 0.26);
    s.setVolume(id, -6.0f);
    sonar(s, 0.2);
    s.autoTouch(id, kVolume, false);
    c = carril(s, id, kVolume);
    CHECK_NEAR(valueAt(kVolume, c, 76800, 0), -6.0, 1e-4);  // 1.6 s
    CHECK_NEAR(valueAt(kVolume, c, 120000, 0), valueAt(kVolume, antes, 120000, 0), 1e-4);  // 2.5 s: como antes
    CHECK_NEAR(valueAt(kVolume, c, 24000, 0), valueAt(kVolume, antes, 24000, 0), 1e-4);    // 0.5 s: como antes
    CHECK(s.undo() && carril(s, id, kVolume) == antes);
    CHECK(s.redo() && carril(s, id, kVolume) == c);

    // Dedo quieto un rato y luego un cambio: el valor se quedó (no una rampa larga); moverlo
    // seguido, en cambio, es una rampa aunque el cursor avance a saltos grandes.
    s.setAutoMode(id2, Studio::kAutoWrite);
    s.seek(3.0);
    sonar(s, 0.05);
    s.autoTouch(id2, kVolume, true);
    s.setVolume(id2, -3.0f);
    sonar(s, 1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    s.setVolume(id2, -12.0f);
    sonar(s, 0.1);
    s.autoTouch(id2, kVolume, false);
    std::vector<Point> q = carril(s, id2, kVolume);
    CHECK(q.size() >= 3);
    CHECK_NEAR(valueAt(kVolume, q, 4 * 48000 - 960, 0), -3.0, 1e-4);
    CHECK_NEAR(valueAt(kVolume, q, 4 * 48000 + 2400, 0), -12.0, 1e-4);
    s.autoClear(id2, kVolume);
    s.setAutoMode(id2, Studio::kAutoRead);

    // Detenido: con puntos, mover el deslizador cambia el punto del cursor.
    s.pause();
    sonar(s, 0.05);
    s.seek(0.5);
    s.autoTouch(id, kVolume, true);
    s.setVolume(id, -3.0f);
    s.autoTouch(id, kVolume, false);
    const std::vector<Point> d = carril(s, id, kVolume);
    CHECK(d.size() == c.size() + 1 && d[0].frame == 24000 && d[0].value == -3.0f);

    // Leer: mover el deslizador se oye mientras está el dedo, pero no se graba.
    s.setAutoMode(id, Studio::kAutoRead);
    s.play();
    s.autoTouch(id, kVolume, true);
    s.automationNow(now);
    CHECK(now[Studio::kAutoNowCount * slot] == 0.0f);
    s.setVolume(id, -40.0f);
    sonar(s, 0.2);
    s.autoTouch(id, kVolume, false);
    CHECK(carril(s, id, kVolume) == d);
    CHECK(estado(s)["mensaje"].asString().find("sigue su automatización") != std::string::npos);

    // Un toque suelto (sin dedo encima) al grabar: un escalón en el cursor; antes, lo de siempre.
    s.setAutoMode(id2, Studio::kAutoWrite);
    s.setPan(id2, 50.0f);
    const std::vector<Point> e = carril(s, id2, kPan);
    CHECK(e.size() == 2 && e[0].value == 0.0f && e[1].value == 50.0f && e[1].frame == e[0].frame + 1);
    // Detenido y con el carril vacío: solo cambia el valor fijo.
    s.pause();
    sonar(s, 0.05);
    s.setVolume(id2, -5.0f);
    CHECK(carril(s, id2, kVolume).empty());
    // Mientras se toca no se puede deshacer; quitar la pista termina el toque.
    s.autoTouch(id2, kPan, true);
    CHECK(!s.undo());
    CHECK(s.removeTrack(id2));
    CHECK(s.undo());
}

// ---------------------------------------------------------------- volumen percibido (LUFS)
// Mide una señal estéreo (generada por partes) con el front-end y el medidor.
static void medir(LoudnessMeter &m, LoudnessFrontEnd &fe, double seconds,
                  const std::function<std::pair<float, float>(int64_t)> &gen, int64_t &frame) {
    const int64_t n = static_cast<int64_t>(seconds * 48000 + 0.5);
    std::vector<float> l(512), r(512);
    LoudnessBlock b[2];
    for (int64_t done = 0; done < n;) {
        const int k = static_cast<int>(std::min<int64_t>(512, n - done));
        for (int i = 0; i < k; ++i) {
            const auto s = gen(frame++);
            l[i] = s.first;
            r[i] = s.second;
        }
        const int made = fe.process(l.data(), r.data(), k, b, 2);
        for (int i = 0; i < made; ++i) m.add(b[i]);
        done += k;
    }
}

static std::function<std::pair<float, float>(int64_t)> senoDb(double hz, double dbfs) {
    const double a = std::pow(10.0, dbfs / 20);
    return [=](int64_t f) {
        const float x = static_cast<float>(a * std::sin(2 * M_PI * hz * f / 48000.0));
        return std::make_pair(x, x);
    };
}

TEST(lufsComoLaNorma) {
    // EBU Tech 3341: seno de 1 kHz a −23 dBFS en los dos canales = −23.0 LUFS.
    LoudnessFrontEnd fe;
    LoudnessMeter m;
    CHECK(m.momentary() == kLoudnessNone && m.integrated() == kLoudnessNone && m.range() == kLoudnessNone);
    int64_t f = 0;
    medir(m, fe, 20, senoDb(1000, -23), f);
    CHECK_NEAR(m.integrated(), -23.0, 0.1);
    CHECK_NEAR(m.momentary(), -23.0, 0.1);
    CHECK_NEAR(m.shortTerm(), -23.0, 0.1);
    CHECK_NEAR(m.maxMomentary(), -23.0, 0.1);
    CHECK_NEAR(m.seconds(), 20.0, 1e-9);
    CHECK(m.range() >= 0.0f && m.range() < 0.2f);
    CHECK_NEAR(m.truePeakDb(), -23.0, 0.1);
    float hm[3], hs[3];
    m.history(hm, hs, 3);
    CHECK_NEAR(hm[2], m.momentary(), 1e-4);
    CHECK_NEAR(hs[2], m.shortTerm(), 1e-4);

    // Caso 3: −36, −23, −36 dBFS (10, 60, 10 s): la compuerta relativa deja fuera lo bajo.
    fe.reset();
    m.reset();
    f = 0;
    medir(m, fe, 10, senoDb(1000, -36), f);
    medir(m, fe, 60, senoDb(1000, -23), f);
    medir(m, fe, 10, senoDb(1000, -36), f);
    CHECK_NEAR(m.integrated(), -23.0, 0.1);
    // Caso 5: −26, −20, −26 dBFS (20, 20.1, 20 s) = −23.0.
    fe.reset();
    m.reset();
    f = 0;
    medir(m, fe, 20, senoDb(1000, -26), f);
    medir(m, fe, 20.1, senoDb(1000, -20), f);
    medir(m, fe, 20, senoDb(1000, -26), f);
    CHECK_NEAR(m.integrated(), -23.0, 0.1);
    CHECK_NEAR(m.maxShortTerm(), -20.0, 0.1);
    // Silencio: debajo de la compuerta absoluta no cuenta.
    fe.reset();
    m.reset();
    medir(m, fe, 5, [](int64_t) { return std::make_pair(0.0f, 0.0f); }, f);
    CHECK(m.integrated() == kLoudnessNone && m.momentary() == kLoudnessNone && m.truePeakDb() == kLoudnessNone);
}

TEST(lufsPonderacionK) {
    // Los agudos pesan más y los graves menos: la curva K de la norma (a 48 kHz, respecto a 1 kHz:
    // +3.34 dB a 10 kHz, −1.83 dB a 100 Hz, −11.1 dB a 25 Hz).
    auto nivel = [](double hz) {
        LoudnessFrontEnd fe;
        LoudnessMeter m;
        int64_t f = 0;
        medir(m, fe, 3, senoDb(hz, -20), f);
        return m.integrated();
    };
    const float k1 = nivel(1000), k10 = nivel(10000), k100 = nivel(100), k25 = nivel(25);
    CHECK_NEAR(k1, -20.0, 0.1);
    CHECK_NEAR(k10 - k1, 3.34, 0.05);
    CHECK_NEAR(k100 - k1, -1.83, 0.05);
    CHECK_NEAR(k25 - k1, -11.09, 0.15);
}

TEST(lufsRangoDeVolumen) {
    // EBU Tech 3342: 20 s a un nivel y 20 s a otro.
    auto rango = [](double a, double b) {
        LoudnessFrontEnd fe;
        LoudnessMeter m;
        int64_t f = 0;
        medir(m, fe, 20, senoDb(1000, a), f);
        medir(m, fe, 20, senoDb(1000, b), f);
        return m.range();
    };
    CHECK_NEAR(rango(-20, -30), 10.0, 1.0);
    CHECK_NEAR(rango(-20, -15), 5.0, 1.0);
    CHECK_NEAR(rango(-40, -20), 20.0, 1.0);
}

TEST(lufsPicoReal) {
    // A un cuarto de la frecuencia de muestreo con fase de 45°, las muestras se quedan en 0.707:
    // el pico real (entre muestras) es 1.0 = 0 dBTP.
    LoudnessFrontEnd fe;
    LoudnessMeter m;
    int64_t f = 0;
    medir(m, fe, 1, [](int64_t k) {
        const float x = static_cast<float>(std::sin(2 * M_PI * 12000.0 * k / 48000.0 + M_PI / 4));
        return std::make_pair(x, -x);
    }, f);
    CHECK_NEAR(m.truePeakDb(), 0.0, 0.25);
    // Un seno lento: el pico real es el de las muestras.
    fe.reset();
    m.reset();
    medir(m, fe, 1, senoDb(997, -6), f);
    CHECK_NEAR(m.truePeakDb(), -6.0, 0.05);
}

// Lee un WAV exportado y lo mide.
static LoudnessMeter medirArchivo(const std::string &path) {
    Clip c;
    LoudnessMeter m;
    if (!readWav(std::fopen(path.c_str(), "rb"), c).empty() || c.channels != 2) return m;
    LoudnessFrontEnd fe(c.sampleRate);
    std::vector<float> l(4096), r(4096);
    LoudnessBlock b[2];
    for (int64_t f = 0; f < c.frames;) {
        const int n = static_cast<int>(std::min<int64_t>(4096, c.frames - f));
        for (int i = 0; i < n; ++i) {
            l[i] = c.data[2 * (f + i)];
            r[i] = c.data[2 * (f + i) + 1];
        }
        const int made = fe.process(l.data(), r.data(), n, b, 2);
        for (int i = 0; i < made; ++i) m.add(b[i]);
        f += n;
    }
    return m;
}

TEST(exportarAjustaLufs) {
    std::unique_ptr<Mixer> live(mezclaDePrueba());
    const LoudnessReport antes = measureMix(*live, 0.0f, false, nullptr, 0, 1, nullptr);
    CHECK(antes.error.empty() && antes.integrated > -40.0f && antes.integrated < -5.0f);
    CHECK(antes.shortTerm.size() == 10);  // 1 s: un valor cada 100 ms
    // Más bajo que como suena: exacto. Más alto: el limitador cuida los picos.
    for (float objetivo : {-23.0f, -16.0f, -9.0f}) {
        const std::string p = tmpPath("lufs.wav");
        ExportFormat f;
        f.bits = 32;
        f.isFloat = true;
        f.targetLufs = objetivo;
        std::atomic<float> prog{0};
        const ExportResult r = exportMix(*live, crear(p), f, &prog, nullptr);
        CHECK(r.error.empty());
        CHECK_NEAR(prog.load(), 1.0, 1e-6);
        const LoudnessMeter m = medirArchivo(p);
        std::printf("  objetivo %.0f: %.2f LUFS (ajuste %+.1f dB, pico real %.2f dBTP)\n", objetivo, m.integrated(),
                    r.gainDb, r.truePeak);
        CHECK_NEAR(m.integrated(), objetivo, objetivo < antes.integrated ? 0.1 : 0.5);
        CHECK_NEAR(r.lufs, m.integrated(), 0.05);
        CHECK_NEAR(r.gainDb, objetivo - antes.integrated, 3.0);
    }
    // Sin objetivo: tal como suena (y lo informa).
    const std::string p = tmpPath("lufs0.wav");
    ExportFormat f;
    f.bits = 32;
    const ExportResult r = exportMix(*live, crear(p), f, nullptr, nullptr);
    CHECK(r.error.empty() && r.gainDb == 0.0f);
    CHECK_NEAR(r.lufs, antes.integrated, 0.01);
    CHECK_NEAR(r.truePeak, antes.truePeak, 0.01);
}

TEST(estudioLufsEnVivo) {
    const std::string dir = nuevaCarpeta("lufs1");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    float v[Studio::kLoudCount];
    s.loudness(v);
    CHECK(v[0] == kLoudnessNone && v[2] == kLoudnessNone && v[7] == 0.0f && v[8] == -14.0f && v[9] == Studio::kLoudNoData);
    // Toda la canción de principio a fin (se detiene sola al final).
    s.play();
    std::vector<float> out(480 * 2);
    for (int i = 0; i < 2000 && s.isPlaying(); ++i) s.mixer().process(out.data(), 480, 2);
    CHECK(!s.isPlaying());
    s.loudness(v);
    CHECK_NEAR(v[7], 12.0, 0.15);
    CHECK(v[0] > -60.0f && v[1] > -60.0f && v[2] > -40.0f && v[2] < -5.0f);
    CHECK(v[3] >= 0.0f && v[4] > -20.0f && v[4] < 1.0f && v[5] >= v[1] - 0.01f);
    CHECK(v[9] != Studio::kLoudNoData && std::fabs(v[10] - (v[2] + 14.0f)) < 1e-4f);
    float hm[10], hs[10];
    s.loudnessHistory(hm, hs, 10);
    CHECK_NEAR(hs[9], v[1], 1e-4);
    // Lo mismo que mide la canción completa fuera de tiempo real.
    CHECK(s.measureSong());
    float song[Studio::kSongLoudCount];
    s.songLoudness(song);
    CHECK(song[0] == 1.0f || song[0] == 2.0f);
    CHECK(s.waitIdle(30000));
    s.songLoudness(song);
    CHECK(song[0] == 2.0f);
    std::printf("  en vivo %.2f LUFS, canción completa %.2f LUFS\n", v[2], song[1]);
    CHECK_NEAR(song[1], v[2], 0.2);
    CHECK_NEAR(song[6], 12.0, 1e-3);
    CHECK_NEAR(song[3], v[4], 0.3);
    const std::vector<float> curva = s.songLoudnessCurve(60);
    CHECK(curva.size() == 60 && curva[30] > -60.0f);
    CHECK(estado(s)["mensaje"].asString().rfind("Canción completa: ", 0) == 0);
    // Cambiar la mezcla deja la medición vieja.
    s.setVolume(idDe(s, 0), -6.0f);
    s.songLoudness(song);
    CHECK(song[0] == 3.0f);
    // Reproducir desde el inicio empieza una medición nueva; desde la mitad, sigue.
    s.seek(6.0);
    s.play();
    for (int i = 0; i < 100; ++i) s.mixer().process(out.data(), 480, 2);
    s.loudness(v);
    CHECK_NEAR(v[7], 13.0, 0.15);
    s.stop();
    s.mixer().process(out.data(), 480, 2);
    s.play();
    for (int i = 0; i < 100; ++i) s.mixer().process(out.data(), 480, 2);
    s.loudness(v);
    CHECK_NEAR(v[7], 1.0, 0.15);
    s.resetLoudness();
    s.loudness(v);
    CHECK(v[7] == 0.0f && v[2] == kLoudnessNone);
    // Objetivo y ajuste al exportar: se guardan.
    s.setLoudnessTarget(-16);
    s.setExportNormalize(true);
    float m[Studio::kMasterStateCount];
    s.masterState(m);
    CHECK(m[6] == -16.0f && m[7] == 1.0f);
    s.setLoudnessTarget(-5);
    s.masterState(m);
    CHECK(m[6] == -14.0f);
    s.setLoudnessTarget(-23);
    CHECK(s.saveNow());
    Studio s2;
    s2.open(dir, dir);
    CHECK(s2.waitIdle(30000));
    s2.masterState(m);
    CHECK(m[6] == -23.0f && m[7] == 1.0f);
    s2.songLoudness(song);
    CHECK(song[0] == 0.0f);
}

// ---------------------------------------------------------------- efectos
static double rmsDe(const std::vector<float> &x, size_t from, size_t to) {
    double s = 0;
    to = std::min(to, x.size());
    if (to <= from) return 0;
    for (size_t i = from; i < to; ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(to - from));
}

TEST(efectosCompresor) {
    // Curva: debajo del umbral no toca; arriba, baja según la proporción; la rodilla es suave.
    CHECK(Compressor::gainComputer(-40, -20, 4) == 0.0f);
    CHECK_NEAR(Compressor::gainComputer(-8, -20, 4), -9.0, 1e-5);
    CHECK(Compressor::gainComputer(-20, -20, 4) < 0.0f && Compressor::gainComputer(-20, -20, 4) > -1.0f);
    CHECK(Compressor::gainComputer(0, -20, 1) == 0.0f);  // 1:1 no comprime
    Compressor c;
    c.setup(48000);
    CompSettings s;
    s.thresholdDb = -20;
    s.ratio = 4;
    s.attackMs = 1;
    s.releaseMs = 50;
    s.makeupDb = 0;
    // Un seno a −6 dBFS: queda unos 10.5 dB más bajo (−16.5 dBFS de pico).
    std::vector<float> l(48000), r(48000);
    for (int i = 0; i < 48000; ++i) l[i] = r[i] = static_cast<float>(0.5 * std::sin(2 * M_PI * 1000 * i / 48000.0));
    float gr = 0;
    for (int i = 0; i < 48000; i += 64) gr = c.process(&l[i], &r[i], 64, s);
    float pk = 0;
    for (int i = 24000; i < 48000; ++i) pk = std::max(pk, std::fabs(l[i]));
    CHECK_NEAR(20 * std::log10(pk), -16.5, 1.0);
    CHECK_NEAR(gr, 10.5, 1.0);
    CHECK(l[30000] == r[30000]);  // los dos canales juntos
    // Debajo del umbral: igual, más la ganancia de compensación.
    c.reset();
    s.makeupDb = 6;
    std::vector<float> q(4800);
    for (int i = 0; i < 4800; ++i) q[i] = static_cast<float>(0.01 * std::sin(2 * M_PI * 440 * i / 48000.0));
    std::vector<float> q0 = q;
    for (int i = 0; i < 4800; i += 64) c.process(&q[i], nullptr, std::min(64, 4800 - i), s);
    CHECK_NEAR(q[3000] / q0[3000], std::pow(10.0, 6.0 / 20), 1e-3);
}

TEST(efectosDelay) {
    StereoDelay d;
    d.allocate(48000, 1550);
    DelaySettings s;
    s.timeMs = 100;
    s.feedback = 0.5f;
    s.toneHz = 16000;
    // Un golpe de 10 ms de seno: ecos a 100 y 200 ms, el segundo a la mitad.
    const int n = 48000;
    std::vector<float> in(n, 0.0f), ol(n), orr(n);
    for (int i = 0; i < 480; ++i) in[i] = static_cast<float>(std::sin(2 * M_PI * 1000 * i / 48000.0));
    for (int i = 0; i < n; i += 64) d.process(&in[i], nullptr, 64, &ol[i], &orr[i], s);
    const double e0 = rmsDe(ol, 0, 4700), e1 = rmsDe(ol, 4800, 5280), e2 = rmsDe(ol, 9600, 10080);
    CHECK(e0 < 1e-6);
    CHECK_NEAR(e1, rmsDe(in, 0, 480), 0.05);
    CHECK_NEAR(e2 / e1, 0.5, 0.05);
    CHECK_NEAR(rmsDe(orr, 4800, 5280), e1, 1e-6);  // sin ping-pong: igual en los dos lados
    // Ping-pong: el primer eco a la izquierda, el segundo a la derecha.
    d.restart();
    s.pingPong = true;
    for (int i = 0; i < n; i += 64) d.process(&in[i], nullptr, 64, &ol[i], &orr[i], s);
    CHECK(rmsDe(ol, 4800, 5280) > 0.3 && rmsDe(orr, 4800, 5280) < 1e-4);
    CHECK(rmsDe(orr, 9600, 10080) > 0.1 && rmsDe(ol, 9600, 10080) < 1e-4);
    // Después de restart() no suenan ecos viejos.
    d.restart();
    std::vector<float> cero(4800, 0.0f), ol2(4800), or2(4800);
    for (int i = 0; i < 4800; i += 64) d.process(&cero[i], nullptr, std::min(64, 4800 - i), &ol2[i], &or2[i], s);
    CHECK(rmsDe(ol2, 0, 4800) == 0.0 && rmsDe(or2, 0, 4800) == 0.0);
    // Menos brillo: los ecos pierden agudos.
    d.restart();
    s.pingPong = false;
    s.toneHz = 1000;
    std::vector<float> hf(n, 0.0f);
    for (int i = 0; i < 480; ++i) hf[i] = static_cast<float>(std::sin(2 * M_PI * 8000 * i / 48000.0));
    for (int i = 0; i < n; i += 64) d.process(&hf[i], nullptr, 64, &ol[i], &orr[i], s);
    CHECK(rmsDe(ol, 4800, 5280) < 0.25 * rmsDe(hf, 0, 480));
}

TEST(efectosReverb) {
    Reverb rv;
    rv.allocate(48000);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    for (float decay : {0.5f, 2.0f, 6.0f}) {
        rv.restart();
        ReverbSettings s;
        s.decayS = decay;
        s.toneHz = 16000;
        s.predelayMs = 0;
        // Ruido 2 s y luego silencio: la cola baja 60 dB en el tiempo pedido.
        const int n = 48000 * 2 + static_cast<int>(48000 * decay * 1.2);
        std::vector<float> in(n, 0.0f), ol(n), orr(n);
        for (int i = 0; i < 96000; ++i) in[i] = u(rng);
        for (int i = 0; i < n; i += 64) rv.process(&in[i], nullptr, std::min(64, n - i), &ol[i], &orr[i], s);
        const double nivel = 20 * std::log10(rmsDe(ol, 48000, 96000) / rmsDe(in, 48000, 96000));
        const size_t a = 96000 + 4800, b = a + static_cast<size_t>(48000 * decay * 0.5);
        const double caida = 20 * std::log10(rmsDe(ol, b - 2400, b + 2400) / rmsDe(ol, a - 2400, a + 2400));
        const double esperada = -60.0 * (b - a) / 48000.0 / decay;
        double corr = 0, el = 0, er = 0;
        for (size_t i = a; i < b; ++i) {
            corr += static_cast<double>(ol[i]) * orr[i];
            el += static_cast<double>(ol[i]) * ol[i];
            er += static_cast<double>(orr[i]) * orr[i];
        }
        std::printf("  reverb %.1f s: nivel %+.1f dB, caída %.1f dB (esperada %.1f), correlación L/R %.2f\n", decay,
                    nivel, caida, esperada, corr / std::sqrt(el * er));
        CHECK(nivel > -9.0 && nivel < 3.0);
        CHECK_NEAR(caida, esperada, std::fabs(esperada) * 0.3);
        CHECK(std::fabs(corr / std::sqrt(el * er)) < 0.5);
        CHECK(std::all_of(ol.begin(), ol.end(), [](float x) { return std::isfinite(x) && std::fabs(x) < 4.0f; }));
    }
    // Pre-delay: nada antes de que pase.
    rv.restart();
    ReverbSettings s;
    s.predelayMs = 50;
    std::vector<float> in(9600, 0.0f), ol(9600), orr(9600);
    in[0] = 1.0f;
    for (int i = 0; i < 9600; i += 64) rv.process(&in[i], nullptr, 64, &ol[i], &orr[i], s);
    CHECK(rmsDe(ol, 0, 2400) == 0.0 && rmsDe(ol, 2400, 9600) > 0.0);
    // Y restart() la deja en silencio.
    rv.restart();
    std::fill(in.begin(), in.end(), 0.0f);
    for (int i = 0; i < 9600; i += 64) rv.process(&in[i], nullptr, 64, &ol[i], &orr[i], s);
    CHECK(rmsDe(ol, 0, 9600) == 0.0);
}

TEST(efectosEnElMezclador) {
    Mixer m(48000);
    m.setLimiter(false);
    m.setTrack(0, genTrack("fx1.wav", 1, 48000, [](int64_t f, int) {
        return f < 4800 ? static_cast<float>(0.3 * std::sin(2 * M_PI * 440 * f / 48000.0)) : 0.0f;
    }));
    const std::vector<float> seco = referencia(m);
    CHECK(m.trackFx(0, false) == nullptr && m.trackFx(3, true) == nullptr);  // espacio vacío
    TrackFx *f = m.trackFx(0, true);
    CHECK(f != nullptr && m.trackFx(0, false) == f);
    // Sin nada prendido suena igual.
    CHECK(referencia(m) == seco);
    // Reverb: la pista mono queda con cola estéreo después de que se calla.
    f->set(fx::kReverbOn, 1);
    f->set(fx::kReverbMix, 0.5f);
    std::vector<float> rev = referencia(m);
    CHECK(rev.size() == seco.size());
    double cola = 0, dif = 0;
    for (size_t i = 2 * 9600; i < rev.size(); i += 2) {
        cola += std::fabs(rev[i]);
        dif += std::fabs(rev[i] - rev[i + 1]);
    }
    CHECK(cola > 1.0 && dif > 0.1);
    // El sonido directo no cambia (la reverb se suma aparte).
    CHECK_NEAR(rev[2 * 100], seco[2 * 100], 0.05);
    // Delay de 50 ms: el eco aparece a los 50 ms.
    f->set(fx::kReverbOn, 0);
    f->set(fx::kDelayOn, 1);
    f->set(fx::kDelayTime, 50);
    f->set(fx::kDelayMix, 1.0f);
    f->set(fx::kDelayFeedback, 0);
    std::vector<float> eco = referencia(m);
    CHECK(rmsDe(eco, 2 * 2400 + 2 * 200, 2 * 4800) > 0.5 * rmsDe(seco, 2 * 200, 2 * 2400));
    CHECK(rmsDe(eco, 2 * 5000, 2 * 7000) > 0.1);  // el eco del final del sonido
    // Compresor: baja el nivel y lo muestra en su medidor.
    f->set(fx::kDelayOn, 0);
    f->set(fx::kCompOn, 1);
    f->set(fx::kCompThreshold, -30);
    f->set(fx::kCompRatio, 10);
    f->set(fx::kCompAttack, 0.5f);
    std::vector<float> comp = referencia(m);
    CHECK(rmsDe(comp, 2 * 2400, 2 * 4800) < 0.5 * rmsDe(seco, 2 * 2400, 2 * 4800));
    m.play();
    run(m, 4800);
    float med[kMeterCount];
    m.readMeters(med);
    CHECK(med[3 + 2 * kMaxTracks] > 10.0f);
    m.readMeters(med);
    CHECK(med[3 + 2 * kMaxTracks] == 0.0f);  // se lee y se borra
    // Silenciar la pista calla también sus envíos.
    f->set(fx::kCompOn, 0);
    f->set(fx::kReverbOn, 1);
    m.setTrackMute(0, true);
    std::vector<float> mudo = referencia(m);
    CHECK(rmsDe(mudo, 0, mudo.size()) == 0.0);
    // Quitar la pista se lleva sus efectos.
    m.takeTrack(0);
    m.setTrack(0, constTrack("fx2.wav", 1, 4800, 0.1f, 0.1f));
    CHECK(m.trackFx(0, false) == nullptr);
}

TEST(estudioEfectos) {
    const std::string dir = nuevaCarpeta("fx1");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    const int id = idDe(s, 1);
    const int slot = static_cast<int>(pistas(s)[1]["slot"].asInt());
    std::vector<float> st = s.fxState(id);
    CHECK(st.size() == fx::kCount && st[fx::kCompOn] == 0.0f && st[fx::kCompThreshold] == -18.0f);
    CHECK(s.fxState(999).empty());
    CHECK(s.mixer().trackFx(slot, false) == nullptr);  // nada prendido: sin memoria de más
    const std::vector<float> info = Studio::fxInfo();
    CHECK(info.size() == 4 * fx::kCount && info[4 * fx::kDelayTime + 1] == 1500.0f && info[4 * fx::kDelayTime + 3] == 1.0f);
    // Valores fuera de rango o con decimales de más se ajustan.
    int32_t v0[Studio::kVersionCount], v1[Studio::kVersionCount];
    s.versions(v0);
    s.setFx(id, fx::kCompThreshold, -99);
    s.setFx(id, fx::kCompRatio, 3.14159f);
    s.setFx(id, fx::kCompOn, 0.7f);
    s.versions(v1);
    CHECK(v1[Studio::verFx(slot)] != v0[Studio::verFx(slot)]);
    st = s.fxState(id);
    CHECK(st[fx::kCompThreshold] == -60.0f && std::fabs(st[fx::kCompRatio] - 3.1f) < 1e-5f && st[fx::kCompOn] == 1.0f);
    TrackFx *f = s.mixer().trackFx(slot, false);
    CHECK(f && f->get(fx::kCompThreshold) == -60.0f && f->get(fx::kCompOn) == 1.0f);
    s.setFx(id, 99, 1);  // parámetro que no existe: nada
    // Presets.
    std::string err;
    const Json p = Json::parse(Studio::fxPresetsJson(), err);
    CHECK(err.empty() && p.size() == static_cast<size_t>(fx::presetCount()) && p[0].asString() == "Voz");
    CHECK(p[p.size() - 1].asString() == "Sin efectos");
    CHECK(s.applyFxPreset(id, 1));  // voz con eco
    st = s.fxState(id);
    CHECK(st[fx::kCompOn] == 1 && st[fx::kDelayOn] == 1 && st[fx::kReverbOn] == 1 && st[fx::kDelayPingPong] == 1);
    CHECK(!s.applyFxPreset(id, 99) && !s.applyFxPreset(999, 0));
    for (int i = 0; i < fx::presetCount(); ++i)
        for (int k = 0; k < fx::kCount; ++k) {
            const float v = fx::preset(i).values[static_cast<size_t>(k)];
            CHECK(v == fx::clampValue(k, v));  // los presets ya vienen ajustados
        }
    // Suena (la canción con el preset es distinta) y se exporta igual que en vivo.
    std::unique_ptr<Mixer> copia(new Mixer(48000));
    CHECK(s.mixer().cloneInto(*copia, err) && copia->trackFx(slot, false) != nullptr);
    CHECK(copia->trackFx(slot, false)->get(fx::kDelayTime) == 320.0f);
    // La sesión lo guarda.
    CHECK(s.saveNow());
    Studio s2;
    s2.open(dir, dir);
    CHECK(s2.waitIdle(30000));
    CHECK(s2.fxState(id) == s.fxState(id));
    CHECK(s2.mixer().trackFx(slot, false) != nullptr);
    CHECK(s2.fxState(idDe(s2, 0))[fx::kCompOn] == 0.0f);
    // Medidor del compresor.
    s2.applyFxPreset(id, 2);
    s2.play();
    std::vector<float> out(480 * 2);
    for (int i = 0; i < 100; ++i) s2.mixer().process(out.data(), 480, 2);
    float med[MeterBallistics::kCount];
    s2.meters(1000, med);
    CHECK(med[MeterBallistics::kCompressor + slot] > 0.5f);
    CHECK(med[MeterBallistics::kCompressor + static_cast<int>(pistas(s2)[0]["slot"].asInt())] == 0.0f);
}

TEST(efectosRapidos) {
    // Doce pistas con todo prendido: muy por debajo del tiempo real.
    Mixer m(48000);
    for (int i = 0; i < kMaxTracks; ++i) {
        m.setTrack(i, seno("rap" + std::to_string(i) + ".wav", i % 2 + 1, 48000 * 4, 200.0 + 50 * i, 0.1));
        TrackFx *f = m.trackFx(i, true);
        for (int k = 0; k < fx::kCount; ++k) f->set(k, fx::preset(1).values[static_cast<size_t>(k)]);
    }
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<float> out = referencia(m);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  12 pistas con compresor, delay y reverb: 4 s de audio en %.2f s\n", s);
    CHECK(out.size() == 48000u * 4 * 2);
    CHECK(std::all_of(out.begin(), out.end(), [](float x) { return std::isfinite(x); }));
}


// ---------------------------------------------------------------- separar la voz (sep/)
// Datos de prueba: tests/native/datos (mini.wvm y sus respuestas, generados con el código
// original en PyTorch por tools/modelos/referencias.py).
static std::string datos(const std::string &n) { return "tests/native/datos/" + n; }

static double snrDb(const std::vector<float> &ref, const std::vector<float> &x) {
    double s = 0, e = 0;
    const size_t n = std::min(ref.size(), x.size());
    for (size_t i = 0; i < n; ++i) {
        s += static_cast<double>(ref[i]) * ref[i];
        const double d = static_cast<double>(ref[i]) - x[i];
        e += d * d;
    }
    return ref.size() == x.size() ? 10 * std::log10(s / std::max(e, 1e-30)) : -999.0;
}

TEST(mitadFlotante) {
    // Las 65536 combinaciones contra la definición de float16.
    int malas = 0;
    for (uint32_t h = 0; h < 65536; ++h) {
        const int sign = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
        double v;
        if (e == 0) v = std::ldexp(m / 1024.0, -14);
        else if (e == 31) v = m ? std::nan("") : INFINITY;
        else v = std::ldexp(1.0 + m / 1024.0, e - 15);
        if (sign) v = -v;
        const float f = halfToFloat(static_cast<uint16_t>(h));
        const bool ok = std::isnan(v) ? std::isnan(f) : (f == static_cast<float>(v) && std::signbit(f) == (sign == 1));
        if (!ok) ++malas;
    }
    CHECK(malas == 0);
    uint16_t in[5] = {0x3C00, 0xC000, 0x7BFF, 0x0001, 0x8000};
    float out[5];
    halfToFloat(in, out, 5);
    CHECK(out[0] == 1.0f && out[1] == -2.0f && out[2] == 65504.0f && out[3] == std::ldexp(1.0f, -24));
    CHECK(out[4] == 0.0f && std::signbit(out[4]));
}

TEST(modeloArchivo) {
    std::string e;
    auto m = ModelFile::open(datos("mini.wvm"), e);
    CHECK(m && e.empty());
    if (!m) return;
    CHECK(m->header()["modelo"].asString() == "mel_band_roformer");
    CHECK(m->header()["config"]["dim"].asInt() == 32);
    // Matrices chicas en float32; las grandes (≥ 4096 valores) en float16.
    const ModelFile::Tensor *t = m->find("layers.0.0.layers.0.0.to_qkv.weight");
    CHECK(t && t->type == ModelFile::kF32 && t->shape.size() == 2 && t->shape[0] == 96 && t->shape[1] == 32);
    const ModelFile::Tensor *grande = m->find("mask_estimators.0.to_freqs.0.0.2.weight");
    CHECK(grande && grande->type == ModelFile::kF16 && grande->count == 128 * 128);
    const ModelFile::Tensor *b = m->find("freqs_per_band");
    CHECK(b && b->type == ModelFile::kU8 && b->count == 12 * 129);
    std::vector<float> v;
    CHECK(m->floats("layers.0.0.layers.0.0.norm.gamma", v, 32) && v.size() == 32);
    CHECK(!m->floats("layers.0.0.layers.0.0.norm.gamma", v, 31));
    CHECK(!m->find("no.existe") && !m->floats("no.existe", v));

    // El mismo modelo dentro de otro archivo (como un asset sin comprimir en el APK).
    std::vector<char> todo;
    {
        FILE *f = std::fopen(datos("mini.wvm").c_str(), "rb");
        int c;
        while ((c = std::fgetc(f)) != EOF) todo.push_back(static_cast<char>(c));
        std::fclose(f);
    }
    const std::string dentro = tmpPath("apk.bin");
    {
        FILE *f = std::fopen(dentro.c_str(), "wb");
        const std::string basura(12345, 'x');
        std::fwrite(basura.data(), 1, basura.size(), f);
        std::fwrite(todo.data(), 1, todo.size(), f);
        std::fwrite(basura.data(), 1, 100, f);
        std::fclose(f);
    }
    const int fd = ::open(dentro.c_str(), O_RDONLY);
    auto m2 = ModelFile::openFd(fd, 12345, static_cast<int64_t>(todo.size()), e);
    ::close(fd);
    std::vector<float> v2;
    CHECK(m2 && m2->floats("layers.0.0.layers.0.0.norm.gamma", v2, 32) && v2 == v);

    // Archivos rotos: no se abren (y no revientan).
    auto roto = [&](const std::vector<char> &bytes) {
        const std::string r = tmpPath("roto.wvm");
        FILE *f = std::fopen(r.c_str(), "wb");
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
        std::string err;
        auto x = ModelFile::open(r, err);
        return !x && !err.empty();
    };
    std::vector<char> malo = todo;
    malo[0] = 'X';
    CHECK(roto(malo));
    CHECK(roto(std::vector<char>(todo.begin(), todo.begin() + todo.size() / 2)));  // cortado
    malo = todo;
    malo[8] = static_cast<char>(0xFF);  // cabecera más larga que el archivo
    malo[9] = static_cast<char>(0xFF);
    CHECK(roto(malo));
    CHECK(roto(std::vector<char>(10, 0)));
    auto noExiste = ModelFile::open(tmpPath("nada.wvm"), e);
    CHECK(!noExiste && !e.empty());
}

TEST(stftIdaYVuelta) {
    const int n = 256, hop = 64, len = 4096;
    StereoStft st(n, hop);
    std::mt19937 rng(3);
    std::normal_distribution<float> g(0.0f, 0.3f);
    std::vector<float> l(len), r(len);
    for (int i = 0; i < len; ++i) {
        l[static_cast<size_t>(i)] = g(rng);
        r[static_cast<size_t>(i)] = 0.5f * std::sin(0.01f * i) + 0.1f * g(rng);
    }
    const int frames = st.frames(len);
    CHECK(frames == 65);
    std::vector<float> spec(static_cast<size_t>(frames) * st.bins() * 4);
    st.forward(l.data(), r.data(), len, spec.data());
    // Un cuadro contra la DFT directa (con el relleno por reflejo y la ventana Hann periódica).
    const int t = 5;
    double peor = 0;
    for (int f : {0, 1, 17, 64, 128}) {
        double re[2] = {0, 0}, im[2] = {0, 0};
        for (int k = 0; k < n; ++k) {
            int i = t * hop - n / 2 + k;
            if (i < 0) i = -i;
            const double w = 0.5 - 0.5 * std::cos(2 * M_PI * k / n);
            const double a = -2 * M_PI * f * k / n;
            for (int c = 0; c < 2; ++c) {
                const double x = (c == 0 ? l : r)[static_cast<size_t>(i)] * w;
                re[c] += x * std::cos(a);
                im[c] += x * std::sin(a);
            }
        }
        const float *s = &spec[(static_cast<size_t>(t) * st.bins() + f) * 4];
        for (int c = 0; c < 2; ++c) {
            peor = std::max(peor, std::fabs(s[2 * c] - re[c]));
            peor = std::max(peor, std::fabs(s[2 * c + 1] - im[c]));
        }
    }
    CHECK(peor < 1e-4);
    // Ida y vuelta: vuelve la misma señal.
    std::vector<float> l2(len), r2(len);
    st.inverse(spec.data(), frames, l2.data(), r2.data());
    double err = 0;
    for (int i = 0; i < len; ++i)
        err = std::max({err, std::fabs(static_cast<double>(l2[static_cast<size_t>(i)] - l[static_cast<size_t>(i)])),
                        std::fabs(static_cast<double>(r2[static_cast<size_t>(i)] - r[static_cast<size_t>(i)]))});
    CHECK(err < 1e-5);
}

TEST(hilosReparten) {
    ThreadPool pool(3);
    CHECK(pool.size() == 3);
    std::vector<std::atomic<int>> veces(1000);
    for (auto &v : veces) v.store(0);
    std::atomic<int> malHilo{0};
    pool.parallelFor(1000, [&](int i, int w) {
        veces[static_cast<size_t>(i)].fetch_add(1);
        if (w < 0 || w >= 3) malHilo.fetch_add(1);
    });
    CHECK(std::all_of(veces.begin(), veces.end(), [](const std::atomic<int> &v) { return v.load() == 1; }));
    CHECK(malHilo.load() == 0);
    // Una tarea que falla: el error sale en quien llamó y el grupo sigue sirviendo.
    bool salio = false;
    try {
        pool.parallelFor(50, [&](int i, int) {
            if (i == 7) throw std::runtime_error("x");
        });
    } catch (const std::runtime_error &) {
        salio = true;
    }
    CHECK(salio);
    std::atomic<int> cuenta{0};
    std::atomic<bool> parar{true};
    pool.parallelFor(100, [&](int, int) { cuenta.fetch_add(1); }, &parar);
    CHECK(cuenta.load() == 0);
    pool.parallelFor(100, [&](int, int) { cuenta.fetch_add(1); });
    CHECK(cuenta.load() == 100);
}

TEST(separadorMini) {
    std::string e;
    auto mf = ModelFile::open(datos("mini.wvm"), e);
    auto ref = ModelFile::open(datos("mini-ref.wvm"), e);
    CHECK(mf && ref);
    if (!mf || !ref) return;
    auto m = MelRoformer::load(*mf, e);
    CHECK(m && e.empty());
    if (!m) return;
    CHECK(m->chunk() == 4096 && m->frames() == 65 && m->config().overlap == 2);
    ThreadPool pool(3);
    std::vector<float> in, out;
    CHECK(ref->floats("entrada", in) && ref->floats("salida", out));
    std::vector<float> l(4096), r(4096);
    MelRoformer::Inside ins;
    CHECK(m->run(in.data(), in.data() + 4096, l.data(), r.data(), pool, nullptr, &ins));
    std::vector<float> mia(l);
    mia.insert(mia.end(), r.begin(), r.end());
    CHECK(snrDb(out, mia) > 110);
    // Lo de adentro también es igual al original (bandas, cada transformer, máscaras).
    std::vector<float> x;
    CHECK(ref->floats("bandas", x) && snrDb(x, ins.bands) > 110);
    for (int i = 0; i < 2; ++i) {
        CHECK(ref->floats("capa" + std::to_string(i) + ".tiempo", x) && snrDb(x, ins.time[static_cast<size_t>(i)]) > 110);
        CHECK(ref->floats("capa" + std::to_string(i) + ".frecuencia", x) &&
              snrDb(x, ins.freq[static_cast<size_t>(i)]) > 110);
    }
    CHECK(ref->floats("mascaras", x) && snrDb(x, ins.masks) > 110);
    // Una segunda pasada da exactamente lo mismo (no queda nada de la anterior).
    std::vector<float> l2(4096), r2(4096);
    CHECK(m->run(in.data(), in.data() + 4096, l2.data(), r2.data(), pool));
    CHECK(l2 == l && r2 == r);

    // La canción entera, troceada como demix_track: con relleno, con el último trozo reflejado
    // y con uno solo rellenado con ceros.
    for (int n : {15000, 3000, 1500}) {
        std::vector<float> ent, sal;
        CHECK(ref->floats("demix" + std::to_string(n) + ".entrada", ent) &&
              ref->floats("demix" + std::to_string(n) + ".salida", sal));
        std::vector<float> gl, gr;
        Separator::Options opt;
        std::string err = Separator::run(*m, pool, n,
            [&](int64_t from, int k, float *a, float *b) {
                CHECK(from >= 0 && from + k <= n);
                std::copy(ent.begin() + from, ent.begin() + from + k, a);
                std::copy(ent.begin() + n + from, ent.begin() + n + from + k, b);
            },
            [&](const float *a, const float *b, int k) {
                gl.insert(gl.end(), a, a + k);
                gr.insert(gr.end(), b, b + k);
                return true;
            }, opt);
        CHECK(err.empty());
        gl.insert(gl.end(), gr.begin(), gr.end());
        CHECK(gl.size() == sal.size());
        CHECK(snrDb(sal, gl) > 110);
    }
    CHECK(Separator::chunkCount(15000, 4096, 2) == 10 && Separator::chunkCount(3000, 4096, 2) == 2);
    CHECK(Separator::chunkCount(1500, 4096, 2) == 1 && Separator::chunkCount(0, 4096, 2) == 0);
}

TEST(separadorSigueDondeQuedo) {
    std::string e;
    auto mf = ModelFile::open(datos("mini.wvm"), e);
    auto ref = ModelFile::open(datos("mini-ref.wvm"), e);
    auto m = mf ? MelRoformer::load(*mf, e) : nullptr;
    CHECK(m && ref);
    if (!m || !ref) return;
    std::vector<float> ent, sal;
    CHECK(ref->floats("demix15000.entrada", ent) && ref->floats("demix15000.salida", sal));
    const int n = 15000;
    ThreadPool pool(2);
    const std::string cache = tmpPath("trozos");
    mkdir(cache.c_str(), 0700);
    auto correr = [&](const std::string &clave, std::atomic<bool> *cancel, int pararEn, int &reusados,
                      std::vector<float> &salida) {
        salida.clear();
        std::vector<float> gr;
        Separator::Options opt;
        opt.cacheDir = cache;
        opt.cacheKey = clave;
        opt.cancel = cancel;
        opt.progress = [&](int hechos, int, int r) {
            reusados = r;
            if (cancel && hechos == pararEn) cancel->store(true);
        };
        std::string err = Separator::run(*m, pool, n,
            [&](int64_t from, int k, float *a, float *b) {
                std::copy(ent.begin() + from, ent.begin() + from + k, a);
                std::copy(ent.begin() + n + from, ent.begin() + n + from + k, b);
            },
            [&](const float *a, const float *b, int k) {
                salida.insert(salida.end(), a, a + k);
                gr.insert(gr.end(), b, b + k);
                return true;
            }, opt);
        salida.insert(salida.end(), gr.begin(), gr.end());
        return err;
    };
    std::atomic<bool> cancel{false};
    int reusados = -1;
    std::vector<float> salida;
    // Se cancela después del 4.º trozo: los 4 quedan guardados.
    CHECK(correr("cancion", &cancel, 4, reusados, salida) == "Cancelado");
    cancel.store(false);
    CHECK(correr("cancion", nullptr, 0, reusados, salida).empty());
    CHECK(reusados == 4);
    CHECK(snrDb(sal, salida) > 110);
    // Todo guardado: otra vez, sin calcular nada.
    CHECK(correr("cancion", nullptr, 0, reusados, salida).empty());
    CHECK(reusados == 10 && snrDb(sal, salida) > 110);
    // Otra entrada (otra clave): no se usan los trozos viejos.
    CHECK(correr("otra", nullptr, 0, reusados, salida).empty());
    CHECK(reusados == 0);
    // Si quien recibe no puede guardar, se avisa.
    Separator::Options opt;
    std::string err = Separator::run(*m, pool, n,
        [&](int64_t from, int k, float *a, float *b) {
            std::copy(ent.begin() + from, ent.begin() + from + k, a);
            std::copy(ent.begin() + n + from, ent.begin() + n + from + k, b);
        },
        [&](const float *, const float *, int) { return false; }, opt);
    CHECK(!err.empty() && err != "Cancelado");
}


// ---------------------------------------------------------------- tono y análisis de la canción
TEST(tonoYin) {
    PitchDetector d(48000, 2048, 70.0f, 1000.0f);
    std::vector<float> x(static_cast<size_t>(d.needed()));
    auto llenar = [&](const std::function<float(int)> &g) {
        for (int i = 0; i < d.needed(); ++i) x[static_cast<size_t>(i)] = g(i);
        return d.detect(x.data());
    };
    PitchDetector::Result r = llenar([](int i) { return 0.5f * std::sin(2.0f * float(M_PI) * 220.0f * i / 48000.0f); });
    CHECK_NEAR(r.hz, 220.0, 0.3);
    CHECK(r.clarity > 0.95f);
    // Diente de sierra (muchos armónicos): la fundamental, no una octava arriba ni abajo.
    r = llenar([](int i) {
        const double ph = 110.0 * i / 48000.0;
        return static_cast<float>(0.6 * (2.0 * (ph - std::floor(ph)) - 1.0));
    });
    CHECK_NEAR(r.hz, 110.0, 0.6);
    // Voz grave (Mi2, 82 Hz) con el segundo armónico más fuerte que la fundamental.
    r = llenar([](int i) {
        const double t = i / 48000.0;
        return static_cast<float>(0.15 * std::sin(2 * M_PI * 82.4 * t) + 0.4 * std::sin(2 * M_PI * 164.8 * t) +
                                  0.2 * std::sin(2 * M_PI * 247.2 * t) + 0.1 * std::sin(2 * M_PI * 329.6 * t));
    });
    CHECK_NEAR(r.hz, 82.4, 0.6);
    // Ruido y silencio: sin tono.
    uint32_t semilla = 1;
    r = llenar([&](int) {
        semilla = semilla * 1664525u + 1013904223u;
        return static_cast<float>(semilla >> 8) / 16777216.0f - 0.5f;
    });
    CHECK(r.hz == 0.0f);
    r = llenar([](int) { return 0.0f; });
    CHECK(r.hz == 0.0f);
    CHECK_NEAR(hzToMidi(440.0f), 69.0, 1e-5);
    CHECK_NEAR(midiToHz(57.0f), 220.0, 1e-3);
}

TEST(tonalidadPerfiles) {
    const float mayor[12] = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
    const float menor[12] = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};
    std::array<float, 12> perfil{};
    for (int p = 0; p < 12; ++p) perfil[static_cast<size_t>(p)] = mayor[((p - 7) % 12 + 12) % 12];  // Sol mayor
    int key = -1;
    bool minor = true;
    float score = 0;
    song::keyFromProfile(perfil, key, minor, score);
    CHECK(key == 7 && !minor && score > 0.99f);
    for (int p = 0; p < 12; ++p) perfil[static_cast<size_t>(p)] = menor[((p - 6) % 12 + 12) % 12];  // Fa# menor
    song::keyFromProfile(perfil, key, minor, score);
    CHECK(key == 6 && minor);
    perfil.fill(0.0f);
    song::keyFromProfile(perfil, key, minor, score);
    CHECK(key == -1);
    CHECK((song::scaleNotes(9, true) == std::array<int, 7>{9, 11, 0, 2, 4, 5, 7}));
    CHECK((song::scaleNotes(0, false) == std::array<int, 7>{0, 2, 4, 5, 7, 9, 11}));
}

// Una canción de prueba en La menor a 100 BPM: acordes, bajo, bombo en cada pulso y platillo en
// corcheas; la "voz" canta notas de la escala de 2 a 6 s y de 8 a 12 s.
static void cancionDePrueba(std::unique_ptr<MappedAudio> &inst, std::unique_ptr<MappedAudio> &voz, int segundos = 20) {
    const int sr = 48000;
    const int64_t n = static_cast<int64_t>(segundos) * sr;
    const int acordes[4][3] = {{57, 60, 64}, {62, 65, 69}, {52, 56, 59}, {57, 60, 64}};
    const int bajos[4] = {45, 50, 40, 45};
    const double pulso = 0.6;
    std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
    uint32_t semilla = 7;
    for (int64_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        const int a = static_cast<int>(t / (4 * pulso)) % 4;
        double v = 0;
        for (int k = 0; k < 3; ++k) {
            const double f = 440.0 * std::pow(2.0, (acordes[a][k] - 69) / 12.0);
            for (int h = 1; h <= 4; ++h) v += 0.08 / h * std::sin(2 * M_PI * f * h * t);
        }
        v += 0.2 * std::sin(2 * M_PI * 440.0 * std::pow(2.0, (bajos[a] - 69) / 12.0) * t);
        const double tb = std::fmod(t, pulso);
        v += 0.6 * std::sin(2 * M_PI * (50 + 100 * std::exp(-tb * 30)) * tb) * std::exp(-tb * 25);
        const double tc = std::fmod(t, pulso / 2);
        semilla = semilla * 1664525u + 1013904223u;
        const double ruido = static_cast<double>(semilla >> 8) / 16777216.0 - 0.5;
        l[static_cast<size_t>(i)] = static_cast<float>(v + 0.15 * ruido * std::exp(-tc * 80));
        r[static_cast<size_t>(i)] = static_cast<float>(v + 0.1 * ruido * std::exp(-tc * 80));
    }
    inst = genTrack("inst.wav", 2, n, [&](int64_t f, int c) { return c ? r[static_cast<size_t>(f)] : l[static_cast<size_t>(f)]; });
    const int notas[8] = {69, 72, 76, 74, 71, 68, 69, 76};
    voz = genTrack("voz.wav", 1, n, [&](int64_t f, int) {
        const double t = static_cast<double>(f) / sr;
        if (!((t >= 2 && t < 6) || (t >= 8 && t < 12))) return 0.0f;
        const int k = static_cast<int>(t / 0.5) % 8;
        const double hz = 440.0 * std::pow(2.0, (notas[k] - 69) / 12.0);
        double s = 0;  // con un poco de vibrato (modulando la fase)
        for (int h = 1; h <= 6; ++h) s += 0.3 / h * std::sin(2 * M_PI * hz * h * t + h * 0.3 * std::sin(2 * M_PI * 5.5 * t));
        const double dentro = std::fmod(t, 0.5);
        return static_cast<float>(s * std::min(1.0, dentro / 0.03) * std::min(1.0, (0.5 - dentro) / 0.03));
    });
}

TEST(analisisCancion) {
    std::unique_ptr<MappedAudio> inst, voz;
    cancionDePrueba(inst, voz);
    CHECK(inst && voz);
    if (!inst || !voz) return;
    SongInfo info;
    float ultimo = -1.0f;
    bool subio = true;
    CHECK(analyzeSong(inst.get(), voz.get(), info, nullptr, [&](float p) {
        if (p < ultimo - 1e-6f) subio = false;
        ultimo = p;
    }));
    CHECK(subio && ultimo == 1.0f);
    std::printf("  tonalidad %d %s (%.2f), %.1f BPM, %zu pulsos, %zu partes con voz\n", info.key,
                info.minor ? "menor" : "mayor", info.keyScore, info.bpm, info.beats.size(), info.vocalStart.size());
    CHECK(info.key == 9 && info.minor);  // La menor
    CHECK_NEAR(info.bpm, 100.0, 2.0);
    // Pulsos cada 0.6 s, sobre los golpes del bombo.
    CHECK(info.beats.size() >= 25);
    int fuera = 0;
    for (size_t i = 0; i < info.beats.size(); ++i) {
        const double resto = std::fmod(info.beats[i] + 0.3, 0.6) - 0.3;
        if (std::fabs(resto) > 0.04) ++fuera;
        if (i > 0 && std::fabs(info.beats[i] - info.beats[i - 1] - 0.6) > 0.05) ++fuera;
    }
    CHECK(fuera <= 2);
    // La voz: dos partes, 2–6 s y 8–12 s.
    CHECK(info.vocalStart.size() == 2);
    if (info.vocalStart.size() == 2) {
        CHECK_NEAR(info.vocalStart[0], 2.0, 0.2);
        CHECK_NEAR(info.vocalEnd[0], 6.0, 0.2);
        CHECK_NEAR(info.vocalStart[1], 8.0, 0.2);
        CHECK_NEAR(info.vocalEnd[1], 12.0, 0.2);
    }
    CHECK(info.envelope.size() == 200);
    CHECK(info.envelope[40] > 0.5f && info.envelope[70] < 0.05f);
    // Las notas de la voz: La es la más usada; Fa# (fuera de la escala) casi nada.
    CHECK(info.notes[9] == 1.0f && info.notes[6] < 0.05f);
    // Cancelar a la mitad.
    std::atomic<bool> cancel{false};
    SongInfo otra;
    CHECK(!analyzeSong(inst.get(), voz.get(), otra, &cancel, [&](float p) {
        if (p > 0.2f) cancel.store(true);
    }));
    // Sin voz o sin instrumental también funciona.
    SongInfo solo;
    CHECK(analyzeSong(inst.get(), nullptr, solo));
    CHECK(solo.vocalStart.empty() && solo.bpm > 90 && solo.key >= 0);
    CHECK(analyzeSong(nullptr, voz.get(), solo));
    CHECK(solo.bpm == 0.0f && solo.vocalStart.size() == 2 && solo.key == 9);
}


// ---------------------------------------------------------------- preparar la canción en el Studio
static Json leerSesion(const std::string &dir) {
    std::ifstream in(dir + "/sesion.json", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string e;
    return Json::parse(ss.str(), e);
}

TEST(estudioSepararVoz) {
    const std::string dir = nuevaCarpeta("separar");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    s.clearProject();
    // Una canción de 20 s en estéreo (con el modelo mini se separa rápido, aunque al azar).
    const std::string wav = tmpPath("larga.wav");
    {
        WavSpec spec;
        spec.format = 3;
        spec.bits = 32;
        makeWav(wav, spec, 48000 * 20, [](int64_t f, int c) {
            const double t = f / 48000.0;
            return 0.3 * std::sin(2 * M_PI * (c ? 330.0 : 220.0) * t) + 0.2 * std::sin(2 * M_PI * 3.1 * t) * std::sin(2 * M_PI * 880.0 * t);
        });
    }
    CHECK(s.importFiles({abrir(wav)}, {"Paraíso.wav"}) && s.waitIdle(60000));
    CHECK(s.trackCount() == 1);
    const int id = idDe(s, 0);

    // Sin modelo no se puede.
    CHECK(!s.hasVoiceModel());
    CHECK(!s.separate(id, Studio::kSepHigh));
    CHECK(estado(s)["mensaje"].asString().find("modelo") != std::string::npos);
    CHECK(!s.setVoiceModel(tmpPath("no-existe.wvm")) && !s.hasVoiceModel());
    CHECK(s.setVoiceModel(datos("mini.wvm")) && s.hasVoiceModel());
    int32_t guardados[2];
    s.separationSaved(id, Studio::kSepHigh, guardados);
    CHECK(guardados[0] == 0 && guardados[1] > 100);  // 20 s a 16 kHz en trozos de 4096 con traslape 2
    const int total = guardados[1];
    CHECK(!s.separate(999, Studio::kSepHigh));

    // Se cancela a la mitad: lo calculado queda guardado.
    CHECK(s.separate(id, Studio::kSepHigh));
    CHECK(!s.separate(id, Studio::kSepHigh));  // ya hay un trabajo
    float st[Studio::kSepStateCount];
    const auto t0 = std::chrono::steady_clock::now();
    do {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        s.separationState(st);
    } while (st[2] < 5 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60));
    CHECK(st[0] == 2.0f && st[3] == static_cast<float>(total) && st[5] == static_cast<float>(id));
    CHECK(st[1] > 0.0f && st[1] < 1.0f);
    s.cancelJob();
    CHECK(s.waitIdle(60000));
    s.separationState(st);
    CHECK(st[0] == 0.0f);
    CHECK(s.trackCount() == 1);
    CHECK(estado(s)["mensaje"].asString().find("Cancelado") != std::string::npos);
    s.separationSaved(id, Studio::kSepHigh, guardados);
    CHECK(guardados[0] >= 5 && guardados[0] < total && guardados[1] == total);
    int32_t otra[2];
    s.separationSaved(id, Studio::kSepMax, otra);  // otra calidad: nada guardado, el doble de trozos
    CHECK(otra[0] == 0 && otra[1] > total + total / 2);

    // Ahora completa (sigue donde quedó).
    CHECK(s.separate(id, Studio::kSepHigh));
    CHECK(s.waitIdle(300000));
    s.separationState(st);
    CHECK(st[0] == 0.0f);
    std::printf("  %s\n", estado(s)["mensaje"].asString().c_str());
    Json ps = pistas(s);
    CHECK(ps.size() == 3);
    CHECK(ps[1]["nombre"].asString() == "Paraíso · instrumental" && ps[2]["nombre"].asString() == "Paraíso · voz original");
    const int instId = static_cast<int>(ps[1]["id"].asInt()), vozId = static_cast<int>(ps[2]["id"].asInt());
    float par[Studio::kTrackParamCount];
    CHECK(s.trackParams(id, par) && par[2] == 1.0f);       // la original, en silencio
    CHECK(s.trackParams(vozId, par) && par[2] == 1.0f);    // la voz guía, en silencio
    CHECK(s.trackParams(instId, par) && par[2] == 0.0f);   // el instrumental suena
    CHECK(ps[1]["canales"].asInt() == 2 && ps[1]["cuadros"].asInt() == 48000 * 20);
    s.separationSaved(id, Studio::kSepHigh, guardados);
    CHECK(guardados[0] == 0);  // los trozos guardados ya se borraron

    // Instrumental + voz = la original (el instrumental es lo que queda sin la voz).
    CHECK(s.saveNow());
    Json ses = leerSesion(dir);
    std::string fOrig, fInst, fVoz;
    for (size_t i = 0; i < ses["pistas"].size(); ++i) {
        const Json &t = ses["pistas"][i];
        const int tid = static_cast<int>(t["id"].asInt());
        if (tid == id) fOrig = t["archivo"].asString();
        if (tid == instId) fInst = t["archivo"].asString();
        if (tid == vozId) fVoz = t["archivo"].asString();
    }
    std::string e;
    auto ao = MappedAudio::open(dir + "/audio/" + fOrig, e), ai = MappedAudio::open(dir + "/audio/" + fInst, e),
         av = MappedAudio::open(dir + "/audio/" + fVoz, e);
    CHECK(ao && ai && av);
    if (ao && ai && av) {
        CHECK(ai->frames() == ao->frames() && av->frames() == ao->frames() && av->channels() == 2);
        double peor = 0, voz = 0;
        for (int64_t k = 0; k < ao->frames() * 2; ++k) {
            peor = std::max(peor, std::fabs(static_cast<double>(ai->data()[k]) + av->data()[k] - ao->data()[k]));
            voz = std::max(voz, std::fabs(static_cast<double>(av->data()[k])));
        }
        CHECK(peor < 1e-5);
        CHECK(voz > 1e-4 && std::isfinite(voz));
    }

    // La canción preparada (y su análisis) queda en la sesión.
    std::string je;
    Json c = Json::parse(s.songJson(), je);
    CHECK(c["lista"].asBool() && c["fuente"].asInt() == id && c["instrumental"].asInt() == instId &&
          c["voz"].asInt() == vozId);
    CHECK(c["notas"].size() == 12);
    CHECK(s.songEnvelope(50).size() == 50);
    {
        Studio s2;
        s2.open(dir, dir);
        CHECK(s2.waitIdle(30000));
        Json c2 = Json::parse(s2.songJson(), je);
        CHECK(c2["lista"].asBool() && c2["instrumental"].asInt() == instId && c2["voz"].asInt() == vozId);
        CHECK(c2["tonica"].asInt() == c["tonica"].asInt() && std::fabs(c2["bpm"].asDouble() - c["bpm"].asDouble()) < 0.02);
        CHECK(c2["partes"].size() == c["partes"].size());
    }
    // La voz en vivo afina en la escala de la canción (la elegida por omisión).
    float vm[VoiceFx::kMeterCount];
    s.voiceMeters(vm);
    if (c["tonica"].asInt() >= 0) {
        CHECK(vm[7] == static_cast<float>(c["tonica"].asInt()));
        CHECK(vm[8] == static_cast<float>(c["menor"].asBool() ? kScaleMinor : kScaleMajor));
    } else {
        CHECK(vm[8] == static_cast<float>(kScaleChromatic));
    }
    CHECK(vm[9] == 1.0f);
    // Quitar la voz guía no deshace la canción; quitar el instrumental, sí.
    CHECK(s.removeTrack(vozId));
    c = Json::parse(s.songJson(), je);
    CHECK(c["lista"].asBool() && c["voz"].asInt() == 0);
    CHECK(s.removeTrack(instId));
    s.voiceMeters(vm);
    CHECK(vm[8] == static_cast<float>(kScaleChromatic));  // sin canción: todas las notas
    c = Json::parse(s.songJson(), je);
    CHECK(!c["lista"].asBool());

    // Sin lugar para dos pistas más, no empieza.
    s.clearProject();
    CHECK(s.loadDemo() && s.waitIdle(30000));
    for (int k = 0; k < 2; ++k) CHECK(s.loadDemo() && s.waitIdle(30000));
    CHECK(s.trackCount() == 12);
    CHECK(!s.separate(idDe(s, 0), Studio::kSepHigh));
    CHECK(estado(s)["mensaje"].asString().find("lugares") != std::string::npos);
}

// ---------------------------------------------------------------- main

// ---------------------------------------------------------------- voz en vivo (auto-tune y efectos)

// Voz sintética: pulsos glotales (diente de sierra suavizado) por tres formantes de "a", con un
// poco de aire. `hz` es la nota en cada muestra (0 = silencio).
static std::vector<float> vozSintetica(const std::vector<double> &hz, double nivel = 0.3, double aire = 0.01,
                                       unsigned semilla = 1) {
    struct Formante {
        double f, bw, z1, z2;
    };
    Formante fs[3] = {{700, 130, 0, 0}, {1220, 70, 0, 0}, {2600, 160, 0, 0}};
    std::mt19937 rng(semilla);
    std::normal_distribution<double> ruido(0.0, 1.0);
    std::vector<float> out(hz.size());
    double fase = 0.0, lp = 0.0;
    for (size_t i = 0; i < hz.size(); ++i) {
        double s = 0.0;
        if (hz[i] > 0) {
            fase += hz[i] / 48000.0;
            if (fase >= 1.0) fase -= 1.0;
            s = 2.0 * fase - 1.0;
        }
        lp += 0.2 * (s - lp);
        const double x = lp + (hz[i] > 0 ? aire * ruido(rng) : 0.0);
        double y = 0.0;
        for (Formante &f : fs) {
            const double r = std::exp(-M_PI * f.bw / 48000.0), th = 2 * M_PI * f.f / 48000.0;
            const double v = x + 2 * r * std::cos(th) * f.z1 - r * r * f.z2;
            f.z2 = f.z1;
            f.z1 = v;
            y += v * (1 - r) * 0.5;
        }
        out[i] = static_cast<float>(y);
    }
    float pk = 0.0f;
    for (float v : out) pk = std::max(pk, std::fabs(v));
    if (pk > 0.0f)
        for (float &v : out) v = static_cast<float>(v / pk * nivel);
    return out;
}

// La nota medida fuera de tiempo real (YIN a 48 kHz, ventana de 25 ms) cada `paso` muestras.
static std::vector<float> tonosDe(const std::vector<float> &x, size_t desde, size_t hasta, int paso = 480) {
    PitchDetector det(48000, 1200, 60.0f, 1000.0f);
    std::vector<float> r;
    for (size_t i = desde; i + det.needed() < hasta; i += paso) r.push_back(det.detect(x.data() + i, 0.15f).hz);
    return r;
}

static double centsEntre(double a, double b) { return 1200.0 * std::log2(a / b); }

static double peorCents(const std::vector<float> &tonos, double hz) {
    double peor = 0.0;
    for (float h : tonos) peor = std::max(peor, h > 0.0f ? std::fabs(centsEntre(h, hz)) : 1200.0);
    return peor;
}

// Máxima segunda diferencia desde `desde` (un clic la dispara).
static double saltoMaximo(const std::vector<float> &x, size_t desde) {
    double d = 0.0;
    for (size_t i = desde; i + 2 < x.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(x[i + 2] - 2 * x[i + 1] + x[i])));
    return d;
}

TEST(seguidorDeTono) {
    // Notas fijas en todo el rango de una voz: la estimación queda a pocos cents.
    for (double f : {90.0, 150.0, 220.0, 330.0, 440.0, 660.0}) {
        const std::vector<float> x = vozSintetica(std::vector<double>(48000, f));
        PitchTracker t(48000);
        double peor = 0.0;
        int con = 0, sin = 0;
        for (int i = 0; i < 48000; i += 64) {
            t.process(x.data() + i, 64);
            if (i < 12000) continue;
            if (!t.voiced()) {
                ++sin;
                continue;
            }
            peor = std::max(peor, std::fabs(centsEntre(t.hz(), f)));
            ++con;
        }
        CHECK(sin == 0 && con > 500);
        CHECK(peor < 8.0);
        CHECK_NEAR(t.period(), 48000.0 / f, 48000.0 / f * 0.005);
    }
    // Después de silencio prende rápido y sigue un cambio de nota.
    std::vector<double> hz(96000, 0.0);
    for (int i = 24000; i < 48000; ++i) hz[i] = 220.0;
    for (int i = 48000; i < 96000; ++i) hz[i] = 246.94;
    const std::vector<float> x = vozSintetica(hz);
    PitchTracker t(48000);
    int prende = -1, cambia = -1;
    bool silencioSinNota = true;
    for (int i = 0; i < 96000; i += 32) {
        t.process(x.data() + i, 32);
        if (i < 24000 && t.voiced()) silencioSinNota = false;
        if (prende < 0 && i >= 24000 && t.voiced()) prende = i + 32 - 24000;
        if (cambia < 0 && i >= 48000 && t.voiced() && std::fabs(centsEntre(t.hz(), 246.94)) < 20) cambia = i + 32 - 48000;
    }
    CHECK(silencioSinNota);
    CHECK(prende > 0 && prende < 48000 * 30 / 1000);
    CHECK(cambia > 0 && cambia < 48000 * 25 / 1000);
    // Con aire, ruido y temblor no confunde la octava ni pierde la nota.
    {
        std::vector<double> h(4 * 48000);
        std::mt19937 g(7);
        std::normal_distribution<double> n(0, 1);
        double j = 0.0;
        for (size_t i = 0; i < h.size(); ++i) {
            j = 0.999 * j + 0.001 * n(g);
            h[i] = 180.0 * std::exp2(40.0 * j / 1200.0) * (1 + 0.003 * n(g));
        }
        std::vector<float> v = vozSintetica(h, 0.3, 0.03);
        std::normal_distribution<double> rn(0, 0.03);
        for (float &s : v) s += static_cast<float>(rn(g));
        PitchTracker tt(48000);
        int octavas = 0, total = 0, sinNota = 0;
        for (int i = 0; i + 64 <= static_cast<int>(v.size()); i += 64) {
            tt.process(v.data() + i, 64);
            if (i < 24000) continue;
            ++total;
            if (!tt.voiced()) ++sinNota;
            else if (std::fabs(centsEntre(tt.hz(), h[static_cast<size_t>(i)])) > 600) ++octavas;
        }
        CHECK(octavas == 0);
        CHECK(sinNota < total / 50);
    }
    // El rango cambia sin reservar memoria y se respeta: una nota de 90 Hz con "aguda" (130 Hz
    // o más) no se toma como tal.
    {
        const std::vector<float> v = vozSintetica(std::vector<double>(48000, 90.0));
        PitchTracker tt(48000);
        tt.setRange(130.0f, 1100.0f);
        bool noventa = false;
        for (int i = 0; i < 48000; i += 64) {
            tt.process(v.data() + i, 64);
            if (tt.voiced() && std::fabs(centsEntre(tt.hz(), 90.0)) < 50) noventa = true;
        }
        CHECK(!noventa);
    }
}

TEST(cambiadorDeTono) {
    // Una voz de 200 Hz corrida ±50 y ±100 cents: sale la nota pedida, sin clics, y con un retraso
    // de entre ~1 ms y un período.
    const double f = 200.0;
    const std::vector<float> x = vozSintetica(std::vector<double>(96000, f));
    const double clicEntrada = saltoMaximo(x, 24000);
    for (double c : {50.0, -50.0, 100.0, -100.0}) {
        PitchShifter sh(48000);
        std::vector<float> y(x.size());
        const float ratio = static_cast<float>(std::exp2(c / 1200.0));
        float dmin = 1e9f, dmax = 0.0f;
        for (size_t i = 0; i < x.size(); i += 64) {
            sh.process(x.data() + i, y.data() + i, 64, ratio, 240.0f, false);
            if (i > 4800) {
                dmin = std::min(dmin, sh.delay());
                dmax = std::max(dmax, sh.delay());
            }
        }
        CHECK(peorCents(tonosDe(y, 24000, y.size()), f * ratio) < 2.0);
        CHECK(saltoMaximo(y, 24000) < 1.5 * clicEntrada);
        CHECK(dmin >= 20.0f && dmax <= PitchShifter::kMinDelay + 240 + 40);
    }
    // Sin corregir (1:1) la salida es la entrada atrasada, igualita.
    {
        PitchShifter sh(48000);
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); i += 64) sh.process(x.data() + i, y.data() + i, 64, 1.0f, 240.0f, false);
        const int d = static_cast<int>(std::lround(sh.delay()));
        CHECK(d > PitchShifter::kMinDelay && d < PitchShifter::kMinDelay + 32);
        double err = 0.0;
        for (size_t i = 1000; i < x.size(); ++i) err = std::max(err, static_cast<double>(std::fabs(y[i] - x[i - d])));
        CHECK(err < 1e-6);
    }
    // Al bajar el tono el retraso crece; en silencio vuelve al mínimo.
    {
        PitchShifter sh(48000);
        std::vector<float> y(x.size());
        const float abajo = static_cast<float>(std::exp2(-100.0 / 1200.0));
        float dmax = 0.0f;
        for (size_t i = 0; i < 48000; i += 64) {
            sh.process(x.data() + i, y.data() + i, 64, abajo, 240.0f, false);
            dmax = std::max(dmax, sh.delay());
        }
        CHECK(dmax > PitchShifter::kMinDelay + 120);
        std::vector<float> ceros(4800, 0.0f), salida(4800);
        for (int i = 0; i < 4800; i += 64) sh.process(ceros.data() + i, salida.data() + i, 64, 1.0f, 0.0f, true);
        CHECK(sh.delay() <= PitchShifter::kMinDelay + 24);
        CHECK(picoDe(salida, 2400) < 1e-6f);
    }
}

TEST(autoTuneEscala) {
    // La menor: La Si Do Re Mi Fa Sol.
    CHECK(AutoTune::inScale(69, 9, kScaleMinor) && AutoTune::inScale(72, 9, kScaleMinor));
    CHECK(!AutoTune::inScale(70, 9, kScaleMinor) && !AutoTune::inScale(73, 9, kScaleMinor));
    CHECK(AutoTune::inScale(61, 0, kScaleChromatic));
    CHECK(AutoTune::nearestNote(69.3f, 9, kScaleMinor, -1) == 69);
    CHECK(AutoTune::nearestNote(70.4f, 9, kScaleMinor, -1) == 71);  // La♯ no está: Si
    CHECK(AutoTune::nearestNote(69.6f, 9, kScaleMinor, -1) == 69);  // ... o La, la más cerca
    // Entre Mi (64) y Fa (65): con la ventaja de la actual no salta de una a otra.
    CHECK(AutoTune::nearestNote(64.55f, 9, kScaleMinor, 64) == 64);
    CHECK(AutoTune::nearestNote(64.55f, 9, kScaleMinor, -1) == 65);
    CHECK(AutoTune::nearestNote(64.8f, 9, kScaleMinor, 64) == 65);
    // Do mayor y cromática.
    CHECK(AutoTune::nearestNote(60.8f, 0, kScaleMajor, -1) == 60);
    CHECK(AutoTune::nearestNote(60.8f, 0, kScaleChromatic, -1) == 61);
    CHECK(AutoTune::nearestNote(61.2f, 0, kScaleMajor, -1) == 62);
}

TEST(autoTuneCorrige) {
    const double desafinado = 440.0 * std::exp2(30.0 / 1200.0);  // La + 30 cents
    const std::vector<float> x = vozSintetica(std::vector<double>(96000, desafinado));
    for (float velocidad : {0.0f, 20.0f}) {
        AutoTune at(48000);
        TuneSettings s;
        s.key = 9;
        s.scale = kScaleMinor;
        s.speedMs = velocidad;
        std::vector<float> y = x;
        for (size_t i = 0; i < y.size(); i += 64) at.process(y.data() + i, 64, s, false);
        CHECK(peorCents(tonosDe(y, 24000, y.size()), 440.0) < 5.0);
        const AutoTune::Info info = at.info();
        CHECK(info.note == 69);
        CHECK_NEAR(info.inputMidi, 69.3, 0.06);
        CHECK_NEAR(info.outputMidi, 69.0, 0.05);
        CHECK(at.latencySamples() < 48000 * 0.008f);  // menos de 8 ms
    }
    // Solo afinador: sale igual que entra, pero dice la nota.
    {
        AutoTune at(48000);
        TuneSettings s;
        s.correct = false;
        std::vector<float> y = x;
        for (size_t i = 0; i < y.size(); i += 64) at.process(y.data() + i, 64, s, false);
        CHECK(peorCents(tonosDe(y, 24000, y.size()), desafinado) < 3.0);
        CHECK(at.info().note == 69);
    }
    // Vibrato de ±40 cents alrededor de Mi: al instante casi lo borra; con 60 ms pasa.
    std::vector<double> hz(3 * 48000);
    for (size_t i = 0; i < hz.size(); ++i) hz[i] = 329.63 * std::exp2(40.0 / 1200.0 * std::sin(2 * M_PI * 5.5 * i / 48000.0));
    const std::vector<float> v = vozSintetica(hz);
    for (float velocidad : {0.0f, 60.0f}) {
        AutoTune at(48000);
        TuneSettings s;
        s.key = 9;
        s.scale = kScaleMinor;
        s.speedMs = velocidad;
        std::vector<float> y = v;
        for (size_t i = 0; i < y.size(); i += 64) at.process(y.data() + i, 64, s, false);
        const double sale = peorCents(tonosDe(y, 48000, y.size(), 240), 329.63);
        if (velocidad == 0.0f) CHECK(sale < 16.0);
        else CHECK(sale > 30.0);
    }
    // En silencio no hay nota ni corrección.
    {
        AutoTune at(48000);
        TuneSettings s;
        std::vector<float> z(24000, 0.0f);
        for (size_t i = 0; i < z.size(); i += 64) at.process(z.data() + i, 64, s, true);
        CHECK(at.info().note == -1 && at.info().inputMidi == 0.0f);
        CHECK(picoDe(z) == 0.0f);
    }
}

TEST(compuertaYDeEsser) {
    NoiseGate g;
    g.setup(48000);
    GateSettings gs;  // abre arriba de -50 dB y baja 30 dB
    // Ruido de fondo de ~-60 dB: se cierra y lo baja casi 30 dB.
    std::vector<float> ruido(96000);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-0.001f, 0.001f);
    for (float &v : ruido) v = u(rng);
    std::vector<float> y = ruido;
    g.process(y.data(), 96000, gs);
    CHECK(!g.open());
    CHECK(rmsDe(y, 72000, 96000) < rmsDe(ruido, 72000, 96000) * 0.04);
    // La voz la abre en menos de 1 ms y pasa entera.
    std::vector<float> v = senoMono(440, 0, 4800, 0.3);
    g.process(v.data(), 4800, gs);
    CHECK(g.open());
    CHECK_NEAR(picoDe(v, 96), 0.3, 0.01);
    // Sostiene un momento después de que se calla, y luego cierra.
    std::vector<float> z(48000, 0.0f);
    g.process(z.data(), 2400, gs);  // 50 ms
    CHECK(g.open());
    g.process(z.data(), 48000, gs);
    CHECK(!g.open());
    CHECK(g.gainDb() < -25.0f);

    DeEsser d;
    d.setup(48000);
    DeEssSettings ds;
    // Lo grave no se toca.
    const std::vector<float> grave = senoMono(400, 0, 48000, 0.5);
    std::vector<float> g2 = grave;
    CHECK(d.process(g2.data(), 48000, ds) < 0.5f);
    double err = 0.0;
    for (size_t i = 0; i < g2.size(); ++i) err = std::max(err, static_cast<double>(std::fabs(g2[i] - grave[i])));
    CHECK(err < 1e-4);
    // Una "s" fuerte (7 kHz) baja varios dB.
    const std::vector<float> ese = senoMono(7000, 0, 48000, 0.3);
    std::vector<float> e2 = ese;
    CHECK(d.process(e2.data(), 48000, ds) > 4.0f);
    CHECK(rmsDe(e2, 24000, 48000) < rmsDe(ese, 24000, 48000) * 0.7);
}

// Pasa una voz mono por la cadena en bloques de 256.
static void porLaCadena(VoiceFx &v, const std::vector<float> &x, std::vector<float> &l, std::vector<float> &r) {
    l.assign(x.size(), 0.0f);
    r.assign(x.size(), 0.0f);
    float mono[VoiceFx::kMaxBlock];
    for (size_t i = 0; i < x.size(); i += VoiceFx::kMaxBlock) {
        const int n = static_cast<int>(std::min<size_t>(VoiceFx::kMaxBlock, x.size() - i));
        std::copy(x.begin() + static_cast<long>(i), x.begin() + static_cast<long>(i) + n, mono);
        v.processPre(mono, n);
        v.processPost(mono, n, l.data() + i, r.data() + i);
    }
}

static void ponerPreset(VoiceFx &v, int i) {
    for (int p = 0; p < vfx::kCount; ++p) v.set(p, vfx::preset(i).values[static_cast<size_t>(p)]);
}

static int presetLlamado(const char *nombre) {
    for (int i = 0; i < vfx::presetCount(); ++i)
        if (std::string(vfx::preset(i).name) == nombre) return i;
    return -1;
}

TEST(vozEnVivoCadena) {
    // Ajustes: rango y paso; los presets prenden la voz.
    CHECK(vfx::clampValue(vfx::kTuneSpeed, -5.0f) == 0.0f);
    CHECK(vfx::clampValue(vfx::kTuneKey, 13.0f) == 11.0f);
    CHECK(vfx::clampValue(vfx::kOn, 0.7f) == 1.0f);
    CHECK(vfx::clampValue(vfx::kTuneAmount, 0.333f) == 0.33f);
    CHECK(vfx::presetCount() >= 5);
    for (int i = 0; i < vfx::presetCount(); ++i) CHECK(vfx::preset(i).values[vfx::kOn] == 1.0f);
    CHECK(presetLlamado("Paraíso") == 0 && presetLlamado("Solo afinar") > 0 && presetLlamado("Sin efectos") > 0);

    VoiceFx v(48000);
    CHECK(!v.on());
    float m[VoiceFx::kMeterCount];
    // La escala de la canción: La menor; sin canción, todas las notas.
    v.setSongKey(9, true);
    v.meters(m);
    CHECK(m[7] == 9.0f && m[8] == static_cast<float>(kScaleMinor) && m[9] == 1.0f);
    v.setSongKey(-1, false);
    v.meters(m);
    CHECK(m[7] == -1.0f && m[8] == static_cast<float>(kScaleChromatic) && m[9] == 1.0f);
    // Escala elegida a mano.
    v.set(vfx::kTuneScale, vfx::kScaleMajorChoice);
    v.set(vfx::kTuneKey, 7);
    v.meters(m);
    CHECK(m[7] == 7.0f && m[8] == static_cast<float>(kScaleMajor) && m[9] == 0.0f);
    v.set(vfx::kTuneScale, vfx::kScaleSong);
    v.setSongKey(9, true);

    // "Solo afinar": La + 30 cents sale en La, igual en los dos lados.
    const std::vector<float> x = vozSintetica(std::vector<double>(96000, 440.0 * std::exp2(30.0 / 1200.0)));
    std::vector<float> l, r;
    ponerPreset(v, presetLlamado("Solo afinar"));
    v.restart();
    porLaCadena(v, x, l, r);
    CHECK(peorCents(tonosDe(l, 24000, l.size()), 440.0) < 5.0);
    CHECK(l == r);
    v.meters(m);
    CHECK(m[2] == 69.0f && m[3] == 1.0f);
    CHECK(m[6] >= 0.0f && m[6] < 8.0f);

    // "Paraíso": estéreo (doblaje, eco de ida y vuelta), finito, con el compresor trabajando.
    ponerPreset(v, presetLlamado("Paraíso"));
    v.setTempo(100.0f);
    v.restart();
    porLaCadena(v, x, l, r);
    bool finito = true;
    double dif = 0.0;
    for (size_t i = 0; i < l.size(); ++i) {
        finito = finito && std::isfinite(l[i]) && std::isfinite(r[i]);
        dif = std::max(dif, static_cast<double>(std::fabs(l[i] - r[i])));
    }
    CHECK(finito);
    CHECK(dif > 1e-3);
    v.meters(m);
    CHECK(m[5] > 0.5f);
    CHECK(m[4] >= 0.0f);

    // El eco va a tiempo con la canción: 1/4 a 100 BPM = 600 ms después del golpe.
    ponerPreset(v, presetLlamado("Sin efectos"));
    v.set(vfx::kDelayOn, 1);
    v.set(vfx::kDelayBeats, 0);
    v.set(vfx::kDelayFeedback, 0);
    v.set(vfx::kDelayMix, 1);
    v.setTempo(100.0f);
    v.restart();
    std::vector<float> golpe(48000, 0.0f);
    for (int i = 4800; i < 4808; ++i) golpe[static_cast<size_t>(i)] = 0.8f;
    porLaCadena(v, golpe, l, r);
    auto picoEn = [](const std::vector<float> &s, size_t a, size_t b) {
        size_t k = a;
        for (size_t i = a; i < b; ++i)
            if (std::fabs(s[i]) > std::fabs(s[k])) k = i;
        return static_cast<long>(k);
    };
    const long seco = picoEn(l, 0, 20000), eco = picoEn(l, 20000, 48000);
    CHECK_NEAR(static_cast<double>(eco - seco), 28800.0, 4.0);
    // Apagada, la entrada en vivo no la usa (no cambia nada aquí, pero dice que está apagada).
    v.set(vfx::kOn, 0);
    CHECK(!v.on());
}

TEST(entradaConVozEnVivo) {
    // La voz en vivo encendida: lo que se escucha sale procesado (y por el master); el retraso
    // del auto-tune es chico.
    Mixer m(48000);
    LiveInput in(48000, m);
    m.setLiveInput(&in);
    ponerPreset(in.voice(), presetLlamado("Solo afinar"));
    in.voice().setSongKey(9, true);
    in.setActive(true, 1);
    in.setMonitor(true);
    const std::vector<float> x = vozSintetica(std::vector<double>(96000, 440.0 * std::exp2(30.0 / 1200.0)));
    std::vector<float> out(192 * 2), salida;
    for (size_t k = 0; k + 192 <= x.size(); k += 192) {
        in.push(x.data() + k, 192, 1);
        m.process(out.data(), 192, 2);
        for (int i = 0; i < 192; ++i) salida.push_back(out[static_cast<size_t>(2 * i)]);
    }
    CHECK(peorCents(tonosDe(salida, 24000, salida.size()), 440.0) < 5.0);
    float met[VoiceFx::kMeterCount];
    in.voice().meters(met);
    CHECK(met[2] == 69.0f);
    m.setLiveInput(nullptr);
}

TEST(estudioVozEnVivo) {
    const std::string dir = nuevaCarpeta("voz-en-vivo");
    Json ses;
    {
        Studio s;
        s.open(dir, dir);
        CHECK(s.waitIdle(30000));
        s.clearProject();
        CHECK(Studio::voiceFxInfo().size() == static_cast<size_t>(vfx::kCount) * 4);
        std::string e;
        const Json nombres = Json::parse(Studio::voicePresetsJson(), e);
        CHECK(nombres.size() == static_cast<size_t>(vfx::presetCount()) && nombres[0].asString() == "Paraíso");
        CHECK(s.voiceFxState()[vfx::kOn] == 0.0f);  // apagada al principio
        int32_t v0[Studio::kVersionCount], v1[Studio::kVersionCount];
        s.versions(v0);
        s.setVoiceFx(vfx::kOn, 1);
        s.versions(v1);
        CHECK(v1[Studio::kVerInput] != v0[Studio::kVerInput]);
        s.setVoiceFx(vfx::kTuneKey, 4);
        s.setVoiceFx(vfx::kTuneScale, vfx::kScaleMajorChoice);
        s.setVoiceFx(vfx::kLevel, -3);
        s.setVoiceFx(vfx::kTuneSpeed, 999);  // fuera de rango
        s.setVoiceFx(-1, 1);
        s.setVoiceFx(vfx::kCount, 1);
        std::vector<float> st = s.voiceFxState();
        CHECK(st.size() == static_cast<size_t>(vfx::kCount));
        CHECK(st[vfx::kOn] == 1.0f && st[vfx::kTuneKey] == 4.0f && st[vfx::kTuneSpeed] == 400.0f);
        // Un preset no toca la escala, la tónica, el registro ni el volumen.
        int trap = -1;
        for (int i = 0; i < vfx::presetCount(); ++i)
            if (std::string(vfx::preset(i).name) == "Trap (robot)") trap = i;
        CHECK(trap >= 0 && s.applyVoicePreset(trap));
        CHECK(!s.applyVoicePreset(99));
        st = s.voiceFxState();
        CHECK(st[vfx::kTuneSpeed] == 0.0f && st[vfx::kDelayOn] == 1.0f);
        CHECK(st[vfx::kTuneKey] == 4.0f && st[vfx::kTuneScale] == vfx::kScaleMajorChoice && st[vfx::kLevel] == -3.0f);
        float m[VoiceFx::kMeterCount];
        s.voiceMeters(m);
        CHECK(m[7] == 4.0f && m[8] == static_cast<float>(kScaleMajor) && m[9] == 0.0f);  // Mi mayor, a mano
        CHECK(s.saveNow());
        ses = leerSesion(dir);
    }
    CHECK(ses["entrada"]["voz"]["encendida"].asDouble() == 1.0);
    CHECK(ses["entrada"]["voz"]["tonica"].asDouble() == 4.0);
    // Al volver a abrir, todo como estaba (y el núcleo de audio lo usa).
    Studio s2;
    s2.open(dir, dir);
    CHECK(s2.waitIdle(30000));
    const std::vector<float> st = s2.voiceFxState();
    CHECK(st[vfx::kOn] == 1.0f && st[vfx::kTuneSpeed] == 0.0f && st[vfx::kLevel] == -3.0f);
    float m[VoiceFx::kMeterCount];
    s2.voiceMeters(m);
    CHECK(m[7] == 4.0f && m[8] == static_cast<float>(kScaleMajor));
}

// ---------------------------------------------------------------- grabar la presentación

TEST(grabadorDePresentacion) {
    // Lo que sale del mezclador queda igualito en el archivo (con la voz en vivo incluida).
    Mixer m(48000);
    m.setTrack(0, genTrack("show1.wav", 2, 48000 * 3, [](int64_t f, int c) { return sine(c ? 330 : 220, 48000, f, 0.3); }));
    LiveInput in(48000, m);
    m.setLiveInput(&in);
    in.setActive(true, 1);
    in.setMonitor(true);
    ShowRecorder rec(48000);
    m.setShowRecorder(&rec);
    std::string e;
    const std::string path = tmpPath("presentacion.wav");
    CHECK(rec.start(path, e) && rec.recording());
    CHECK(!rec.start(path, e));  // ya está grabando
    m.play();
    std::vector<float> out(192 * 2), todo;
    for (int k = 0; k < 250; ++k) {  // 1 s
        const std::vector<float> x = senoMono(440, k * 192, 192, 0.2);
        in.push(x.data(), 192, 1);
        m.process(out.data(), 192, 2);
        todo.insert(todo.end(), out.begin(), out.end());
    }
    const ShowRecorder::Result r = rec.stop();
    CHECK(r.error.empty() && r.frames == 250 * 192 && r.dropped == 0 && r.path == path);
    CHECK(!rec.recording());
    auto a = MappedAudio::open(path, e);
    CHECK(a && a->channels() == 2 && a->frames() == 250 * 192);
    if (a) {
        double err = 0.0;
        for (size_t i = 0; i < todo.size(); ++i) err = std::max(err, static_cast<double>(std::fabs(a->data()[i] - todo[i])));
        CHECK(err == 0.0);
    }
    CHECK(!rec.stop().error.empty());  // no se estaba grabando
    // Sin grabar, el mezclador no deja nada.
    for (int k = 0; k < 10; ++k) m.process(out.data(), 192, 2);
    CHECK(rec.seconds() == 0.0 || !rec.recording());
    m.setShowRecorder(nullptr);
    m.setLiveInput(nullptr);
}

TEST(estudioPresentacion) {
    const std::string dir = nuevaCarpeta("presentacion");
    float st[Studio::kShowStateCount];
    {
        Studio s;
        s.open(dir, dir);
        CHECK(s.waitIdle(30000));
        CHECK(s.loadDemo() && s.waitIdle(60000));
        s.showState(st);
        CHECK(st[0] == 0.0f && st[2] == 0.0f);   // nada grabado todavía
        CHECK(!s.saveShow(crear(tmpPath("nada.wav")), "nada.wav"));
        CHECK(s.showStart());
        s.play();
        std::vector<float> out(480 * 2);
        for (int k = 0; k < 150; ++k) s.mixer().process(out.data(), 480, 2);  // 1.5 s
        s.showState(st);
        CHECK(st[0] == 1.0f && st[2] == 0.0f);
        CHECK(s.showStop());
        s.pause();
        s.showState(st);
        CHECK(st[0] == 0.0f && st[2] == 1.0f);
        CHECK_NEAR(st[3], 1.5, 0.01);
        CHECK(estado(s)["mensaje"].asString().rfind("Presentación grabada (0:01)", 0) == 0);
        CHECK(!s.showStop());
        // Guardarla en el teléfono: con el formato de exportar (16 bits a 44.1 kHz aquí).
        s.setExportFormat(16, 44100);
        const std::string out16 = tmpPath("presentacion-16.wav");
        CHECK(s.saveShow(crear(out16), "Mi presentación.wav"));
        CHECK(s.waitIdle(30000));
        const Json j = estado(s);
        CHECK(j["ultimaOk"].asBool() && j["exportaciones"].asInt() == 1);
        CHECK(j["mensaje"].asString().rfind("Presentación guardada: Mi presentación.wav (16 bits · 44100 Hz · 0:01", 0) == 0);
        WavReader wr;
        CHECK(wr.open(std::fopen(out16.c_str(), "rb")).empty());
        CHECK(wr.sampleRate() == 44100 && wr.bits() == 16 && wr.channels() == 2);
        CHECK(std::llabs(wr.frames() - static_cast<int64_t>(1.5 * 44100)) <= 2);
    }
    // Al volver a abrir, la última presentación sigue lista para guardar.
    Studio s2;
    s2.open(dir, dir);
    CHECK(s2.waitIdle(30000));
    s2.showState(st);
    CHECK(st[2] == 1.0f && std::fabs(st[3] - 1.5f) < 0.01f);
}

// La pantalla del escenario: cuánto falta para entrar a cantar. Las partes siguen los pedazos del
// instrumental (si se mueve o se corta, se mueven con él).
TEST(cuentaParaEntrar) {
    const std::string dir = nuevaCarpeta("cuenta");
    ::mkdir((dir + "/audio").c_str(), 0700);
    CHECK(writeDemoTrack(1, dir + "/audio/inst.wav", 48000).empty());
    // Una canción preparada a mano: canta de 2 a 5 s y de 8 a 10.5 s.
    std::string env;
    for (int i = 0; i < 120; ++i) {
        const double t = i / 10.0;
        env += std::string(i ? "," : "") + ((t >= 2 && t < 5) || (t >= 8 && t < 10.5) ? "1" : "0");
    }
    const std::string json =
        R"({"version":1,"siguienteId":5,"pistas":[{"id":4,"nombre":"Paraíso · instrumental","archivo":"inst.wav",)"
        R"("info":"","volDb":0,"pan":0}],"cancion":{"fuente":0,"instrumental":4,"voz":0,"tonica":9,"menor":true,)"
        R"("bpm":96,"inicios":[2,8],"finales":[5,10.5],"envolvente":[)" + env + "]}}";
    FILE *f = std::fopen((dir + "/sesion.json").c_str(), "wb");
    std::fputs(json.c_str(), f);
    std::fclose(f);
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    CHECK_NEAR(s.duration(), 12.0, 1e-9);
    std::string je;
    Json c = Json::parse(s.songJson(), je);
    CHECK(c["lista"].asBool() && c["partes"].size() == 2);
    CHECK_NEAR(c["partes"][1][0].asDouble(), 8.0, 1e-4);
    CHECK_NEAR(c["partes"][1][1].asDouble(), 10.5, 1e-4);
    float q[Studio::kCueCount];
    auto cue = [&](double t) {
        s.seek(t);
        s.songCue(q);
    };
    cue(0.0);
    CHECK(q[0] == Studio::kCueWaiting && q[2] == 0.0f && q[3] == 2.0f);
    CHECK_NEAR(q[1], 2.0, 1e-3);
    CHECK_NEAR(q[4], 0.0, 1e-3);
    cue(3.0);
    CHECK(q[0] == Studio::kCueSinging && q[2] == 0.0f);
    CHECK_NEAR(q[1], 2.0, 1e-3);
    CHECK_NEAR(q[4], 1.0 / 3.0, 1e-3);
    cue(6.5);
    CHECK(q[0] == Studio::kCueWaiting && q[2] == 1.0f);
    CHECK_NEAR(q[1], 1.5, 1e-3);
    CHECK_NEAR(q[4], 0.5, 1e-3);
    cue(11.0);
    CHECK(q[0] == Studio::kCueDone && q[2] == 2.0f && q[3] == 2.0f);
    std::vector<float> e = s.songEnvelope(12);  // una columna por segundo
    CHECK(e.size() == 12 && e[0] < 0.1f && e[3] > 0.9f && e[6] < 0.1f && e[9] > 0.9f && e[11] < 0.1f);

    // El instrumental 1 s más tarde: todo se corre con él (y la pantalla sabe que cambió).
    int32_t va[Studio::kVersionCount], vb[Studio::kVersionCount];
    s.versions(va);
    s.nudgeRegion(4, 0, 1000);
    s.versions(vb);
    CHECK(vb[Studio::kVerSong] != va[Studio::kVerSong]);
    CHECK(s.regions(4)[0].start == 48000);
    c = Json::parse(s.songJson(), je);
    CHECK_NEAR(c["partes"][0][0].asDouble(), 3.0, 1e-4);
    CHECK_NEAR(c["partes"][1][1].asDouble(), 11.5, 1e-4);
    cue(2.5);
    CHECK(q[0] == Studio::kCueWaiting && q[2] == 0.0f);
    CHECK_NEAR(q[1], 0.5, 1e-3);
    e = s.songEnvelope(13);  // ahora la mezcla dura 13 s
    CHECK(e[0] < 0.1f && e[2] < 0.1f && e[3] > 0.9f && e[7] < 0.1f && e[10] > 0.9f);
    // Cortado en medio de una parte, sigue siendo una (se juntan los pedazos que se tocan).
    s.seek(4.0);
    CHECK(s.cut(4, 0) == 1);
    c = Json::parse(s.songJson(), je);
    CHECK(c["partes"].size() == 2);
    CHECK_NEAR(c["partes"][0][0].asDouble(), 3.0, 1e-4);
    CHECK_NEAR(c["partes"][0][1].asDouble(), 6.0, 1e-4);
    // Sin la parte de en medio (se borra el pedazo de 4 s en adelante): solo queda lo de antes.
    CHECK(s.deleteRegion(4, 1));
    c = Json::parse(s.songJson(), je);
    CHECK(c["partes"].size() == 1);
    CHECK_NEAR(c["partes"][0][1].asDouble(), 4.0, 1e-4);
    // Sin canción preparada: nada que contar.
    CHECK(s.removeTrack(4));
    s.songCue(q);
    CHECK(q[0] == Studio::kCueNone && q[3] == 0.0f);
}

// Edición al azar sobre pistas largas (como una canción de verdad): nada se rompe y los pedazos
// siempre son válidos, también pidiendo la onda con valores raros (lo que hace la pantalla).
TEST(edicionAlAzar) {
    const std::string dir = nuevaCarpeta("edicion-azar");
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    s.clearProject();
    const std::string a = tmpPath("larga-mono.wav"), b = tmpPath("larga-estereo.wav");
    const int64_t largo[2] = {48000LL * 200, 48000LL * 150};
    {
        WavSpec spec;
        spec.channels = 1;
        makeWav(a, spec, largo[0], [](int64_t f, int) { return 0.3 * std::sin(f * 0.01); });
    }
    {
        WavSpec spec;
        makeWav(b, spec, largo[1], [](int64_t f, int c) { return 0.2 * std::sin(f * (c ? 0.02 : 0.03)); });
    }
    CHECK(s.importFiles({abrir(a), abrir(b)}, {"Canción.wav", "Otra.wav"}) && s.waitIdle(120000));
    CHECK(s.trackCount() == 2);
    const int ids[2] = {idDe(s, 0), idDe(s, 1)};
    std::mt19937 rng(1234);
    auto azar = [&](int64_t lo, int64_t hi) { return std::uniform_int_distribution<int64_t>(lo, hi)(rng); };
    int fallas = 0;
    for (int k = 0; k < 6000; ++k) {
        const int id = ids[azar(0, 1)];
        const int n = static_cast<int>(s.regions(id).size());
        const int i = static_cast<int>(azar(-1, n));  // a veces fuera de rango
        switch (azar(0, 11)) {
            case 0:
            case 1:  // arrastre: mover, recortar o fundidos
                if (s.editBegin(id, i, static_cast<int>(azar(1, 5)))) {
                    for (int d = 0; d < 4; ++d) s.editDrag(azar(-48000LL * 60, 48000LL * 60), azar(0, 4800));
                    s.editEnd();
                }
                break;
            case 2:
                s.seek(static_cast<double>(azar(0, 48000LL * 220)) / 48000.0);
                s.cut(azar(0, 3) == 0 ? -1 : id, i);
                break;
            case 3: s.deleteRegion(id, i); break;
            case 4: s.nudgeRegion(id, i, static_cast<int>(azar(-5000, 5000))); break;
            case 5: s.setRegionFade(id, i, azar(0, 1) == 1, azar(-100, 48000LL * 30)); break;
            case 6: s.undo(); break;
            case 7: s.redo(); break;
            case 8:
                if (azar(0, 20) == 0) s.restoreRegions(id);
                break;
            default: {  // la onda para dibujar
                const int64_t from = azar(-48000, 48000LL * 220), to = from + azar(-100, 48000LL * 100);
                (void)s.regionWave(id, i, from, to, static_cast<int>(azar(-5, 2000)));
                break;
            }
        }
        for (int t = 0; t < 2; ++t) {
            const std::vector<Region> rr = s.regions(ids[t]);
            for (size_t q = 0; q < rr.size(); ++q) {
                const Region &g = rr[q];
                const bool ok = g.length > 0 && g.start >= 0 && g.src >= 0 && g.src + g.length <= largo[t] &&
                                g.fadeIn >= 0 && g.fadeOut >= 0 && g.fadeIn + g.fadeOut <= g.length;
                if (!ok && fallas++ < 5)
                    std::printf("  pedazo inválido (vuelta %d): pista %d #%zu inicio %lld origen %lld largo %lld fundidos %lld/%lld\n",
                                k, t, q, static_cast<long long>(g.start), static_cast<long long>(g.src),
                                static_cast<long long>(g.length), static_cast<long long>(g.fadeIn),
                                static_cast<long long>(g.fadeOut));
            }
        }
    }
    CHECK(fallas == 0);
    // Y la mezcla sigue sonando con todo eso.
    std::vector<float> out(2 * 480);
    s.seek(0);
    s.play();
    for (int k = 0; k < 50; ++k) s.mixer().process(out.data(), 480, 2);
    for (float v : out) CHECK(std::isfinite(v));
}

// Que se note: con la velocidad con que empieza la voz en vivo, una nota desafinada (+25 cents)
// con vibrato de ±40 cents queda pegada a la nota (lo que hace que el auto-tune se oiga). Con la
// de antes (20 ms) el vibrato pasaba a medias y casi no se notaba.
TEST(autoTuneSeNota) {
    std::vector<double> hz(4 * 48000);
    for (size_t i = 0; i < hz.size(); ++i)
        hz[i] = 329.63 * std::exp2((25.0 + 40.0 * std::sin(2 * M_PI * 5.5 * i / 48000.0)) / 1200.0);
    const std::vector<float> v = vozSintetica(hz);
    const float porOmision = vfx::info(vfx::kTuneSpeed).def;
    CHECK(porOmision <= 5.0f);
    for (float velocidad : {porOmision, 20.0f}) {
        AutoTune at(48000);
        TuneSettings s;
        s.key = 11;  // Si menor (tiene Mi)
        s.scale = kScaleMinor;
        s.speedMs = velocidad;
        std::vector<float> y = v;
        for (size_t i = 0; i < y.size(); i += 64) at.process(y.data() + i, 64, s, false);
        // Ventanas de 10 ms cada 2.5 ms (el tono de lo que sale en cada momento).
        PitchDetector det(48000, 480, 60.0f, 1000.0f);
        int total = 0, cerca = 0;
        double suma = 0;
        for (size_t i = 48000; i + det.needed() < y.size(); i += 120) {
            const float h = det.detect(y.data() + i, 0.15f).hz;
            const double c = h > 0 ? std::fabs(centsEntre(h, 329.63)) : 1200.0;
            ++total;
            suma += std::min(c, 100.0);
            cerca += c <= 10.0;
        }
        std::printf("  %.0f ms: a ≤10 cents %.0f %%, desvío medio %.1f cents\n", velocidad, 100.0 * cerca / total, suma / total);
        if (velocidad == porOmision) {
            CHECK(cerca >= total * 9 / 10);
            CHECK(suma / total < 7.0);
        } else {
            CHECK(cerca < total * 8 / 10);
        }
    }
}

// Las sesiones de antes de la versión 2 de la voz: la afinación que casi no se notaba (20 ms por
// omisión, 12 ms en "Paraíso") pasa a la nueva; lo que se puso a mano se queda.
TEST(sesionVozVieja) {
    for (const auto &[guardada, esperada] : std::vector<std::pair<double, float>>{{20, 5}, {12, 4}, {33, 33}}) {
        const std::string dir = nuevaCarpeta("voz-vieja-" + std::to_string(static_cast<int>(guardada)));
        const std::string json = R"({"version":1,"siguienteId":1,"pistas":[],"entrada":{"voz":{"encendida":1,"velocidad":)" +
                                 std::to_string(guardada) + "}}}";
        FILE *f = std::fopen((dir + "/sesion.json").c_str(), "wb");
        std::fputs(json.c_str(), f);
        std::fclose(f);
        Studio s;
        s.open(dir, dir);
        CHECK(s.waitIdle(30000));
        CHECK(s.voiceFxState()[vfx::kTuneSpeed] == esperada);
        CHECK(s.voiceFxState()[vfx::kOn] == 1.0f);
        CHECK(s.saveNow());
        CHECK(leerSesion(dir)["entrada"]["voz"]["version"].asInt() == 2);
    }
    // Ya en la versión 2, 20 ms (puesto a mano) se respeta.
    const std::string dir = nuevaCarpeta("voz-v2");
    const char *json = R"({"version":1,"siguienteId":1,"pistas":[],"entrada":{"voz":{"velocidad":20,"version":2}}})";
    FILE *f = std::fopen((dir + "/sesion.json").c_str(), "wb");
    std::fputs(json, f);
    std::fclose(f);
    Studio s;
    s.open(dir, dir);
    CHECK(s.waitIdle(30000));
    CHECK(s.voiceFxState()[vfx::kTuneSpeed] == 20.0f);
}

// ---------------------------------------------------------------- relojes de la entrada y la salida

// Simula la entrada (con su propio reloj, en ráfagas) y la salida (que pide de a 64 cuadros, 3
// por ráfaga de 192): un seno limpio debe salir sin faltas, saltos ni chasquidos.
struct SimRelojes {
    DriftFifo fifo{2, 8192};
    double entradaHz = 48000, fase = 0;
    int rafagaEntrada = 240;
    int64_t entregados = 0;  // cuadros de entrada ya entregados
    double maxSegunda = 0;   // peor "segunda diferencia" (un chasquido la dispara)
    float prev[2] = {0, 0}, prev2[2] = {0, 0};
    int64_t muestras = 0;
    int maxNivel = 0;
    // Corre `segundos` de la salida. `traba`: momento (s) en que la entrada se detiene `trabaMs`.
    // `pausa`: momento en que la salida deja de pedir `pausaMs` (la entrada sigue llegando).
    void correr(double segundos, double traba = -1, double trabaMs = 0, double pausa = -1, double pausaMs = 0) {
        std::vector<float> rafaga, out(2 * 64);
        const int64_t total = static_cast<int64_t>(segundos * 48000 / 192);
        for (int64_t k = 0; k < total; ++k) {
            const double t = (k + 1) * 192 / 48000.0;  // hora (reloj de la salida)
            // La entrada entrega cada ráfaga cuando su reloj la completa (salvo durante la traba).
            for (;;) {
                const double listo = (entregados + rafagaEntrada) / entradaHz;
                if (listo > t) break;
                if (traba >= 0 && listo > traba && t < traba + trabaMs / 1000) break;
                rafaga.resize(static_cast<size_t>(2 * rafagaEntrada));
                for (int i = 0; i < rafagaEntrada; ++i) {
                    const float v = static_cast<float>(0.5 * std::sin(fase));
                    fase += 2 * M_PI * 440 / entradaHz;
                    rafaga[2 * i] = v;
                    rafaga[2 * i + 1] = -v;
                }
                fifo.push(rafaga.data(), rafagaEntrada);
                entregados += rafagaEntrada;
            }
            if (pausa >= 0 && t > pausa && t < pausa + pausaMs / 1000) continue;  // la salida no pide
            for (int b = 0; b < 3; ++b) {
                fifo.pull(out.data(), 64);
                for (int i = 0; i < 64; ++i) {
                    for (int c = 0; c < 2; ++c) {
                        const float v = out[2 * i + c];
                        if (muestras > 2) maxSegunda = std::max(maxSegunda, std::fabs(static_cast<double>(v) - 2 * prev[c] + prev2[c]));
                        prev2[c] = prev[c];
                        prev[c] = v;
                    }
                    ++muestras;
                }
                maxNivel = std::max(maxNivel, fifo.level());
            }
        }
    }
};

TEST(relojesDistintos) {
    // Un seno de 0.5 a 440 Hz cambia su segunda diferencia en ~0.0017 por muestra: un chasquido
    // (un salto o un hueco) la lleva a más de 0.05.
    for (double ppm : {150.0, -150.0, 0.0}) {
        SimRelojes s;
        s.entradaHz = 48000 * (1 + ppm * 1e-6);
        s.correr(600);  // 10 minutos
        std::printf("  %+.0f ppm: faltas %lld, saltos %lld, velocidad %.6f, reserva máx %d, 2.ª dif %.4f\n", ppm,
                    static_cast<long long>(s.fifo.underruns()), static_cast<long long>(s.fifo.skips()), s.fifo.ratio(),
                    s.maxNivel, s.maxSegunda);
        CHECK(s.fifo.underruns() == 0 && s.fifo.skips() == 0);
        CHECK(s.maxSegunda < 0.01);  // el fundido de entrada da ~0.005; un chasquido, más de 0.05
        // El retraso no crece (sin corregir, 150 ppm en 10 min serían 4320 cuadros de más).
        CHECK(s.maxNivel < 64 + 2 * 240 + 192 + 64);
        CHECK(s.fifo.level() < 64 + 240 + 192 + 64);
    }
    // Ráfagas grandes de la entrada (como algunos celulares: 20 ms) y relojes distintos.
    {
        SimRelojes s;
        s.entradaHz = 48000 * (1 + 120e-6);
        s.rafagaEntrada = 960;
        s.correr(300);
        std::printf("  ráfagas de 960: faltas %lld, saltos %lld, reserva máx %d, 2.ª dif %.4f\n",
                    static_cast<long long>(s.fifo.underruns()), static_cast<long long>(s.fifo.skips()), s.maxNivel, s.maxSegunda);
        CHECK(s.fifo.underruns() == 0 && s.fifo.skips() == 0 && s.maxSegunda < 0.01);
        CHECK(s.maxNivel < 64 + 2 * 960 + 192 + 64);
    }
    // La entrada se traba 15 ms: baja a silencio con fundido (sin chasquido) y se recupera sola.
    {
        SimRelojes s;
        s.correr(30, 10.0, 15.0);
        std::printf("  traba de 15 ms: faltas %lld, saltos %lld, reserva %d (margen %d), 2.ª dif %.4f\n",
                    static_cast<long long>(s.fifo.underruns()), static_cast<long long>(s.fifo.skips()), s.fifo.level(),
                    s.fifo.safety(), s.maxSegunda);
        CHECK(s.fifo.underruns() == 1 && s.fifo.safety() == 96);
        CHECK(s.maxSegunda < 0.02);
        CHECK(s.fifo.level() < 96 + 240 + 192 + 64);
    }
    // La salida se traba 80 ms y la entrada sigue llegando (sobran 80 ms): salta con fundido
    // cruzado y vuelve al mínimo.
    {
        SimRelojes s;
        s.correr(20, -1, 0, 5.0, 80.0);
        std::printf("  80 ms de más: faltas %lld, saltos %lld, reserva %d, 2.ª dif %.4f\n",
                    static_cast<long long>(s.fifo.underruns()), static_cast<long long>(s.fifo.skips()), s.fifo.level(), s.maxSegunda);
        CHECK(s.fifo.skips() == 1 && s.fifo.underruns() == 0);
        CHECK(s.maxSegunda < 0.02);
        CHECK(s.fifo.level() < 64 + 240 + 192 + 64);
    }
}

int main(int argc, char **argv) {
    char tmpl[] = "/tmp/wavapp-pruebas-XXXXXX";
    gDir = mkdtemp(tmpl);
    for (auto &t : tests()) {
        bool elegida = argc <= 1;
        for (int i = 1; i < argc; ++i) elegida = elegida || std::strcmp(argv[i], t.name) == 0;
        if (!elegida) continue;
        std::printf("• %s\n", t.name);
        t.fn();
    }
    std::string cmd = "rm -rf '" + gDir + "'";
    if (std::system(cmd.c_str()) != 0) std::printf("no se pudo borrar %s\n", gDir.c_str());
    std::printf("\n%d comprobaciones, %d fallas\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
