#include "inst/Pattern.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

#include "inst/Patches.h"

namespace inst {

namespace {

constexpr int kMajor[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kMinor[7] = {0, 2, 3, 5, 7, 8, 10};
// Como los escribe la pantalla.
const char *const kNames[12] = {"Do", "Do\u266F", "Re", "Mi\u266D", "Mi", "Fa", "Fa\u266F", "Sol", "La\u266D", "La", "Si\u266D", "Si"};

const int *scaleOf(bool minor) { return minor ? kMinor : kMajor; }

// La nota del grado `degree` (puede pasar de 7: sube de octava) lo más cerca de `near`.
int degreePitch(int key, bool minor, int degree, int near) {
    const int d = ((degree % 7) + 7) % 7;
    int p = key + scaleOf(minor)[d];
    while (p < near - 6) p += 12;
    while (p > near + 6) p -= 12;
    return p;
}

int scaleDegree(int pitch, int key, bool minor) {
    const int pc = ((pitch - key) % 12 + 12) % 12;
    for (int i = 0; i < 7; ++i)
        if (scaleOf(minor)[i] == pc) return i;
    return -1;
}

struct Event {
    double frame;
    bool on;
    int pitch;
    float vel;
    uint32_t seed;
};

uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    return h | 1u;
}

// Patrones de batería: una cadena de 16 pasos por pieza ('x' fuerte, 'o' suave, '.' nada).
struct DrumLine {
    int piece;
    const char *steps;
};

void addLine(Pattern &p, int bar, const DrumLine &l, int velHi, int velLo) {
    for (int s = 0; s < kStepsPerBar && l.steps[s]; ++s) {
        const char c = l.steps[s];
        if (c != 'x' && c != 'o') continue;
        p.notes.push_back({bar * kBarTicks + s * kStepTicks, kStepTicks, l.piece, c == 'x' ? velHi : velLo});
    }
}

// Redoble (hi-hat en semicorcheas partidas) en los últimos pasos de un compás.
void addRoll(Pattern &p, int bar, int fromStep, int split, int piece) {
    for (int s = fromStep; s < kStepsPerBar; ++s)
        for (int k = 0; k < split; ++k)
            p.notes.push_back({bar * kBarTicks + s * kStepTicks + k * kStepTicks / split, kStepTicks / split, piece,
                               k == 0 ? 92 : 70});
}

Pattern drums(int kitIndex, int bars, int variant) {
    Pattern p;
    p.bars = bars;
    std::mt19937 rng(static_cast<uint32_t>(variant * 7919 + kitIndex));
    auto pick = [&](std::initializer_list<const char *> l) {
        std::vector<const char *> v(l);
        return v[static_cast<size_t>(variant) % v.size()];
    };
    for (int b = 0; b < bars; ++b) {
        const bool last = b == bars - 1 && bars > 1;
        switch (kitIndex) {
            case 0: {  // Trap (medio tiempo)
                addLine(p, b, {0, pick({"x.........x.....", "x......x..x.....", "x.....x...x..x.."})}, 118, 90);
                addLine(p, b, {1, "........x......."}, 110, 80);
                addLine(p, b, {2, "........x......."}, 105, 80);
                if (last) {
                    addLine(p, b, {3, "x.x.x.x.x.x....."}, 96, 70);
                    addRoll(p, b, 12, (variant % 2) ? 3 : 2, 3);
                } else {
                    addLine(p, b, {3, "x.x.x.x.x.x.x.x."}, 96, 70);
                }
                if (b % 2 == 1) addLine(p, b, {4, "..............x."}, 80, 60);
                break;
            }
            case 1:  // Boom bap
                addLine(p, b, {0, pick({"x......x..x.....", "x.........x.x...", "x..x......x....."})}, 120, 95);
                addLine(p, b, {1, "....x.......x..."}, 118, 80);
                addLine(p, b, {3, "x.x.x.x.x.x.x.x."}, 92, 70);
                if (last) addLine(p, b, {7, ".........o....x."}, 90, 70);
                p.swing = 0.35f;
                break;
            case 2:  // Reggaetón (dembow)
                addLine(p, b, {0, "x...x...x...x..."}, 118, 90);
                addLine(p, b, {1, "...x..x....x..x."}, 108, 85);
                addLine(p, b, {3, pick({"x.x.x.x.x.x.x.x.", "..x...x...x...x."})}, 84, 60);
                addLine(p, b, {7, "xoooxoooxoooxooo"}, 80, 50);
                break;
            case 3:  // Pop / rock
                addLine(p, b, {0, pick({"x.......x.x.....", "x.......x.......", "x..x....x.x....."})}, 118, 90);
                addLine(p, b, {1, "....x.......x..."}, 115, 80);
                addLine(p, b, {3, "x.x.x.x.x.x.x.x."}, 90, 70);
                if (b == 0) addLine(p, b, {6, "x..............."}, 100, 80);
                if (last) addLine(p, b, {5, "............xx.."}, 110, 90);
                break;
            case 4:  // Electrónica
                addLine(p, b, {0, "x...x...x...x..."}, 120, 90);
                addLine(p, b, {2, "....x.......x..."}, 112, 80);
                addLine(p, b, {4, "..x...x...x...x."}, 90, 70);
                addLine(p, b, {3, "oxoooxoooxoooxoo"}, 80, 52);
                break;
            case 5:  // Lo-fi
                addLine(p, b, {0, pick({"x.....x...x.....", "x......x..x.....", "x.........x..x.."})}, 105, 85);
                addLine(p, b, {1, "....x.......x..."}, 100, 75);
                addLine(p, b, {3, "x.x.x.x.x.x.x.x."}, 80, 60);
                addLine(p, b, {7, "..o...o...o...o."}, 70, 50);
                p.swing = 0.45f;
                break;
            default:  // Indie / alternativo: caja en 2 y 4, hi-hat en corcheas y pandereta a contratiempo
                addLine(p, b, {0, pick({"x......x..x.....", "x.....x.x.......", "x......xx.x....."})}, 116, 92);
                addLine(p, b, {1, "....x.......x..."}, 114, 80);
                addLine(p, b, {3, last ? "x.o.x.o.x.o....." : "x.o.x.o.x.o.x.o."}, 84, 60);
                addLine(p, b, {7, "..x...x...x...x."}, 74, 56);
                if (b == 0) addLine(p, b, {6, "x..............."}, 96, 80);
                if (last) {
                    addLine(p, b, {4, "............x..."}, 80, 60);
                    addLine(p, b, {5, "..............xx"}, 100, 86);
                }
                break;
        }
    }
    (void)rng;
    return p;
}

std::vector<int> progression(bool minor, int variant) {
    // Menor: i–VI–III–VII, i–iv–v–i, i–VII–VI–VII, i–VI–iv–v, i–iv–III–VI (la del indie oscuro).
    static const std::vector<std::vector<int>> mi = {{0, 5, 2, 6}, {0, 3, 4, 0}, {0, 6, 5, 6}, {0, 5, 3, 4}, {0, 3, 2, 5}};
    static const std::vector<std::vector<int>> ma = {{0, 4, 5, 3}, {0, 5, 3, 4}, {0, 3, 0, 4}, {5, 3, 0, 4}};
    const auto &l = minor ? mi : ma;
    return l[static_cast<size_t>(variant) % l.size()];
}

// Notas de la tríada del grado `d` cerca de `near` (la fundamental primero).
std::vector<int> triad(int key, bool minor, int d, int near) {
    const int root = degreePitch(key, minor, d, near);
    std::vector<int> t = {root};
    for (int k : {2, 4}) {
        int q = degreePitch(key, minor, d + k, root + 5);
        while (q <= root) q += 12;
        t.push_back(q);
    }
    return t;
}

void addNote(Pattern &p, int bar, int step, int lenSteps, int pitch, int vel) {
    p.notes.push_back({bar * kBarTicks + step * kStepTicks, std::max(1, lenSteps) * kStepTicks, pitch, vel});
}

// Una nota que se deja sonar termina donde vuelve a tocarse la misma (sin notas encimadas).
void trimOverlaps(Pattern &p) {
    for (Note &n : p.notes)
        for (const Note &o : p.notes)
            if (o.pitch == n.pitch && o.tick > n.tick && o.tick < n.tick + n.len) n.len = o.tick - n.tick;
}

Pattern melodicIdea(const Sound &s, int key, bool minor, int bars, int variant) {
    Pattern p;
    p.bars = bars;
    const std::vector<int> prog = progression(minor, variant);
    std::mt19937 rng(static_cast<uint32_t>(variant * 104729 + s.kind * 31 + s.preset));
    const char *name = presetName(s.kind, s.preset);
    auto is = [&](const char *n) { return std::strcmp(name, n) == 0; };
    for (int b = 0; b < bars; ++b) {
        const int deg = prog[static_cast<size_t>(b) % prog.size()];
        switch (s.kind) {
            case k808: {
                const int root = degreePitch(key, minor, deg, 34);
                const int steps[3] = {0, 7, 10};
                const int lens[3] = {6, 3, 6};
                for (int i = 0; i < 3; ++i) addNote(p, b, steps[i], lens[i], root + (i == 1 && variant % 2 ? 12 : 0), 110);
                break;
            }
            case kBass: {
                const int root = degreePitch(key, minor, deg, 38);
                if (is("Bajo eléctrico")) {
                    const int st[6] = {0, 3, 6, 8, 11, 14};
                    const int ln[6] = {3, 2, 2, 3, 2, 2};
                    for (int i = 0; i < 6; ++i) addNote(p, b, st[i], ln[i], i == 4 ? root + 7 : root, i == 0 ? 112 : 96);
                } else if (is("Bajo indie")) {
                    // Notas largas en la fundamental y la quinta al final, para llegar al siguiente acorde.
                    const int st[5] = {0, 6, 8, 12, 14};
                    const int ln[5] = {6, 2, 4, 2, 2};
                    const int vl[5] = {112, 90, 104, 88, 92};
                    for (int i = 0; i < 5; ++i) addNote(p, b, st[i], ln[i], i == 4 ? root + 7 : root, vl[i]);
                } else {
                    for (int st = 0; st < 16; st += 2)
                        addNote(p, b, st, 2, (st % 8 == 6) ? root + 12 : root, st % 4 == 0 ? 112 : 92);
                }
                break;
            }
            case kGuitar: {
                if (is("Eléctrica soñadora")) {
                    // Arpegio en corcheas que deja sonar las cuerdas: con el chorus y la reverb, flota.
                    const std::vector<int> t = triad(key, minor, deg, 52);
                    const int v[5] = {t[0], t[2], t[0] + 12, t[1] + 12, t[2] + 12};
                    static const int up[8] = {0, 1, 2, 3, 4, 3, 2, 1}, alt[8] = {0, 2, 1, 3, 2, 4, 3, 1};
                    const int *ord = variant % 2 ? alt : up;
                    for (int i = 0; i < 8; ++i) addNote(p, b, 2 * i, 6, v[ord[i]], i == 0 ? 108 : (i % 2 ? 84 : 96));
                } else if (is("Eléctrica con trémolo")) {
                    // Acordes largos: el trémolo los hace latir.
                    std::vector<int> c = triad(key, minor, deg, 55);
                    c.insert(c.begin(), c[0] - 12);
                    for (int st : {0, 8})
                        for (int n : c) addNote(p, b, st, 8, n, st == 0 ? 104 : 90);
                } else if (is("Eléctrica distorsionada") || is("Muteada")) {
                    const int root = degreePitch(key, minor, deg, 45);
                    const std::vector<int> power = {root, root + 7, root + 12};
                    const int stepLen = is("Muteada") ? 1 : 2;
                    for (int st = 0; st < 16; st += stepLen)
                        for (int n : power) addNote(p, b, st, stepLen, n, st % 4 == 0 ? 112 : 90);
                } else {
                    std::vector<int> c = triad(key, minor, deg, 57);
                    c.insert(c.begin(), c[0] - 12);  // la fundamental abajo (suena más llena)
                    c.push_back(c[1] + 12);
                    const int st[5] = {0, 6, 8, 10, 12};
                    const int ln[5] = {4, 2, 2, 2, 4};
                    for (int i = 0; i < 5; ++i)
                        for (int n : c) addNote(p, b, st[i], ln[i], n, i == 0 || i == 2 ? 108 : 86);
                }
                break;
            }
            default: {
                const std::vector<int> c = triad(key, minor, deg, 64);
                if (is("Pluck") || is("Campanas")) {
                    const int up = is("Campanas") ? 12 : 0;
                    const int arp[4] = {c[0], c[1], c[2], c[0] + 12};
                    const int step = is("Campanas") ? 2 : 1;
                    int k = 0;
                    for (int st = 0; st < 16; st += step, ++k) addNote(p, b, st, step, arp[(k + variant) % 4] + up, st % 4 == 0 ? 108 : 88);
                } else if (is("Lead") || is("Lead soñador")) {
                    std::uniform_int_distribution<int> pickTone(0, 2), pickRhythm(0, 2);
                    static const int rh[3][5] = {{0, 3, 6, 10, 12}, {0, 4, 6, 8, 12}, {2, 4, 7, 10, 14}};
                    const int *r = rh[pickRhythm(rng)];
                    for (int i = 0; i < 5; ++i) {
                        const int len = (i < 4 ? r[i + 1] : 16) - r[i];
                        addNote(p, b, r[i], len, c[static_cast<size_t>(pickTone(rng))] + 12, 100);
                    }
                } else if (is("Supersaw")) {
                    const int st[5] = {0, 3, 6, 10, 12};
                    for (int t : st)
                        for (int n : c) addNote(p, b, t, 2, n, 104);
                } else if (is("Piano eléctrico")) {
                    const int st[4] = {0, 6, 8, 14};
                    const int ln[4] = {6, 2, 6, 2};
                    for (int i = 0; i < 4; ++i)
                        for (int n : c) addNote(p, b, st[i], ln[i], n, i % 2 ? 80 : 100);
                } else if (is("Órgano")) {
                    for (int st : {0, 8})
                        for (int n : c) addNote(p, b, st, 8, n, 96);
                } else if (is("Pad oscuro")) {
                    // Abajo: más oscuro, con la fundamental una octava más abajo.
                    const std::vector<int> d = triad(key, minor, deg, 55);
                    addNote(p, b, 0, 16, d[0] - 12, 88);
                    for (int n : d) addNote(p, b, 0, 16, n, 88);
                } else {  // Pad, Cuerdas, Atmósfera
                    for (int n : c) addNote(p, b, 0, 16, n, 92);
                }
                break;
            }
            case kPiano: {
                const std::vector<int> c = triad(key, minor, deg, 62);
                const int bass = degreePitch(key, minor, deg, 45);
                if (is("Piano suave") || is("Piano oscuro")) {
                    // Balada: el bajo en la mano izquierda y el acorde despacio, nota por nota.
                    addNote(p, b, 0, 16, bass, 90);
                    if (variant % 2) {
                        for (int st : {0, 8})
                            for (int n : c) addNote(p, b, st, 8, n, st == 0 ? 82 : 72);
                    } else {
                        const int seq[4] = {c[0], c[1], c[2], c[1]};
                        for (int i = 0; i < 4; ++i) addNote(p, b, 4 * i, 8 - (i == 3 ? 4 : 0), seq[i], i == 0 ? 84 : 74);
                    }
                } else if (is("Piano viejo")) {
                    // Lo-fi: acordes con séptima, el segundo un poco antes del pulso.
                    std::vector<int> c7 = c;
                    int sev = degreePitch(key, minor, deg + 6, c[2] + 3);
                    while (sev <= c[2]) sev += 12;
                    c7.push_back(sev);
                    addNote(p, b, 0, 16, bass, 88);
                    for (int n : c7) addNote(p, b, 0, 6, n, 84);
                    for (int n : c7) addNote(p, b, 7, 9, n, 74);
                } else {
                    // Pop: el bajo en 1 y 3, el acorde en cada pulso.
                    for (int st : {0, 8}) addNote(p, b, st, 8, bass, 96);
                    for (int st = 0; st < 16; st += 4)
                        for (int n : c) addNote(p, b, st, 4, n, st % 8 == 0 ? 96 : 80);
                }
                break;
            }
        }
    }
    trimOverlaps(p);
    return p;
}

}  // namespace

// ---------------------------------------------------------------- patrón

void normalize(Pattern &p, int kind) {
    p.bars = std::clamp(p.bars, 1, kMaxBars);
    p.repeats = std::clamp(p.repeats, 1, kMaxRepeats);
    if (!std::isfinite(p.swing)) p.swing = 0;
    p.swing = std::clamp(std::round(p.swing * 100.0f) / 100.0f, 0.0f, 1.0f);
    const int maxTick = kMaxBars * kBarTicks;
    std::vector<Note> v;
    v.reserve(p.notes.size());
    for (Note n : p.notes) {
        if (n.tick < 0 || n.tick >= maxTick) continue;
        n.len = std::clamp(n.len, 1, maxTick);
        n.vel = std::clamp(n.vel, 1, 127);
        n.pitch = kind == kDrums ? std::clamp(n.pitch, 0, kDrumPieces - 1) : std::clamp(n.pitch, 0, 127);
        v.push_back(n);
    }
    // La última que se agregó gana si hay dos iguales en el mismo pulso.
    std::stable_sort(v.begin(), v.end(), [](const Note &a, const Note &b) {
        return a.tick != b.tick ? a.tick < b.tick : a.pitch < b.pitch;
    });
    std::vector<Note> out;
    out.reserve(v.size());
    for (const Note &n : v) {
        if (!out.empty() && out.back().tick == n.tick && out.back().pitch == n.pitch) out.back() = n;
        else out.push_back(n);
    }
    if (out.size() > static_cast<size_t>(kMaxNotes)) out.resize(kMaxNotes);
    p.notes = std::move(out);
}

int patternTicks(const Pattern &p) { return std::clamp(p.bars, 1, kMaxBars) * kBarTicks; }

double framesPerTick(double bpm, int sampleRate) {
    bpm = std::clamp(std::isfinite(bpm) ? bpm : 90.0, 30.0, 300.0);
    return sampleRate * 60.0 / (bpm * kPpq);
}

double patternFrames(const Pattern &p, double bpm, int sampleRate) {
    return patternTicks(p) * framesPerTick(bpm, sampleRate);
}

int findNote(const Pattern &p, int tick, int pitch) {
    for (size_t i = 0; i < p.notes.size(); ++i) {
        const Note &n = p.notes[i];
        if (n.pitch == pitch && tick >= n.tick && tick < n.tick + n.len) return static_cast<int>(i);
    }
    return -1;
}

std::vector<float> render(const Part &part, double bpm, int sr, const std::atomic<bool> *cancel) {
    const Pattern &pat = part.pattern;
    const Sound sound = clampSound(part.sound);
    const double fpt = framesPerTick(bpm, sr);
    const int ticks = patternTicks(pat);
    const double loop = ticks * fpt;

    // Los eventos de una vuelta (con swing y, en la guitarra, el rasgueo de los acordes).
    std::vector<Event> ev;
    float strumMs = 0;
    if (sound.kind == kGuitar) strumMs = melodic(kGuitar, sound.preset).str.strumMs;
    for (size_t i = 0; i < pat.notes.size(); ++i) {
        const Note &n = pat.notes[i];
        if (n.tick >= ticks) continue;
        double start = n.tick;
        if (n.tick % (2 * kStepTicks) == kStepTicks) start += pat.swing * kStepTicks * 0.5;
        double f = start * fpt;
        if (strumMs > 0) {
            // Las cuerdas de un acorde suenan una tras otra, de la más grave a la más aguda.
            int below = 0;
            for (const Note &o : pat.notes)
                if (o.tick == n.tick && o.pitch < n.pitch) ++below;
            f += below * strumMs * 0.001 * sr;
        }
        const double end = f + std::max(1.0, n.len * fpt - 1.0);
        const uint32_t seed = mix(static_cast<uint32_t>(n.tick), static_cast<uint32_t>(n.pitch * 131 + static_cast<int>(i)));
        ev.push_back({f, true, n.pitch, n.vel / 127.0f, seed});
        ev.push_back({end, false, n.pitch, 0, 0});
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Event &a, const Event &b) {
        if (a.frame != b.frame) return a.frame < b.frame;
        return !a.on && b.on;  // a la misma hora, primero se suelta y después se toca
    });

