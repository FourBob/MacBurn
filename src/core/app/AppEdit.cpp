// MacBurn – Formen bearbeiten: Auswahl, Verschieben, Maße, Drehen, Spiegeln, Duplizieren, Löschen.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppController.hpp"

#include <algorithm>
#include <cmath>

namespace mb {

void AppController::editDoc(const std::string& label, const std::function<void(Document&)>& fn) {
    if (!canEdit()) return;
    Document before = doc_, after = doc_;
    fn(after);
    undo_.perform(doc_, {label, [after](Document& d) { d = after; }, [before](Document& d) { d = before; }});
    isSample_ = false;
    changed();
}

// ---------- Maus ----------

void AppController::pointerDown(Vec2 p, double tol, bool extend) {
    if (!canEdit()) return;
    auto hit = doc_.hitTest(p, tol);
    if (!hit) {
        if (!extend) selection_.clear();
        drag_.reset();
        changed();
        return;
    }
    if (extend) {
        if (selection_.count(*hit)) selection_.erase(*hit);
        else selection_.insert(*hit);
    } else if (!selection_.count(*hit)) {
        selection_ = {*hit};
    }
    // Ebene des angeklickten Objekts im Inspektor zeigen
    for (const auto& s : doc_.shapes) if (s.object == *hit) { layer_ = int(s.layer); break; }
    drag_ = selection_.empty() ? std::nullopt : std::optional<Drag>(Drag{doc_, p, false});
    changed();
}

void AppController::pointerDrag(Vec2 p) {
    if (!drag_ || selection_.empty()) return;
    Vec2 d = p - drag_->start;
    if (!drag_->moved && d.length() < 0.2) return;   // kleines Zittern ist kein Ziehen
    drag_->moved = true;
    doc_ = drag_->before;
    doc_.transform(selection_, [d](Vec2 q) { return q + d; });
    changed();
}

void AppController::pointerUp() {
    if (drag_ && drag_->moved) {
        Document before = drag_->before, after = doc_;
        undo_.record({"Verschieben", [after](Document& d) { d = after; }, [before](Document& d) { d = before; }});
        isSample_ = false;
    }
    drag_.reset();
    changed();
}

// ---------- Befehle ----------

void AppController::nudge(double dx, double dy) {
    if (selection_.empty()) return;
    editDoc("Verschieben", [&](Document& d) { d.transform(selection_, [dx, dy](Vec2 q) { return q + Vec2{dx, dy}; }); });
}

void AppController::selectAll() {
    selection_ = doc_.allObjects();
    changed();
}

void AppController::clearSelection() {
    if (selection_.empty()) return;
    selection_.clear();
    changed();
}

void AppController::deleteSelection() {
    if (selection_.empty()) return;
    ObjectSet sel = selection_;
    editDoc(sel.size() == 1 ? "Objekt löschen" : "Objekte löschen", [sel](Document& d) {
        d.shapes.erase(std::remove_if(d.shapes.begin(), d.shapes.end(), [&](const Shape& s) { return sel.count(s.object) > 0; }),
                       d.shapes.end());
    });
    selection_.clear();
    changed();
}

void AppController::duplicateSelection() {
    if (selection_.empty()) return;
    ObjectSet fresh;
    ObjectSet sel = selection_;
    editDoc("Duplizieren", [&](Document& d) {
        uint32_t next = d.nextObjectId();
        std::map<uint32_t, uint32_t> ids;
        std::vector<Shape> copies;
        for (const auto& s : d.shapes) {
            if (!sel.count(s.object)) continue;
            if (!ids.count(s.object)) ids[s.object] = next++;
            Shape c = s;
            c.object = ids[s.object];
            for (auto& p : c.path.points) p = p + Vec2{5, -5};   // leicht versetzt, damit man die Kopie sieht
            copies.push_back(c);
        }
        for (auto& [from, to] : ids) fresh.insert(to);
        d.shapes.insert(d.shapes.end(), copies.begin(), copies.end());
    });
    selection_ = fresh;
    changed();
}

void AppController::rotateSelection(double deg) {
    if (selection_.empty()) return;
    Rect b = doc_.bounds(selection_);
    Vec2 c{b.x + b.w / 2, b.y + b.h / 2};
    double r = deg * M_PI / 180, cs = std::cos(r), sn = std::sin(r);
    Rect wa = workArea();
    editDoc("Drehen", [&](Document& d) {
        d.transform(selection_, [=](Vec2 q) {
            Vec2 v = q - c;
            return Vec2{c.x + cs * v.x - sn * v.y, c.y + sn * v.x + cs * v.y};
        });
        // Lag es vorher auf der Fläche, soll es auch gedreht dort bleiben (falls es hineinpasst).
        Rect nb = d.bounds(selection_);
        if (wa.contains(b) && nb.w <= wa.w && nb.h <= wa.h) {
            Vec2 shift{std::clamp(nb.x, wa.x, wa.x + wa.w - nb.w) - nb.x, std::clamp(nb.y, wa.y, wa.y + wa.h - nb.h) - nb.y};
            if (shift.length() > 1e-9) d.transform(selection_, [shift](Vec2 q) { return q + shift; });
        }
    });
}

void AppController::flipSelection(bool horizontal) {
    if (selection_.empty()) return;
    Rect b = doc_.bounds(selection_);
    double cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    editDoc(horizontal ? "Horizontal spiegeln" : "Vertikal spiegeln", [&](Document& d) {
        d.transform(selection_, [=](Vec2 q) { return horizontal ? Vec2{2 * cx - q.x, q.y} : Vec2{q.x, 2 * cy - q.y}; });
    });
}

void AppController::centerSelection() {
    if (selection_.empty()) return;
    Rect b = doc_.bounds(selection_), wa = workArea();
    Vec2 d{wa.x + wa.w / 2 - (b.x + b.w / 2), wa.y + wa.h / 2 - (b.y + b.h / 2)};
    editDoc("Auf der Fläche zentrieren", [&](Document& doc) { doc.transform(selection_, [d](Vec2 q) { return q + d; }); });
}

void AppController::setSelectionNumber(const std::string& key, double v) {
    if (selection_.empty()) return;
    Rect b = doc_.bounds(selection_);
    if (key == "selX" || key == "selY") {
        Vec2 d = key == "selX" ? Vec2{v - b.x, 0} : Vec2{0, v - b.y};
        editDoc("Position ändern", [&](Document& doc) { doc.transform(selection_, [d](Vec2 q) { return q + d; }); });
        return;
    }
    // Größe: unten links bleibt stehen
    if (v <= 0.01) return;
    double sx = 1, sy = 1;
    if (key == "selW" && b.w > 1e-9) { sx = v / b.w; if (lockRatio_) sy = sx; }
    if (key == "selH" && b.h > 1e-9) { sy = v / b.h; if (lockRatio_) sx = sy; }
    editDoc("Größe ändern", [&](Document& doc) {
        doc.transform(selection_, [=](Vec2 q) { return Vec2{b.x + (q.x - b.x) * sx, b.y + (q.y - b.y) * sy}; });
    });
}

// ---------- Inspektor ----------

void AppController::addSelectionFields(std::vector<Field>& f) const {
    if (selection_.empty()) return;
    Rect b = doc_.bounds(selection_);
    size_t n = selection_.size();
    f.push_back({Field::Kind::Header, "", n == 1 ? "Auswahl" : "Auswahl (" + std::to_string(n) + " Objekte)"});
    auto num = [&](const char* key, const char* label, double v, double lo) {
        Field x{Field::Kind::Number, key, label};
        x.value = std::round(v * 100) / 100;
        x.min = lo; x.max = 2000; x.step = 0.1; x.unit = "mm";
        x.enabled = canEdit();
        f.push_back(x);
    };
    num("selX", "X", b.x, -2000);
    num("selY", "Y", b.y, -2000);
    num("selW", "Breite", b.w, 0.01);
    num("selH", "Höhe", b.h, 0.01);
    Field lock{Field::Kind::Toggle, "lockRatio", "Proportional"};
    lock.on = lockRatio_;
    f.push_back(lock);
    f.push_back({Field::Kind::Button, "rot90", "Um 90° drehen"});
    f.push_back({Field::Kind::Button, "flipH", "Horizontal spiegeln"});
    f.push_back({Field::Kind::Button, "flipV", "Vertikal spiegeln"});
    f.push_back({Field::Kind::Button, "center", "Auf der Fläche zentrieren"});
    f.push_back({Field::Kind::Button, "duplicate", "Duplizieren (⌘D)"});
    f.push_back({Field::Kind::Button, "delete", "Löschen (⌫)"});
}

} // namespace mb
