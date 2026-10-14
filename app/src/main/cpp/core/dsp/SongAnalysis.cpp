#include "dsp/SongAnalysis.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "dsp/Fft.h"
#include "dsp/Pitch.h"
#include "io/MappedAudio.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// Perfiles de tonalidad de Krumhansl y Kessler (1982), desde la tónica.
constexpr float kMajor[12] = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
constexpr float kMinor[12] = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};

float pearson(const float *a, const float *b, int n) {
    double ma = 0, mb = 0;
    for (int i = 0; i < n; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= n;
    mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < n; ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return saa > 0 && sbb > 0 ? static_cast<float>(sab / std::sqrt(saa * sbb)) : 0.0f;
}

// La pista en mono (promedio de los canales), en [from, from + n).
void readMono(const MappedAudio &a, int64_t from, size_t n, float *out) {
    const int ch = a.channels();
    const float *d = a.data() + from * ch;
    for (size_t i = 0; i < n; ++i) {
        float s = 0.0f;
        for (int c = 0; c < ch; ++c) s += d[i * ch + c];
        out[i] = s / static_cast<float>(ch);
    }
}

void normalizeMax(std::array<float, 12> &v) {
    const float m = *std::max_element(v.begin(), v.end());
    if (m > 0)
        for (float &x : v) x /= m;
}

// Perfil de notas del instrumental: magnitud de la FFT repartida por nota (80 Hz – 5 kHz),
// cada cuadro con el mismo peso (si no es silencio).
bool chromaOf(const MappedAudio &a, std::array<float, 12> &chroma, const std::atomic<bool> *cancel,
              const std::function<void(float)> &progress) {
    constexpr int N = 8192, hop = 4096;
    const int sr = a.sampleRate();
    const int64_t frames = a.frames();
    chroma.fill(0.0f);
    if (frames < N) return true;
    FFT fft(N);
    std::vector<double> re(N), im(N), win(N);
    for (int i = 0; i < N; ++i) win[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2 * kPi * i / N);
    std::vector<int> pc(N / 2, -1);
    for (int k = 1; k < N / 2; ++k) {
        const double f = static_cast<double>(k) * sr / N;
        if (f < 80.0 || f > 5000.0) continue;
        const int midi = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(f / 440.0)));
        pc[static_cast<size_t>(k)] = ((midi % 12) + 12) % 12;
    }
    std::vector<float> mono(N);
    const int64_t count = (frames - N) / hop + 1;
    for (int64_t t = 0; t < count; ++t) {
        if (cancel && cancel->load()) return false;
        readMono(a, t * hop, N, mono.data());
        for (int i = 0; i < N; ++i) {
            re[static_cast<size_t>(i)] = mono[static_cast<size_t>(i)] * win[static_cast<size_t>(i)];
            im[static_cast<size_t>(i)] = 0.0;
        }
        fft.forward(re.data(), im.data());
        std::array<double, 12> frame{};
        double total = 0;
        for (int k = 1; k < N / 2; ++k) {
            const int p = pc[static_cast<size_t>(k)];
            if (p < 0) continue;
            const double m = std::hypot(re[static_cast<size_t>(k)], im[static_cast<size_t>(k)]);
            frame[static_cast<size_t>(p)] += m;
            total += m;
        }
        if (total > 1e-3)
            for (int p = 0; p < 12; ++p) chroma[static_cast<size_t>(p)] += static_cast<float>(frame[static_cast<size_t>(p)] / total);
        if (progress && t % 64 == 0) progress(static_cast<float>(t) / count);
    }
    normalizeMax(chroma);
    return true;
}

