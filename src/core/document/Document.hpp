// MacBurn – Dokumentmodell mit Rückgängig-Stapel im Kern.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "geometry/Geometry.hpp"
#include "profiles/Profiles.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mb {

struct Color { float r = 0, g = 0, b = 0, a = 1; };

struct Layer {
    std::string name;
    Color color;
    Intent intent = Intent::Engrave;
    bool output = true;
    IntentSettings overrides;   // Projekt-Abweichungen vom Material
    std::string colorKey;       // "#rrggbb" aus dem Import – gleiche Farbe = gleiche Ebene
};

struct Shape {
    std::string name;
    size_t layer = 0;
    Polyline path;
};

struct Document {
    std::vector<Layer> layers;
    std::vector<Shape> shapes;

    Rect bounds() const;
    static Document sample();   // Beispielprojekt für den ersten Start
};

// Jede Änderung ist ein Befehl. Undo/Redo liegt hier, nicht in Cocoa.
class UndoStack {
public:
    struct Step {
        std::string label;
        std::function<void(Document&)> apply, revert;
    };

    void perform(Document& doc, Step step);
    bool canUndo() const { return pos_ > 0; }
    bool canRedo() const { return pos_ < steps_.size(); }
    std::string undoLabel() const { return canUndo() ? steps_[pos_ - 1].label : std::string(); }
    std::string redoLabel() const { return canRedo() ? steps_[pos_].label : std::string(); }
    void undo(Document& doc);
    void redo(Document& doc);

private:
    std::vector<Step> steps_;
    size_t pos_ = 0;
};

} // namespace mb
