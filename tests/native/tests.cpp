// Pruebas del núcleo nativo (sin Oboe ni Android). Se compilan y corren en Linux:
//   tests/native/run.sh
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

#include "Equalizer.h"
#include "Importer.h"
#include "Limiter.h"
#include "MappedAudio.h"
#include "Mixer.h"
#include "Resampler.h"
#include "WavReader.h"
#include "WavWriter.h"

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

// ---------------------------------------------------------------- main
int main(int argc, char **argv) {
    char tmpl[] = "/tmp/wavapp-pruebas-XXXXXX";
    gDir = mkdtemp(tmpl);
    const char *only = argc > 1 ? argv[1] : nullptr;
    for (auto &t : tests()) {
        if (only && std::strcmp(only, t.name) != 0) continue;
        std::printf("• %s\n", t.name);
        t.fn();
    }
    std::string cmd = "rm -rf '" + gDir + "'";
    if (std::system(cmd.c_str()) != 0) std::printf("no se pudo borrar %s\n", gDir.c_str());
    std::printf("\n%d comprobaciones, %d fallas\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
