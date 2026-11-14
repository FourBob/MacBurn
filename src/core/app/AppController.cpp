// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppController.hpp"
#include "io/Project.hpp"
#include "util/NumberInput.hpp"
#include "io/SvgImport.hpp"

#include <fstream>
#include <sstream>

#include <algorithm>
#include <cctype>
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
    userProfilesDir_ = userProfiles;
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
    device_ = std::make_unique<grbl::GrblDevice>([this](const grbl::DeviceEvent& e) {
        // Geräte-Thread → Main-Thread
        grbl::DeviceEvent copy = e;
        if (mainQueue_) mainQueue_([this, copy] { handleDevice(copy); });
        else handleDevice(copy);
    });
    refreshPorts();
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
    settingsDirty_ = true;
    changed();
}

void AppController::selectVariant(int index) { variant_ = index; settingsDirty_ = true; changed(); }
void AppController::selectMaterial(int index) { material_ = index; settingsDirty_ = true; changed(); }
void AppController::selectLayer(int index) { layer_ = index; changed(); }

void AppController::setChoice(const std::string& key, int index) {
    if (key == "intent" && index >= 0 && index < 4) {
        Intent i = kAllIntents[index];
        editLayer(std::string("Absicht ") + intentLabel(i), [i](Layer& l) { l.intent = i; });
    } else if (key == "variant") {
        selectVariant(index);
    } else if (key == "port") {
        port_ = index; changed();
    } else if (key == "jogStep") {
        jogStep_ = index; changed();
    } else if (key == "origin") {
        origin_ = index; settingsDirty_ = true; lockedHead_.reset(); changed();
    } else if (key == "testWhat" && test_) {
        test_->spec.engrave = index != 2;
        test_->spec.cut = index != 1;
        changed();
    } else if (key == "selLayer") {
        int offset = selectionLayer() ? 0 : 1;   // „Gemischt“ steht vorne
        if (index - offset >= 0) moveSelectionToLayer(size_t(index - offset));
    } else if (key == "textFamily") {
        const auto& fams = families();
        if (index < 0 || index >= int(fams.size())) return;
        std::string fam = fams[size_t(index)];
        auto styles = fonts().styles(fam);
        editText([&](TextSpec& t) {
            t.family = fam;
            // Schnitt behalten, wenn es ihn gibt; sonst „Regular“ oder den ersten
            if (std::find(styles.begin(), styles.end(), t.style) == styles.end())
                t.style = std::find(styles.begin(), styles.end(), "Regular") != styles.end() ? "Regular"
                        : styles.empty() ? "" : styles.front();
        }, "Schrift");
    } else if (key == "textStyle") {
        TextSpec cur = selectedTextPart() ? doc_.texts.at(*selectedTextPart()) : textTemplate_;
        auto styles = fonts().styles(cur.family);
        if (index >= 0 && index < int(styles.size())) editText([&](TextSpec& t) { t.style = styles[size_t(index)]; }, "Schriftschnitt");
    } else if (key == "refPoint") {
        refPoint_ = std::clamp(index, 0, 8); changed();
    } else if (key == "anchor") {
        anchor_ = std::clamp(index, 0, 8); settingsDirty_ = true; lockedHead_.reset(); changed();
    }
}

