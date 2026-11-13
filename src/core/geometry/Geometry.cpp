// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/Geometry.hpp"

#include <algorithm>
#include <limits>

namespace mb {

Rect Polyline::bounds() const {
    if (points.empty()) return {};
    double x0 = std::numeric_limits<double>::max(), y0 = x0, x1 = -x0, y1 = -x0;
    for (auto p : points) {
        x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

double Polyline::length() const {
    double l = 0;
    for (size_t i = 1; i < points.size(); ++i) l += (points[i] - points[i - 1]).length();
    if (closed && points.size() > 2) l += (points.front() - points.back()).length();
    return l;
}

Polyline Polyline::rectangle(double x, double y, double w, double h) {
    return {{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, true};
}

Polyline Polyline::ellipse(double cx, double cy, double rx, double ry, int segments) {
    Polyline p;
    p.closed = true;
    for (int i = 0; i < segments; ++i) {
        double a = 2.0 * M_PI * i / segments;
        p.points.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
    }
    return p;
}

Polyline Polyline::roundedRect(double x, double y, double w, double h, double radius) {
    double r = std::clamp(radius, 0.0, std::min(std::abs(w), std::abs(h)) / 2);
    if (r < 1e-6) return rectangle(x, y, w, h);
    Polyline p;
    p.closed = true;
    const int seg = std::clamp(int(std::ceil(r * 4)), 4, 32);   // Punkte je Viertelkreis
    auto arc = [&](double cx, double cy, double a0) {
        for (int i = 0; i <= seg; ++i) {
            double a = a0 + (M_PI / 2) * i / seg;
            p.points.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
        }
    };
    arc(x + w - r, y + r, -M_PI / 2);   // unten rechts
    arc(x + w - r, y + h - r, 0);       // oben rechts
    arc(x + r, y + h - r, M_PI / 2);    // oben links
    arc(x + r, y + r, M_PI);            // unten links
    return p;
}

Polyline Polyline::regularPolygon(double cx, double cy, double r, int sides, double rotation) {
    Polyline p;
    p.closed = true;
    sides = std::max(3, sides);
    for (int i = 0; i < sides; ++i) {
        double a = M_PI / 2 + rotation + 2 * M_PI * i / sides;
        p.points.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    return p;
}

Polyline Polyline::star(double cx, double cy, double r, int points, double innerRatio, double rotation) {
    Polyline p;
    p.closed = true;
    points = std::max(3, points);
    double ri = r * std::clamp(innerRatio, 0.05, 0.95);
    for (int i = 0; i < 2 * points; ++i) {
        double a = M_PI / 2 + rotation + M_PI * i / points;
        double rr = i % 2 ? ri : r;
        p.points.push_back({cx + rr * std::cos(a), cy + rr * std::sin(a)});
    }
    return p;
}

} // namespace mb
