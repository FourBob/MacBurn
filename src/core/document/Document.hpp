// MacBurn – Dokumentmodell mit Rückgängig-Stapel im Kern.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "geometry/Geometry.hpp"
#include "profiles/Profiles.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <optional>
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
    uint32_t object = 0;   // zusammengehörige Formen (z. B. ein importiertes SVG) bewegen sich gemeinsam
    uint32_t part = 0;     // ursprüngliches Element (z. B. ein SVG-Pfad mit Löchern) – für „Gruppierung aufheben“
};

using ObjectSet = std::set<uint32_t>;

// Bearbeitbarer Text. Die Buchstaben sind normale Formen eines Teils (part);
// m bildet das Text-Koordinatensystem (Grundlinie der ersten Zeile bei 0/0) auf die Fläche ab.
struct TextSpec {
    std::string text = "Text";
    std::string family = "Helvetica Neue", style = "Bold";
    double capHeightMm = 10;     // Höhe der Großbuchstaben
    int align = 0;               // 0 links, 1 mittig, 2 rechts
    double letterSpacingMm = 0;
    double lineSpacing = 1.2;    // Vielfaches der Schrifthöhe
    Affine m;
    bool operator==(const TextSpec&) const = default;
};

struct Document {
    std::vector<Layer> layers;
    std::vector<Shape> shapes;
    std::map<uint32_t, TextSpec> texts;   // je Teil (part), der ein Text ist

    Rect bounds() const;
    Rect bounds(const ObjectSet& objects) const;
    uint32_t nextObjectId() const;
    uint32_t nextPartId() const;
    ObjectSet allObjects() const;
    // Oberstes Objekt an einem Punkt (mm); tolerance für Linien und Konturen
    std::optional<uint32_t> hitTest(Vec2 p, double tolerance) const;
    // Objekte affin abbilden; Texte merken sich die Abbildung und bleiben bearbeitbar
    void transform(const ObjectSet& objects, const Affine& m);
    // Texte ohne Formen (z. B. nach Kombinieren oder Löschen) entfernen
    void pruneTexts();
    static Document sample();   // Beispielprojekt für den ersten Start
};

// Jede Änderung ist ein Befehl. Undo/Redo liegt hier, nicht in Cocoa.
class UndoStack {
public:
    struct Step {
        std::string label;
        std::function<void(Document&)> apply, revert;
        uint64_t id = 0;   // vom Stapel vergeben
    };

    void perform(Document& doc, Step step);
    void record(Step step);   // Änderung ist schon angewendet (z. B. nach dem Ziehen mit der Maus)
    bool canUndo() const { return pos_ > 0; }
    bool canRedo() const { return pos_ < steps_.size(); }
    std::string undoLabel() const { return canUndo() ? steps_[pos_ - 1].label : std::string(); }
    std::string redoLabel() const { return canRedo() ? steps_[pos_].label : std::string(); }
    void undo(Document& doc);
    void redo(Document& doc);
    void clear() { steps_.clear(); pos_ = 0; }
    // Kennung des aktuellen Zustands: gleich = gleiches Dokument wie damals (z. B. beim Sichern)
    uint64_t stateId() const { return pos_ ? steps_[pos_ - 1].id : 0; }

private:
    uint64_t nextId_ = 1;
    std::vector<Step> steps_;
    size_t pos_ = 0;
};

} // namespace mb
