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

// Affine Abbildung: x' = a·x + c·y + e, y' = b·x + d·y + f
struct Affine {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
    // (*this) * o: erst o, dann this
    Affine operator*(const Affine& o) const {
        return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d,
                a * o.e + c * o.f + e, b * o.e + d * o.f + f};
    }
    bool operator==(const Affine&) const = default;
    static Affine translate(double dx, double dy) { return {1, 0, 0, 1, dx, dy}; }
    static Affine translate(Vec2 d) { return translate(d.x, d.y); }
    static Affine scaleAround(Vec2 p, double sx, double sy) { return {sx, 0, 0, sy, p.x - sx * p.x, p.y - sy * p.y}; }
    static Affine rotateAround(Vec2 p, double radians) {
        double cs = std::cos(radians), sn = std::sin(radians);
        return {cs, sn, -sn, cs, p.x - cs * p.x + sn * p.y, p.y - sn * p.x - cs * p.y};
    }
    // Neigen um den Punkt p: x' = x + tx·(y − p.y), y' = y + ty·(x − p.x)
    static Affine skewAround(Vec2 p, double tx, double ty) { return {1, ty, tx, 1, -tx * p.y, -ty * p.x}; }
};

// Bis Bézier-Kurven kommen, ist alles eine Polylinie.
struct Polyline {
    std::vector<Vec2> points;
    bool closed = false;

    Rect bounds() const;
    double length() const;

    static Polyline rectangle(double x, double y, double w, double h);
    static Polyline ellipse(double cx, double cy, double rx, double ry, int segments = 96);
    // Rechteck mit abgerundeten Ecken (radius wird auf die halbe Kante begrenzt)
    static Polyline roundedRect(double x, double y, double w, double h, double radius);
    // Regelmäßiges Vieleck; rotation in Bogenmaß, 0 = erste Ecke oben
    static Polyline regularPolygon(double cx, double cy, double r, int sides, double rotation = 0);
    // Stern mit points Zacken; innerRatio = Innenradius / Außenradius
    static Polyline star(double cx, double cy, double r, int points, double innerRatio, double rotation = 0);
};

} // namespace mb
