// MacBurn – Zeichenwerkzeuge: Linie, Rechteck, Ellipse, Dreieck, Polygon, Stern, Polylinie.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppController.hpp"

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

const char* toolName(Tool t) {
    switch (t) {
        case Tool::Select: return "Auswahl";
        case Tool::Line: return "Linie";
        case Tool::Rect: return "Rechteck";
        case Tool::Ellipse: return "Ellipse";
        case Tool::Triangle: return "Dreieck";
        case Tool::Polygon: return "Polygon";
        case Tool::Star: return "Stern";
        case Tool::Polyline: return "Polylinie";
    }
    return "";
}

double snapAngle(double a, double stepDeg) {
    double s = stepDeg * M_PI / 180;
    return std::round(a / s) * s;
}

// Segmente so wählen, dass die Abweichung vom echten Kreis unter 0,02 mm bleibt
int circleSegments(double r) {
    if (r <= 0.02) return 16;
    return std::clamp(int(std::ceil(M_PI / std::acos(1 - 0.02 / r))), 24, 720);
}

} // namespace

std::vector<ToolItem> AppController::toolItems() const {
    std::vector<ToolItem> items = {
        {Tool::Select, "cursorarrow", "Auswahl", "V"},
        {Tool::Line, "line.diagonal", "Linie", "L"},
        {Tool::Rect, "rectangle", "Rechteck", "R"},
        {Tool::Ellipse, "circle", "Ellipse", "E"},
        {Tool::Triangle, "triangle", "Dreieck", "D"},
        {Tool::Polygon, "hexagon", "Polygon", "P"},
        {Tool::Star, "star", "Stern", "S"},
        {Tool::Polyline, "point.3.connected.trianglepath.dotted", "Polylinie", "N"},
    };
    for (auto& i : items) i.selected = i.tool == tool_;
    return items;
}

void AppController::setTool(Tool t) {
    cancelCreation();
    tool_ = t;
    if (t != Tool::Select) message_ = std::string(toolName(t)) +
        (t == Tool::Polyline ? ": Punkte klicken, Doppelklick oder ↩ beendet, Esc bricht ab."
                             : ": auf der Fläche ziehen, ⇧ für gleichmäßige Form. Ein Klick legt sie in 20 mm an.");
    else message_.clear();
    changed();
}

bool AppController::selectToolByKey(const std::string& key) {
    std::string k = key;
    for (auto& ch : k) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    for (const auto& i : toolItems()) if (i.key == k) { setTool(i.tool); return true; }
    return false;
}

size_t AppController::drawingLayer() {
    // In die gewählte Ebene zeichnen; gibt es keine, eine Schnittebene anlegen.
    if (doc_.layers.empty()) {
        Layer l;
        l.name = "Schnitt";
        l.color = {0.90f, 0.20f, 0.18f, 1};
        l.intent = Intent::Cut;
        l.colorKey = "#e5332e";
        doc_.layers.push_back(l);
        layer_ = 0;
    }
    return size_t(std::clamp(layer_, 0, int(doc_.layers.size()) - 1));
}

Polyline AppController::shapeFor(Vec2 a, Vec2 b, bool shift) const {
    Vec2 d = b - a;
    switch (tool_) {
        case Tool::Line: {
            if (shift) {
                double ang = snapAngle(std::atan2(d.y, d.x), 45), len = d.length();
                b = a + Vec2{std::cos(ang) * len, std::sin(ang) * len};
            }
            return {{a, b}, false};
        }
        case Tool::Rect:
        case Tool::Ellipse: {
            double w = std::abs(d.x), h = std::abs(d.y);
            if (shift) w = h = std::max(w, h);
            double x = d.x < 0 ? a.x - w : a.x, y = d.y < 0 ? a.y - h : a.y;
            if (tool_ == Tool::Rect) return Polyline::roundedRect(x, y, w, h, rectRadius_);
            return Polyline::ellipse(x + w / 2, y + h / 2, w / 2, h / 2, circleSegments(std::max(w, h) / 2));
        }
        case Tool::Triangle:
        case Tool::Polygon:
        case Tool::Star: {
            // Von der Mitte aus: Abstand = Radius, Richtung = erste Ecke
            double r = d.length();
            double rot = std::atan2(d.y, d.x) - M_PI / 2;
            if (shift) rot = snapAngle(rot, 15);
            if (tool_ == Tool::Star) return Polyline::star(a.x, a.y, r, starPoints_, starInner_, rot);
            return Polyline::regularPolygon(a.x, a.y, r, tool_ == Tool::Triangle ? 3 : polygonSides_, rot);
        }
        default: return {};
    }
}

void AppController::beginCreate(Vec2 p) {
    if (tool_ == Tool::Polyline && create_) {
        // Klick auf den Startpunkt schließt die Form
        if (create_->points.size() >= 3 && (p - create_->points.front()).length() <= 6.0 / pxPerMm_) {
            create_->points.push_back(create_->points.front());
            commitCreate(false);
            return;
        }
        create_->points.push_back(p);
        create_->current = p;
        updateCreate(p, false);
        return;
    }
    drawingLayer();
    create_ = Create{doc_, p, p, {p}, false};
}

