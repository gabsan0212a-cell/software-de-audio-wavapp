#include "engine/Automation.h"

#include <algorithm>
#include <cmath>

namespace autom {

namespace {
constexpr float kVolSpan = kVolMax - kVolMin;  // 66 dB

// Distancia (en la escala del carril) del punto i a la recta entre a y b.
float deviation(const std::vector<PosPoint> &p, size_t a, size_t b, size_t i) {
    const double span = static_cast<double>(p[b].frame - p[a].frame);
    const double t = span > 0 ? (p[i].frame - p[a].frame) / span : 0.0;
    const double line = p[a].pos + (p[b].pos - p[a].pos) * t;
    return static_cast<float>(std::fabs(p[i].pos - line));
}

// Ramer–Douglas–Peucker sin recursión (las tomas largas tienen miles de puntos).
void rdp(const std::vector<PosPoint> &p, float tol, std::vector<bool> &keep) {
    std::vector<std::pair<size_t, size_t>> stack = {{0, p.size() - 1}};
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1) continue;
        float worst = -1.0f;
        size_t at = a;
        for (size_t i = a + 1; i < b; ++i) {
            const float d = deviation(p, a, b, i);
            if (d > worst) {
                worst = d;
                at = i;
            }
        }
        if (worst > tol) {
            keep[at] = true;
            stack.push_back({a, at});
            stack.push_back({at, b});
        }
    }
}
}  // namespace

float toPos(int param, float value) {
    if (param == kPan) return std::clamp((100.0f - value) / 200.0f, 0.0f, 1.0f);
    const float v = std::clamp(value, kVolMin, kVolMax);
    return 1.0f - std::sqrt((kVolMax - v) / kVolSpan);
}

float fromPos(int param, float pos) {
    pos = std::clamp(pos, 0.0f, 1.0f);
    if (param == kPan) return 100.0f - 200.0f * pos;
    const float k = 1.0f - pos;
    return kVolMax - kVolSpan * k * k;
}

float clampValue(int param, float value) {
    if (!std::isfinite(value)) value = 0.0f;
    if (param == kPan) return std::round(std::clamp(value, -100.0f, 100.0f));
    return std::clamp(std::round(value * 10.0f) / 10.0f, kVolMin, kVolMax);
}

float gainFromPos(float pos) {
    const float db = fromPos(kVolume, pos);
    return db <= kVolMin + 0.1f ? 0.0f : std::pow(10.0f, db / 20.0f);
}

float panFromPos(float pos) { return std::clamp(1.0f - 2.0f * pos, -1.0f, 1.0f); }

float posAt(const std::vector<PosPoint> &pts, int64_t frame) {
    if (pts.empty()) return 0.0f;
    if (frame <= pts.front().frame) return pts.front().pos;
    if (frame >= pts.back().frame) return pts.back().pos;
    const auto it = std::upper_bound(pts.begin(), pts.end(), frame,
                                     [](int64_t f, const PosPoint &p) { return f < p.frame; });
    const PosPoint &b = *it, &a = *(it - 1);
    const double t = static_cast<double>(frame - a.frame) / static_cast<double>(b.frame - a.frame);
    return static_cast<float>(a.pos + (b.pos - a.pos) * t);
}

std::vector<PosPoint> toPosPoints(int param, const std::vector<Point> &pts) {
    std::vector<PosPoint> out;
    out.reserve(pts.size());
    for (const Point &p : pts) out.push_back({p.frame, toPos(param, p.value)});
    return out;
}

float valueAt(int param, const std::vector<Point> &pts, int64_t frame, float fallback) {
    if (pts.empty()) return fallback;
    if (frame <= pts.front().frame) return pts.front().value;
    if (frame >= pts.back().frame) return pts.back().value;
    const auto it = std::upper_bound(pts.begin(), pts.end(), frame,
                                     [](int64_t f, const Point &p) { return f < p.frame; });
    const Point &b = *it, &a = *(it - 1);
    const double t = static_cast<double>(frame - a.frame) / static_cast<double>(b.frame - a.frame);
    const float pa = toPos(param, a.value), pb = toPos(param, b.value);
    return fromPos(param, static_cast<float>(pa + (pb - pa) * t));
}

void normalize(int param, std::vector<Point> &pts) {
    for (Point &p : pts) {
        p.frame = std::max<int64_t>(0, p.frame);
        p.value = clampValue(param, p.value);
    }
    std::stable_sort(pts.begin(), pts.end(), [](const Point &a, const Point &b) { return a.frame < b.frame; });
    std::vector<Point> out;
    out.reserve(pts.size());
    for (const Point &p : pts) {
        if (!out.empty() && out.back().frame == p.frame) out.back() = p;
        else out.push_back(p);
    }
    if (static_cast<int>(out.size()) > kMaxPoints) out.resize(kMaxPoints);
    pts = std::move(out);
}

std::vector<Point> simplify(int param, const std::vector<Point> &pts, float tolerance) {
    if (pts.size() <= 2) return pts;
    const std::vector<PosPoint> p = toPosPoints(param, pts);
    std::vector<bool> keep(p.size(), false);
    keep.front() = keep.back() = true;
    rdp(p, tolerance, keep);
    std::vector<Point> out;
    for (size_t i = 0; i < pts.size(); ++i)
        if (keep[i]) out.push_back(pts[i]);
    return out;
}

}  // namespace autom
