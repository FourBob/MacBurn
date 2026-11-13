// SPDX-License-Identifier: GPL-3.0-or-later
#include "document/Document.hpp"

#include <algorithm>

namespace mb {

Rect Document::bounds() const {
    bool first = true;
    Rect r;
    for (const auto& s : shapes) {
        Rect b = s.path.bounds();
        if (first) { r = b; first = false; continue; }
        double x1 = std::max(r.x + r.w, b.x + b.w), y1 = std::max(r.y + r.h, b.y + b.h);
        r.x = std::min(r.x, b.x); r.y = std::min(r.y, b.y);
        r.w = x1 - r.x; r.h = y1 - r.y;
    }
    return r;
}

Rect Document::bounds(const ObjectSet& objects) const {
    Polyline all;
    for (const auto& s : shapes)
        if (objects.count(s.object)) all.points.insert(all.points.end(), s.path.points.begin(), s.path.points.end());
    return all.bounds();
}

uint32_t Document::nextObjectId() const {
    uint32_t m = 0;
    for (const auto& s : shapes) m = std::max(m, s.object);
    return m + 1;
}

ObjectSet Document::allObjects() const {
    ObjectSet o;
    for (const auto& s : shapes) o.insert(s.object);
    return o;
}

namespace {

double distToSegment(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 d = b - a;
    double len2 = d.x * d.x + d.y * d.y;
    double t = len2 > 0 ? std::clamp(((p.x - a.x) * d.x + (p.y - a.y) * d.y) / len2, 0.0, 1.0) : 0.0;
    return (p - (a + d * t)).length();
}

bool insidePolygon(Vec2 p, const std::vector<Vec2>& pts) {
    bool in = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        if ((pts[i].y > p.y) != (pts[j].y > p.y) &&
            p.x < (pts[j].x - pts[i].x) * (p.y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x)
            in = !in;
    }
    return in;
}

} // namespace

std::optional<uint32_t> Document::hitTest(Vec2 p, double tol) const {
    // Erst Linien und Konturen (oberste zuerst), dann Flächen geschlossener Formen.
    for (auto it = shapes.rbegin(); it != shapes.rend(); ++it) {
        const auto& pts = it->path.points;
        for (size_t i = 1; i < pts.size(); ++i)
            if (distToSegment(p, pts[i - 1], pts[i]) <= tol) return it->object;
        if (it->path.closed && pts.size() > 2 && distToSegment(p, pts.back(), pts.front()) <= tol) return it->object;
    }
    for (auto it = shapes.rbegin(); it != shapes.rend(); ++it)
        if (it->path.closed && it->path.points.size() > 2 && insidePolygon(p, it->path.points)) return it->object;
    return std::nullopt;
}

void Document::transform(const ObjectSet& objects, const std::function<Vec2(Vec2)>& fn) {
    for (auto& s : shapes)
        if (objects.count(s.object))
            for (auto& p : s.path.points) p = fn(p);
}

Document Document::sample() {
    Document d;
    d.layers = {
        {"Schnitt", {0.90f, 0.20f, 0.18f, 1}, Intent::Cut, true, {}},
        {"Gravur", {0.14f, 0.39f, 0.92f, 1}, Intent::Engrave, true, {}},
        {"Ritzen", {0.09f, 0.64f, 0.29f, 1}, Intent::Mark, true, {}},
    };
    // Untersetzer Ø 100 mm mit graviertem Rahmen und Anhänger daneben.
    d.shapes = {
        {"Untersetzer", 0, Polyline::ellipse(80, 80, 50, 50), 1},
        {"Ring", 1, Polyline::ellipse(80, 80, 42, 42), 1},
        {"Feld", 1, Polyline::rectangle(60, 70, 40, 20), 1},
        {"Anhänger", 0, Polyline::rectangle(150, 40, 40, 80), 2},
        {"Loch", 0, Polyline::ellipse(170, 110, 3, 3, 32), 2},
        {"Ritzlinie", 2, {{{155, 50}, {185, 50}}, false}, 2},
    };
    return d;
}

void UndoStack::perform(Document& doc, Step step) {
    step.apply(doc);
    step.id = nextId_++;
    steps_.resize(pos_);
    steps_.push_back(std::move(step));
    ++pos_;
}

void UndoStack::record(Step step) {
    step.id = nextId_++;
    steps_.resize(pos_);
    steps_.push_back(std::move(step));
    ++pos_;
}

void UndoStack::undo(Document& doc) {
    if (!canUndo()) return;
    steps_[--pos_].revert(doc);
}

void UndoStack::redo(Document& doc) {
    if (!canRedo()) return;
    steps_[pos_++].apply(doc);
}

} // namespace mb
