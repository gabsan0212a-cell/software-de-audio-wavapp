#include "project/Edit.h"

#include <algorithm>
#include <cstdlib>

namespace edit {

namespace {
Region fitFades(Region r) {
    r.fadeIn = std::min(r.fadeIn, r.length);
    r.fadeOut = std::min(r.fadeOut, r.length - r.fadeIn);
    return r;
}
}  // namespace

Region move(const Region &r, int64_t start) {
    Region o = r;
    o.start = std::max<int64_t>(0, start);
    return o;
}

Region trimStart(const Region &r, int64_t newStart) {
    const int64_t lo = std::max<int64_t>(r.start - r.src, 0);  // no antes del principio del archivo
    const int64_t hi = std::max(lo, r.end() - std::min(kMinLength, r.length));
    const int64_t n = std::clamp(newStart, lo, hi);
    const int64_t d = n - r.start;
    Region o = r;
    o.start = n;
    o.src = r.src + d;
    o.length = r.length - d;
    return fitFades(o);
}

Region trimEnd(const Region &r, int64_t newEnd, int64_t fileFrames) {
    const int64_t hi = r.start + (fileFrames - r.src);
    const int64_t lo = std::min(hi, r.start + std::min(kMinLength, r.length));
    const int64_t n = std::clamp(newEnd, lo, hi);
    Region o = r;
    o.length = n - r.start;
    return fitFades(o);
}

Region setFadeIn(const Region &r, int64_t frames) {
    Region o = r;
    o.fadeIn = std::clamp<int64_t>(frames, 0, r.length - r.fadeOut);
    return o;
}

Region setFadeOut(const Region &r, int64_t frames) {
    Region o = r;
    o.fadeOut = std::clamp<int64_t>(frames, 0, r.length - r.fadeIn);
    return o;
}

bool cut(const Region &r, int64_t at, Region &left, Region &right) {
    if (at < r.start + kMinLength || at > r.end() - kMinLength) return false;
    const int64_t l = at - r.start;
    left = r;
    left.length = l;
    left.fadeIn = std::min(r.fadeIn, l);
    left.fadeOut = 0;
    right = Region{at, r.src + l, r.length - l, 0, std::min(r.fadeOut, r.length - l)};
    return true;
}

int64_t snapOffset(const std::vector<int64_t> &edges, const std::vector<int64_t> &points, int64_t threshold) {
    if (threshold <= 0) return 0;
    int64_t best = 0, dist = threshold + 1;
    for (int64_t e : edges) {
        for (int64_t p : points) {
            const int64_t d = std::llabs(p - e);
            if (d < dist) {
                dist = d;
                best = p - e;
            }
        }
    }
    return dist <= threshold ? best : 0;
}

}  // namespace edit
