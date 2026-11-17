// MacBurn – Farbzuweisung: Formen einer Ebene (Farbe) zuordnen.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppController.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace mb {

namespace {

struct PaletteDef { const char* key; const char* name; Intent intent; Color color; };

const PaletteDef kPalette[] = {
    {"#e5332e", "Rot", Intent::Cut, {0.898f, 0.200f, 0.180f, 1}},
    {"#2463eb", "Blau", Intent::Engrave, {0.141f, 0.388f, 0.922f, 1}},
    {"#16a34a", "Grün", Intent::Mark, {0.086f, 0.639f, 0.290f, 1}},
};
constexpr int kPaletteSize = int(sizeof kPalette / sizeof kPalette[0]);

// Weitere Farben für zusätzliche Ebenen (z. B. ein zweiter Schnitt mit anderen Werten)
const Color kExtraColors[] = {
    {0.918f, 0.345f, 0.047f, 1},   // Orange
    {0.486f, 0.227f, 0.929f, 1},   // Violett
    {0.051f, 0.580f, 0.533f, 1},   // Petrol
    {0.859f, 0.153f, 0.467f, 1},   // Magenta
    {0.792f, 0.541f, 0.016f, 1},   // Ocker
    {0.278f, 0.333f, 0.412f, 1},   // Schiefer
    {0.396f, 0.639f, 0.051f, 1},   // Hellgrün
    {0.012f, 0.518f, 0.780f, 1},   // Himmelblau
};

const char* layerNoun(Intent i) {
    switch (i) {
        case Intent::Cut: return "Schnitt";
        case Intent::Engrave: return "Gravur";
        case Intent::Mark: return "Ritzen";
        case Intent::Photo: return "Foto";
    }
    return "Ebene";
}

double colorDistance(const Color& a, const Color& b) {
    return std::sqrt((a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g) + (a.b - b.b) * (a.b - b.b));
}

} // namespace

std::optional<size_t> AppController::paletteLayer(int index) const {
    if (index < 0 || index >= kPaletteSize) return std::nullopt;
    const auto& p = kPalette[index];
    // Gleiche Kennung, gleicher Name (z. B. „Rot“ aus dem SVG-Import) oder sehr ähnliche Farbe
    for (size_t i = 0; i < doc_.layers.size(); ++i) {
        const auto& l = doc_.layers[i];
        if (l.colorKey == p.key || l.name == p.name || colorDistance(l.color, p.color) < 0.1) return i;
    }
    return std::nullopt;
}

std::optional<size_t> AppController::selectionLayer() const {
    std::optional<size_t> layer;
    for (const auto& s : doc_.shapes) {
        if (!selection_.count(s.object)) continue;
        if (layer && *layer != s.layer) return std::nullopt;   // gemischt
        layer = s.layer;
    }
    return layer;
}

std::vector<PaletteItem> AppController::paletteItems() const {
    std::optional<size_t> current = selection_.empty() ? std::optional<size_t>(layer_ >= 0 ? size_t(layer_) : 0) : selectionLayer();
    std::vector<PaletteItem> out;
    for (int i = 0; i < kPaletteSize; ++i) {
        const auto& p = kPalette[i];
        auto l = paletteLayer(i);
        out.push_back({p.key, p.name, intentLabel(p.intent), p.color, l && current && *l == *current});
    }
    return out;
}

void AppController::moveSelectionToLayer(size_t layer) {
    if (selection_.empty() || layer >= doc_.layers.size()) return;
    ObjectSet sel = selection_;
    std::string name = doc_.layers[layer].name;
    size_t newIndex = layer;
    editDoc("In Ebene „" + name + "“", [&](Document& d) {
        for (auto& s : d.shapes) if (sel.count(s.object)) s.layer = layer;
        auto map = d.removeEmptyLayers();   // verlassene Ebenen aufräumen
        newIndex = size_t(std::max(0, map[layer]));
    });
    layer_ = int(newIndex);
    changed();
}

void AppController::applyPaletteColor(int index) {
    if (index < 0 || index >= kPaletteSize || !canEdit()) return;
    const auto& p = kPalette[index];
    auto existing = paletteLayer(index);
    if (selection_.empty()) {
        // Nur die Zeichenfarbe wählen; Ebene erst anlegen, wenn wirklich gezeichnet wird
        if (existing) { layer_ = int(*existing); changed(); return; }
        Layer l;
        l.name = p.name; l.color = p.color; l.intent = p.intent; l.colorKey = p.key;
        doc_.layers.push_back(l);   // leere Ebene: wird beim nächsten Umfärben wieder aufgeräumt
        layer_ = int(doc_.layers.size() - 1);
        changed();
        return;
    }
    if (existing) { moveSelectionToLayer(*existing); return; }
    // Ebene anlegen und Auswahl hineinlegen – ein Widerrufschritt
    ObjectSet sel = selection_;
    size_t newIndex = 0;
    editDoc(std::string("In Ebene „") + p.name + "“", [&](Document& d) {
        Layer l;
        l.name = p.name; l.color = p.color; l.intent = p.intent; l.colorKey = p.key;
        d.layers.push_back(l);
        size_t li = d.layers.size() - 1;
        for (auto& s : d.shapes) if (sel.count(s.object)) s.layer = li;
        auto map = d.removeEmptyLayers();
        newIndex = size_t(std::max(0, map[li]));
    });
    layer_ = int(newIndex);
    changed();
}

void AppController::addLayer(Intent intent) {
    if (!canEdit()) return;
    auto used = [&](const Color& c) {
        for (const auto& l : doc_.layers) if (colorDistance(l.color, c) < 0.1) return true;
        return false;
    };
    Layer l;
    l.intent = intent;
    // Zuerst die feste Farbe der Absicht, sonst die nächste freie Zusatzfarbe
    for (const auto& p : kPalette)
        if (p.intent == intent && !used(p.color)) { l.color = p.color; l.colorKey = p.key; break; }
    if (l.colorKey.empty()) {
        l.color = kExtraColors[doc_.layers.size() % std::size(kExtraColors)];
        for (const auto& c : kExtraColors) if (!used(c)) { l.color = c; break; }
    }
    // Name: „Schnitt“, „Schnitt 2“, …
    std::string base = layerNoun(intent);
    l.name = base;
    for (int n = 2;; ++n) {
        bool taken = false;
        for (const auto& o : doc_.layers) if (o.name == l.name) { taken = true; break; }
        if (!taken) break;
        l.name = base + " " + std::to_string(n);
    }
    ObjectSet sel = selection_;
    size_t newIndex = 0;
    editDoc("Neue Ebene „" + l.name + "“", [&](Document& d) {
        d.layers.push_back(l);
        newIndex = d.layers.size() - 1;
        if (sel.empty()) return;
        for (auto& s : d.shapes) if (sel.count(s.object)) s.layer = newIndex;
        auto map = d.removeEmptyLayers();
        newIndex = size_t(std::max(0, map[newIndex]));
    });
    layer_ = int(newIndex);
    message_ = sel.empty() ? "Neue Ebene „" + l.name + "“ – neue Formen landen dort"
                           : "Auswahl liegt jetzt in der neuen Ebene „" + l.name + "“";
    changed();
}

void AppController::dropSelectionOnLayer(size_t layer) {
    if (!movingSelection() || layer >= doc_.layers.size()) return;
    doc_ = drag_->before;   // Verschieben zurücknehmen: die Formen bleiben, wo sie waren
    drag_.reset();
    moveSelectionToLayer(layer);
    message_ = "In Ebene „" + doc_.layers[size_t(layer_)].name + "“ gelegt";
    changed();
}

} // namespace mb
