// MacBurn – Materialtest-Assistent: Verläufe brennen, Skala ablesen, als „✓ getestet“ speichern.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppController.hpp"
#include "util/Json.hpp"
#include "util/NumberInput.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace mb {

namespace {

std::string slug(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c)) out += char(std::tolower(c));
        else if (c == 0xC3) continue;   // Umlaute grob: ä→a usw. (zweites Byte folgt)
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "material" : out;
}

std::string fmt1(double v) { return formatNumber(v, 1); }

} // namespace

void AppController::startMaterialTest(bool newMaterial) {
    TestState t;
    t.newMaterial = newMaterial;
    auto* m = machine();
    auto* mat = material();
    if (m) {
        t.spec.travelSpeed = m->travelSpeedMmMin;
        t.spec.engraveSpeed = std::min(3000.0, m->maxSpeedMmMin);
        t.spec.cutSpeedMax = std::min(1200.0, m->maxSpeedMmMin);
    }
    if (mat && !newMaterial) {
        t.name = mat->name;
        t.category = mat->category;
        t.thicknessMm = mat->thicknessMm;
        // Bereich um die bisherigen Werte legen
        if (m) {
            auto cut = ProfileStore::resolve(*m, *mat, Intent::Cut);
            if (cut.possible) {
                t.spec.cutSpeedMax = std::min(m->maxSpeedMmMin, std::round(cut.speed * 2.5 / 10) * 10);
                t.spec.cutSpeedMin = std::max(30.0, std::round(cut.speed * 0.4 / 10) * 10);
                t.spec.cutPower = cut.power;
            } else {
                t.spec.cut = false;
            }
            auto eng = ProfileStore::resolve(*m, *mat, Intent::Engrave);
            if (eng.possible) { t.spec.engraveSpeed = eng.speed; t.spec.lineIntervalMm = eng.lineIntervalMm; }
        }
    } else {
        t.name = "Neues Material";
    }
    test_ = t;
    lockedHead_.reset();
    phase_ = Phase::Laser;
    message_ = "Materialtest: Werte prüfen, Kopf auf eine freie Stelle des Materials fahren, Rahmen abfahren, dann lasern.";
    changed();
}

void AppController::endMaterialTest() {
    test_.reset();
    lockedHead_.reset();
    message_.clear();
    changed();
}

MaterialTestPlate AppController::testPlate() const {
    if (!test_) return {};
    return buildMaterialTest(test_->spec, &fonts());
}

std::optional<Vec2> AppController::testOffset() const {
    // Linke untere Ecke der Testplatte am Laserkopf
    if (!dev_.connected) return std::nullopt;
    auto plate = testPlate();
    Vec2 off = headForPlacement();
    Rect wa = workArea();
    Rect moved{plate.bounds.x + off.x, plate.bounds.y + off.y, plate.bounds.w, plate.bounds.h};
    if (!wa.contains(moved)) return std::nullopt;
    return off;
}

bool AppController::setTestNumber(const std::string& key, double v) {
    if (!test_) return false;
    auto& s = test_->spec;
    auto& t = *test_;
    if (key == "testSpeed") s.engraveSpeed = std::clamp(v, 50.0, machine() ? machine()->maxSpeedMmMin : 6000.0);
    else if (key == "testPmin") s.powerMin = std::clamp(v, 0.0, s.powerMax - 1);
    else if (key == "testPmax") s.powerMax = std::clamp(v, s.powerMin + 1, 100.0);
    else if (key == "testCutMax") s.cutSpeedMax = std::max(v, s.cutSpeedMin + 10);
    else if (key == "testCutMin") s.cutSpeedMin = std::clamp(v, 10.0, s.cutSpeedMax - 10);
    else if (key == "testCutPower") s.cutPower = std::clamp(v, 1.0, 100.0);
    else if (key == "testPasses") s.cutPasses = std::clamp(int(std::lround(v)), 1, 10);
    else if (key == "testThickness") t.thicknessMm = std::max(0.0, v);
    else if (key == "testReadEngrave") t.readEngrave = std::clamp(v, 0.0, 100.0);
    else if (key == "testReadControl") t.readControl = std::clamp(v, 0.0, 100.0);
    else if (key == "testReadCut") t.readCut = std::clamp(v, 0.0, 100.0);
    else return false;
    changed();
    return true;
}

