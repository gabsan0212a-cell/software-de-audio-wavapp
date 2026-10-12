// Pruebas del núcleo nativo (sin Oboe ni Android). Se compilan y corren en Linux:
//   tests/native/run.sh
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "Studio.h"
#include "dsp/AnalysisView.h"
#include "dsp/Analyzer.h"
#include "dsp/Equalizer.h"
#include "dsp/Limiter.h"
#include "dsp/Resampler.h"
#include "engine/Exporter.h"
#include "engine/Importer.h"
#include "engine/Meters.h"
#include "engine/Mixer.h"
#include "io/Json.h"
#include "io/MappedAudio.h"
#include "io/WavReader.h"
#include "io/WavWriter.h"
#include "project/Edit.h"
#include "project/Session.h"

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
    float v[5];
    CHECK(s.trackParams(idDe(s, 2), v) && v[0] == -2.0f && v[1] == 60.0f);
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
    float p1[5], m1[6];
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
    float p2[5], m2[6];
    CHECK(s2.trackParams(id, p2));
    CHECK(std::equal(p1, p1 + 5, p2));
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
    float m[6];
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
    CHECK(estado(s)["mensaje"].asString() == "Sin pistas. Agrega archivos WAV o carga las pistas de prueba.");
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
        switch (rng() % 16) {
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

// ---------------------------------------------------------------- main
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
