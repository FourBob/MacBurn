// MacBurn – Grundgeometrie. Alle Maße in mm, Ursprung vorne links, y nach hinten.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <vector>

namespace mb {

struct Vec2 {
    double x = 0, y = 0;
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double f) const { return {x * f, y * f}; }
    double length() const { return std::hypot(x, y); }
};

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(const Rect& o) const { return o.x >= x && o.y >= y && o.x + o.w <= x + w && o.y + o.h <= y + h; }
};

// Bis Bézier-Kurven kommen, ist alles eine Polylinie.
struct Polyline {
    std::vector<Vec2> points;
    bool closed = false;

    Rect bounds() const;
    double length() const;

    static Polyline rectangle(double x, double y, double w, double h);
    static Polyline ellipse(double cx, double cy, double rx, double ry, int segments = 96);
};

} // namespace mb
