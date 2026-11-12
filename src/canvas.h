// Small software rasteriser for 32-bit top-down bitmaps (pixels are 0x00RRGGBB).
// Shapes are anti-aliased from signed distances, so they render identically on
// every machine. No dependencies beyond the C++ standard library.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cv {

struct Color {
    float r = 0, g = 0, b = 0;  // 0..255
    float a = 1;                // 0..1
};

inline Color Hex(uint32_t rgb, float a = 1.0f) {
    Color c;
    c.r = (float)((rgb >> 16) & 0xFF);
    c.g = (float)((rgb >> 8) & 0xFF);
    c.b = (float)(rgb & 0xFF);
    c.a = a;
    return c;
}

inline Color Fade(Color c, float k) {
    c.a *= k;
    return c;
}

inline Color Mix(const Color& p, const Color& q, float t) {
    Color c;
    c.r = p.r + (q.r - p.r) * t;
    c.g = p.g + (q.g - p.g) * t;
    c.b = p.b + (q.b - p.b) * t;
    c.a = p.a + (q.a - p.a) * t;
    return c;
}

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline uint32_t Pack(const Color& c) {
    return ((uint32_t)(c.r + 0.5f) << 16) | ((uint32_t)(c.g + 0.5f) << 8) | (uint32_t)(c.b + 0.5f);
}

inline void Blend(uint32_t& dst, const Color& c, float coverage) {
    const float a = c.a * coverage;
    if (a < 0.004f) return;
    if (a > 0.996f) {
        dst = Pack(c);
        return;
    }
    const float dr = (float)((dst >> 16) & 0xFF), dg = (float)((dst >> 8) & 0xFF), db = (float)(dst & 0xFF);
    dst = ((uint32_t)(dr + (c.r - dr) * a + 0.5f) << 16) | ((uint32_t)(dg + (c.g - dg) * a + 0.5f) << 8) |
          (uint32_t)(db + (c.b - db) * a + 0.5f);
}

struct Pt {
    float x, y;
};

// A pixel buffer plus a clip rectangle. Nothing is ever drawn outside the clip.
struct Surface {
    uint32_t* px = nullptr;
    int w = 0, h = 0;
    int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;  // clip, half-open

    void attach(uint32_t* pixels, int width, int height) {
        px = pixels;
        w = width;
        h = height;
        resetClip();
    }
    void resetClip() {
        cx0 = 0;
        cy0 = 0;
        cx1 = w;
        cy1 = h;
    }
    void setClip(int x0, int y0, int x1, int y1) {
        cx0 = std::max(0, std::min(w, x0));
        cy0 = std::max(0, std::min(h, y0));
        cx1 = std::max(cx0, std::min(w, x1));
        cy1 = std::max(cy0, std::min(h, y1));
    }
    bool clipEmpty() const { return cx0 >= cx1 || cy0 >= cy1; }
    bool touches(float x, float y, float width, float height) const {
        return x < (float)cx1 && y < (float)cy1 && x + width > (float)cx0 && y + height > (float)cy0;
    }
    uint32_t* row(int y) const { return px + (size_t)y * (size_t)w; }
};

// ---------------------------------------------------------------------------
// Axis-aligned rectangles (pixel aligned)
// ---------------------------------------------------------------------------
inline void FillRect(Surface& s, int x, int y, int w, int h, const Color& c) {
    const int x0 = std::max(x, s.cx0), y0 = std::max(y, s.cy0);
    const int x1 = std::min(x + w, s.cx1), y1 = std::min(y + h, s.cy1);
    if (x0 >= x1 || y0 >= y1) return;
    if (c.a > 0.996f) {
        const uint32_t v = Pack(c);
        for (int yy = y0; yy < y1; ++yy) std::fill(s.row(yy) + x0, s.row(yy) + x1, v);
    } else {
        for (int yy = y0; yy < y1; ++yy) {
            uint32_t* row = s.row(yy);
            for (int xx = x0; xx < x1; ++xx) Blend(row[xx], c, 1.0f);
        }
    }
}