    // Una vuelta con su cola.
    Engine e(sr);
    e.setSound(sound);
    const int64_t maxTail = static_cast<int64_t>(kMaxTailSeconds * sr);
    std::vector<float> L, R;
    const int64_t loopLen = static_cast<int64_t>(std::llround(loop));
    const int64_t lastEvent = ev.empty() ? 0 : static_cast<int64_t>(std::ceil(ev.back().frame));
    const int64_t limit = std::max(loopLen, lastEvent) + maxTail;
    L.reserve(static_cast<size_t>(std::max(loopLen, lastEvent) + 4 * sr));
    R.reserve(L.capacity());
    constexpr int kBlock = 256;
    float bl[kBlock], br[kBlock];
    int64_t pos = 0;
    size_t k = 0;
    while (pos < limit) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return {};
        while (k < ev.size() && ev[k].frame <= static_cast<double>(pos)) {
            if (ev[k].on) e.noteOn(ev[k].pitch, ev[k].vel, ev[k].seed);
            else e.noteOff(ev[k].pitch);
            ++k;
        }
        int64_t n = kBlock;
        if (k < ev.size()) n = std::min<int64_t>(n, std::max<int64_t>(1, static_cast<int64_t>(std::ceil(ev[k].frame)) - pos));
        n = std::min(n, limit - pos);
        if (k >= ev.size() && pos >= loopLen && !e.active()) break;
        e.render(bl, br, static_cast<int>(n));
        L.insert(L.end(), bl, bl + n);
        R.insert(R.end(), br, br + n);
        pos += n;
    }
    const int64_t once = static_cast<int64_t>(L.size());

    // Las vueltas seguidas: cada una suma su cola sobre las siguientes.
    const int reps = std::clamp(pat.repeats, 1, kMaxRepeats);
    const int64_t lastStart = static_cast<int64_t>(std::llround(loop * (reps - 1)));
    const int64_t total = std::max(static_cast<int64_t>(std::llround(loop * reps)), lastStart + once);
    std::vector<float> out(static_cast<size_t>(total) * 2, 0.0f);
    for (int r = 0; r < reps; ++r) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return {};
        const int64_t off = static_cast<int64_t>(std::llround(loop * r));
        float *o = out.data() + static_cast<size_t>(off) * 2;
        const int64_t n = std::min(once, total - off);
        for (int64_t i = 0; i < n; ++i) {
            o[2 * i] += L[static_cast<size_t>(i)];
            o[2 * i + 1] += R[static_cast<size_t>(i)];
        }
    }
    return out;
}

