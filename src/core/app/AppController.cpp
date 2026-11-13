// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppController.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mb {

namespace {

std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

std::string thickness(double mm) {
    if (mm <= 0) return "";
    std::string s = fmt(std::fmod(mm, 1.0) == 0 ? "%.0f" : "%.1f", mm);
    std::replace(s.begin(), s.end(), '.', ',');
    return s + " mm";
}

int intentIndex(Intent i) {
    for (int k = 0; k < 4; ++k) if (kAllIntents[k] == i) return k;
    return 0;
}

} // namespace

AppController::AppController(const std::string& systemProfiles, const std::string& userProfiles) {
    store_.loadDirectory(systemProfiles, "system");
    store_.loadDirectory(userProfiles, "user");
    if (store_.machines().empty()) {
        MachineProfile fallback;
        fallback.id = "generic/grbl";
        fallback.vendor = "GRBL";
        fallback.model = "Diodenlaser";
        fallback.powerClass = "diode-10w-445nm";
        fallback.opticalPowerW = 10;
        fallback.variants.push_back({"standard", "Standard", {400, 400}});
        store_.add(fallback);
        message_ = "Keine Maschinenprofile gefunden – allgemeines GRBL-Profil aktiv.";
    }
    doc_ = Document::sample();
    rebuild();
}

// ---------- Zugriff ----------

const MachineProfile* AppController::machine() const {
    const auto& ms = store_.machines();
    return machine_ >= 0 && machine_ < int(ms.size()) ? &ms[size_t(machine_)] : nullptr;
}

std::vector<ProfileStore::MaterialChoice> AppController::choices() const {
    auto* m = machine();
    return m ? store_.materialsFor(*m) : std::vector<ProfileStore::MaterialChoice>{};
}

const MaterialProfile* AppController::material() const {
    auto c = choices();
    return material_ >= 0 && material_ < int(c.size()) ? c[size_t(material_)].material : nullptr;
}

MaterialStatus AppController::materialStatus() const {
    for (const auto& l : doc_.layers) if (!l.overrides.empty()) return MaterialStatus::Modified;
    auto c = choices();
    return material_ < int(c.size()) ? c[size_t(material_)].status : MaterialStatus::Derived;
}

Rect AppController::workArea() const {
    auto* m = machine();
    if (!m || m->variants.empty()) return {0, 0, 400, 400};
    const auto& v = m->variants[size_t(std::clamp(variant_, 0, int(m->variants.size()) - 1))];
    return {0, 0, v.area.x, v.area.y};
}

void AppController::rebuild() {
    job_ = {};
    if (auto* m = machine(); m && material()) job_ = JobBuilder(*m, *material()).build(doc_);
}

void AppController::changed() {
    rebuild();
    if (onChange_) onChange_();
}

void AppController::editLayer(const std::string& label, std::function<void(Layer&)> fn) {
    if (layer_ < 0 || layer_ >= int(doc_.layers.size())) return;
    size_t li = size_t(layer_);
    Layer before = doc_.layers[li];
    Layer after = before;
    fn(after);
    undo_.perform(doc_, {label,
        [li, after](Document& d) { d.layers[li] = after; },
        [li, before](Document& d) { d.layers[li] = before; }});
    changed();
}

// ---------- Befehle ----------

void AppController::setPhase(Phase p) { phase_ = p; changed(); }

void AppController::selectMachine(int index) {
    // Material über die gemeinsame Kennung auf die neue Maschine übertragen.
    std::string matId = material() ? material()->id : "";
    machine_ = index;
    variant_ = 0;
    material_ = 0;
    auto c = choices();
    for (size_t i = 0; i < c.size(); ++i) if (c[i].material->id == matId) { material_ = int(i); break; }
    message_.clear();
    changed();
}

void AppController::selectVariant(int index) { variant_ = index; changed(); }
void AppController::selectMaterial(int index) { material_ = index; changed(); }
void AppController::selectLayer(int index) { layer_ = index; changed(); }

void AppController::setChoice(const std::string& key, int index) {
    if (key == "intent" && index >= 0 && index < 4) {
        Intent i = kAllIntents[index];
        editLayer(std::string("Absicht ") + intentLabel(i), [i](Layer& l) { l.intent = i; });
    } else if (key == "variant") {
        selectVariant(index);
    }
}

void AppController::setNumber(const std::string& key, double v) {
    editLayer("Einstellung ändern", [&](Layer& l) {
        if (key == "speed") l.overrides.speed = v;
        else if (key == "power") l.overrides.power = v;
        else if (key == "passes") l.overrides.passes = int(std::lround(v));
        else if (key == "interval") l.overrides.lineIntervalMm = v;
    });
}

void AppController::setToggle(const std::string& key, bool on) {
    if (key == "output") editLayer(on ? "Ebene ausgeben" : "Ebene nicht ausgeben", [on](Layer& l) { l.output = on; });
}