// Las notas que canta la voz (YIN a 12 kHz, cada 20 ms), pesadas por su volumen.
bool vocalNotes(const MappedAudio &a, std::array<float, 12> &notes, const std::atomic<bool> *cancel,
                const std::function<void(float)> &progress) {
    notes.fill(0.0f);
    const int down = std::max(1, a.sampleRate() / 12000);
    const int sr = a.sampleRate() / down;
    const int64_t n = a.frames() / down;
    std::vector<float> x(static_cast<size_t>(std::max<int64_t>(n, 0)));
    std::vector<float> block(static_cast<size_t>(down) * 4096);
    for (int64_t i = 0; i < n; i += 4096) {
        const int64_t m = std::min<int64_t>(4096, n - i);
        readMono(a, i * down, static_cast<size_t>(m * down), block.data());
        for (int64_t j = 0; j < m; ++j) {
            float s = 0.0f;
            for (int k = 0; k < down; ++k) s += block[static_cast<size_t>(j * down + k)];
            x[static_cast<size_t>(i + j)] = s / static_cast<float>(down);
        }
    }
    PitchDetector det(sr, sr * 43 / 1000, 70.0f, 1000.0f);
    const int hop = sr / 50;
    if (n < det.needed() + 1) return true;
    const int64_t count = (n - det.needed()) / hop + 1;
    std::vector<float> rms(static_cast<size_t>(count));
    for (int64_t t = 0; t < count; ++t) {
        const float *f = &x[static_cast<size_t>(t * hop)];
        double s = 0;
        for (int i = 0; i < det.window(); ++i) s += f[i] * f[i];
        rms[static_cast<size_t>(t)] = static_cast<float>(std::sqrt(s / det.window()));
    }
    std::vector<float> sorted(rms);
    std::sort(sorted.begin(), sorted.end());
    const float loud = sorted[static_cast<size_t>(0.98 * (sorted.size() - 1))];
    const float floor = std::max(loud * 0.03f, 1e-4f);  // 30 dB bajo lo más fuerte
    for (int64_t t = 0; t < count; ++t) {
        if (cancel && cancel->load()) return false;
        if (rms[static_cast<size_t>(t)] < floor) continue;
        const PitchDetector::Result r = det.detect(&x[static_cast<size_t>(t * hop)]);
        if (r.hz <= 0.0f || r.clarity < 0.8f) continue;
        const int midi = static_cast<int>(std::lround(hzToMidi(r.hz)));
        notes[static_cast<size_t>(((midi % 12) + 12) % 12)] += rms[static_cast<size_t>(t)];
        if (progress && t % 256 == 0) progress(static_cast<float>(t) / count);
    }
    normalizeMax(notes);
    return true;
}

// Dónde canta la voz original: volumen cada 100 ms, partes con huecos cortos rellenados.
void vocalMap(const MappedAudio &a, SongInfo &out) {
    const int step = static_cast<int>(a.sampleRate() / song::kEnvelopeRate);
    const int64_t count = a.frames() / step;
    std::vector<float> rms(static_cast<size_t>(count)), mono(static_cast<size_t>(step));
    for (int64_t t = 0; t < count; ++t) {
        readMono(a, t * step, static_cast<size_t>(step), mono.data());
        double s = 0;
        for (float v : mono) s += v * v;
        rms[static_cast<size_t>(t)] = static_cast<float>(std::sqrt(s / step));
    }
    out.envelope.assign(static_cast<size_t>(count), 0.0f);
    out.vocalStart.clear();
    out.vocalEnd.clear();
    if (count == 0) return;
    std::vector<float> sorted(rms);
    std::sort(sorted.begin(), sorted.end());
    const float loud = sorted[static_cast<size_t>(0.99 * (sorted.size() - 1))];
    if (loud < 1e-4f) return;  // sin voz
    std::vector<char> on(static_cast<size_t>(count), 0);
    for (int64_t t = 0; t < count; ++t) {
        const float db = 20.0f * std::log10(std::max(rms[static_cast<size_t>(t)], 1e-9f) / loud);
        out.envelope[static_cast<size_t>(t)] = std::clamp((db + 40.0f) / 40.0f, 0.0f, 1.0f);
        on[static_cast<size_t>(t)] = db > -30.0f && rms[static_cast<size_t>(t)] > 1e-4f;
    }
    // Huecos de hasta medio segundo se rellenan; partes de menos de 0.3 s se quitan.
    const int gap = 5, shortest = 3;
    int64_t t = 0;
    while (t < count) {
        while (t < count && !on[static_cast<size_t>(t)]) ++t;
        if (t >= count) break;
        const int64_t s = t;
        int64_t e = t;
        while (t < count) {
            if (on[static_cast<size_t>(t)]) {
                e = t + 1;
                ++t;
                continue;
            }
            int64_t k = t;
            while (k < count && !on[static_cast<size_t>(k)] && k - t < gap) ++k;
            if (k < count && on[static_cast<size_t>(k)] && k - t < gap) {
                t = k;
                continue;
            }
            break;
        }
        if (e - s >= shortest) {
            out.vocalStart.push_back(static_cast<float>(s) / song::kEnvelopeRate);
            out.vocalEnd.push_back(static_cast<float>(e) / song::kEnvelopeRate);
        }
        t = std::max(t, e);
    }
}

}  // namespace

