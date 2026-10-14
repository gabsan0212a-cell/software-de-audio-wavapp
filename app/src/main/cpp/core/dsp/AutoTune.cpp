#include "dsp/AutoTune.h"

#include <algorithm>
#include <cmath>

#include "dsp/Pitch.h"

namespace {

constexpr float kHysteresis = 0.2f;   // semitonos de ventaja para la nota actual
constexpr float kReleaseMs = 30.0f;   // sin nota, la corrección vuelve a cero en este tiempo

// Notas (0..11) de cada escala, a partir de la tónica.
constexpr int kMajor[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kMinor[7] = {0, 2, 3, 5, 7, 8, 10};

}  // namespace

AutoTune::AutoTune(int sampleRate) : mRate(sampleRate), mTracker(sampleRate), mShifter(sampleRate) {}

void AutoTune::reset() {
    mTracker.reset();
    mShifter.reset();
    mNote = -1;
    mCorr = 0.0f;
    mInMidi.store(0.0f);
    mOutMidi.store(0.0f);
    mInfoNote.store(-1);
    mLatency.store(mShifter.delay());
}

bool AutoTune::inScale(int note, int key, int scale) {
    if (scale == kScaleChromatic) return true;
    const int pc = ((note - key) % 12 + 12) % 12;
    const int *s = scale == kScaleMajor ? kMajor : kMinor;
    for (int i = 0; i < 7; ++i)
        if (s[i] == pc) return true;
    return false;
}

int AutoTune::nearestNote(float midi, int key, int scale, int current) {
    int best = -1;
    float bestD = 1e9f;
    const int c = static_cast<int>(std::lround(midi));
    for (int n = c - 2; n <= c + 2; ++n) {
        if (!inScale(n, key, scale)) continue;
        const float d = std::fabs(midi - static_cast<float>(n));
        if (d < bestD) {
            bestD = d;
            best = n;
        }
    }
    // La nota actual se queda mientras siga casi igual de cerca (no salta entre dos notas).
    if (current >= 0 && inScale(current, key, scale) && std::fabs(midi - static_cast<float>(current)) < bestD + kHysteresis)
        return current;
    return best;
}

void AutoTune::process(float *x, int n, const TuneSettings &s, bool quiet) {
    const int key = ((s.key % 12) + 12) % 12;
    const int scale = std::clamp(s.scale, 0, kScaleCount - 1);
    const float amount = std::clamp(s.amount, 0.0f, 1.0f);
    for (int done = 0; done < n;) {
        const int k = std::min(kSub, n - done);
        float *b = x + done;
        mTracker.process(b, k);
        float target = 0.0f;
        float inMidi = 0.0f;
        if (mTracker.voiced()) {
            inMidi = hzToMidi(mTracker.hz());
            mNote = nearestNote(inMidi, key, scale, mNote);
            if (mNote >= 0) target = (static_cast<float>(mNote) - inMidi) * amount;
        } else {
            mNote = -1;
        }
        if (!s.correct) target = 0.0f;
        // La corrección va hacia la nota con la velocidad elegida (sin nota, vuelve a cero).
        const float tau = mTracker.voiced() ? s.speedMs : kReleaseMs;
        const float a = tau <= 0.0f ? 1.0f : 1.0f - std::exp(-static_cast<float>(k) / (tau * 0.001f * mRate));
        mCorr += (target - mCorr) * a;
        if (std::fabs(mCorr) < 1e-5f) mCorr = 0.0f;

        // Siempre pasa por el cambiador (con la corrección en cero apenas retrasa ~1 ms).
        const bool silent = quiet || mTracker.levelDb() < PitchTracker::kFloorDb - 5.0f;
        mShifter.process(b, mTmp, k, std::exp2(mCorr / 12.0f), mTracker.period(), silent);
        std::copy(mTmp, mTmp + k, b);
        mInMidi.store(inMidi, std::memory_order_relaxed);
        mOutMidi.store(inMidi > 0.0f ? inMidi + mCorr : 0.0f, std::memory_order_relaxed);
        mInfoNote.store(mNote, std::memory_order_relaxed);
        mLatency.store(mShifter.delay(), std::memory_order_relaxed);
        done += k;
    }
}

AutoTune::Info AutoTune::info() const {
    Info i;
    i.inputMidi = mInMidi.load(std::memory_order_relaxed);
    i.note = mInfoNote.load(std::memory_order_relaxed);
    i.outputMidi = mOutMidi.load(std::memory_order_relaxed);
    return i;
}