void AppController::setNumber(const std::string& key, double v) {
    if (key.rfind("test", 0) == 0 && setTestNumber(key, v)) return;
    if (key == "textCap") { editText([&](TextSpec& t) { t.capHeightMm = std::max(0.1, v); }, "Schrifthöhe"); return; }
    if (key == "textSpacing") { editText([&](TextSpec& t) { t.letterSpacingMm = v; }, "Zeichenabstand"); return; }
    if (key == "textLine") { editText([&](TextSpec& t) { t.lineSpacing = std::clamp(v / 100, 0.3, 5.0); }, "Zeilenabstand"); return; }
    if (key == "toolRadius") { rectRadius_ = std::max(0.0, v); changed(); return; }
    if (key == "toolX") { toolX_ = v; changed(); return; }
    if (key == "toolY") { toolY_ = v; changed(); return; }
    if (key == "toolW") { toolW_ = std::max(0.01, v); changed(); return; }
    if (key == "toolH") { toolH_ = std::max(0.01, v); changed(); return; }
    if (key == "toolDia") { toolDia_ = std::max(0.01, v); changed(); return; }
    if (key == "toolRot") { toolRot_ = v; changed(); return; }
    if (key == "toolLen") { toolLen_ = std::max(0.01, v); changed(); return; }
    if (key == "toolAngle") { toolAngle_ = v; changed(); return; }
    if (key == "toolSides") { polygonSides_ = std::clamp(int(std::lround(v)), 3, 64); changed(); return; }
    if (key == "toolPoints") { starPoints_ = std::clamp(int(std::lround(v)), 3, 64); changed(); return; }
    if (key == "toolInner") { starInner_ = std::clamp(v / 100.0, 0.05, 0.95); changed(); return; }
    if (key.rfind("sel", 0) == 0) { setSelectionNumber(key, v); return; }
    editLayer("Einstellung ändern", [&](Layer& l) {
        if (key == "speed") l.overrides.speed = v;
        else if (key == "power") l.overrides.power = v;
        else if (key == "passes") l.overrides.passes = int(std::lround(v));
        else if (key == "interval") l.overrides.lineIntervalMm = v;
    });
}

bool AppController::setNumberText(const std::string& key, const std::string& text) {
    auto v = parseNumberInput(text);
    if (!v) {
        message_ = "„" + text + "“ ist keine Zahl. Möglich sind z. B. 12,5 · 100/3 · 25+12,5 · 2in · 1,5cm.";
        changed();   // Feld zeigt wieder den alten Wert
        return false;
    }
    if (message_.rfind("„", 0) == 0) message_.clear();
    setNumber(key, *v);
    return true;
}

void AppController::setString(const std::string& key, const std::string& value) {
    if (key == "testName" && test_) { test_->name = value; changed(); return; }
    if (key == "textContent") {
        focusText_ = false;
        editText([&](TextSpec& t) { t.text = value; }, "Text ändern");
    }
}

void AppController::setToggle(const std::string& key, bool on) {
    if (key == "lockRatio") { lockRatio_ = on; changed(); return; }
    if (key == "output") editLayer(on ? "Ebene ausgeben" : "Ebene nicht ausgeben", [on](Layer& l) { l.output = on; });
}

void AppController::pressButton(const std::string& key) {
    if (key == "toolSelect") { setTool(Tool::Select); return; }
    if (key == "testSave") { saveTestedMaterial(); return; }
    if (key == "testCancel") { endMaterialTest(); return; }
    if (key == "testFrame") { frame(); return; }
    if (key == "materialTest") { startMaterialTest(false); return; }
    if (key == "textToPaths") { convertTextToPaths(); return; }
    if (key.rfind("textAlign:", 0) == 0) {
        int a = std::clamp(std::atoi(key.c_str() + 10), 0, 2);
        editText([&](TextSpec& t) { t.align = a; }, "Text ausrichten", true);   // Block bleibt an Ort und Stelle
        return;
    }
    if (key == "toolCreate") { createFromFields(std::nullopt); return; }
    if (key == "rot90" || key == "rotR") { rotateSelection(-90); return; }
    if (key == "rotL") { rotateSelection(90); return; }
    if (key == "group") { groupSelection(); return; }
    if (key.rfind("palette:", 0) == 0) { applyPaletteColor(std::atoi(key.c_str() + 8)); return; }
    if (key.rfind("combine:", 0) == 0) { combineSelection(std::atoi(key.c_str() + 8)); return; }
    if (key.rfind("align:", 0) == 0) { alignSelection(std::atoi(key.c_str() + 6)); return; }
    if (key == "distribute:0") { distributeSelection(true); return; }
    if (key == "distribute:1") { distributeSelection(false); return; }
    if (key == "ungroup") { ungroupSelection(); return; }
    if (key == "flipH") { flipSelection(true); return; }
    if (key == "flipV") { flipSelection(false); return; }
    if (key == "center") { centerSelection(); return; }
    if (key == "duplicate") { duplicateSelection(); return; }
    if (key == "delete") { deleteSelection(); return; }
    if (key == "connect") { connect(); return; }
    if (key == "disconnect") { disconnect(); return; }
    if (key == "refreshPorts") { refreshPorts(); changed(); return; }
    if (key == "home") { home(); return; }
    if (key == "unlock") { unlock(); return; }
    if (key == "frame") { frame(); return; }
    if (key == "stop") { stop(); return; }
    if (key == "jog+x") { jog(1, 0); return; }
    if (key == "jog-x") { jog(-1, 0); return; }
    if (key == "jog+y") { jog(0, 1); return; }
    if (key == "jog-y") { jog(0, -1); return; }
    if (key == "resetLayer") editLayer("Ebene zurücksetzen", [](Layer& l) { l.overrides = {}; });
    else if (key == "resetAll") {
        Document before = doc_, after = doc_;
        for (auto& l : after.layers) l.overrides = {};
        undo_.perform(doc_, {"Material zurücksetzen",
            [after](Document& d) { d = after; }, [before](Document& d) { d = before; }});
        changed();
    }
}