namespace song {

void keyFromProfile(const std::array<float, 12> &profile, int &key, bool &minor, float &score) {
    key = -1;
    minor = false;
    score = 0.0f;
    float best = -2.0f;
    for (int mode = 0; mode < 2; ++mode) {
        const float *ref = mode ? kMinor : kMajor;
        for (int tonic = 0; tonic < 12; ++tonic) {
            float rot[12];
            for (int p = 0; p < 12; ++p) rot[p] = ref[((p - tonic) % 12 + 12) % 12];
            const float r = pearson(profile.data(), rot, 12);
            if (r > best) {
                best = r;
                key = tonic;
                minor = mode == 1;
            }
        }
    }
    if (*std::max_element(profile.begin(), profile.end()) <= 0.0f) {
        key = -1;
        return;
    }
    score = std::max(0.0f, best);
}

std::array<int, 7> scaleNotes(int key, bool minor) {
    static const int kMaj[7] = {0, 2, 4, 5, 7, 9, 11};
    static const int kMin[7] = {0, 2, 3, 5, 7, 8, 10};
    std::array<int, 7> out{};
    for (int i = 0; i < 7; ++i) out[static_cast<size_t>(i)] = ((minor ? kMin[i] : kMaj[i]) + key) % 12;
    return out;
}

std::vector<float> onsetEnvelope(const float *mono, size_t n, int sampleRate, float &fps) {
    constexpr int N = 2048, kBands = 40;
    const int hop = std::max(1, sampleRate / 100);
    fps = static_cast<float>(sampleRate) / hop;
    std::vector<float> out;
    if (n < static_cast<size_t>(N)) return out;
    // Bandas mel triangulares de 30 Hz a 8 kHz.
    auto mel = [](double f) { return 2595.0 * std::log10(1.0 + f / 700.0); };
    auto hz = [](double m) { return 700.0 * (std::pow(10.0, m / 2595.0) - 1.0); };
    std::vector<double> edges(kBands + 2);
    for (int i = 0; i < kBands + 2; ++i) edges[static_cast<size_t>(i)] = hz(mel(30.0) + (mel(8000.0) - mel(30.0)) * i / (kBands + 1));
    std::vector<std::vector<std::pair<int, float>>> bands(kBands);
    for (int b = 0; b < kBands; ++b)
        for (int k = 1; k < N / 2; ++k) {
            const double f = static_cast<double>(k) * sampleRate / N;
            const double lo = edges[static_cast<size_t>(b)], mid = edges[static_cast<size_t>(b + 1)], hi = edges[static_cast<size_t>(b + 2)];
            double w = 0;
            if (f > lo && f <= mid) w = (f - lo) / (mid - lo);
            else if (f > mid && f < hi) w = (hi - f) / (hi - mid);
            if (w > 0) bands[static_cast<size_t>(b)].emplace_back(k, static_cast<float>(w));
        }
    FFT fft(N);
    std::vector<double> re(N), im(N), win(N);
    for (int i = 0; i < N; ++i) win[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2 * kPi * i / N);
    std::vector<float> prev(kBands, 0.0f), cur(kBands), power(N / 2);
    // Cuadros centrados: el cuadro t mira [t·hop - N/2, t·hop + N/2), así sus tiempos son t / fps.
    const size_t count = n / static_cast<size_t>(hop) + 1;
    out.assign(count, 0.0f);
    for (size_t t = 0; t < count; ++t) {
        for (int i = 0; i < N; ++i) {
            const int64_t j = static_cast<int64_t>(t * hop) + i - N / 2;
            const float x = j >= 0 && j < static_cast<int64_t>(n) ? mono[static_cast<size_t>(j)] : 0.0f;
            re[static_cast<size_t>(i)] = x * win[static_cast<size_t>(i)];
            im[static_cast<size_t>(i)] = 0.0;
        }
        fft.forward(re.data(), im.data());
        for (int k = 0; k < N / 2; ++k)
            power[static_cast<size_t>(k)] = static_cast<float>(re[static_cast<size_t>(k)] * re[static_cast<size_t>(k)] +
                                                               im[static_cast<size_t>(k)] * im[static_cast<size_t>(k)]);
        float flux = 0.0f;
        for (int b = 0; b < kBands; ++b) {
            float e = 0.0f;
            for (const auto &kw : bands[static_cast<size_t>(b)]) e += power[static_cast<size_t>(kw.first)] * kw.second;
            // Amplitud de la banda (1 ≈ un tono a todo volumen) comprimida con log(1 + 100·a): lo
            // que casi no suena no pesa (en dB, un platillo suave sobre silencio pesaría más que el
            // bombo). Lo grave pesa un poco más: bombo y bajo marcan el pulso.
            const float amp = std::sqrt(e) / (0.25f * N);
            cur[static_cast<size_t>(b)] = std::log1p(100.0f * amp);
            if (t > 0)
                flux += std::max(0.0f, cur[static_cast<size_t>(b)] - prev[static_cast<size_t>(b)]) *
                        (2.0f / (1.0f + static_cast<float>(b) / 8.0f));
        }
        out[t] = flux / kBands;
        prev.swap(cur);
    }
    return out;
}

float tempo(const std::vector<float> &onset, float fps) {
    const int n = static_cast<int>(onset.size());
    const int minLag = std::max(2, static_cast<int>(std::floor(60.0f * fps / 240.0f)));
    const int maxLag = static_cast<int>(std::ceil(60.0f * fps / 40.0f));
    if (n < 2 * maxLag) return 0.0f;
    const double mean = std::accumulate(onset.begin(), onset.end(), 0.0) / n;
    std::vector<double> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = onset[static_cast<size_t>(i)] - mean;
    double ac0 = 0;
    for (double v : x) ac0 += v * v;
    if (ac0 <= 0) return 0.0f;
    std::vector<double> score(static_cast<size_t>(maxLag) + 2, 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag) {
        double s = 0;
        for (int i = 0; i + lag < n; ++i) s += x[static_cast<size_t>(i)] * x[static_cast<size_t>(i + lag)];
        const double bpm = 60.0 * fps / lag;
        const double z = std::log2(bpm / 120.0);  // preferencia por tempos cerca de 120 (como librosa)
        score[static_cast<size_t>(lag)] = s / ac0 * std::exp(-0.5 * z * z);
    }
    int best = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (score[static_cast<size_t>(lag)] > score[static_cast<size_t>(best)]) best = lag;
    if (score[static_cast<size_t>(best)] <= 0) return 0.0f;
    double shift = 0;
    if (best > minLag && best < maxLag) {
        const double a = score[static_cast<size_t>(best - 1)], b = score[static_cast<size_t>(best)], c = score[static_cast<size_t>(best + 1)];
        const double den = a - 2 * b + c;
        if (den < 0) shift = std::clamp(0.5 * (a - c) / den, -0.5, 0.5);
    }
    return static_cast<float>(60.0 * fps / (best + shift));
}

std::vector<float> beats(const std::vector<float> &onset, float fps, float bpm) {
    std::vector<float> out;
    const int n = static_cast<int>(onset.size());
    if (bpm <= 0 || n < 4) return out;
    const double period = 60.0 * fps / bpm;
    // Fuerza normalizada y suavizada (gaussiana del ancho del pulso).
    double mean = 0, var = 0;
    for (float v : onset) mean += v;
    mean /= n;
    for (float v : onset) var += (v - mean) * (v - mean);
    const double sd = std::sqrt(var / std::max(1, n - 1));
    if (sd <= 0) return out;
    const int half = static_cast<int>(std::round(period));
    std::vector<double> gauss(static_cast<size_t>(2 * half + 1));
    for (int k = -half; k <= half; ++k) gauss[static_cast<size_t>(k + half)] = std::exp(-0.5 * std::pow(k * 32.0 / period, 2));
    std::vector<double> local(static_cast<size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        double s = 0;
        for (int k = std::max(-half, -i); k <= std::min(half, n - 1 - i); ++k)
            s += onset[static_cast<size_t>(i + k)] * gauss[static_cast<size_t>(k + half)];
        local[static_cast<size_t>(i)] = s / sd;
    }
    // Programación dinámica: cada pulso busca al anterior a ~un período (tightness 100).
    const int from = static_cast<int>(std::round(2 * period)), to = static_cast<int>(std::round(period / 2));
    std::vector<double> txw(static_cast<size_t>(from) + 1, 0.0);  // castigo por alejarse del período
    for (int d = std::max(1, to); d <= from; ++d) txw[static_cast<size_t>(d)] = -100.0 * std::pow(std::log(d / period), 2);
    std::vector<double> cum(static_cast<size_t>(n), 0.0);
    std::vector<int> back(static_cast<size_t>(n), -1);
    const double maxLocal = *std::max_element(local.begin(), local.end());
    bool first = true;
    for (int i = 0; i < n; ++i) {
        double best = -1e300;
        int arg = -1;
        for (int j = std::max(0, i - from); j <= i - to; ++j) {
            const double tx = txw[static_cast<size_t>(i - j)];
            const double c = cum[static_cast<size_t>(j)] + tx;
            if (c > best) {
                best = c;
                arg = j;
            }
        }
        cum[static_cast<size_t>(i)] = local[static_cast<size_t>(i)] + (arg >= 0 ? best : 0.0);
        if (first && local[static_cast<size_t>(i)] < 0.01 * maxLocal) {
            back[static_cast<size_t>(i)] = -1;
        } else {
            back[static_cast<size_t>(i)] = arg;
            first = false;
        }
    }
    // El último pulso: el último máximo local con puntaje alto.
    std::vector<double> peaks;
    for (int i = 1; i + 1 < n; ++i)
        if (cum[static_cast<size_t>(i)] > cum[static_cast<size_t>(i - 1)] && cum[static_cast<size_t>(i)] >= cum[static_cast<size_t>(i + 1)])
            peaks.push_back(cum[static_cast<size_t>(i)]);
    if (peaks.empty()) return out;
    std::vector<double> sp(peaks);
    std::nth_element(sp.begin(), sp.begin() + static_cast<ptrdiff_t>(sp.size() / 2), sp.end());
    const double thr = 0.5 * sp[sp.size() / 2];
    int last = -1;
    for (int i = n - 2; i >= 1; --i)
        if (cum[static_cast<size_t>(i)] > cum[static_cast<size_t>(i - 1)] && cum[static_cast<size_t>(i)] >= cum[static_cast<size_t>(i + 1)] &&
            cum[static_cast<size_t>(i)] >= thr) {
            last = i;
            break;
        }
    if (last < 0) return out;
    std::vector<int> idx;
    for (int b = last; b >= 0; b = back[static_cast<size_t>(b)]) idx.push_back(b);
    std::reverse(idx.begin(), idx.end());
    // Se quitan los pulsos débiles del principio y del final (silencio antes y después).
    double rms = 0;
    for (int b : idx) rms += local[static_cast<size_t>(b)] * local[static_cast<size_t>(b)];
    rms = std::sqrt(rms / idx.size());
    size_t s = 0, e = idx.size();
    while (s < e && local[static_cast<size_t>(idx[s])] < 0.5 * rms) ++s;
    while (e > s && local[static_cast<size_t>(idx[e - 1])] < 0.5 * rms) --e;
    for (size_t k = s; k < e; ++k) out.push_back(static_cast<float>(idx[k] / fps));
    return out;
}

}  // namespace song

