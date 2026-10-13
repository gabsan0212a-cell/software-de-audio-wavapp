#include "engine/Exporter.h"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "engine/Mixer.h"
#include "dsp/Resampler.h"

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 4096;

void put32(std::vector<uint8_t> &v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void put16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void tag(std::vector<uint8_t> &v, const char *t) { v.insert(v.end(), t, t + 4); }

int64_t outFrames(int64_t frames48k, int rate) {
    return rate == kRate ? frames48k : Resampler::outputFrames(frames48k, kRate, rate);
}

// Encabezado completo con los tamaños ya conocidos. Float lleva fmt de 18 bytes y "fact".
std::vector<uint8_t> header(const ExportFormat &f, uint32_t dataBytes, uint32_t frames) {
    const int bytes = f.bits / 8, block = 2 * bytes;
    std::vector<uint8_t> h;
    tag(h, "RIFF");
    put32(h, 0);  // se completa abajo
    tag(h, "WAVE");
    tag(h, "fmt ");
    put32(h, f.isFloat ? 18 : 16);
    put16(h, f.isFloat ? 3 : 1);
    put16(h, 2);
    put32(h, static_cast<uint32_t>(f.sampleRate));
    put32(h, static_cast<uint32_t>(f.sampleRate * block));
    put16(h, static_cast<uint16_t>(block));
    put16(h, static_cast<uint16_t>(f.bits));
    if (f.isFloat) {
        put16(h, 0);
        tag(h, "fact");
        put32(h, 4);
        put32(h, frames);
    }
    tag(h, "data");
    put32(h, dataBytes);
    const uint32_t riff = static_cast<uint32_t>(h.size() - 8 + dataBytes);
    std::memcpy(&h[4], &riff, 4);
    return h;
}

bool writeAll(int fd, const void *p, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    while (n > 0) {
        const ssize_t w = ::write(fd, b, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        b += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

// Convierte float intercalado al formato pedido. Enteros: dither triangular (TPDF) de 1 LSB.
class Converter {
public:
    explicit Converter(const ExportFormat &f) : mF(f) {}
    void convert(const float *in, size_t samples, std::vector<uint8_t> &out) {
        out.clear();
        if (mF.isFloat) {
            out.resize(samples * 4);
            std::memcpy(out.data(), in, samples * 4);
            return;
        }
        const int bytes = mF.bits / 8;
        const double scale = mF.bits == 16 ? 32768.0 : 8388608.0;
        const double lo = -scale, hi = scale - 1;
        out.resize(samples * bytes);
        uint8_t *o = out.data();
        for (size_t i = 0; i < samples; ++i) {
            const double d = rnd() - rnd();  // triangular en [-1, 1] LSB
            const double v = std::clamp(std::nearbyint(in[i] * scale + d), lo, hi);
            const int32_t q = static_cast<int32_t>(v);
            for (int b = 0; b < bytes; ++b) *o++ = static_cast<uint8_t>(q >> (8 * b));
        }
    }

private:
    double rnd() {  // xorshift32 → [0, 1)
        mSeed ^= mSeed << 13;
        mSeed ^= mSeed >> 17;
        mSeed ^= mSeed << 5;
        return mSeed / 4294967296.0;
    }
    ExportFormat mF;
    uint32_t mSeed = 0x9E3779B9u;
};

// Copia del mezclador lista para renderizar desde el inicio, con ganancia extra en el master.
std::unique_ptr<Mixer> offlineMixer(const Mixer &live, float gainDb, bool forceLimiter, std::string &err) {
    auto m = std::make_unique<Mixer>(kRate);  // en el heap: es grande
    if (!live.cloneInto(*m, err)) return nullptr;
    if (gainDb != 0.0f) m->setMasterGain(m->masterGain() * std::pow(10.0f, gainDb / 20.0f));
    if (forceLimiter) m->setLimiter(true);
    m->prepareOffline();
    return m;
}

// Mide un bloque intercalado (L R L R...) de lo que sale.
void measureBlock(LoudnessFrontEnd &fe, LoudnessMeter &meter, const float *x, int n, std::vector<float> &l,
                  std::vector<float> &r, std::vector<float> *curve) {
    l.resize(static_cast<size_t>(n));
    r.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        l[i] = x[2 * i];
        r[i] = x[2 * i + 1];
    }
    LoudnessBlock b[2];
    const int made = fe.process(l.data(), r.data(), n, b, 2);
    for (int i = 0; i < made; ++i) {
        meter.add(b[i]);
        if (curve) curve->push_back(meter.shortTerm());
    }
}

}  // namespace

LoudnessReport measureMix(const Mixer &live, float gainDb, bool forceLimiter, std::atomic<float> *progress,
                          float from, float to, const std::atomic<bool> *cancel) {
    LoudnessReport rep;
    std::string err;
    auto mixer = offlineMixer(live, gainDb, forceLimiter, err);
    if (!mixer) {
        rep.error = err;
        return rep;
    }
    const int64_t length = mixer->length();
    if (length <= 0) {
        rep.error = "No hay pistas";
        return rep;
    }
    LoudnessFrontEnd fe(kRate);
    LoudnessMeter meter;
    std::vector<float> block(kBlock * 2), l, r;
    rep.shortTerm.reserve(static_cast<size_t>(length / (kRate / 10) + 1));
    for (int64_t done = 0; done < length;) {
        if (cancel && cancel->load()) {
            rep.error = "Cancelado";
            return rep;
        }
        const int n = static_cast<int>(std::min<int64_t>(kBlock, length - done));
        mixer->process(block.data(), n, 2);
        measureBlock(fe, meter, block.data(), n, l, r, &rep.shortTerm);
        done += n;
        if (progress) progress->store(from + (to - from) * static_cast<float>(done) / static_cast<float>(length));
    }
    rep.integrated = meter.integrated();
    rep.range = meter.range();
    rep.truePeak = meter.truePeakDb();
    rep.maxMomentary = meter.maxMomentary();
    rep.maxShortTerm = meter.maxShortTerm();
    rep.seconds = static_cast<double>(length) / kRate;
    return rep;
}

int64_t exportBytes(int64_t frames48k, const ExportFormat &fmt) {
    return outFrames(frames48k, fmt.sampleRate) * 2 * (fmt.bits / 8) + (fmt.isFloat ? 58 : 44);
}

ExportResult exportMix(const Mixer &live, int fd, const ExportFormat &fmtIn, std::atomic<float> *progress,
                       const std::atomic<bool> *cancel) {
    ExportResult res;
    ExportFormat fmt = fmtIn;
    if (fmt.isFloat) fmt.bits = 32;
    if (fmt.bits != 16 && fmt.bits != 24 && fmt.bits != 32) fmt.bits = 24;
    if (fmt.bits == 32) fmt.isFloat = true;  // 32 bits es float
    if (fmt.sampleRate != 44100) fmt.sampleRate = kRate;
    if (progress) progress->store(0.0f);

    auto fail = [&](const std::string &e) {
        if (fd >= 0) ::close(fd);
        res.error = e;
        return res;
    };
    if (fd < 0) return fail("No se pudo crear el archivo");

    if (live.length() <= 0) return fail("No hay pistas para exportar");
    // Ajuste al objetivo: se mide lo que saldría, se corrige el master y se vuelve a medir (el
    // limitador hace que subir 3 dB no siempre suba 3 dB). Cada pasada es un tercio del progreso.
    float gainDb = 0.0f;
    bool forceLimiter = false;
    float writeFrom = 0.0f;
    if (fmt.targetLufs < 0.0f) {
        for (int pass = 0; pass < 2; ++pass) {
            const LoudnessReport m =
                measureMix(live, gainDb, forceLimiter, progress, pass * 0.3f, (pass + 1) * 0.3f, cancel);
            if (!m.error.empty()) return fail(cancel && cancel->load() ? "Exportación cancelada" : m.error);
            if (m.integrated <= -69.0f) break;  // silencio: no hay nada que ajustar
            gainDb = std::clamp(gainDb + (fmt.targetLufs - m.integrated), -40.0f, 30.0f);
            forceLimiter = gainDb > 0.0f;
        }
        writeFrom = 0.6f;
    }
    std::string err;
    auto mixer = offlineMixer(live, gainDb, forceLimiter, err);
    if (!mixer) return fail(err);
    const int64_t length = mixer->length();
    if (length <= 0) return fail("No hay pistas para exportar");

    const int64_t frames = outFrames(length, fmt.sampleRate);
    const uint64_t dataBytes = static_cast<uint64_t>(frames) * 2 * (fmt.bits / 8);
    if (dataBytes > 0xFFFFFFFFull - 100) return fail("La mezcla es demasiado larga para un WAV (más de 4 GB)");
    const std::vector<uint8_t> h = header(fmt, static_cast<uint32_t>(dataBytes), static_cast<uint32_t>(frames));
    if (!writeAll(fd, h.data(), h.size())) return fail("No se pudo escribir el archivo (¿falta espacio?)");

    std::unique_ptr<Resampler> rs;
    if (fmt.sampleRate != kRate) rs = std::make_unique<Resampler>(2, kRate, fmt.sampleRate);
    Converter conv(fmt);
    std::vector<float> block(kBlock * 2), resampled, ml, mr;
    LoudnessFrontEnd fe(kRate);
    LoudnessMeter meter;
    std::vector<uint8_t> bytes;
    int64_t done = 0, written = 0;

    auto emit = [&](const float *x, int64_t n) -> bool {
        n = std::min(n, frames - written);  // nunca más de lo que dice el encabezado
        if (n <= 0) return true;
        conv.convert(x, static_cast<size_t>(n * 2), bytes);
        if (!writeAll(fd, bytes.data(), bytes.size())) return false;
        written += n;
        return true;
    };

    while (done < length) {
        if (cancel && cancel->load()) return fail("Exportación cancelada");
        const int n = static_cast<int>(std::min<int64_t>(kBlock, length - done));
        mixer->process(block.data(), n, 2);
        measureBlock(fe, meter, block.data(), n, ml, mr, nullptr);
        bool ok;
        if (rs) {
            resampled.clear();
            rs->process(block.data(), static_cast<size_t>(n), resampled);
            ok = emit(resampled.data(), static_cast<int64_t>(resampled.size() / 2));
        } else {
            ok = emit(block.data(), n);
        }
        if (!ok) return fail("No se pudo escribir el archivo (¿falta espacio?)");
        done += n;
        if (progress)
            progress->store(writeFrom + (1.0f - writeFrom) * static_cast<float>(done) / static_cast<float>(length));
    }
    if (rs) {
        resampled.clear();
        rs->finish(resampled);
        if (!emit(resampled.data(), static_cast<int64_t>(resampled.size() / 2)))
            return fail("No se pudo escribir el archivo (¿falta espacio?)");
    }
    // Por si el remuestreo quedó corto: se completa con silencio hasta lo prometido.
    std::fill(block.begin(), block.end(), 0.0f);
    while (written < frames) {
        if (!emit(block.data(), std::min<int64_t>(kBlock, frames - written)))
            return fail("No se pudo escribir el archivo (¿falta espacio?)");
    }
    if (::close(fd) != 0) {
        res.error = "No se pudo terminar de guardar el archivo";
        return res;
    }
    res.frames = frames;
    res.bytes = static_cast<int64_t>(h.size() + dataBytes);
    res.lufs = meter.integrated();
    res.truePeak = meter.truePeakDb();
    res.gainDb = gainDb;
    if (progress) progress->store(1.0f);
    return res;
}
