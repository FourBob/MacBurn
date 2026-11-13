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

Document Document::sample() {
    Document d;
    d.layers = {
        {"Schnitt", {0.90f, 0.20f, 0.18f, 1}, Intent::Cut, true, {}},
        {"Gravur", {0.14f, 0.39f, 0.92f, 1}, Intent::Engrave, true, {}},
        {"Ritzen", {0.09f, 0.64f, 0.29f, 1}, Intent::Mark, true, {}},
    };
    // Untersetzer Ø 100 mm mit graviertem Rahmen und Anhänger daneben.
    d.shapes = {
        {"Untersetzer", 0, Polyline::ellipse(80, 80, 50, 50)},
        {"Ring", 1, Polyline::ellipse(80, 80, 42, 42)},
        {"Feld", 1, Polyline::rectangle(60, 70, 40, 20)},
        {"Anhänger", 0, Polyline::rectangle(150, 40, 40, 80)},
        {"Loch", 0, Polyline::ellipse(170, 110, 3, 3, 32)},
        {"Ritzlinie", 2, {{{155, 50}, {185, 50}}, false}},
    };
    return d;
}

void UndoStack::perform(Document& doc, Step step) {
    step.apply(doc);
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