bool analyzeSong(const MappedAudio *instrumental, const MappedAudio *vocal, SongInfo &out,
                 const std::atomic<bool> *cancel, const std::function<void(float)> &progress) {
    out = SongInfo();
    auto part = [&](float from, float span) {
        return [&progress, from, span](float p) {
            if (progress) progress(from + span * std::clamp(p, 0.0f, 1.0f));
        };
    };
    if (instrumental && instrumental->frames() > 0) {
        if (!chromaOf(*instrumental, out.chroma, cancel, part(0.0f, 0.3f))) return false;
        // Tempo y pulsos con el instrumental en mono.
        const int64_t n = instrumental->frames();
        std::vector<float> mono(static_cast<size_t>(n));
        readMono(*instrumental, 0, static_cast<size_t>(n), mono.data());
        if (cancel && cancel->load()) return false;
        float fps = 100.0f;
        const std::vector<float> onset = song::onsetEnvelope(mono.data(), mono.size(), instrumental->sampleRate(), fps);
        out.bpm = song::tempo(onset, fps);
        out.beats = song::beats(onset, fps, out.bpm);
        if (progress) progress(0.5f);
    }
    if (vocal && vocal->frames() > 0) {
        if (!vocalNotes(*vocal, out.notes, cancel, part(0.5f, 0.45f))) return false;
        vocalMap(*vocal, out);
    }
    // Tonalidad: los acordes del instrumental y las notas de la voz, juntos.
    std::array<float, 12> profile{};
    const bool hasChroma = *std::max_element(out.chroma.begin(), out.chroma.end()) > 0;
    const bool hasNotes = *std::max_element(out.notes.begin(), out.notes.end()) > 0;
    for (int p = 0; p < 12; ++p)
        profile[static_cast<size_t>(p)] = (hasChroma ? out.chroma[static_cast<size_t>(p)] : 0.0f) +
                                          (hasNotes ? out.notes[static_cast<size_t>(p)] : 0.0f);
    song::keyFromProfile(profile, out.key, out.minor, out.keyScore);
    if (progress) progress(1.0f);
    return true;
}