void AppController::pressButton(const std::string& key) {
    if (key == "resetLayer") editLayer("Ebene zurücksetzen", [](Layer& l) { l.overrides = {}; });
    else if (key == "resetAll") {
        Document before = doc_, after = doc_;
        for (auto& l : after.layers) l.overrides = {};
        undo_.perform(doc_, {"Material zurücksetzen",
            [after](Document& d) { d = after; }, [before](Document& d) { d = before; }});
        changed();
    }
}

void AppController::undo() { undo_.undo(doc_); changed(); }
void AppController::redo() { undo_.redo(doc_); changed(); }

std::string AppController::exportGcode() const {
    GcodeOptions o;
    if (auto* m = machine()) { o.sMax = m->sMax; o.airAssistGcode = m->airAssistControllable; }
    return writeGcode(job_, o);
}

// ---------- View-Modelle ----------

CardModel AppController::machineCard() const {
    auto* m = machine();
    if (!m) return {"Maschine", "Keine Maschine", ""};
    std::string sub = fmt("Diode %.0f W", m->opticalPowerW) + fmt(" · %.0f nm", m->wavelengthNm);
    if (m->variants.size() > 1) sub += " · " + m->variants[size_t(variant_)].name;
    return {"Maschine", m->displayName(), sub};
}

CardModel AppController::materialCard() const {
    auto* mat = material();
    if (!mat) return {"Material", "Kein Material", "Für diese Maschine ist noch nichts hinterlegt"};
    auto st = materialStatus();
    std::string sub = thickness(mat->thicknessMm);
    if (!sub.empty()) sub += " · ";
    sub += std::string(statusGlyph(st)) + " " + statusLabel(st);
    return {"Material", mat->name, sub};
}

std::vector<ListItem> AppController::machineItems() const {
    std::vector<ListItem> out;
    for (size_t i = 0; i < store_.machines().size(); ++i) {
        const auto& m = store_.machines()[i];
        out.push_back({m.displayName(), fmt("%.0f W", m.opticalPowerW), int(i) == machine_, false});
    }
    return out;
}

std::vector<ListItem> AppController::materialItems() const {
    std::vector<ListItem> out;
    auto c = choices();
    std::string lastCat;
    for (size_t i = 0; i < c.size(); ++i) {
        const auto* m = c[i].material;
        out.push_back({std::string(statusGlyph(c[i].status)) + "  " + m->name, thickness(m->thicknessMm),
                       int(i) == material_, i > 0 && m->category != lastCat});
        lastCat = m->category;
    }
    return out;
}

std::vector<LayerRow> AppController::layerRows() const {
    std::vector<LayerRow> out;
    auto* m = machine();
    auto* mat = material();
    for (size_t i = 0; i < doc_.layers.size(); ++i) {
        const auto& l = doc_.layers[i];
        LayerRow r{l.name, intentLabel(l.intent), "—", l.color, int(i) == layer_, !l.overrides.empty(), l.output};
        if (m && mat) {
            auto s = ProfileStore::resolve(*m, *mat, l.intent, l.overrides);
            if (s.possible) r.values = fmt("%.0f", s.speed) + fmt(" · %.0f %%", s.power);
            else r.values = "nicht möglich";
        }
        out.push_back(r);
    }
    return out;
}