uint64_t renderHash(const Part &part, double bpm) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    auto add = [&](int64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= static_cast<uint64_t>(v >> (8 * i)) & 0xFF;
            h *= 1099511628211ull;
        }
    };
    add(part.sound.kind);
    add(part.sound.preset);
    for (float v : part.sound.values) add(std::llround(static_cast<double>(v) * 1e4));
    add(part.pattern.bars);
    add(part.pattern.repeats);
    add(std::llround(part.pattern.swing * 1e4));
    for (const Note &n : part.pattern.notes) {
        add(n.tick);
        add(n.len);
        add(n.pitch);
        add(n.vel);
    }
    add(std::llround(bpm * 1e3));
    add(5);  // versión del motor: subirla si cambia cómo suena (así se renderiza de nuevo)
    return h;
}

// ---------------------------------------------------------------- ayudas

bool inScale(int pitch, int key, bool minor) { return scaleDegree(pitch, key, minor) >= 0; }

std::vector<int> chord(int root, int key, bool minor) {
    const int d = scaleDegree(root, key, minor);
    if (d < 0) return {root, root + 4, root + 7};
    std::vector<int> c = {root};
    for (int k : {2, 4}) {
        int q = degreePitch(key, minor, d + k, root + 5);
        while (q <= root) q += 12;
        c.push_back(q);
    }
    return c;
}