bool AppController::isModified() const {
    if (isSample_ && path_.empty()) return false;   // unverändertes Beispiel
    return undo_.stateId() != savedState_ || settingsDirty_;
}

AppController::ImportResult AppController::openFile(const std::string& path) {
    std::string lower = path;
    for (auto& ch : lower) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (lower.size() < 8 || lower.substr(lower.size() - 8) != ".macburn") return importFile(path);
    if (dev_.jobRunning) return {false, "Während ein Auftrag läuft, kann kein Projekt geöffnet werden."};

    std::ifstream in(path, std::ios::binary);
    if (!in) return {false, "Die Datei lässt sich nicht lesen."};
    std::stringstream ss;
    ss << in.rdbuf();
    std::string err;
    auto p = readProject(ss.str(), &err);
    if (!p) return {false, err};

    std::vector<std::string> notes;
    // Maschine
    const auto& ms = store_.machines();
    auto mit = std::find_if(ms.begin(), ms.end(), [&](const MachineProfile& m) { return m.id == p->machineId; });
    if (mit != ms.end()) machine_ = int(mit - ms.begin());
    else if (!p->machineId.empty()) notes.push_back("Maschine „" + p->machineId + "“ ist hier nicht eingerichtet");
    variant_ = 0;
    if (auto* m = machine())
        for (size_t i = 0; i < m->variants.size(); ++i) if (m->variants[i].id == p->variantId) variant_ = int(i);
    // Material; fehlt es, die mitgesicherten Werte verwenden
    auto pick = [&] {
        auto c = choices();
        for (size_t i = 0; i < c.size(); ++i) if (c[i].material->id == p->materialId) { material_ = int(i); return true; }
        return false;
    };
    if (!pick() && p->material) {
        MaterialProfile snap = *p->material;
        snap.machine = machine() ? machine()->id : "";
        snap.name += " (aus Projekt)";
        store_.add(snap);
        pick();
        notes.push_back("Material „" + p->material->name + "“ fehlt hier – die Werte aus dem Projekt werden verwendet");
    }
    origin_ = p->position;
    anchor_ = p->anchor;

    doc_ = std::move(p->doc);
    undo_.clear();
    savedState_ = undo_.stateId();
    settingsDirty_ = false;
    isSample_ = false;
    selection_.clear();
    layer_ = 0;
    path_ = path;
    std::string name = path.substr(path.find_last_of('/') + 1);
    title_ = name.substr(0, name.size() - 8);
    message_ = name + " geöffnet";
    for (auto& n : notes) message_ += " · " + n;
    changed();
    return {true, message_};
}

AppController::ImportResult AppController::saveProject(const std::string& path) {
    ProjectData p;
    p.doc = doc_;
    if (auto* m = machine()) {
        p.machineId = m->id;
        if (variant_ < int(m->variants.size())) p.variantId = m->variants[size_t(variant_)].id;
    }
    if (auto* mat = material()) { p.materialId = mat->id; p.material = *mat; }
    p.position = origin_;
    p.anchor = anchor_;
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return {false, "Die Datei lässt sich hier nicht anlegen."};
        out << writeProject(p);
        if (!out) return {false, "Beim Schreiben ist ein Fehler aufgetreten."};
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {   // erst vollständig schreiben, dann ersetzen
        std::remove(tmp.c_str());
        return {false, "Die Datei lässt sich nicht ersetzen."};
    }
    path_ = path;
    std::string name = path.substr(path.find_last_of('/') + 1);
    title_ = name.size() > 8 && name.substr(name.size() - 8) == ".macburn" ? name.substr(0, name.size() - 8) : name;
    savedState_ = undo_.stateId();
    settingsDirty_ = false;
    isSample_ = false;
    message_ = "Gesichert.";
    changed();
    return {true, message_};
}

