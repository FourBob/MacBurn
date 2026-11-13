// MacBurn – Formen bearbeiten: Auswahl, Verschieben, Maße, Drehen, Spiegeln, Duplizieren, Löschen.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppController.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace mb {

void AppController::editDoc(const std::string& label, const std::function<void(Document&)>& fn) {
    if (!canEdit()) return;
    Document before = doc_, after = doc_;
    fn(after);
    undo_.perform(doc_, {label, [after](Document& d) { d = after; }, [before](Document& d) { d = before; }});
    isSample_ = false;
    changed();
}

// ---------- Anfasser ----------

namespace {
// Reihenfolge: 0 unten links, 1 unten Mitte, 2 unten rechts, 3 rechts Mitte,
//              4 oben rechts, 5 oben Mitte, 6 oben links, 7 links Mitte, 8 Drehen
constexpr double kHx[8] = {0, 0.5, 1, 1, 1, 0.5, 0, 0};
constexpr double kHy[8] = {0, 0, 0, 0.5, 1, 1, 1, 0.5};

Vec2 onRect(const Rect& r, int h) { return {r.x + kHx[h] * r.w, r.y + kHy[h] * r.h}; }
}

std::vector<Vec2> AppController::selectionHandles() const {
    if (selection_.empty()) return {};
    Rect b = doc_.bounds(selection_);
    double m = 4.0 / pxPerMm_;   // wie der Rahmen: 4 Punkte Abstand
    Rect e{b.x - m, b.y - m, b.w + 2 * m, b.h + 2 * m};
    std::vector<Vec2> h;
    for (int i = 0; i < 8; ++i) h.push_back(onRect(e, i));
    h.push_back({e.x + e.w / 2, e.y + e.h + 22.0 / pxPerMm_});   // Drehen: über der Mitte
    return h;
}

// ---------- Maus ----------

void AppController::pointerDown(Vec2 p, double tol, bool extend) {
    if (!canEdit()) return;
    // Zuerst die Anfasser der aktuellen Auswahl
    if (!selection_.empty() && !extend && phase_ != Phase::Laser) {
        auto hs = selectionHandles();
        for (int i = int(hs.size()) - 1; i >= 0; --i) {
            if ((hs[size_t(i)] - p).length() <= tol * 1.2) {
                drag_ = Drag{doc_, p, false, i, doc_.bounds(selection_)};
                return;
            }
        }
    }
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
    drag_ = selection_.empty() ? std::nullopt : std::optional<Drag>(Drag{doc_, p, false, -1, {}});
    changed();
}

void AppController::pointerDrag(Vec2 p, bool shift) {
    if (!drag_ || selection_.empty()) return;
    Vec2 d = p - drag_->start;
    if (!drag_->moved && d.length() < 0.2) return;   // kleines Zittern ist kein Ziehen
    drag_->moved = true;
    doc_ = drag_->before;
    const Rect& b = drag_->box;

    if (drag_->handle < 0) {                          // verschieben
        doc_.transform(selection_, [d](Vec2 q) { return q + d; });
    } else if (drag_->handle == 8) {                  // drehen um die Mitte
        Vec2 c{b.x + b.w / 2, b.y + b.h / 2};
        double a0 = std::atan2(drag_->start.y - c.y, drag_->start.x - c.x);
        double a = std::atan2(p.y - c.y, p.x - c.x) - a0;
        if (shift) a = std::round(a / (M_PI / 12)) * (M_PI / 12);   // 15°-Schritte
        double cs = std::cos(a), sn = std::sin(a);
        doc_.transform(selection_, [=](Vec2 q) {
            Vec2 v = q - c;
            return Vec2{c.x + cs * v.x - sn * v.y, c.y + sn * v.x + cs * v.y};
        });
        message_ = "Drehung " + std::to_string(int(std::lround(a * 180 / M_PI))) + "°";
    } else {                                          // skalieren gegenüber dem Anfasser
        int h = drag_->handle;
        int opp = (h + 4) % 8;
        Vec2 anchor = onRect(b, opp), from = onRect(b, h);
        bool corner = h % 2 == 0;
        double sx = 1, sy = 1;
        if (std::abs(from.x - anchor.x) > 1e-9) sx = (from.x + d.x - anchor.x) / (from.x - anchor.x);
        if (std::abs(from.y - anchor.y) > 1e-9) sy = (from.y + d.y - anchor.y) / (from.y - anchor.y);
        if (!corner) {                                // Kante: nur eine Richtung …
            if (h == 1 || h == 5) sx = shift ? sy : 1;
            else sy = shift ? sx : 1;                 // … mit ⇧ proportional
        } else if (lockRatio_ != shift) {             // Ecke: proportional (⇧ kehrt um)
            double s = std::abs(sx) > std::abs(sy) ? sx : sy;
            sx = sy = s;
        }
        sx = std::max(sx, 0.01);
        sy = std::max(sy, 0.01);
        doc_.transform(selection_, [=](Vec2 q) {
            return Vec2{anchor.x + (q.x - anchor.x) * sx, anchor.y + (q.y - anchor.y) * sy};
        });
    }
    changed();
}