void AppController::addTestFields(std::vector<Field>& f) const {
    if (!test_) return;
    const auto& t = *test_;
    const auto& s = t.spec;
    auto num = [&](const char* key, const char* label, double v, double lo, double hi, double step, const char* unit, bool en = true) {
        Field x{Field::Kind::Number, key, label};
        x.value = v; x.min = lo; x.max = hi; x.step = step; x.unit = unit; x.enabled = en && !dev_.jobRunning;
        f.push_back(x);
    };
    f.push_back({Field::Kind::Header, "", "Materialtest"});
    if (t.newMaterial) {
        Field name{Field::Kind::TextArea, "testName", ""};
        name.text = t.name;
        f.push_back(name);
        num("testThickness", "Dicke", t.thicknessMm, 0, 50, 0.5, "mm");
    } else {
        f.push_back({Field::Kind::Info, "", "Material: " + t.name + (t.thicknessMm > 0 ? " " + fmt1(t.thicknessMm) + " mm" : "")});
    }
    Field what{Field::Kind::Choice, "testWhat", "Testen", {"Gravur und Schnitt", "Nur Gravur", "Nur Schnitt"}};
    what.selected = s.engrave && s.cut ? 0 : s.engrave ? 1 : 2;
    what.enabled = !dev_.jobRunning;
    f.push_back(what);
    if (s.engrave) {
        num("testSpeed", "Gravur-Tempo", s.engraveSpeed, 50, 6000, 10, "mm/min");
        num("testPmin", "Leistung von", s.powerMin, 0, 99, 1, "%");
        num("testPmax", "bis", s.powerMax, 1, 100, 1, "%");
    }
    if (s.cut) {
        num("testCutMax", "Schnitt-Tempo von", s.cutSpeedMax, 20, 6000, 10, "mm/min");
        num("testCutMin", "bis", s.cutSpeedMin, 10, 6000, 10, "mm/min");
        num("testCutPower", "Schnitt-Leistung", s.cutPower, 1, 100, 1, "%");
        num("testPasses", "Durchgänge", s.cutPasses, 1, 10, 1, "");
    }
    auto plate = testPlate();
    auto* m = machine();
    double secs = estimateSeconds(plate.job, m ? m->travelSpeedMmMin : 3000, m ? m->accelerationMmS2 : 1000);
    char b[96];
    std::snprintf(b, sizeof b, "Testplatte %.0f × %.0f mm · ca. %d:%02d min", plate.bounds.w, plate.bounds.h, int(secs) / 60, int(secs) % 60);
    f.push_back({Field::Kind::Info, "", b});
    f.push_back({Field::Kind::Info, "", lockedHead_ ? "📍 Position festgelegt (Rahmen/Test). Pfeiltasten lösen sie wieder."
                                                  : "Die linke untere Ecke liegt am Laserkopf. Kopf auf eine freie Stelle fahren, Rahmen abfahren."});
    if (dev_.connected && !testOffset()) {
        Field w{Field::Kind::Info, "", "⚠ Von hier aus ragt die Testplatte über die Arbeitsfläche."};
        w.highlight = true;
        f.push_back(w);
    }
    Field fr{Field::Kind::Button, "testFrame", "Rahmen abfahren (Laser aus)"};
    fr.enabled = deviceIdle() && testOffset().has_value();
    f.push_back(fr);

    // Auswertung
    f.push_back({Field::Kind::Header, "", "Auswertung"});
    f.push_back({Field::Kind::Info, "", t.ran ? "Skalenwerte ablesen und eintragen (0–100)."
                                            : "Nach dem Brennen ablesen (0–100): Gravur-Wert = wo die Gravur am besten aussieht; Schnitt-Wert = ab wo der Streifen durchgetrennt ist."});
    if (s.engrave) {
        num("testReadEngrave", "Gravur-Wert", std::max(0.0, t.readEngrave), 0, 100, 1, "");
        if (t.readEngrave >= 0) {
            double p = testEngravePower(s, t.readEngrave);
            f.push_back({Field::Kind::Info, "", "→ Gravieren: " + fmt1(s.engraveSpeed) + " mm/min · " + fmt1(p) + " %"});
        }
        num("testReadControl", "Kontroll-Wert", std::max(0.0, t.readControl), 0, 100, 1, "");
        f.push_back({Field::Kind::Info, "", "Kontroll-Wert: wo der obere Streifen genauso dunkel ist. Nur eintragen, wenn es nicht dieselbe Stelle ist."});
    }
    if (s.cut) {
        num("testReadCut", "Schnitt-Wert", std::max(0.0, t.readCut), 0, 100, 1, "");
        if (t.readCut >= 0) {
            double v = testCutSpeed(s, t.readCut);
            f.push_back({Field::Kind::Info, "", "→ Schneiden: durch bei " + fmt1(v) + " mm/min, gespeichert mit 15 % Reserve: " +
                                                    fmt1(std::round(v * 0.85 / 10) * 10) + " mm/min"});
        }
    }
    Field save{Field::Kind::Button, "testSave", "Als Material speichern (✓ getestet)"};
    save.enabled = (!s.engrave || t.readEngrave >= 0) && (!s.cut || t.readCut >= 0) && !dev_.jobRunning;
    f.push_back(save);
    f.push_back({Field::Kind::Button, "testCancel", "Materialtest beenden"});
}