void AppController::newDocument() {
    Document before = doc_, after;
    undo_.perform(doc_, {"Neues Projekt", [after](Document& d) { d = after; }, [before](Document& d) { d = before; }});
    isSample_ = false;
    selection_.clear();
    title_ = "Unbenannt";
    path_.clear();
    savedState_ = undo_.stateId();
    settingsDirty_ = false;
    layer_ = 0;
    message_.clear();
    changed();
}

AppController::ImportResult AppController::importFile(const std::string& path) {
    std::string ext = path.size() >= 4 ? path.substr(path.size() - 4) : "";
    for (auto& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (ext != ".svg") return {false, "Dieses Format wird noch nicht unterstützt. Möglich ist bisher SVG."};

    SvgImportResult r = importSvgFile(path);
    if (!r.ok) return {false, r.error};

    Document before = doc_;
    Document after = isSample_ ? Document{} : doc_;
    // Ebenen nach Farbe zusammenführen
    std::vector<size_t> map(r.layers.size());
    for (size_t i = 0; i < r.layers.size(); ++i) {
        auto it = std::find_if(after.layers.begin(), after.layers.end(),
                               [&](const Layer& l) { return !l.colorKey.empty() && l.colorKey == r.layers[i].colorKey; });
        if (it != after.layers.end()) map[i] = size_t(it - after.layers.begin());
        else { map[i] = after.layers.size(); after.layers.push_back(r.layers[i]); }
    }
    size_t firstNew = after.shapes.size();
    uint32_t object = after.nextObjectId();   // ein importiertes SVG ist ein Objekt
    uint32_t partBase = after.nextPartId();
    for (auto& s : r.shapes) {
        s.layer = map[s.layer];
        s.object = object;
        s.part += partBase;
        after.shapes.push_back(std::move(s));
    }

    std::string name = path.substr(path.find_last_of('/') + 1);
    undo_.perform(doc_, {"Import " + name, [after](Document& d) { d = after; }, [before](Document& d) { d = before; }});
    if (isSample_) { title_ = name.substr(0, name.size() - 4); isSample_ = false; layer_ = 0; }
    selection_ = {object};   // das Importierte ist gleich ausgewählt

    size_t count = after.shapes.size() - firstNew;
    message_ = name + ": " + std::to_string(count) + (count == 1 ? " Form" : " Formen") + " in "
             + std::to_string(r.layers.size()) + (r.layers.size() == 1 ? " Ebene" : " Ebenen") + " importiert";
    for (auto& w : r.warnings) message_ += " · " + w;
    changed();
    return {true, message_};
}

void AppController::undo() { undo_.undo(doc_); selection_.clear(); changed(); }
void AppController::redo() { undo_.redo(doc_); selection_.clear(); changed(); }

std::string AppController::exportGcode() const {
    GcodeOptions o;
    if (auto* m = machine()) { o.sMax = m->sMax; o.airAssistGcode = m->airAssistControllable; o.travelSpeed = m->travelSpeedMmMin; }
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
        addDeviceFields(f);
        addTestFields(f);
        f.push_back({Field::Kind::Header, "", "Maschine"});
        if (m) {
            Field var{Field::Kind::Choice, "variant", "Ausführung"};
            for (const auto& v : m->variants) var.options.push_back(v.name + fmt(" (%.0f", v.area.x) + fmt(" × %.0f mm)", v.area.y));
            var.selected = variant_;
            f.push_back(var);
            if (!m->focusHint.empty()) f.push_back({Field::Kind::Info, "", "Fokus: " + m->focusHint});
            if (!m->airAssistControllable) f.push_back({Field::Kind::Info, "", "Die Luftpumpe wird am Gerät geschaltet."});
        }
        return f;
    }

    addToolFields(f);
    addTextFields(f);
    if (tool_ == Tool::Select) addSelectionFields(f);
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
    else if (st == MaterialStatus::Vendor) stText += "Werte aus der Anleitung des Herstellers. Ein eigener Materialtest kann sie verbessern.";
    else if (st == MaterialStatus::Derived) stText += "Aus Systemprofil abgeleitet. Ein Materialtest wird empfohlen.";
    else stText += "Im Projekt geändert.";
    Field stf{Field::Kind::Info, "", stText};
    stf.highlight = st == MaterialStatus::Derived || st == MaterialStatus::Modified;
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
    const Job& j = test_ ? testPlate().job : job_;   // im Materialtest: Zeit der Testplatte
    double s = m ? estimateSeconds(j, m->travelSpeedMmMin, m->accelerationMmS2) : estimateSeconds(j, 3000);
    int t = int(std::lround(s));
    char b[32];
    if (t >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else std::snprintf(b, sizeof b, "%02d:%02d", t / 60, t % 60);
    return b;
}

std::string AppController::statusText() const {
    auto* m = machine();
    std::string s = m ? m->displayName() : "Keine Maschine";
    if (dev_.connected) {
        Vec2 p = workPos();
        std::string st = dev_.state.rfind("Run", 0) == 0 ? (dev_.jobRunning ? "Lasert …" : "Fährt (Laser aus)")
                                                         : grbl::describeState(dev_.state);
        s += " · " + st + fmt(" · X %.1f", p.x) + fmt("  Y %.1f", p.y);
    } else if (dev_.connecting) s += " · verbinde …";
    else s += " · nicht verbunden";
    Rect wa = workArea(), b = doc_.bounds();
    if (!wa.contains(b)) s += " · ⚠ Design liegt außerhalb der Arbeitsfläche";
    if (!store_.errors().empty()) s += " · " + std::to_string(store_.errors().size()) + " Profil(e) fehlerhaft";
    if (!message_.empty()) s = message_ + "   —   " + s;
    return s;
}

std::string AppController::primaryActionTitle() const {
    switch (primaryAction()) {
        case PrimaryAction::GoToLaser: return "Weiter zum Lasern";
        case PrimaryAction::Connect: return "Verbinden";
        case PrimaryAction::Connecting: return "Verbinde …";
        case PrimaryAction::StartJob: return test_ ? "Test lasern …" : "Lasern …";
        case PrimaryAction::Pause: return "Pause";
        case PrimaryAction::Resume: return "Fortsetzen";
        case PrimaryAction::Unlock: return "Entsperren";
    }
    return "";
}

std::string AppController::undoTitle() const { return canUndo() ? "Widerrufen: " + undo_.undoLabel() : "Widerrufen"; }
std::string AppController::redoTitle() const { return canRedo() ? "Wiederholen: " + undo_.redoLabel() : "Wiederholen"; }

RenderList AppController::renderList() const {
    RenderList rl = buildRenderList(doc_, job_, phase_, workArea());
    if (!selection_.empty() && phase_ != Phase::Laser) {
        rl.selection = doc_.bounds(selection_);
        rl.handles = selectionHandles();
    }
    if (phase_ == Phase::Laser && test_) {
        // Statt des Designs: die Testplatte am Laserkopf (ohne Verbindung bei 10/10)
        auto plate = testPlate();
        Vec2 off = testOffset().value_or(dev_.connected ? headForPlacement() : Vec2{10, 10});
        rl.strokes.clear();
        for (auto& p : plate.preview) {
            Stroke st{{}, p.closed, {0.48f, 0.26f, 0.13f, 1}, 1.0f, false};
            for (auto& q : p.points) st.points.push_back(q + off);
            rl.strokes.push_back(std::move(st));
        }
        rl.jobFrame = Rect{plate.bounds.x + off.x, plate.bounds.y + off.y, plate.bounds.w, plate.bounds.h};
        if (dev_.connected) rl.head = workPos();
        return rl;
    }
    if (phase_ == Phase::Laser) {
        if (dev_.connected) rl.head = workPos();
        if (auto off = offsetFor()) {
            Rect b = outputBounds();
            rl.jobFrame = Rect{b.x + off->x, b.y + off->y, b.w, b.h};
            if (off->x != 0 || off->y != 0)
                for (auto& st : rl.strokes) for (auto& p : st.points) p = p + *off;
        }
    }
    return rl;
}

} // namespace mb
