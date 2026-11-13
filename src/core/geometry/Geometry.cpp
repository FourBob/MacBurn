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

} // namespace mb