void AppController::pointerUp() {
    if (drag_ && drag_->moved) {
        Document before = drag_->before, after = doc_;
        std::string label = drag_->handle < 0 ? "Verschieben" : drag_->handle == 8 ? "Drehen" : "Skalieren";
        undo_.record({label, [after](Document& d) { d = after; }, [before](Document& d) { d = before; }});
        isSample_ = false;
        if (drag_->handle == 8) message_.clear();
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
        uint32_t nextPart = d.nextPartId();
        std::map<uint32_t, uint32_t> ids, parts;
        std::vector<Shape> copies;
        for (const auto& s : d.shapes) {
            if (!sel.count(s.object)) continue;
            if (!ids.count(s.object)) ids[s.object] = next++;
            Shape c = s;
            c.object = ids[s.object];
            if (!parts.count(s.part)) parts[s.part] = nextPart++;
            c.part = parts[s.part];
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

void AppController::skewSelection(double degX, double degY) {
    if (selection_.empty() || (std::abs(degX) < 1e-9 && std::abs(degY) < 1e-9)) return;
    Rect b = doc_.bounds(selection_);
    Vec2 c{b.x + b.w / 2, b.y + b.h / 2};
    double tx = std::tan(std::clamp(degX, -80.0, 80.0) * M_PI / 180);
    double ty = std::tan(std::clamp(degY, -80.0, 80.0) * M_PI / 180);
    editDoc("Neigen", [&](Document& d) {
        d.transform(selection_, [=](Vec2 q) { return Vec2{q.x + tx * (q.y - c.y), q.y + ty * (q.x - c.x)}; });
    });
}

void AppController::alignSelection(int mode) {
    if (selection_.empty()) return;
    // Mehrere Objekte: aneinander; ein Objekt: an der Arbeitsfläche
    Rect ref = selection_.size() > 1 ? doc_.bounds(selection_) : workArea();
    static const char* labels[] = {"Links ausrichten", "Horizontal zentrieren", "Rechts ausrichten",
                                   "Oben ausrichten", "Vertikal zentrieren", "Unten ausrichten"};
    ObjectSet sel = selection_;
    editDoc(labels[std::clamp(mode, 0, 5)], [&](Document& d) {
        for (uint32_t o : sel) {
            Rect b = d.bounds(ObjectSet{o});
            Vec2 delta{0, 0};
            switch (mode) {
                case 0: delta.x = ref.x - b.x; break;
                case 1: delta.x = ref.x + ref.w / 2 - (b.x + b.w / 2); break;
                case 2: delta.x = ref.x + ref.w - (b.x + b.w); break;
                case 3: delta.y = ref.y + ref.h - (b.y + b.h); break;   // oben = hinten = größtes y
                case 4: delta.y = ref.y + ref.h / 2 - (b.y + b.h / 2); break;
                case 5: delta.y = ref.y - b.y; break;
            }
            d.transform(ObjectSet{o}, [delta](Vec2 q) { return q + delta; });
        }
    });
}

void AppController::distributeSelection(bool horizontal) {
    if (!canDistribute()) return;
    ObjectSet sel = selection_;
    editDoc(horizontal ? "Horizontal verteilen" : "Vertikal verteilen", [&](Document& d) {
        std::vector<std::pair<uint32_t, Rect>> items;
        for (uint32_t o : sel) items.push_back({o, d.bounds(ObjectSet{o})});
        auto lo = [&](const Rect& r) { return horizontal ? r.x : r.y; };
        auto len = [&](const Rect& r) { return horizontal ? r.w : r.h; };
        std::sort(items.begin(), items.end(), [&](auto& a, auto& b) { return lo(a.second) < lo(b.second); });
        double start = lo(items.front().second);
        double end = lo(items.back().second) + len(items.back().second);
        double total = 0;
        for (auto& it : items) total += len(it.second);
        double gap = (end - start - total) / double(items.size() - 1);   // gleiche Lücken
        double pos = start;
        for (auto& [o, r] : items) {
            double delta = pos - lo(r);
            d.transform(ObjectSet{o}, [=](Vec2 q) { return horizontal ? Vec2{q.x + delta, q.y} : Vec2{q.x, q.y + delta}; });
            pos += len(r) + gap;
        }
    });
}

bool AppController::canUngroup() const {
    // Aufheben geht, wenn ein ausgewähltes Objekt aus mehr als einer Form besteht.
    std::map<uint32_t, int> count;
    for (const auto& s : doc_.shapes) if (selection_.count(s.object)) ++count[s.object];
    for (auto& [o, n] : count) if (n > 1) return true;
    return false;
}

void AppController::groupSelection() {
    if (!canGroup()) return;
    ObjectSet sel = selection_;
    uint32_t target = *sel.begin();
    editDoc("Gruppieren", [&](Document& d) { for (auto& s : d.shapes) if (sel.count(s.object)) s.object = target; });
    selection_ = {target};
    changed();
}

void AppController::ungroupSelection() {
    if (!canUngroup()) return;
    ObjectSet sel = selection_, fresh;
    editDoc("Gruppierung aufheben", [&](Document& d) {
        uint32_t next = d.nextObjectId();
        for (uint32_t obj : sel) {
            // Erst nach ursprünglichen Elementen trennen; ist alles ein Element, nach einzelnen Formen.
            std::set<uint32_t> parts;
            for (const auto& s : d.shapes) if (s.object == obj) parts.insert(s.part);
            bool byShape = parts.size() <= 1;
            std::map<uint32_t, uint32_t> ids;
            bool first = true;
            uint32_t nextPart = d.nextPartId();
            for (auto& s : d.shapes) {
                if (s.object != obj) continue;
                if (byShape) {
                    s.object = first ? obj : next++;
                    if (!first) s.part = nextPart++;
                    first = false;
                } else {
                    if (!ids.count(s.part)) ids[s.part] = ids.empty() ? obj : next++;
                    s.object = ids[s.part];
                }
                fresh.insert(s.object);
            }
        }
    });
    selection_ = fresh;
    changed();
}

void AppController::setSelectionNumber(const std::string& key, double v) {
    if (selection_.empty()) return;
    if (key == "selRotate") { rotateSelection(v); return; }
    if (key == "selSkewX") { skewSelection(v, 0); return; }
    if (key == "selSkewY") { skewSelection(0, v); return; }
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
    // Relative Eingaben: Wert eingeben, ↩ wendet ihn an, das Feld steht danach wieder auf 0.
    auto rel = [&](const char* key, const char* label, double lo, double hi) {
        Field x{Field::Kind::Number, key, label};
        x.value = 0; x.min = lo; x.max = hi; x.step = 1; x.unit = "°";
        x.enabled = canEdit();
        f.push_back(x);
    };
    f.push_back({Field::Kind::Header, "", "Drehen und Neigen"});
    rel("selRotate", "Drehen um", -360, 360);
    f.push_back({Field::Kind::Button, "rotL", "90° nach links"});
    f.push_back({Field::Kind::Button, "rotR", "90° nach rechts"});
    rel("selSkewX", "Neigen X", -80, 80);
    rel("selSkewY", "Neigen Y", -80, 80);
    f.push_back({Field::Kind::Header, "", selection_.size() > 1 ? "Ausrichten (aneinander)" : "Ausrichten (an der Fläche)"});
    Field al{Field::Kind::ButtonRow, "align", ""};
    al.options = {"align.horizontal.left|Links", "align.horizontal.center|Horizontal mittig", "align.horizontal.right|Rechts",
                  "align.vertical.top|Oben", "align.vertical.center|Vertikal mittig", "align.vertical.bottom|Unten"};
    al.enabled = canEdit();
    f.push_back(al);
    if (canDistribute()) {
        Field di{Field::Kind::ButtonRow, "distribute", ""};
        di.options = {"distribute.horizontal.center|Horizontal verteilen", "distribute.vertical.center|Vertikal verteilen"};
        f.push_back(di);
    }
    f.push_back({Field::Kind::Header, "", "Anordnen"});
    f.push_back({Field::Kind::Button, "flipH", "Horizontal spiegeln"});
    f.push_back({Field::Kind::Button, "flipV", "Vertikal spiegeln"});
    f.push_back({Field::Kind::Button, "center", "Auf der Fläche zentrieren"});
    if (canGroup()) f.push_back({Field::Kind::Button, "group", "Gruppieren (⌘G)"});
    if (canUngroup()) f.push_back({Field::Kind::Button, "ungroup", "Gruppierung aufheben (⇧⌘G)"});
    f.push_back({Field::Kind::Button, "duplicate", "Duplizieren (⌘D)"});
    f.push_back({Field::Kind::Button, "delete", "Löschen (⌫)"});
}

} // namespace mb
