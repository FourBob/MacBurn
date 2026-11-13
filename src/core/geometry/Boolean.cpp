// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/Boolean.hpp"

#include "clipper2/clipper.h"

#include <cmath>

namespace mb::geo {

namespace {

using namespace Clipper2Lib;
constexpr int kPrecision = 4;   // 0,0001 mm

PathsD toPaths(const Region& r) {
    PathsD out;
    for (const auto& p : r) {
        if (!p.closed || p.points.size() < 3) continue;
        PathD path;
        path.reserve(p.points.size());
        for (const auto& v : p.points) path.push_back({v.x, v.y});
        out.push_back(std::move(path));
    }
    return out;
}

Region toRegion(const PathsD& paths) {
    Region r;
    for (const auto& path : paths) {
        if (path.size() < 3) continue;
        Polyline p;
        p.closed = true;
        p.points.reserve(path.size());
        for (const auto& pt : path) p.points.push_back({pt.x, pt.y});
        r.push_back(std::move(p));
    }
    return r;
}

PathsD op(ClipType t, const PathsD& a, const PathsD& b) {
    return BooleanOp(t, FillRule::EvenOdd, a, b, kPrecision);
}

// Gerade-ungerade gefüllte Eingabe zuerst in eine saubere Fläche verwandeln,
// damit Überlappungen zwischen Formen nicht als Löcher gelten.
PathsD normalize(const Region& r) { return Union(toPaths(r), FillRule::EvenOdd, kPrecision); }

} // namespace

Region unite(const std::vector<Region>& regions) {
    PathsD acc;
    for (const auto& r : regions) acc = op(ClipType::Union, acc, normalize(r));
    return toRegion(acc);
}

Region subtract(const Region& base, const std::vector<Region>& cutters) {
    PathsD acc = normalize(base);
    for (const auto& c : cutters) acc = op(ClipType::Difference, acc, normalize(c));
    return toRegion(acc);
}

Region intersect(const std::vector<Region>& regions) {
    if (regions.empty()) return {};
    PathsD acc = normalize(regions[0]);
    for (size_t i = 1; i < regions.size(); ++i) acc = op(ClipType::Intersection, acc, normalize(regions[i]));
    return toRegion(acc);
}

std::vector<Region> divide(const std::vector<Region>& regions) {
    std::vector<PathsD> parts;   // bisherige Teilflächen
    PathsD covered;              // Vereinigung aller bisherigen Eingaben
    for (const auto& r : regions) {
        PathsD x = normalize(r);
        std::vector<PathsD> next;
        for (const auto& p : parts) {
            PathsD inside = op(ClipType::Intersection, p, x);
            PathsD outside = op(ClipType::Difference, p, x);
            if (!inside.empty()) next.push_back(inside);
            if (!outside.empty()) next.push_back(outside);
        }
        PathsD fresh = op(ClipType::Difference, x, covered);
        if (!fresh.empty()) next.push_back(fresh);
        covered = op(ClipType::Union, covered, x);
        parts = std::move(next);
    }
    std::vector<Region> out;
    for (const auto& p : parts) {
        Region reg = toRegion(p);
        if (area(reg) > 1e-6) out.push_back(std::move(reg));   // Splitter verwerfen
    }
    return out;
}

Region offset(const std::vector<Polyline>& paths, double distance, double tolerance) {
    PathsD closed, open;
    for (const auto& p : paths) {
        if (p.points.size() < 2) continue;
        PathD path;
        for (const auto& v : p.points) path.push_back({v.x, v.y});
        if (p.closed && p.points.size() >= 3) closed.push_back(std::move(path));
        else open.push_back(std::move(path));
    }
    PathsD result;
    if (!closed.empty()) {
        PathsD clean = Union(closed, FillRule::EvenOdd, kPrecision);
        result = InflatePaths(clean, distance, JoinType::Round, EndType::Polygon, 2.0, kPrecision, tolerance);
    }
    if (!open.empty() && std::abs(distance) > 0) {
        PathsD lines = InflatePaths(open, std::abs(distance), JoinType::Round, EndType::Round, 2.0, kPrecision, tolerance);
        result = op(ClipType::Union, result, lines);
    }
    return toRegion(result);
}

double area(const Region& r) {
    // Clipper liefert Außenkonturen und Löcher mit entgegengesetztem Umlaufsinn.
    double a = 0;
    for (const auto& p : r) {
        double s = 0;
        for (size_t i = 0; i < p.points.size(); ++i) {
            const Vec2& u = p.points[i];
            const Vec2& v = p.points[(i + 1) % p.points.size()];
            s += u.x * v.y - v.x * u.y;
        }
        a += s / 2;
    }
    return std::abs(a);
}

} // namespace mb::geo
