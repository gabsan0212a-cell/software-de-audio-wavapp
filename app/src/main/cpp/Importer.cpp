#include "Importer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "Resampler.h"
#include "WavReader.h"
#include "WavWriter.h"

namespace {

constexpr size_t kChunk = 8192;

std::string writeFailed() { return "No se pudo guardar la pista (¿falta espacio?)"; }

// Reescribe un WAV estéreo interno tomando solo el canal izquierdo.
std::string toMono(const std::string &stereoPath, const std::string &monoPath, int sampleRate, int64_t &frames) {
    WavReader r;
    std::string err = r.open(std::fopen(stereoPath.c_str(), "rb"));
    if (!err.empty()) return err;
    WavWriter w;
    if (!w.open(monoPath, 1, sampleRate)) return writeFailed();
    std::vector<float> in(kChunk * 2), out(kChunk);
    size_t n;
    while ((n = r.read(in.data(), kChunk)) > 0) {
        for (size_t i = 0; i < n; ++i) out[i] = in[2 * i];
        if (!w.write(out.data(), n)) return writeFailed();
    }
    frames = w.frames();
    return w.close() ? "" : writeFailed();
}

}  // namespace

std::string importWav(FILE *src, const std::string &dstPath, int sampleRate, ImportInfo &info,
                      std::atomic<float> *progress) {
    if (progress) progress->store(0.0f);
    WavReader r;
    std::string err = r.open(src);
    if (!err.empty()) return err;

    info.srcRate = r.sampleRate();
    info.srcChannels = r.channels();
    info.srcBits = r.bits();
    info.srcFloat = r.isFloat();
    const int srcCh = r.channels();
    const int outCh = std::min(2, srcCh);  // con más de 2 canales se usan los dos primeros

    const std::string tmp = dstPath + ".tmp";
    WavWriter w;
    if (!w.open(tmp, outCh, sampleRate)) return writeFailed();
    std::unique_ptr<Resampler> rs;
    if (r.sampleRate() != sampleRate) rs = std::make_unique<Resampler>(outCh, r.sampleRate(), sampleRate);

    std::vector<float> in(kChunk * srcCh), sel(kChunk * outCh), out;
    bool dual = outCh == 2;
    int64_t read = 0;
    const int64_t total = r.frames();
    size_t n;
    while ((n = r.read(in.data(), kChunk)) > 0) {
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < outCh; ++c) sel[i * outCh + c] = in[i * srcCh + c];
        if (dual)
            for (size_t i = 0; i < n && dual; ++i) dual = std::fabs(sel[2 * i] - sel[2 * i + 1]) <= 1e-6f;
        bool ok;
        if (rs) {
            out.clear();
            rs->process(sel.data(), n, out);
            ok = w.write(out.data(), out.size() / outCh);
        } else {
            ok = w.write(sel.data(), n);
        }
        if (!ok) {
            w.close();
            std::remove(tmp.c_str());
            return writeFailed();
        }
        read += static_cast<int64_t>(n);
        if (progress && total > 0) progress->store(0.95f * static_cast<float>(read) / static_cast<float>(total));
    }
    if (rs) {
        out.clear();
        rs->finish(out);
        if (!w.write(out.data(), out.size() / outCh)) {
            w.close();
            std::remove(tmp.c_str());
            return writeFailed();
        }
    }
    int64_t frames = w.frames();
    if (!w.close() || frames == 0) {
        std::remove(tmp.c_str());
        return frames == 0 ? "El WAV está vacío" : writeFailed();
    }

    info.dualMono = dual;
    info.channels = outCh;
    if (dual) {
        err = toMono(tmp, dstPath, sampleRate, frames);
        std::remove(tmp.c_str());
        if (!err.empty()) {
            std::remove(dstPath.c_str());
            return err;
        }
        info.channels = 1;
    } else if (std::rename(tmp.c_str(), dstPath.c_str()) != 0) {
        std::remove(tmp.c_str());
        return writeFailed();
    }
    info.frames = frames;
    if (progress) progress->store(1.0f);
    return "";
}