std::vector<int> rows(int kind, int key, bool minor, bool scaleOnly) {
    if (kind == kDrums) return {6, 4, 3, 7, 5, 2, 1, 0};
    int lo, hi;
    noteRange(kind, lo, hi);
    std::vector<int> r;
    for (int p = hi; p >= lo; --p)
        if (!scaleOnly || inScale(p, key, minor)) r.push_back(p);
    return r;
}

std::string noteName(int midi) {
    const int pc = ((midi % 12) + 12) % 12;
    return std::string(kNames[pc]) + std::to_string(midi / 12 - 1);
}

std::string keyName(int key, bool minor) {
    return std::string(kNames[((key % 12) + 12) % 12]) + (minor ? " menor" : " mayor");
}

// ---------------------------------------------------------------- estilos

namespace {

// Do = 0 ... Si = 11.
const std::vector<Style> &styles() {
    static const std::vector<Style> v = {
        // Como "Daddy Issues" de The Neighbourhood: lento, en menor, guitarra limpia que flota con
        // chorus y reverb, bajo de notas largas, piano opaco, pad oscuro y batería de banda en una
        // sala (la idea y los sonidos son propios: no copia la canción).
        {"Indie oscuro", "Guitarra que flota, bajo, piano, pad y batería en una sala", 85, 7, true, 4, 0.0f,
         {{kDrums, "Indie / alternativo", 0},
          {kBass, "Bajo indie", -3},
          {kGuitar, "Eléctrica soñadora", -4},
          {kPiano, "Piano oscuro", -7},
          {kSynth, "Pad oscuro", -9}}},
        {"Trap", "808, campanas y pad", 140, 1, true, 0, 0.0f,
         {{kDrums, "Trap", 0}, {k808, "808 clásico", -2}, {kSynth, "Campanas", -9}, {kSynth, "Pad", -12}}},
        {"Reggaetón", "Dembow, bajo, piano y pluck", 95, 9, true, 3, 0.0f,
         {{kDrums, "Reggaetón", 0}, {kBass, "Bajo sintetizado", -4}, {kPiano, "Piano de cola", -8}, {kSynth, "Pluck", -11}}},
        {"Lo-fi", "Piano viejo, bajo y batería con swing", 78, 3, false, 1, 0.45f,
         {{kDrums, "Lo-fi", 0}, {kBass, "Bajo eléctrico", -4}, {kPiano, "Piano viejo", -5}}},
    };
    return v;
}

}  // namespace

int styleCount() { return static_cast<int>(styles().size()); }

const Style &style(int index) { return styles()[static_cast<size_t>(std::clamp(index, 0, styleCount() - 1))]; }

Pattern starter(const Sound &s, int key, bool minor, int bars, int variant) {
    bars = std::clamp(bars, 1, kMaxBars);
    variant = std::max(0, variant);
    Pattern p = s.kind == kDrums ? drums(std::clamp(s.preset, 0, kitCount() - 1), bars, variant)
                                 : melodicIdea(s, ((key % 12) + 12) % 12, minor, bars, variant);
    normalize(p, s.kind);
    return p;
}

}  // namespace inst