// Multiplies every pixel by `keep` (0 = black, 1 = unchanged).
inline void Dim(Surface& s, int x, int y, int w, int h, float keep) {
    const int x0 = std::max(x, s.cx0), y0 = std::max(y, s.cy0);
    const int x1 = std::min(x + w, s.cx1), y1 = std::min(y + h, s.cy1);
    if (x0 >= x1 || y0 >= y1) return;
    const uint32_t k = (uint32_t)(Clamp01(keep) * 256.0f + 0.5f);
    if (k >= 256) return;
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = s.row(yy);
        for (int xx = x0; xx < x1; ++xx) {
            const uint32_t v = row[xx];
            row[xx] = ((((v & 0xFF00FFu) * k) >> 8) & 0xFF00FFu) | ((((v & 0x00FF00u) * k) >> 8) & 0x00FF00u);
        }
    }
}

// ---------------------------------------------------------------------------
// Rounded rectangles
// ---------------------------------------------------------------------------
// Signed distance to a rounded rectangle (negative inside).
struct RRect {
    float cx, cy, hx, hy, r;

    RRect(float x, float y, float w, float h, float radius) {
        r = std::max(0.0f, std::min(radius, std::min(w, h) / 2.0f));
        cx = x + w / 2.0f;
        cy = y + h / 2.0f;
        hx = w / 2.0f - r;
        hy = h / 2.0f - r;
    }

    float dist(float px, float py) const {
        const float qx = std::fabs(px - cx) - hx, qy = std::fabs(py - cy) - hy;
        if (qx <= 0.0f && qy <= 0.0f) return std::max(qx, qy) - r;
        const float ax = std::max(qx, 0.0f), ay = std::max(qy, 0.0f);
        return std::sqrt(ax * ax + ay * ay) - r;
    }

    // Largest |px - cx| on the row `py` whose distance is <= limit (negative: none).
    float reach(float py, float limit) const {
        const float qy = std::fabs(py - cy) - hy;
        const float rr = r + limit;
        if (qy <= 0.0f) return qy > rr ? -1.0f : hx + rr;
        if (rr < qy) return -1.0f;
        return hx + std::sqrt(rr * rr - qy * qy);
    }
};

namespace detail {
// Pixel columns whose centres lie within `reach` of cx; false when there are none.
inline bool Span(float cx, float reach, int clip0, int clip1, int& a, int& b) {
    if (reach < 0.0f) return false;
    a = std::max(clip0, (int)std::ceil(cx - reach - 0.5f));
    b = std::min(clip1 - 1, (int)std::floor(cx + reach - 0.5f));
    return a <= b;
}
}  // namespace detail

// Solid rounded rectangle. A radius of half the size gives a pill or a circle.
inline void FillRoundRect(Surface& s, float x, float y, float w, float h, float radius, const Color& c) {
    if (w <= 0.0f || h <= 0.0f || c.a <= 0.0f || !s.touches(x, y, w, h)) return;
    const RRect rr(x, y, w, h, radius);
    const int y0 = std::max(s.cy0, (int)std::floor(y)), y1 = std::min(s.cy1, (int)std::ceil(y + h));
    const bool opaque = c.a > 0.996f;
    const uint32_t packed = Pack(c);
    for (int yy = y0; yy < y1; ++yy) {
        const float py = (float)yy + 0.5f;
        int xa, xb;
        if (!detail::Span(rr.cx, rr.reach(py, 0.5f), s.cx0, s.cx1, xa, xb)) continue;
        int ia, ib;  // fully covered run
        if (!detail::Span(rr.cx, rr.reach(py, -0.5f), xa, xb + 1, ia, ib)) {
            ia = xb + 1;
            ib = xb;
        }
        uint32_t* row = s.row(yy);
        for (int xx = xa; xx < ia; ++xx) Blend(row[xx], c, Clamp01(0.5f - rr.dist((float)xx + 0.5f, py)));
        if (ia <= ib) {
            if (opaque) std::fill(row + ia, row + ib + 1, packed);
            else
                for (int xx = ia; xx <= ib; ++xx) Blend(row[xx], c, 1.0f);
        }
        for (int xx = std::max(ib + 1, xa); xx <= xb; ++xx)
            Blend(row[xx], c, Clamp01(0.5f - rr.dist((float)xx + 0.5f, py)));
    }
}