// Misma síntesis que demoTracks() del prototipo web.
std::vector<float> synthDemo(int index, int sampleRate) {
    const double sr = sampleRate, TAU = 2.0 * 3.14159265358979323846;
    const size_t n = static_cast<size_t>(std::floor(sr * 12));
    std::vector<float> d(n);
    auto env = [](double u, double len, double dec) {
        return std::min(1.0, u / 0.008) * std::exp(-u * dec) * std::min(1.0, (len - u) / 0.02);
    };
    double ph = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        double v = 0.0;
        switch (index) {
            case 0: {  // Bajo
                static const double notes[] = {55, 55, 73.42, 65.41};
                const double k = std::floor(t / 0.5), u = t - k * 0.5;
                const double f = notes[static_cast<int>(std::floor(k / 2)) % 4];
                v = 0.30 * env(u, 0.5, 2.2) * (std::sin(TAU * f * u) + 0.25 * std::sin(TAU * 2 * f * u));
                break;
            }
            case 1: {  // Voz
                static const double notes[] = {220, 246.9, 261.6, 246.9};
                const double f = notes[static_cast<int>(std::floor(t / 3)) % 4];
                ph += TAU * f * (1 + 0.005 * std::sin(TAU * 5 * t)) / sr;
                const double a = 0.5 - 0.5 * std::cos(TAU * (std::fmod(t, 3.0) / 3));
                v = 0.13 * a * (std::sin(ph) + 0.35 * std::sin(2 * ph) + 0.12 * std::sin(3 * ph));
                break;
            }
            case 2: {  // Guitarra
                static const double notes[] = {196, 246.9, 293.7, 246.9};
                const double k = std::floor(t / 0.25), u = t - k * 0.25;
                const double f = notes[static_cast<int64_t>(k) % 4];
                double s = 0.0;
                for (int h = 1; h <= 4; ++h) s += std::sin(TAU * f * h * u) / (h * h);
                v = 0.16 * env(u, 0.25, 7) * s;
                break;
            }
            default: {  // Teclado
                static const double c1[] = {220, 277.2, 329.6}, c2[] = {261.6, 329.6, 392};
                const double *c = (static_cast<int64_t>(std::floor(t / 3)) % 2) ? c1 : c2;
                const double a = 0.5 - 0.5 * std::cos(TAU * (std::fmod(t, 3.0) / 3));
                v = 0.05 * a * (std::sin(TAU * c[0] * t) + std::sin(TAU * c[1] * t) + std::sin(TAU * c[2] * t));
                break;
            }
        }
        d[i] = static_cast<float>(v);
    }
    // Fundido de 50 ms en los extremos, sin clics.
    const size_t f = static_cast<size_t>(std::floor(sr * 0.05));
    for (size_t i = 0; i < f; ++i) {
        const float g = static_cast<float>(i) / static_cast<float>(f);
        d[i] *= g;
        d[n - 1 - i] *= g;
    }
    return d;
}

std::string writeDemoTrack(int index, const std::string &dstPath, int sampleRate) {
    if (index < 0 || index >= kDemoTracks) return "Pista de prueba inválida";
    const std::vector<float> d = synthDemo(index, sampleRate);
    WavWriter w;
    if (!w.open(dstPath, 1, sampleRate) || !w.write(d.data(), d.size()) || !w.close()) return writeFailed();
    return "";
}

std::vector<float> computePeaks(const MappedAudio &audio, int framesPerBucket) {
    const int ch = audio.channels();
    const int64_t frames = audio.frames();
    const int64_t buckets = (frames + framesPerBucket - 1) / framesPerBucket;
    std::vector<float> peaks(static_cast<size_t>(buckets) * ch * 2);
    const float *d = audio.data();
    for (int64_t b = 0; b < buckets; ++b) {
        const int64_t start = b * framesPerBucket, end = std::min(frames, start + framesPerBucket);
        for (int c = 0; c < ch; ++c) {
            float mn = 0.0f, mx = 0.0f;
            for (int64_t f = start; f < end; ++f) {
                const float v = d[f * ch + c];
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
            peaks[(static_cast<size_t>(b) * ch + c) * 2] = mn;
            peaks[(static_cast<size_t>(b) * ch + c) * 2 + 1] = mx;
        }
    }
    return peaks;
}
