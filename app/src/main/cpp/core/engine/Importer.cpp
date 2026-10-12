#include "engine/Importer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "io/WavReader.h"

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

ImportSink::ImportSink(std::string dstPath, int projectRate)
    : mDst(std::move(dstPath)), mTmp(mDst + ".tmp"), mRate(projectRate) {}

ImportSink::~ImportSink() { abort(); }

void ImportSink::abort() {
    if (!mOpen) return;
    mWriter.close();
    std::remove(mTmp.c_str());
    mOpen = false;
}

std::string ImportSink::fail(const std::string &e) {
    abort();
    return e;
}

std::string ImportSink::begin(int sampleRate, int channels) {
    if (mOpen) return "";
    if (sampleRate <= 0 || channels <= 0) return "Formato de audio inválido";
    mSrcRate = sampleRate;
    mSrcCh = channels;
    mOutCh = std::min(2, channels);  // con más de 2 canales se usan los dos primeros
    mDual = mOutCh == 2;
    if (!mWriter.open(mTmp, mOutCh, mRate)) return writeFailed();
    if (sampleRate != mRate) mResampler = std::make_unique<Resampler>(mOutCh, sampleRate, mRate);
    mOpen = true;
    return "";
}

std::string ImportSink::write(const float *in, size_t n) {
    if (!mOpen) return "No se pudo guardar la pista";
    mSel.resize(n * mOutCh);
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < mOutCh; ++c) mSel[i * mOutCh + c] = in[i * mSrcCh + c];
    if (mDual)
        for (size_t i = 0; i < n && mDual; ++i) mDual = std::fabs(mSel[2 * i] - mSel[2 * i + 1]) <= 1e-6f;
    bool ok;
    if (mResampler) {
        mOut.clear();
        mResampler->process(mSel.data(), n, mOut);
        ok = mWriter.write(mOut.data(), mOut.size() / mOutCh);
    } else {
        ok = mWriter.write(mSel.data(), n);
    }
    return ok ? "" : fail(writeFailed());
}

std::string ImportSink::finish(ImportInfo &info) {
    if (!mOpen) return "El archivo no tiene audio";
    if (mResampler) {
        mOut.clear();
        mResampler->finish(mOut);
        if (!mWriter.write(mOut.data(), mOut.size() / mOutCh)) return fail(writeFailed());
    }
    int64_t frames = mWriter.frames();
    const bool closed = mWriter.close();
    mOpen = false;
    if (!closed || frames == 0) {
        std::remove(mTmp.c_str());
        return frames == 0 ? "El archivo no tiene audio" : writeFailed();
    }
    info.srcRate = mSrcRate;
    info.srcChannels = mSrcCh;
    info.dualMono = mDual;
    info.channels = mOutCh;
    if (mDual) {
        const std::string err = toMono(mTmp, mDst, mRate, frames);
        std::remove(mTmp.c_str());
        if (!err.empty()) {
            std::remove(mDst.c_str());
            return err;
        }
        info.channels = 1;
    } else if (std::rename(mTmp.c_str(), mDst.c_str()) != 0) {
        std::remove(mTmp.c_str());
        return writeFailed();
    }
    info.frames = frames;
    return "";
}

std::string importWav(FILE *src, const std::string &dstPath, int sampleRate, ImportInfo &info,
                      std::atomic<float> *progress) {
    if (progress) progress->store(0.0f);
    WavReader r;
    std::string err = r.open(src);
    if (!err.empty()) return err;
    ImportSink sink(dstPath, sampleRate);
    err = sink.begin(r.sampleRate(), r.channels());
    if (!err.empty()) return err;
    std::vector<float> in(kChunk * r.channels());
    const int64_t total = r.frames();
    int64_t read = 0;
    size_t n;
    while ((n = r.read(in.data(), kChunk)) > 0) {
        err = sink.write(in.data(), n);
        if (!err.empty()) return err;
        read += static_cast<int64_t>(n);
        if (progress && total > 0) progress->store(0.95f * static_cast<float>(read) / static_cast<float>(total));
    }
    err = sink.finish(info);
    if (!err.empty()) return err == "El archivo no tiene audio" ? "El WAV está vacío" : err;
    info.srcBits = r.bits();
    info.srcFloat = r.isFloat();
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