// Rounded rectangle whose colour comes from `shade(px, py)` (gradients, glows).
template <class Shade>
inline void FillRoundRectFn(Surface& s, float x, float y, float w, float h, float radius, Shade&& shade) {
    if (w <= 0.0f || h <= 0.0f || !s.touches(x, y, w, h)) return;
    const RRect rr(x, y, w, h, radius);
    const int y0 = std::max(s.cy0, (int)std::floor(y)), y1 = std::min(s.cy1, (int)std::ceil(y + h));
    for (int yy = y0; yy < y1; ++yy) {
        const float py = (float)yy + 0.5f;
        int xa, xb;
        if (!detail::Span(rr.cx, rr.reach(py, 0.5f), s.cx0, s.cx1, xa, xb)) continue;
        int ia, ib;
        if (!detail::Span(rr.cx, rr.reach(py, -0.5f), xa, xb + 1, ia, ib)) {
            ia = xb + 1;
            ib = xb;
        }
        uint32_t* row = s.row(yy);
        for (int xx = xa; xx <= xb; ++xx) {
            const float px = (float)xx + 0.5f;
            float cov = 1.0f;
            if (xx < ia || xx > ib) {
                cov = Clamp01(0.5f - rr.dist(px, py));
                if (cov <= 0.0f) continue;
            }
            Blend(row[xx], shade(px, py), cov);
        }
    }
}

// Outline drawn inside the edge of a rounded rectangle.
inline void StrokeRoundRect(Surface& s, float x, float y, float w, float h, float radius, float width,
                            const Color& c) {
    if (w <= 0.0f || h <= 0.0f || width <= 0.0f || c.a <= 0.0f || !s.touches(x, y, w, h)) return;
    const RRect rr(x, y, w, h, radius);
    const int y0 = std::max(s.cy0, (int)std::floor(y)), y1 = std::min(s.cy1, (int)std::ceil(y + h));
    for (int yy = y0; yy < y1; ++yy) {
        const float py = (float)yy + 0.5f;
        int xa, xb;
        if (!detail::Span(rr.cx, rr.reach(py, 0.5f), s.cx0, s.cx1, xa, xb)) continue;
        int ha, hb;  // hollow middle
        if (!detail::Span(rr.cx, rr.reach(py, -0.5f - width), xa, xb + 1, ha, hb)) {
            ha = xb + 1;
            hb = xb;
        }
        uint32_t* row = s.row(yy);
        for (int xx = xa; xx <= xb; ++xx) {
            if (xx >= ha && xx <= hb) {
                xx = hb;
                continue;
            }
            const float d = rr.dist((float)xx + 0.5f, py);
            const float cov = Clamp01(0.5f - d) - Clamp01(0.5f - (d + width));
            if (cov > 0.0f) Blend(row[xx], c, cov);
        }
    }
}

// Soft shadow or glow that fades out over `spread` pixels around a rounded
// rectangle. The inside of the shape is left untouched.
inline void Shadow(Surface& s, float x, float y, float w, float h, float radius, float spread, const Color& c) {
    if (w <= 0.0f || h <= 0.0f || spread <= 0.0f || c.a <= 0.0f) return;
    if (!s.touches(x - spread, y - spread, w + 2.0f * spread, h + 2.0f * spread)) return;
    const RRect rr(x, y, w, h, radius);
    const int y0 = std::max(s.cy0, (int)std::floor(y - spread)), y1 = std::min(s.cy1, (int)std::ceil(y + h + spread));
    for (int yy = y0; yy < y1; ++yy) {
        const float py = (float)yy + 0.5f;
        int xa, xb;
        if (!detail::Span(rr.cx, rr.reach(py, spread), s.cx0, s.cx1, xa, xb)) continue;
        int ia, ib;
        if (!detail::Span(rr.cx, rr.reach(py, -0.5f), xa, xb + 1, ia, ib)) {
            ia = xb + 1;
            ib = xb;
        }
        uint32_t* row = s.row(yy);
        for (int xx = xa; xx <= xb; ++xx) {
            if (xx >= ia && xx <= ib) {
                xx = ib;
                continue;
            }
            const float d = rr.dist((float)xx + 0.5f, py);
            const float t = 1.0f - std::max(d, 0.0f) / spread;
            if (t > 0.0f) Blend(row[xx], c, t * t);
        }
    }
}

inline void FillCircle(Surface& s, float cx, float cy, float radius, const Color& c) {
    FillRoundRect(s, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, radius, c);
}

