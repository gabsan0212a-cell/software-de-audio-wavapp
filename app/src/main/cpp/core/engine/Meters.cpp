#include "engine/Meters.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kFloor = -120.0f;

float toDb(float x) { return x > 1e-6f ? 20.0f * std::log10(x) : kFloor; }
}  // namespace

void MeterBallistics::reset() {
    for (auto &t : mTrack) t[0] = t[1] = kFloor;
    mL = mR = mTickL = mTickR = mMaxL = mMaxR = kFloor;
    mReduction = 0.0f;
    mClipL = mClipR = false;
    mTickTimeL = mTickTimeR = mLast = 0;
}

void MeterBallistics::update(const float *raw, int64_t nowMs) {
    const float dt = mLast == 0 ? 0.016f : static_cast<float>(std::clamp<int64_t>(nowMs - mLast, 0, 200)) / 1000.0f;
    mLast = nowMs;
    const float fallTrack = 45.0f * dt, fallMaster = 36.0f * dt;
    for (int s = 0; s < kMaxTracks; ++s) {
        mTrack[s][0] = std::max(toDb(raw[3 + 2 * s]), mTrack[s][0] - fallTrack);
        mTrack[s][1] = std::max(toDb(raw[4 + 2 * s]), mTrack[s][1] - fallTrack);
    }
    const float dl = toDb(raw[0]), dr = toDb(raw[1]);
    mL = std::max(dl, mL - fallMaster);
    mR = std::max(dr, mR - fallMaster);
    if (dl >= mTickL) {
        mTickL = dl;
        mTickTimeL = nowMs;
    } else if (nowMs - mTickTimeL > 1200) {
        mTickL = std::max(dl, mTickL - 15.0f * dt);
    }
    if (dr >= mTickR) {
        mTickR = dr;
        mTickTimeR = nowMs;
    } else if (nowMs - mTickTimeR > 1200) {
        mTickR = std::max(dr, mTickR - 15.0f * dt);
    }
    mMaxL = std::max(mMaxL, dl);
    mMaxR = std::max(mMaxR, dr);
    if (raw[0] >= 0.999f) mClipL = true;
    if (raw[1] >= 0.999f) mClipR = true;
    mReduction = raw[2];
}

bool MeterBallistics::moving() const {
    if (mL > -119.0f || mR > -119.0f) return true;
    for (const auto &t : mTrack)
        if (t[0] > -119.0f || t[1] > -119.0f) return true;
    return false;
}

void MeterBallistics::read(float *out) const {
    for (int s = 0; s < kMaxTracks; ++s) {
        out[2 * s] = mTrack[s][0];
        out[2 * s + 1] = mTrack[s][1];
    }
    out[kMasterL] = mL;
    out[kMasterR] = mR;
    out[kTickL] = mTickL;
    out[kTickR] = mTickR;
    out[kMaxL] = mMaxL;
    out[kMaxR] = mMaxR;
    out[kClipL] = mClipL ? 1.0f : 0.0f;
    out[kClipR] = mClipR ? 1.0f : 0.0f;
    out[kReduction] = mReduction;
    out[kMoving] = moving() ? 1.0f : 0.0f;
}

void MeterBallistics::clearClip() {
    mClipL = mClipR = false;
    mMaxL = mMaxR = kFloor;
}