void AppController::saveTestedMaterial() {
    if (!test_) return;
    auto& t = *test_;
    auto* m = machine();
    if (!m) return;
    const auto& s = t.spec;
    MaterialProfile p;
    const MaterialProfile* base = t.newMaterial ? nullptr : material();
    if (base) p = *base;
    p.name = t.name.empty() ? "Material" : t.name;
    p.thicknessMm = t.thicknessMm;
    p.category = t.category;
    if (!base) p.id = slug(p.name) + (t.thicknessMm > 0 ? "-" + slug(formatNumber(t.thicknessMm, 1)) + "mm" : "");
    p.machine = m->id;
    p.powerClass.clear();
    p.source = "user";

    std::string notes = "Materialtest am ";
    if (s.engrave && t.readEngrave >= 0) {
        IntentSettings e = p.intents.count(Intent::Engrave) ? p.intents[Intent::Engrave] : IntentSettings{};
        double p1 = testEngravePower(s, t.readEngrave);
        e.speed = s.engraveSpeed;
        e.power = p1;
        e.lineIntervalMm = s.lineIntervalMm;
        e.powerBySpeed = {{s.engraveSpeed, p1}};
        if (t.readControl >= 0) {
            double f = std::clamp(s.controlSpeedFactor, 0.1, 1.0);
            e.powerBySpeed.push_back({s.engraveSpeed * f, testEngravePower(s, t.readControl) * f});
        }
        p.intents[Intent::Engrave] = e;
        if (!p.intents.count(Intent::Mark)) p.intents[Intent::Mark] = {s.engraveSpeed, std::round(p1 * 0.4), {}, 1, {}, {}, {}};
    }
    if (s.cut && t.readCut >= 0) {
        IntentSettings c = p.intents.count(Intent::Cut) ? p.intents[Intent::Cut] : IntentSettings{};
        c.speed = std::round(testCutSpeed(s, t.readCut) * 0.85 / 10) * 10;
        c.power = s.cutPower;
        c.passes = s.cutPasses;
        c.airAssist = true;
        c.powerBySpeed.clear();
        p.intents[Intent::Cut] = c;
    }
    p.note = "Eigener Materialtest (Verlaufsstreifen) auf " + m->displayName() + ".";

    // Speichern unter <Benutzerprofile>/materials/<maschine>/<id>.json
    std::string machineDir = m->id;
    std::replace(machineDir.begin(), machineDir.end(), '/', '-');
    namespace fs = std::filesystem;
    fs::path dir = fs::path(userProfilesDir_) / "materials" / machineDir;
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream out(dir / (p.id + ".json"), std::ios::binary | std::ios::trunc);
    if (!out) { message_ = "Das Profil lässt sich nicht speichern (" + dir.string() + ")."; changed(); return; }
    out << json::write(materialToJson(p));
    out.close();

    store_.add(p);
    // Das neue Profil auswählen
    auto c = choices();
    for (size_t i = 0; i < c.size(); ++i)
        if (c[i].material->id == p.id && c[i].status == MaterialStatus::Tested) { material_ = int(i); break; }
    test_.reset();
    settingsDirty_ = true;
    message_ = "„" + p.name + "“ als getestetes Material gespeichert.";
    changed();
}

} // namespace mb