// Round glow: full strength at the centre, fading to nothing at `radius`.
inline void Glow(Surface& s, float cx, float cy, float radius, const Color& c) {
    if (radius <= 0.0f || c.a <= 0.0f || !s.touches(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f)) return;
    const int y0 = std::max(s.cy0, (int)std::floor(cy - radius)), y1 = std::min(s.cy1, (int)std::ceil(cy + radius));
    const int x0 = std::max(s.cx0, (int)std::floor(cx - radius)), x1 = std::min(s.cx1, (int)std::ceil(cx + radius));
    const float inv = 1.0f / radius;
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = s.row(yy);
        const float dy = (float)yy + 0.5f - cy;
        for (int xx = x0; xx < x1; ++xx) {
            const float dx = (float)xx + 0.5f - cx;
            const float t = 1.0f - std::sqrt(dx * dx + dy * dy) * inv;
            if (t > 0.0f) Blend(row[xx], c, t * t);
        }
    }
}

// ---------------------------------------------------------------------------
// Arcs, polylines and convex polygons
// ---------------------------------------------------------------------------
// Circular arc with round caps. Angles are in radians, measured clockwise from
// 3 o'clock (the y axis points down); `sweep` must be positive.
inline void StrokeArc(Surface& s, float cx, float cy, float radius, float width, float start, float sweep,
                      const Color& c) {
    if (radius <= 0.0f || width <= 0.0f || sweep <= 0.0f || c.a <= 0.0f) return;
    const float half = width / 2.0f, ext = radius + half + 1.0f;
    if (!s.touches(cx - ext, cy - ext, ext * 2.0f, ext * 2.0f)) return;
    const float twoPi = 6.28318531f;
    const bool full = sweep >= twoPi - 1e-4f;
    const float ax = cx + radius * std::cos(start), ay = cy + radius * std::sin(start);
    const float bx = cx + radius * std::cos(start + sweep), by = cy + radius * std::sin(start + sweep);
    const int y0 = std::max(s.cy0, (int)std::floor(cy - ext)), y1 = std::min(s.cy1, (int)std::ceil(cy + ext));
    const int x0 = std::max(s.cx0, (int)std::floor(cx - ext)), x1 = std::min(s.cx1, (int)std::ceil(cx + ext));
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = s.row(yy);
        const float py = (float)yy + 0.5f, dy = py - cy;
        for (int xx = x0; xx < x1; ++xx) {
            const float px = (float)xx + 0.5f, dx = px - cx;
            const float band = std::fabs(std::sqrt(dx * dx + dy * dy) - radius);
            if (band > half + 0.5f) continue;  // the caps lie inside this band as well
            float d = band;
            if (!full) {
                float ang = std::atan2(dy, dx) - start;
                ang -= twoPi * std::floor(ang / twoPi);
                if (ang > sweep) {
                    const float da = std::sqrt((px - ax) * (px - ax) + (py - ay) * (py - ay));
                    const float db = std::sqrt((px - bx) * (px - bx) + (py - by) * (py - by));
                    d = std::min(da, db);
                }
            }
            const float cov = 0.5f - (d - half);
            if (cov > 0.0f) Blend(row[xx], c, Clamp01(cov));
        }
    }
}

// Stroke along a polyline, with round joins and caps.
inline void StrokePath(Surface& s, const Pt* pts, int n, bool closed, float width, const Color& c) {
    if (n < 2 || width <= 0.0f || c.a <= 0.0f) return;
    const float half = width / 2.0f;
    float minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
    for (int i = 1; i < n; ++i) {
        minX = std::min(minX, pts[i].x);
        maxX = std::max(maxX, pts[i].x);
        minY = std::min(minY, pts[i].y);
        maxY = std::max(maxY, pts[i].y);
    }
    const int x0 = std::max(s.cx0, (int)std::floor(minX - half - 1.0f));
    const int y0 = std::max(s.cy0, (int)std::floor(minY - half - 1.0f));
    const int x1 = std::min(s.cx1, (int)std::ceil(maxX + half + 1.0f));
    const int y1 = std::min(s.cy1, (int)std::ceil(maxY + half + 1.0f));
    if (x0 >= x1 || y0 >= y1) return;
    const int segs = closed ? n : n - 1;
    const float reach = (half + 0.5f) * (half + 0.5f);
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = s.row(yy);
        const float py = (float)yy + 0.5f;
        for (int xx = x0; xx < x1; ++xx) {
            const float px = (float)xx + 0.5f;
            float best = 1e12f;
            for (int i = 0; i < segs; ++i) {
                const Pt& a = pts[i];
                const Pt& b = pts[(i + 1) % n];
                const float vx = b.x - a.x, vy = b.y - a.y;
                const float wx = px - a.x, wy = py - a.y;
                const float len2 = vx * vx + vy * vy;
                float t = len2 > 0.0f ? (wx * vx + wy * vy) / len2 : 0.0f;
                t = Clamp01(t);
                const float ex = wx - t * vx, ey = wy - t * vy;
                best = std::min(best, ex * ex + ey * ey);
            }
            if (best >= reach) continue;
            const float cov = 0.5f - (std::sqrt(best) - half);
            if (cov > 0.0f) Blend(row[xx], c, Clamp01(cov));
        }
    }
}