void AppController::updateCreate(Vec2 p, bool shift) {
    if (!create_) return;
    create_->current = p;
    doc_ = create_->before;
    Polyline shape;
    if (tool_ == Tool::Polyline) {
        shape.points = create_->points;
        if ((p - shape.points.back()).length() > 1e-6) shape.points.push_back(p);   // Vorschau bis zur Maus
    } else {
        if ((p - create_->start).length() > 0.3) create_->dragged = true;
        if (!create_->dragged) { changed(); return; }
        shape = shapeFor(create_->start, p, shift);
    }
    if (shape.points.size() >= 2)
        doc_.shapes.push_back({toolName(tool_), drawingLayer(), shape, doc_.nextObjectId(), doc_.nextPartId()});
    changed();
}

void AppController::commitCreate(bool clicked) {
    if (!create_) return;
    Document before = create_->before;
    Polyline shape;
    if (tool_ == Tool::Polyline) {
        shape.points = create_->points;
        if (shape.points.size() >= 4 && (shape.points.back() - shape.points.front()).length() < 1e-6) {
            shape.points.pop_back();
            shape.closed = true;
        }
        if (shape.points.size() < 2) { cancelCreation(); return; }
    } else if (clicked) {
        // Klick ohne Ziehen: Form in 20 mm Größe an dieser Stelle
        Vec2 a = create_->start;
        bool centered = tool_ == Tool::Triangle || tool_ == Tool::Polygon || tool_ == Tool::Star;
        shape = centered ? shapeFor(a, a + Vec2{0, 10}, false) : shapeFor(a, a + Vec2{20, 20}, false);
    } else {
        shape = shapeFor(create_->start, create_->current, false);
        // Ziehen mit ⇧ ist schon in der Vorschau enthalten – die letzte Vorschauform übernehmen
        if (!doc_.shapes.empty() && doc_.shapes.size() == before.shapes.size() + 1) shape = doc_.shapes.back().path;
    }
    Rect b = shape.bounds();
    if (shape.points.size() < 2 || (b.w < 0.05 && b.h < 0.05)) { cancelCreation(); return; }

    Document after = before;
    uint32_t obj = after.nextObjectId();
    after.shapes.push_back({toolName(tool_), drawingLayer(), shape, obj, after.nextPartId()});
    if (after.layers.empty()) after.layers = doc_.layers;   // neu angelegte Ebene mitnehmen
    else if (after.layers.size() < doc_.layers.size()) after.layers = doc_.layers;
    doc_ = before;
    if (doc_.layers.size() < after.layers.size()) doc_.layers = after.layers;
    create_.reset();
    undo_.perform(doc_, {std::string(toolName(tool_)) + " zeichnen", [after](Document& d) { d = after; },
                         [before](Document& d) { d = before; }});
    isSample_ = false;
    selection_ = {obj};
    tool_ = Tool::Select;   // nach dem Zeichnen zurück zur Auswahl
    message_.clear();
    changed();
}

void AppController::pointerMove(Vec2 p) {
    if (create_ && tool_ == Tool::Polyline) updateCreate(p, false);
}

void AppController::pointerDoubleClick(Vec2) {
    if (create_ && tool_ == Tool::Polyline) {
        // Der Doppelklick hat beim ersten Klick schon einen Punkt gesetzt – doppelte Punkte entfernen
        auto& pts = create_->points;
        while (pts.size() >= 2 && (pts.back() - pts[pts.size() - 2]).length() < 0.5) pts.pop_back();
        commitCreate(false);
    }
}

bool AppController::finishCreation() {
    if (!create_ || tool_ != Tool::Polyline) return false;
    commitCreate(false);
    return true;
}

bool AppController::cancelCreation() {
    if (!create_) return false;
    doc_ = create_->before;
    create_.reset();
    changed();
    return true;
}

void AppController::addToolFields(std::vector<Field>& f) const {
    if (tool_ == Tool::Select) return;
    f.push_back({Field::Kind::Header, "", std::string("Werkzeug: ") + toolName(tool_)});
    auto num = [&](const char* key, const char* label, double v, double lo, double hi, double step, const char* unit) {
        Field x{Field::Kind::Number, key, label};
        x.value = v; x.min = lo; x.max = hi; x.step = step; x.unit = unit;
        f.push_back(x);
    };
    if (tool_ == Tool::Rect) num("toolRadius", "Eckenradius", rectRadius_, 0, 500, 0.5, "mm");
    if (tool_ == Tool::Polygon) num("toolSides", "Ecken", polygonSides_, 3, 64, 1, "");
    if (tool_ == Tool::Star) {
        num("toolPoints", "Zacken", starPoints_, 3, 64, 1, "");
        num("toolInner", "Innenradius", std::round(starInner_ * 100), 5, 95, 5, "%");
    }
    std::string layer = layer_ >= 0 && layer_ < int(doc_.layers.size()) ? doc_.layers[size_t(layer_)].name : "Schnitt (neu)";
    f.push_back({Field::Kind::Info, "", "Neue Formen kommen in die Ebene „" + layer + "“. Eine andere Ebene links wählen."});
    f.push_back({Field::Kind::Button, "toolSelect", "Fertig (V)"});
}

} // namespace mb