std::vector<Field> AppController::inspector() const {
    std::vector<Field> f;
    auto* m = machine();
    auto* mat = material();

    if (phase_ == Phase::Laser) {
        f.push_back({Field::Kind::Header, "", "Maschine"});
        if (m) {
            f.push_back({Field::Kind::Info, "", m->displayName()});
            Field var{Field::Kind::Choice, "variant", "Ausführung"};
            for (const auto& v : m->variants) var.options.push_back(v.name + fmt(" (%.0f", v.area.x) + fmt(" × %.0f mm)", v.area.y));
            var.selected = variant_;
            f.push_back(var);
            if (!m->focusHint.empty()) f.push_back({Field::Kind::Info, "", "Fokus: " + m->focusHint});
            if (!m->airAssistControllable) f.push_back({Field::Kind::Info, "", "Luftpumpe wird am Gerät geschaltet."});
        }
        f.push_back({Field::Kind::Header, "", "Auftrag"});
        f.push_back({Field::Kind::Info, "", "Geschätzte Laufzeit: " + estimateText()});
        f.push_back({Field::Kind::Info, "", fmt("Brennweg %.0f mm", job_.burnLengthMm) + fmt(" · Leerfahrt %.0f mm", job_.travelLengthMm)});
        f.push_back({Field::Kind::Info, "", "Gerätesteuerung (Verbinden, Joggen, Rahmen) folgt im nächsten Schritt. G-Code-Export: Ablage › G-Code exportieren …"});
        return f;
    }

    if (layer_ < 0 || layer_ >= int(doc_.layers.size())) return f;
    const Layer& l = doc_.layers[size_t(layer_)];
    f.push_back({Field::Kind::Header, "", "Ebene " + l.name});
    Field intent{Field::Kind::Choice, "intent", "Absicht"};
    for (Intent i : kAllIntents) intent.options.push_back(intentLabel(i));
    intent.selected = intentIndex(l.intent);
    f.push_back(intent);
    Field out{Field::Kind::Toggle, "output", "Ausgeben"};
    out.on = l.output;
    f.push_back(out);

    if (phase_ == Phase::Design) {
        int n = 0;
        for (const auto& s : doc_.shapes) if (s.layer == size_t(layer_)) ++n;
        f.push_back({Field::Kind::Info, "", std::to_string(n) + (n == 1 ? " Form" : " Formen") + " auf dieser Ebene"});
        return f;
    }

    // Vorbereiten
    f.push_back({Field::Kind::Header, "", "Maschine → Material"});
    if (!m || !mat) {
        f.push_back({Field::Kind::Info, "", "Kein Material gewählt."});
        return f;
    }
    f.push_back({Field::Kind::Info, "", m->displayName() + " → " + mat->name + " " + thickness(mat->thicknessMm)});
    auto st = materialStatus();
    std::string stText = std::string(statusGlyph(st)) + " ";
    if (st == MaterialStatus::Tested) stText += "Auf dieser Maschine getestet.";
    else if (st == MaterialStatus::Derived) stText += "Aus Systemprofil abgeleitet. Ein Materialtest wird empfohlen.";
    else stText += "Im Projekt geändert.";
    Field stf{Field::Kind::Info, "", stText};
    stf.highlight = st != MaterialStatus::Tested;
    f.push_back(stf);

    auto s = ProfileStore::resolve(*m, *mat, l.intent, l.overrides);
    if (!s.possible) {
        Field w{Field::Kind::Info, "", std::string("Dieses Material lässt sich nicht ") + (l.intent == Intent::Cut ? "schneiden." : "so bearbeiten.")};
        w.highlight = true;
        f.push_back(w);
        return f;
    }
    auto num = [&](const char* key, const char* label, double v, double lo, double hi, double step, const char* unit, bool mod) {
        Field n{Field::Kind::Number, key, label};
        n.value = v; n.min = lo; n.max = hi; n.step = step; n.unit = unit; n.highlight = mod;
        f.push_back(n);
    };
    f.push_back({Field::Kind::Header, "", "Einstellungen"});
    num("speed", "Geschwindigkeit", s.speed, 50, m->maxSpeedMmMin, 10, "mm/min", bool(l.overrides.speed));
    num("power", "Leistung", s.power, 0, 100, 1, "%", bool(l.overrides.power));
    if (l.intent == Intent::Cut || l.intent == Intent::Mark)
        num("passes", "Durchgänge", s.passes, 1, 20, 1, "", bool(l.overrides.passes));
    else
        num("interval", "Zeilenabstand", s.lineIntervalMm, 0.02, 1.0, 0.01, "mm", bool(l.overrides.lineIntervalMm));
    if (!l.overrides.empty()) f.push_back({Field::Kind::Button, "resetLayer", "Ebene auf Materialwerte zurücksetzen"});
    if (st == MaterialStatus::Modified) f.push_back({Field::Kind::Button, "resetAll", "Alle Änderungen verwerfen"});
    return f;
}

std::string AppController::estimateText() const {
    auto* m = machine();
    double s = estimateSeconds(job_, m ? m->maxSpeedMmMin : 6000);
    int t = int(std::lround(s));
    char b[32];
    if (t >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else std::snprintf(b, sizeof b, "%02d:%02d", t / 60, t % 60);
    return b;
}

std::string AppController::statusText() const {
    if (!message_.empty()) return message_;
    auto* m = machine();
    std::string s = m ? m->displayName() + " · nicht verbunden" : "Keine Maschine";
    Rect wa = workArea(), b = doc_.bounds();
    if (!wa.contains(b)) s += " · ⚠ Design liegt außerhalb der Arbeitsfläche";
    if (!store_.errors().empty()) s += " · " + std::to_string(store_.errors().size()) + " Profil(e) fehlerhaft";
    return s;
}

std::string AppController::primaryActionTitle() const {
    return phase_ == Phase::Laser ? "Lasern" : "Weiter zum Lasern";
}

std::string AppController::undoTitle() const { return canUndo() ? "Widerrufen: " + undo_.undoLabel() : "Widerrufen"; }
std::string AppController::redoTitle() const { return canRedo() ? "Wiederholen: " + undo_.redoLabel() : "Wiederholen"; }

RenderList AppController::renderList() const {
    return buildRenderList(doc_, job_, phase_, workArea());
}

} // namespace mb