inline void StrokeLine(Surface& s, float x0, float y0, float x1, float y1, float width, const Color& c) {
    const Pt p[2] = { { x0, y0 }, { x1, y1 } };
    StrokePath(s, p, 2, false, width, c);
}

// Filled convex polygon.
inline void FillConvex(Surface& s, const Pt* pts, int n, const Color& c) {
    if (n < 3 || c.a <= 0.0f) return;
    float area = 0.0f;
    float minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
    for (int i = 0; i < n; ++i) {
        const Pt& a = pts[i];
        const Pt& b = pts[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
        minX = std::min(minX, a.x);
        maxX = std::max(maxX, a.x);
        minY = std::min(minY, a.y);
        maxY = std::max(maxY, a.y);
    }
    const float sign = area >= 0.0f ? 1.0f : -1.0f;
    const int x0 = std::max(s.cx0, (int)std::floor(minX - 1.0f)), y0 = std::max(s.cy0, (int)std::floor(minY - 1.0f));
    const int x1 = std::min(s.cx1, (int)std::ceil(maxX + 1.0f)), y1 = std::min(s.cy1, (int)std::ceil(maxY + 1.0f));
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = s.row(yy);
        const float py = (float)yy + 0.5f;
        for (int xx = x0; xx < x1; ++xx) {
            const float px = (float)xx + 0.5f;
            float d = -1e12f;  // largest signed distance to an edge (negative inside)
            for (int i = 0; i < n; ++i) {
                const Pt& a = pts[i];
                const Pt& b = pts[(i + 1) % n];
                const float ex = b.x - a.x, ey = b.y - a.y;
                const float len = std::sqrt(ex * ex + ey * ey);
                if (len <= 0.0f) continue;
                d = std::max(d, ((px - a.x) * ey - (py - a.y) * ex) / len * sign);
            }
            const float cov = 0.5f - d;
            if (cov > 0.0f) Blend(row[xx], c, Clamp01(cov));
        }
    }
}

inline void AddArc(std::vector<Pt>& out, float cx, float cy, float radius, float startDeg, float sweepDeg,
                   int steps) {
    for (int i = 0; i <= steps; ++i) {
        const float a = (startDeg + sweepDeg * (float)i / (float)steps) * 3.14159265f / 180.0f;
        out.push_back({ cx + radius * std::cos(a), cy + radius * std::sin(a) });
    }
}

inline void AddBezier(std::vector<Pt>& out, Pt p0, Pt c1, Pt c2, Pt p3, int steps) {
    for (int i = 1; i <= steps; ++i) {
        const float t = (float)i / (float)steps, m = 1.0f - t;
        const float b0 = m * m * m, b1 = 3.0f * m * m * t, b2 = 3.0f * m * t * t, b3 = t * t * t;
        out.push_back({ b0 * p0.x + b1 * c1.x + b2 * c2.x + b3 * p3.x, b0 * p0.y + b1 * c1.y + b2 * c2.y + b3 * p3.y });
    }
}

// Hue in degrees, saturation and lightness in 0..1.
inline Color Hsl(float h, float sat, float light, float a = 1.0f) {
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    const float ch = (1.0f - std::fabs(2.0f * light - 1.0f)) * sat;
    const float hp = h / 60.0f;
    const float x = ch * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if (hp < 1) { r = ch; g = x; }
    else if (hp < 2) { r = x; g = ch; }
    else if (hp < 3) { g = ch; b = x; }
    else if (hp < 4) { g = x; b = ch; }
    else if (hp < 5) { r = x; b = ch; }
    else { r = ch; b = x; }
    const float m = light - ch / 2.0f;
    Color c;
    c.r = Clamp01(r + m) * 255.0f;
    c.g = Clamp01(g + m) * 255.0f;
    c.b = Clamp01(b + m) * 255.0f;
    c.a = a;
    return c;
}

}  // namespace cv
