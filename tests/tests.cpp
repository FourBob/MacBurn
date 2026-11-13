// MacBurn – Kerntests ohne Fenster und ohne Hardware.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppController.hpp"
#include "device/Grbl.hpp"
#include "device/GrblDevice.hpp"
#include "geometry/Boolean.hpp"
#include "io/Project.hpp"
#include "text/Font.hpp"
#include "io/SvgImport.hpp"
#include "job/Job.hpp"
#include "profiles/Profiles.hpp"
#include "util/Json.hpp"
#include "util/NumberInput.hpp"

#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <thread>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; std::printf("  FEHLER %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b) CHECK(std::abs(double(a) - double(b)) < 1e-6)
#define NEAR_OK(a, b) (std::abs(double(a) - double(b)) < 1e-6)

static std::vector<std::pair<const char*, std::function<void()>>> tests;
#define TEST(name) static void name(); static bool reg_##name = (tests.push_back({#name, name}), true); static void name()

using namespace mb;

TEST(json_parses_profiles) {
    auto v = json::parse(R"({"a": [1, 2.5, true, null], "s": "Pappel ä", "o": {"x": -3e2}})");
    CHECK(v.has_value());
    CHECK((*v)["a"].asArray().size() == 4);
    NEAR((*v)["a"].asArray()[1].asNumber(), 2.5);
    CHECK((*v)["s"].asString() == "Pappel ä");
    NEAR((*v)["o"]["x"].asNumber(), -300);
    CHECK((*v)["fehlt"].isNull());
    json::ParseError e;
    CHECK(!json::parse("{\"a\": }", &e));
}

TEST(loads_reference_machine_and_materials) {
    ProfileStore store;
    store.loadDirectory(std::string(MB_SOURCE_DIR) + "/profiles", "system");
    CHECK(store.errors().empty());
    auto* m = store.machine("mecpow/x3-pro");
    CHECK(m != nullptr);
    if (!m) return;
    NEAR(m->opticalPowerW, 10);
    NEAR(m->variants.at(0).area.x, 410);
    NEAR(m->variants.at(0).area.y, 410);   // laut $131
    NEAR(m->maxSpeedMmMin, 6000);           // laut $110
    NEAR(m->accelerationMmS2, 500);         // laut $120
    NEAR(m->travelSpeedMmMin, 3000);
    NEAR(m->variants.at(1).area.x, 750);
    CHECK(!m->airAssistControllable);
    auto mats = store.materialsFor(*m);
    CHECK(mats.size() >= 5);
    int vendor = 0;
    for (auto& c : mats) {
        CHECK(c.status != MaterialStatus::Tested);
        if (c.status == MaterialStatus::Vendor) {
            ++vendor;
            CHECK(c.material->machine == "mecpow/x3-pro");
        }
    }
    CHECK(vendor >= 3);
    // Herstellerwert ersetzt das Klassenprofil gleicher Kennung
    int poplar = 0;
    for (auto& c : mats) if (c.material->id == "plywood-poplar-3mm") { ++poplar; CHECK(c.status == MaterialStatus::Vendor); }
    CHECK(poplar == 1);
    for (auto& c : mats) if (c.material->id == "plywood-poplar-3mm") {
        auto s = ProfileStore::resolve(*m, *c.material, Intent::Cut);
        NEAR(s.speed, 250); NEAR(s.power, 100); CHECK(s.passes == 1);
    }
}

TEST(resolve_scales_and_applies_overrides) {
    MachineProfile m5; m5.id = "t/5w"; m5.opticalPowerW = 5; m5.powerClass = "diode-5w-445nm"; m5.maxSpeedMmMin = 10000;
    MaterialProfile mat; mat.id = "x"; mat.powerClass = "diode-10w-445nm";
    mat.intents[Intent::Cut] = {300.0, 100.0, {}, 2, {}, {}, {}};
    auto s = ProfileStore::resolve(m5, mat, Intent::Cut);
    NEAR(s.speed, 150);          // halbe Leistung → halbe Geschwindigkeit
    CHECK(s.passes == 2);
    IntentSettings ov; ov.power = 80;
    s = ProfileStore::resolve(m5, mat, Intent::Cut, ov);
    NEAR(s.power, 80);
    CHECK(!ProfileStore::resolve(m5, mat, Intent::Photo).possible);
}

TEST(user_profile_shadows_system_profile) {
    ProfileStore store;
    MachineProfile m; m.id = "mecpow/x3-pro"; m.powerClass = "diode-10w-445nm"; m.opticalPowerW = 10;
    store.add(m);
    MaterialProfile sys; sys.id = "plywood"; sys.name = "Sperrholz"; sys.powerClass = "diode-10w-445nm";
    MaterialProfile usr = sys; usr.machine = "mecpow/x3-pro"; usr.powerClass.clear(); usr.source = "user";
    store.add(sys); store.add(usr);
    auto c = store.materialsFor(*store.machine("mecpow/x3-pro"));
    CHECK(c.size() == 1);
    CHECK(c[0].status == MaterialStatus::Tested);
}

TEST(scanline_fill_square) {
    Polyline sq = Polyline::rectangle(0, 0, 10, 10);
    auto lines = JobBuilder::scanlineFill({&sq}, 1.0);
    CHECK(lines.size() == 10);
    NEAR(lines[0].first.x, 0);  NEAR(lines[0].second.x, 10);
    NEAR(lines[1].first.x, 10); NEAR(lines[1].second.x, 0);   // bidirektional
}

TEST(job_orders_and_writes_gcode) {
    MachineProfile m; m.id = "t"; m.opticalPowerW = 10; m.powerClass = "diode-10w-445nm"; m.maxSpeedMmMin = 6000;
    MaterialProfile mat; mat.powerClass = "diode-10w-445nm";
    mat.intents[Intent::Cut] = {300.0, 100.0, {}, 1, {}, {}, {}};
    mat.intents[Intent::Mark] = {3000.0, 10.0, {}, 1, {}, {}, {}};
    Document d;
    d.layers = {{"Schnitt", {}, Intent::Cut, true, {}}, {"Ritzen", {}, Intent::Mark, true, {}}};
    d.shapes = {{"aussen", 0, Polyline::rectangle(0, 0, 50, 50)},
                {"innen", 0, Polyline::rectangle(20, 20, 5, 5)},
                {"linie", 1, {{{1, 1}, {2, 1}}, false}}};
    Job job = JobBuilder(m, mat).build(d);
    // Erste Brennbewegung gehört zur Markierung, dann innen vor außen.
    size_t firstBurn = 0;
    while (job.moves[firstBurn].kind != Move::Kind::Burn) ++firstBurn;
    NEAR(job.moves[firstBurn].power, 10);
    size_t firstCut = firstBurn;
    while (job.moves[firstCut].power != 100) ++firstCut;
    NEAR(job.moves[firstCut - 1].to.x, 20);   // Anfahrt zur inneren Kontur
    GcodeOptions o; o.sMax = 1000;
    std::string g = writeGcode(job, o);
    CHECK(g.find("M4 S0") != std::string::npos);
    CHECK(g.find("S1000") != std::string::npos);
    CHECK(g.find("S100\n") != std::string::npos || g.find("S100 ") != std::string::npos || g.find(" S100") != std::string::npos);
    CHECK(g.rfind("M5") != std::string::npos);
    CHECK(estimateSeconds(job, 6000) > 0);
}

TEST(grbl_parsing) {
    auto st = grbl::parseStatus("<Idle|MPos:12.500,3.000,0.000|FS:0,0|Pn:XY>");
    CHECK(st.has_value());
    CHECK(st->state == "Idle");
    NEAR(st->machinePos.x, 12.5);
    CHECK(st->pins == "XY");
    CHECK(!grbl::parseStatus("ok"));
    auto s = grbl::parseSetting("$130=410.000");
    CHECK(s && s->key == 130);
    NEAR(s->value, 410);
    CHECK(!grbl::parseSetting("$N0="));
    CHECK(grbl::describeState("Hold:0") == "Pausiert");
}

TEST(controller_undo_and_status) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    CHECK(c.materialCard().subtitle.find("geändert") == std::string::npos);
    c.selectLayer(0);
    c.setNumber("power", 77);
    CHECK(c.materialCard().subtitle.find("geändert") != std::string::npos);
    CHECK(c.canUndo());
    c.undo();
    CHECK(c.materialCard().subtitle.find("geändert") == std::string::npos);
    c.redo();
    c.pressButton("resetAll");
    CHECK(c.materialCard().subtitle.find("geändert") == std::string::npos);
    c.setPhase(Phase::Prepare);
    CHECK(!c.renderList().strokes.empty());
    CHECK(c.exportGcode().find("G1") != std::string::npos);
}

// ---------- SVG-Import ----------

static Rect boundsOf(const SvgImportResult& r, size_t layer) {
    Polyline all;
    for (auto& s : r.shapes) if (s.layer == layer) all.points.insert(all.points.end(), s.path.points.begin(), s.path.points.end());
    return all.bounds();
}

TEST(svg_units_viewbox_and_flip) {
    // 100 × 50 mm, viewBox in 1/10 mm
    auto r = importSvg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="50mm" viewBox="0 0 1000 500">
        <rect x="100" y="100" width="200" height="100" fill="none" stroke="#f00"/></svg>)svg");
    CHECK(r.ok);
    NEAR(r.page.w, 100); NEAR(r.page.h, 50);
    CHECK(r.layers.size() == 1);
    CHECK(r.layers[0].intent == Intent::Cut);
    CHECK(r.layers[0].colorKey == "#ff0000");
    CHECK(r.layers[0].name == "Rot");
    Rect b = boundsOf(r, 0);
    NEAR(b.x, 10); NEAR(b.w, 20); NEAR(b.h, 10);
    NEAR(b.y, 30);   // y gespiegelt: 50 - (10 + 10)
}

TEST(svg_px_default_96dpi) {
    auto r = importSvg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="96" height="96"><line x1="0" y1="48" x2="96" y2="48" stroke="black"/></svg>)svg");
    CHECK(r.ok);
    NEAR(r.page.w, 25.4);
    CHECK(r.layers[0].intent == Intent::Mark);   // offene Linie
    CHECK(!r.shapes[0].path.closed);
}

TEST(svg_path_commands_relative_and_arc) {
    auto r = importSvg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="100mm" viewBox="0 0 100 100">
        <path d="m10,10 h20 v20 h-20 z M50-0 l10.5.5-1e1 2Z" fill="none" stroke="blue"/>
        <path d="M60 80 a10 10 0 1 0 20 0 a10 10 0 1 0-20 0z" fill="none" stroke="#00f"/></svg>)svg");
    CHECK(r.ok);
    CHECK(r.layers.size() == 1);   // blue und #00f sind dieselbe Farbe
    CHECK(r.shapes.size() == 3);
    Rect sq = r.shapes[0].path.bounds();
    NEAR(sq.x, 10); NEAR(sq.w, 20); NEAR(sq.h, 20);
    Rect circle = r.shapes[2].path.bounds();
    CHECK(std::abs(circle.w - 20) < 0.05);
    CHECK(std::abs(circle.h - 20) < 0.05);
    CHECK(std::abs(circle.x - 60) < 0.05);
    CHECK(std::abs((circle.y + circle.h / 2) - 20) < 0.05);   // Mitte bei y = 100 - 80
}

TEST(svg_transforms_and_styles) {
    auto r = importSvg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="100mm" viewBox="0 0 100 100">
        <style>.cut{fill:none;stroke:#FF0000} .eng { fill : #0000ff }</style>
        <g transform="translate(10 20) scale(2)">
          <rect class="cut" width="10" height="5"/>
          <circle class="eng" cx="20" cy="20" r="5"/>
          <text x="0" y="0">Hallo</text>
        </g>
        <rect width="5" height="5" style="display:none"/>
        <rect width="5" height="5" fill="none"/></svg>)svg");
    CHECK(r.ok);
    CHECK(r.layers.size() == 2);
    size_t cut = r.layers[0].colorKey == "#ff0000" ? 0 : 1;
    CHECK(r.layers[cut].intent == Intent::Cut);
    CHECK(r.layers[1 - cut].intent == Intent::Engrave);
    Rect b = boundsOf(r, cut);
    NEAR(b.x, 10); NEAR(b.w, 20); NEAR(b.h, 10);
    NEAR(b.y, 100 - 20 - 10);
    CHECK(r.warnings.size() == 1);   // Text übersprungen
}

TEST(svg_rounded_rect_and_use) {
    auto r = importSvg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="50mm" height="50mm" viewBox="0 0 50 50">
        <defs><rect id="r" width="10" height="10" rx="2" fill="none" stroke="black"/></defs>
        <use xlink:href="#r" x="5" y="5"/><use href="#r" x="30" y="30"/></svg>)svg");
    CHECK(r.ok);
    CHECK(r.shapes.size() == 2);   // nur die Verwendungen, nicht die Definition
    Rect a = r.shapes[0].path.bounds();
    NEAR(a.x, 5); NEAR(a.w, 10);
    CHECK(r.shapes[0].path.points.size() > 8);   // gerundete Ecken
}

TEST(svg_errors_are_explained) {
    CHECK(!importSvg("kein xml").ok);
    CHECK(!importSvg("<html></html>").ok);
    auto t = importSvg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><text>Nur Text</text></svg>)svg");
    CHECK(!t.ok);
    CHECK(t.error.find("Pfade") != std::string::npos);
}

TEST(controller_imports_and_merges_layers) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    auto r = c.importFile(std::string(MB_SOURCE_DIR) + "/examples/schild.svg");
    CHECK(r.ok);
    CHECK(c.documentTitle() == "schild");
    size_t layers = c.layerRows().size();
    CHECK(layers == 3);
    CHECK(c.layerRows()[2].intent == "Markieren");   // Grün: Rahmen + offene Linie
    c.importFile(std::string(MB_SOURCE_DIR) + "/examples/schild.svg");   // gleiche Farben → keine neuen Ebenen
    CHECK(c.layerRows().size() == layers);
    c.undo();
    c.undo();
    CHECK(c.layerRows().size() == 3);   // zurück zum Beispiel
    CHECK(!c.importFile("/tmp/datei.pdf").ok);
}

// ---------- Gerät mit simulierter GRBL-Steuerung ----------

// Bestätigt Zeilen verzögert (eine pro Lesevorgang) und merkt sich die Pufferbelegung.
class FakeGrbl : public grbl::Transport {
public:
    std::vector<std::string> lines;   // empfangene Zeilen
    std::string realtimeBytes;
    size_t maxBuffered = 0;
    bool held = false;

    bool write(const std::string& d) override {
        for (char ch : d) {
            if (partial_.empty() && (ch == '?' || ch == '!' || ch == '~' || ch == 0x18 || uint8_t(ch) == 0x85)) {
                realtimeBytes += ch;
                if (ch == '?') out_.push_back(std::string("<") + (held ? "Hold:0" : unacked_.empty() ? "Idle" : "Run") + "|MPos:10.000,20.000,0.000|FS:0,0>");
                if (ch == '!') held = true;
                if (ch == '~') held = false;
                if (ch == 0x18) { unacked_.clear(); buffered_ = 0; out_.push_back("Grbl 1.1h ['$' for help]"); }
                continue;
            }
            if (ch == '\n') {
                lines.push_back(partial_);
                buffered_ += partial_.size() + 1;
                maxBuffered = std::max(maxBuffered, buffered_);
                unacked_.push_back(partial_);
                partial_.clear();
            } else partial_ += ch;
        }
        return true;
    }
    std::optional<std::string> readLine(int timeoutMs) override {
        if (out_.empty() && !unacked_.empty() && !held) {
            std::string l = unacked_.front();
            unacked_.pop_front();
            buffered_ -= l.size() + 1;
            if (l == "$I") { out_.push_back("[VER:1.1h.20231023:]"); out_.push_back("[PRODUCER:Test]"); }
            if (l == "$$") out_.push_back("$130=410.000");
            out_.push_back(l.rfind("G99", 0) == 0 ? "error:20" : "ok");
        }
        if (out_.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(std::min(timeoutMs, 2))); return std::nullopt; }
        std::string l = out_.front();
        out_.pop_front();
        return l;
    }
    void close() override {}
    std::string description() const override { return "fake"; }

private:
    std::string partial_;
    std::deque<std::string> unacked_, out_;
    size_t buffered_ = 0;
};

struct EventLog {
    std::mutex m;
    std::vector<grbl::DeviceEvent> events;
    void add(const grbl::DeviceEvent& e) { std::lock_guard<std::mutex> l(m); events.push_back(e); }
    bool has(grbl::DeviceEvent::Kind k) { std::lock_guard<std::mutex> l(m); for (auto& e : events) if (e.kind == k) return true; return false; }
    bool waitFor(grbl::DeviceEvent::Kind k, int ms = 5000) {
        for (int i = 0; i < ms / 5; ++i) { if (has(k)) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        return false;
    }
};

TEST(device_streams_job_within_rx_buffer) {
    EventLog log;
    auto fake = std::make_unique<FakeGrbl>();
    FakeGrbl* f = fake.get();
    grbl::GrblDevice dev([&](const grbl::DeviceEvent& e) { log.add(e); }, 120);
    dev.attach(std::move(fake), 0);
    CHECK(log.waitFor(grbl::DeviceEvent::Kind::Connected));
    std::vector<std::string> job;
    for (int i = 0; i < 300; ++i) job.push_back("G1 X" + std::to_string(i) + ".500 Y12.250 F1000 S500");
    dev.startJob(job);
    CHECK(log.waitFor(grbl::DeviceEvent::Kind::JobFinished));
    dev.detach();
    CHECK(f->maxBuffered <= 120);
    CHECK(f->maxBuffered > 60);   // Puffer wird auch wirklich genutzt
    size_t jobLines = 0;
    for (auto& l : f->lines) if (l.rfind("G1", 0) == 0) { CHECK(l == job[jobLines]); ++jobLines; }
    CHECK(jobLines == 300);
    CHECK(log.has(grbl::DeviceEvent::Kind::Identity));
    CHECK(log.has(grbl::DeviceEvent::Kind::Setting));
    CHECK(log.has(grbl::DeviceEvent::Kind::Disconnected));
}

TEST(device_abort_stops_and_resets) {
    EventLog log;
    auto fake = std::make_unique<FakeGrbl>();
    FakeGrbl* f = fake.get();
    grbl::GrblDevice dev([&](const grbl::DeviceEvent& e) { log.add(e); }, 120);
    dev.attach(std::move(fake), 0);
    CHECK(log.waitFor(grbl::DeviceEvent::Kind::Connected));
    std::vector<std::string> job(5000, "G1 X1.000 Y1.000 F1000 S500");
    dev.startJob(job);
    CHECK(log.waitFor(grbl::DeviceEvent::Kind::Progress));
    dev.abortJob("Test");
    CHECK(log.waitFor(grbl::DeviceEvent::Kind::JobAborted));
    CHECK(!dev.jobActive());
    dev.detach();
    CHECK(f->realtimeBytes.find('!') != std::string::npos);
    CHECK(f->realtimeBytes.find(char(0x18)) != std::string::npos);   // Reset schaltet den Laser ab
    size_t sent = 0;
    for (auto& l : f->lines) if (l.rfind("G1", 0) == 0) ++sent;
    CHECK(sent < 5000);
}

TEST(device_error_in_job_aborts) {
    EventLog log;
    auto fake = std::make_unique<FakeGrbl>();
    grbl::GrblDevice dev([&](const grbl::DeviceEvent& e) { log.add(e); }, 120);
    dev.attach(std::move(fake), 0);
    CHECK(log.waitFor(grbl::DeviceEvent::Kind::Connected));
    dev.startJob({"G1 X1 F100 S10", "G99", "G1 X2 F100 S10"});
    CHECK(log.waitFor(grbl::DeviceEvent::Kind::JobAborted));
    CHECK(log.has(grbl::DeviceEvent::Kind::Error));
    dev.detach();
}

TEST(device_detach_during_job_stops_laser) {
    EventLog log;
    auto fake = std::make_unique<FakeGrbl>();
    FakeGrbl* f = fake.get();
    auto dev = std::make_unique<grbl::GrblDevice>([&](const grbl::DeviceEvent& e) { log.add(e); }, 120);
    dev->attach(std::move(fake), 0);
    CHECK(log.waitFor(grbl::DeviceEvent::Kind::Connected));
    dev->startJob(std::vector<std::string>(2000, "G1 X1 F100 S10"));
    CHECK(log.waitFor(grbl::DeviceEvent::Kind::Progress));
    dev->detach();
    CHECK(f->realtimeBytes.find(char(0x18)) != std::string::npos);
}

TEST(anchor_points) {
    Rect r{10, 20, 100, 40};
    Vec2 c = anchorPoint(r, 4);
    NEAR(c.x, 60); NEAR(c.y, 40);
    Vec2 tl = anchorPoint(r, 0);
    NEAR(tl.x, 10); NEAR(tl.y, 60);   // oben = hinten = größtes y
    Vec2 br = anchorPoint(r, 8);
    NEAR(br.x, 110); NEAR(br.y, 20);
    CHECK(std::string(anchorName(4)) == "Mitte");
}

TEST(controller_preflight_without_device) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.setPhase(Phase::Laser);
    CHECK(c.primaryAction() == PrimaryAction::Connect);
    auto items = c.preflight();
    bool laserNotReady = false, manual = false;
    for (auto& ch : items) {
        if (ch.text == "Laser bereit") laserNotReady = !ch.ok;
        if (!ch.automatic) manual = true;
    }
    CHECK(laserNotReady);
    CHECK(manual);
    GcodeOptions o; o.offset = {100, 50};
    MachineProfile m; m.opticalPowerW = 10; m.powerClass = "diode-10w-445nm";
    MaterialProfile mat; mat.intents[Intent::Mark] = {1000.0, 10.0, {}, 1, {}, {}, {}};
    Document d; d.layers = {{"L", {}, Intent::Mark, true, {}}};
    d.shapes = {{"s", 0, {{{0, 0}, {10, 0}}, false}}};
    std::string g = writeGcode(JobBuilder(m, mat).build(d), o);
    CHECK(g.find("G1 X110.000 Y50.000") != std::string::npos);
    CHECK(g.find("G0 X100.000 Y50.000") != std::string::npos);   // Rückkehr zum Startpunkt
    o.travelSpeed = 3000;
    g = writeGcode(JobBuilder(m, mat).build(d), o);
    CHECK(g.find("G0") == std::string::npos);                       // kein Eilgang
    CHECK(g.find("G1 X100.000 Y50.000 F3000 S0") != std::string::npos);
}

// ---------- Formen bearbeiten ----------

static Rect selRect(AppController& c) {
    for (auto& f : c.inspector()) if (f.key == "selX") {
        Rect r; for (auto& g : c.inspector()) {
            if (g.key == "selX") r.x = g.value; if (g.key == "selY") r.y = g.value;
            if (g.key == "selW") r.w = g.value; if (g.key == "selH") r.h = g.value;
        }
        return r;
    }
    return {};
}

TEST(edit_hit_test_and_drag_with_undo) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    // Beispiel: Untersetzer (Kreis um 80/80, r 50) ist Objekt 1
    c.pointerDown({130, 80}, 1.0, false);   // auf der Kontur
    CHECK(c.hasSelection());
    Rect r = selRect(c);
    NEAR(r.w, 100);
    c.pointerDrag({140, 90});
    c.pointerUp();
    r = selRect(c);
    CHECK(std::abs(r.x - 40) < 0.01);   // 30 + 10
    CHECK(c.undoTitle().find("Verschieben") != std::string::npos);
    c.undo();
    c.pointerDown({80, 80}, 1.0, false);     // innen in der Fläche
    CHECK(std::abs(selRect(c).x - 30) < 0.01);
    c.pointerDown({300, 300}, 1.0, false);   // daneben: Auswahl weg
    CHECK(!c.hasSelection());
}

TEST(edit_numbers_rotate_flip_duplicate_delete) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.pointerDown({150, 80}, 1.0, false);   // Anhänger 40 × 80 bei 150/40
    Rect r = selRect(c);
    NEAR(r.x, 150); NEAR(r.w, 40); NEAR(r.h, 80);
    c.setNumber("selW", 20);                // proportional
    r = selRect(c);
    CHECK(std::abs(r.w - 20) < 0.01 && std::abs(r.h - 40) < 0.01);
    NEAR(r.x, 150); NEAR(r.y, 40);          // unten links bleibt
    c.setNumber("selX", 200);
    NEAR(selRect(c).x, 200);
    c.rotateSelection(-90);
    r = selRect(c);
    CHECK(std::abs(r.w - 40) < 0.01 && std::abs(r.h - 20) < 0.01);
    c.nudge(0, 10);
    double y = selRect(c).y;
    c.flipSelection(false);
    CHECK(std::abs(selRect(c).y - y) < 0.01);   // Spiegeln bleibt an Ort und Stelle
    size_t before = c.layerRows().size();
    c.duplicateSelection();
    CHECK(c.hasSelection());
    CHECK(c.layerRows().size() == before);      // Kopie liegt auf denselben Ebenen
    c.centerSelection();
    r = selRect(c);
    CHECK(std::abs(r.x + r.w / 2 - 205) < 0.01);   // Fläche 410 × 410
    c.deleteSelection();
    CHECK(!c.hasSelection());
    c.selectAll();
    CHECK(c.hasSelection());
}

TEST(rotate_keeps_object_on_bed) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.importFile(std::string(MB_SOURCE_DIR) + "/examples/schild.svg");   // liegt unten links
    c.rotateSelection(-90);
    Rect r = selRect(c);
    CHECK(r.y >= -1e-6);
    CHECK(r.x >= -1e-6);
    CHECK(std::abs(r.h - 119) < 0.1);
}

TEST(import_is_one_selected_object) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.importFile(std::string(MB_SOURCE_DIR) + "/examples/schild.svg");
    CHECK(c.hasSelection());
    Rect r = selRect(c);
    CHECK(std::abs(r.w - 119) < 0.1);       // ganze Außenkontur
    c.nudge(100, 100);
    CHECK(std::abs(selRect(c).x - 100.5) < 0.1);
}

// ---------- Projekte ----------

TEST(json_write_roundtrip) {
    using json::Value;
    Value v = Value::object({{"text", Value::string("Zeile \"1\"\nÄ\\")}, {"n", Value::number(1.23456789)},
                             {"p", Value::array({Value::number(0.5), Value::number(-0.0)})}, {"b", Value::boolean(true)}, {"z", Value()}});
    std::string out = json::write(v);
    CHECK(out.find("1.2346") != std::string::npos);
    CHECK(out.find("[0.5, 0]") != std::string::npos);   // Punkte bleiben einzeilig
    auto back = json::parse(out);
    CHECK(back.has_value());
    CHECK((*back)["text"].asString() == "Zeile \"1\"\nÄ\\");
    CHECK((*back)["z"].isNull());
}

TEST(project_roundtrip) {
    ProjectData p;
    p.doc = Document::sample();
    p.doc.layers[0].overrides.power = 77;
    p.doc.layers[2].output = false;
    p.machineId = "mecpow/x3-pro";
    p.variantId = "extended";
    p.materialId = "plywood-poplar-3mm";
    MaterialProfile mat; mat.id = "plywood-poplar-3mm"; mat.name = "Pappel"; mat.intents[Intent::Cut] = {250.0, 100.0, {}, 1, {}, {}, {}};
    p.material = mat;
    p.position = 1;
    p.anchor = 2;
    std::string err;
    auto back = readProject(writeProject(p), &err);
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->doc.layers.size() == 3);
    CHECK(back->doc.shapes.size() == p.doc.shapes.size());
    NEAR(*back->doc.layers[0].overrides.power, 77);
    CHECK(!back->doc.layers[2].output);
    CHECK(back->doc.layers[1].intent == Intent::Engrave);
    CHECK(back->doc.shapes[3].object == 2);
    CHECK(back->doc.shapes[5].path.closed == false);
    CHECK(std::abs(back->doc.shapes[0].path.points[7].x - p.doc.shapes[0].path.points[7].x) < 1e-4);
    CHECK(back->variantId == "extended" && back->position == 1 && back->anchor == 2);
    CHECK(back->material && NEAR_OK(*back->material->intents[Intent::Cut].speed, 250));
    CHECK(!readProject("{\"format\":\"anders\"}", &err));
    CHECK(!readProject("{\"format\":\"macburn\",\"version\":99}", &err));
    CHECK(err.find("neueren") != std::string::npos);
}

static std::string tempPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / ("macburn-test-" + name)).string();
}

TEST(controller_save_open_and_modified) {
    std::string file = tempPath("schild.macburn");
    {
        AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
        CHECK(!c.isModified());                       // unverändertes Beispiel
        c.importFile(std::string(MB_SOURCE_DIR) + "/examples/schild.svg");
        CHECK(c.isModified());
        c.nudge(30, 40);
        c.setChoice("origin", 1);
        auto r = c.saveProject(file);
        CHECK(r.ok);
        CHECK(!c.isModified());
        CHECK(c.documentTitle() == "macburn-test-schild");
        c.nudge(1, 0);
        CHECK(c.isModified());
        c.undo();
        CHECK(!c.isModified());                       // zurück auf den gesicherten Stand
    }
    AppController d(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    auto r = d.openFile(file);
    CHECK(r.ok);
    CHECK(!d.isModified());
    CHECK(d.documentTitle() == "macburn-test-schild");
    CHECK(d.positionMode() == 1);
    CHECK(d.layerRows().size() == 3);
    d.selectAll();
    bool found = false;
    for (auto& f : d.inspector()) if (f.key == "selX") { found = true; CHECK(std::abs(f.value - 30.5) < 0.01); }
    CHECK(found);
    std::filesystem::remove(file);
}

TEST(open_project_with_missing_material_uses_saved_values) {
    std::string file = tempPath("fremd.macburn");
    ProjectData p;
    p.doc = Document::sample();
    p.machineId = "mecpow/x3-pro";
    p.materialId = "gibt-es-nicht";
    MaterialProfile mat; mat.id = "gibt-es-nicht"; mat.name = "Kork 5 mm"; mat.thicknessMm = 5;
    mat.intents[Intent::Cut] = {111.0, 99.0, {}, 3, {}, {}, {}};
    mat.intents[Intent::Engrave] = {2222.0, 33.0, {}, {}, {}, {}, {}};
    mat.intents[Intent::Mark] = {3333.0, 11.0, {}, {}, {}, {}, {}};
    p.material = mat;
    { std::ofstream(file) << writeProject(p); }
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    auto r = c.openFile(file);
    CHECK(r.ok);
    CHECK(r.message.find("fehlt") != std::string::npos);
    CHECK(c.materialCard().title.find("Kork") != std::string::npos);
    CHECK(c.materialCard().subtitle.find("abgeleitet") != std::string::npos);
    CHECK(c.layerRows()[0].values.find("111") != std::string::npos);
    std::filesystem::remove(file);
    CHECK(!c.openFile("/nonexistent/datei.macburn").ok);
}

// ---------- Anfasser, Drehen, Neigen, Gruppieren ----------

TEST(handles_scale_and_rotate) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.setViewScale(1.0);                       // 1 Punkt = 1 mm: Rahmenabstand 4 mm
    c.pointerDown({150, 80}, 1.0, false);      // Anhänger 40 × 80 bei 150/40
    Rect r = selRect(c);
    NEAR(r.w, 40); NEAR(r.h, 80);
    // Ecke oben rechts (Anfasser 4) liegt bei 194/124 – proportional verdoppeln
    c.pointerDown({194, 124}, 2.0, false);
    c.pointerDrag({234, 204});
    c.pointerUp();
    r = selRect(c);
    CHECK(std::abs(r.w - 80) < 0.01 && std::abs(r.h - 160) < 0.01);
    NEAR(r.x, 150); NEAR(r.y, 40);             // Gegenecke bleibt stehen
    CHECK(c.undoTitle().find("Skalieren") != std::string::npos);
    // Kante rechts (Anfasser 3): nur die Breite
    c.pointerDown({234, 120}, 2.0, false);
    c.pointerDrag({194, 120});
    c.pointerUp();
    r = selRect(c);
    CHECK(std::abs(r.w - 40) < 0.01 && std::abs(r.h - 160) < 0.01);
    // Drehen: Anfasser über der Mitte (170 / 204 + 22), mit ⇧ auf 15° einrasten
    c.pointerDown({170, 226}, 2.0, false);
    c.pointerDrag({170 - 106, 120.5}, true);   // fast genau 90° nach links
    c.pointerUp();
    r = selRect(c);
    CHECK(std::abs(r.w - 160) < 0.05 && std::abs(r.h - 40) < 0.05);
    CHECK(c.undoTitle().find("Drehen") != std::string::npos);
}

TEST(skew_and_rotate_by_angle) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.pointerDown({150, 80}, 1.0, false);
    c.setNumber("selSkewX", 45);               // Höhe 80 → Breite 40 + 80
    Rect r = selRect(c);
    CHECK(std::abs(r.w - 120) < 0.01 && std::abs(r.h - 80) < 0.01);
    c.undo();
    c.pointerDown({150, 80}, 1.0, false);
    c.setNumber("selRotate", 180);
    r = selRect(c);
    CHECK(std::abs(r.w - 40) < 0.01 && std::abs(r.h - 80) < 0.01);
    bool zero = false;
    for (auto& f : c.inspector()) if (f.key == "selRotate") zero = f.value == 0;   // Feld steht wieder auf 0
    CHECK(zero);
}

TEST(group_and_ungroup) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.selectAll();                              // Untersetzer und Anhänger
    CHECK(c.canGroup());
    c.groupSelection();
    CHECK(!c.canGroup());
    c.clearSelection();
    c.pointerDown({150, 80}, 1.0, false);       // ein Klick wählt jetzt alles
    NEAR(selRect(c).x, 30);
    CHECK(c.canUngroup());
    c.ungroupSelection();                       // in die sechs ursprünglichen Teile
    c.clearSelection();
    c.pointerDown({150, 80}, 1.0, false);
    NEAR(selRect(c).w, 40);                     // nur noch der Anhänger
    CHECK(!c.canUngroup());
}

TEST(import_ungroup_by_svg_element_and_parts_saved) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.importFile(std::string(MB_SOURCE_DIR) + "/examples/schild.svg");
    CHECK(c.canUngroup());
    c.ungroupSelection();
    c.clearSelection();
    c.selectAll();
    int n = 0;
    for (auto& f : c.inspector()) if (f.label.rfind("Auswahl (", 0) == 0) n = std::atoi(f.label.c_str() + 9);
    CHECK(n == 8);                              // 8 SVG-Elemente
    std::string file = tempPath("teile.macburn");
    c.saveProject(file);
    AppController d(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    d.openFile(file);
    d.selectAll();
    CHECK(d.canGroup());
    std::filesystem::remove(file);
}

TEST(align_and_distribute) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    // Ein Objekt: an der Fläche (410 × 410)
    c.pointerDown({150, 80}, 1.0, false);   // Anhänger 40 × 80
    c.alignSelection(2);
    NEAR(selRect(c).x, 370);
    c.alignSelection(3);
    NEAR(selRect(c).y, 330);
    // Drei Objekte: aneinander und verteilt
    c.duplicateSelection();
    c.nudge(-200, -200);
    c.selectAll();
    c.alignSelection(5);                    // alle unten bündig
    c.distributeSelection(true);
    NEAR(selRect(c).y, 30);                 // unterste Kante: Kreis des Untersetzers

    // Gleiche Abstände: drei 30-mm-Quadrate bei x = 5, 45, 205 → mittleres auf x = 105
    AppController d(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    d.newDocument();
    std::string svg = std::string(MB_SOURCE_DIR) + "/examples/test-30mm.svg";
    d.importFile(svg);
    d.importFile(svg); d.nudge(40, 0);
    d.importFile(svg); d.nudge(200, 0);
    d.selectAll();
    CHECK(d.canDistribute());
    d.distributeSelection(true);
    d.clearSelection();
    d.pointerDown({120, 6}, 0.5, false);    // Rand des mittleren Quadrats
    NEAR(selRect(d).x, 105);
}

// ---------- Boolesche Operationen und Versatz ----------

static geo::Region square(double x, double y, double size) { return {Polyline::rectangle(x, y, size, size)}; }
#define NEARTOL(a, b, t) CHECK(std::abs(double(a) - double(b)) < (t))

TEST(boolean_basics) {
    auto a = square(0, 0, 10), b = square(5, 5, 10);
    NEARTOL(geo::area(geo::unite({a, b})), 175, 1e-6);
    NEARTOL(geo::area(geo::subtract(a, {b})), 75, 1e-6);
    NEARTOL(geo::area(geo::intersect({a, b})), 25, 1e-6);
    auto parts = geo::divide({a, b});
    CHECK(parts.size() == 3);
    double sum = 0;
    for (auto& p : parts) sum += geo::area(p);
    NEARTOL(sum, 175, 1e-6);
    // Fläche mit Loch (gerade-ungerade) bleibt beim Vereinen ein Loch
    geo::Region ring = {Polyline::rectangle(0, 0, 30, 30), Polyline::rectangle(10, 10, 10, 10)};
    NEARTOL(geo::area(geo::unite({ring, square(40, 0, 10)})), 900 - 100 + 100, 1e-6);
    CHECK(geo::intersect({a, square(50, 50, 5)}).empty());
}

TEST(offset_closed_and_open) {
    auto out = geo::offset({Polyline::rectangle(0, 0, 10, 10)}, 1);
    NEARTOL(geo::area(out), 100 + 4 * 10 + M_PI, 0.1);    // Rundungen an den Ecken (angenähert)
    auto in = geo::offset({Polyline::rectangle(0, 0, 10, 10)}, -1);
    NEARTOL(geo::area(in), 64, 0.01);
    CHECK(geo::offset({Polyline::rectangle(0, 0, 10, 10)}, -6).empty());
    auto line = geo::offset({{{{0, 0}, {20, 0}}, false}}, 1);      // Linie → Kontur 2 mm breit
    NEARTOL(geo::area(line), 40 + M_PI, 0.1);
}

TEST(controller_combine_and_offset) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.newDocument();
    std::string svg = std::string(MB_SOURCE_DIR) + "/examples/test-30mm.svg";
    c.importFile(svg);
    c.importFile(svg); c.nudge(15, 0);
    c.selectAll();
    CHECK(c.canCombine());
    c.combineSelection(0);                      // vereinen
    CHECK(c.undoTitle().find("Vereinen") != std::string::npos);
    Rect r = selRect(c);
    NEARTOL(r.w, 45, 0.01);                     // 30 + 15
    // Farben bleiben erhalten: weiterhin Gravur-, Markier- und Schnittebene mit Inhalt
    for (auto& row : c.layerRows()) CHECK(row.values != "—");
    c.setPhase(Phase::Prepare);
    std::string g = c.exportGcode();
    CHECK(g.find("S1000") != std::string::npos);   // Schneiden (100 %) ist noch da
    c.setPhase(Phase::Design);
    c.undo();
    c.selectAll();
    c.combineSelection(3);                      // teilen
    CHECK(c.hasSelection());
    c.undo();
    c.selectAll();
    c.combineSelection(2);                      // Schnittmenge: 15 mm breit
    NEARTOL(selRect(c).w, 15, 0.01);
    c.offsetSelection(2);                       // Kontur 2 mm außen, neues Objekt
    NEARTOL(selRect(c).w, 19, 0.01);
    CHECK(c.undoTitle().find("Versatz") != std::string::npos);
    c.offsetSelection(-50);                     // nichts übrig → kein Schritt, Hinweis
    CHECK(c.undoTitle().find("Versatz außen") != std::string::npos);
    CHECK(c.lastMessage().find("nichts") != std::string::npos);
}

// ---------- Zeichenwerkzeuge ----------

static void dragDraw(AppController& c, Tool t, Vec2 a, Vec2 b, bool shift = false) {
    c.setTool(t);
    c.pointerDown(a, 0.5, false);
    c.pointerDrag(b, shift);
    c.pointerUp();
}

TEST(draw_rect_ellipse_line) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.newDocument();
    CHECK(c.layerRows().empty());
    dragDraw(c, Tool::Rect, {10, 10}, {50, 30});
    CHECK(c.layerRows().size() == 1);              // Schnittebene automatisch angelegt
    CHECK(c.tool() == Tool::Select);               // danach zurück zur Auswahl
    Rect r = selRect(c);
    NEAR(r.x, 10); NEAR(r.w, 40); NEAR(r.h, 20);
    CHECK(c.undoTitle().find("Rechteck") != std::string::npos);
    dragDraw(c, Tool::Rect, {100, 100}, {60, 70}, true);   // rückwärts, ⇧ = Quadrat
    r = selRect(c);
    NEAR(r.w, 40); NEAR(r.h, 40); NEAR(r.x, 60);
    dragDraw(c, Tool::Ellipse, {0, 200}, {100, 250});
    r = selRect(c);
    NEARTOL(r.w, 100, 0.01); NEARTOL(r.h, 50, 0.01);
    dragDraw(c, Tool::Line, {200, 200}, {240, 205}, true); // ⇧ rastet waagerecht ein
    r = selRect(c);
    NEARTOL(r.h, 0, 1e-6);
    c.undo();
    CHECK(c.undoTitle().find("Ellipse") != std::string::npos);
}

TEST(draw_polygon_star_triangle_and_click) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.newDocument();
    c.setTool(Tool::Polygon);
    c.setNumber("toolSides", 4);
    c.pointerDown({100, 100}, 0.5, false);
    c.pointerDrag({100, 120});                     // Radius 20, erste Ecke oben
    c.pointerUp();
    Rect r = selRect(c);
    NEARTOL(r.w, 40, 1e-6); NEARTOL(r.h, 40, 1e-6);   // Raute
    dragDraw(c, Tool::Triangle, {200, 100}, {200, 110});
    r = selRect(c);
    NEARTOL(r.h, 15, 1e-6);                        // r + r/2
    c.setTool(Tool::Star);
    c.setNumber("toolPoints", 5);
    c.pointerDown({300, 300}, 0.5, false);
    c.pointerUp();                                 // Klick ohne Ziehen: Radius 10
    r = selRect(c);
    NEARTOL(r.y + r.h, 310, 1e-6);
    c.setTool(Tool::Ellipse);
    c.pointerDown({50, 350}, 0.5, false);
    c.pointerUp();                                 // Klick: 20 mm
    NEARTOL(selRect(c).w, 20, 0.01);
}

TEST(draw_polyline_open_closed_cancel) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.newDocument();
    c.setTool(Tool::Polyline);
    c.pointerDown({0, 0}, 0.5, false); c.pointerUp();
    c.pointerMove({5, 5});
    CHECK(c.isCreating());
    c.pointerDown({10, 0}, 0.5, false); c.pointerUp();
    c.pointerDown({10, 10}, 0.5, false); c.pointerUp();
    CHECK(c.finishCreation());                     // ↩: offene Linie
    Rect r = selRect(c);
    NEAR(r.w, 10); NEAR(r.h, 10);
    c.setPhase(Phase::Prepare);
    CHECK(!c.renderList().strokes.empty());
    c.setPhase(Phase::Design);
    // Geschlossen durch Klick auf den Startpunkt
    c.setTool(Tool::Polyline);
    for (Vec2 p : std::vector<Vec2>{{100, 100}, {120, 100}, {110, 120}, {100.2, 100.2}}) { c.pointerDown(p, 0.5, false); c.pointerUp(); }
    CHECK(!c.isCreating());
    c.clearSelection();
    c.pointerDown({110, 105}, 0.5, false);         // Klick ins Innere trifft: Fläche ist geschlossen
    CHECK(c.hasSelection());
    // Abbrechen
    c.setTool(Tool::Polyline);
    c.pointerDown({300, 300}, 0.5, false); c.pointerUp();
    CHECK(c.cancelCreation());
    CHECK(!c.isCreating());
    CHECK(c.selectToolByKey("r") && c.tool() == Tool::Rect);
    CHECK(!c.selectToolByKey("x"));
}

// ---------- Genaue Maße ----------

TEST(number_input) {
    NEARTOL(*parseNumberInput("12,5"), 12.5, 1e-12);
    NEARTOL(*parseNumberInput(" 12.5 mm"), 12.5, 1e-12);
    NEARTOL(*parseNumberInput("100/3"), 100.0 / 3, 1e-12);
    NEARTOL(*parseNumberInput("25+12,5"), 37.5, 1e-12);
    NEARTOL(*parseNumberInput("(10+2)*3"), 36, 1e-12);
    NEARTOL(*parseNumberInput("2in"), 50.8, 1e-12);
    NEARTOL(*parseNumberInput("1,5 cm"), 15, 1e-12);
    NEARTOL(*parseNumberInput("-4,2"), -4.2, 1e-12);
    NEARTOL(*parseNumberInput("3 x 4"), 12, 1e-12);
    CHECK(!parseNumberInput(""));
    CHECK(!parseNumberInput("abc"));
    CHECK(!parseNumberInput("5/0"));
    CHECK(!parseNumberInput("5+"));
    CHECK(formatNumber(33.33333) == "33,333");
    CHECK(formatNumber(20) == "20");
    CHECK(formatNumber(-0.0001) == "0");
}

TEST(create_from_fields_and_return) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.newDocument();
    c.setTool(Tool::Rect);
    CHECK(c.setNumberText("toolX", "10"));
    CHECK(c.setNumberText("toolY", "20"));
    CHECK(c.setNumberText("toolW", "100/3"));
    CHECK(c.setNumberText("toolH", "1,5cm"));
    CHECK(!c.setNumberText("toolH", "viel"));     // ungültig: Wert bleibt
    c.pressButton("toolCreate");
    Rect r = selRect(c);
    NEARTOL(r.x, 10, 1e-9); NEARTOL(r.y, 20, 1e-9);
    NEARTOL(r.w, 100.0 / 3, 0.001); NEARTOL(r.h, 15, 1e-9);
    CHECK(c.tool() == Tool::Select);
    // ↩ mit aktivem Werkzeug erstellt ebenfalls
    c.setTool(Tool::Polygon);
    c.setNumber("toolX", 200); c.setNumber("toolY", 200); c.setNumber("toolDia", 50); c.setNumber("toolSides", 6);
    c.setNumber("toolRot", 30);                     // 6-Eck mit waagerechter Oberkante
    CHECK(c.finishCreation());
    r = selRect(c);
    NEARTOL(r.w, 50, 1e-9);
    NEARTOL(r.h, 50 * std::sqrt(3) / 2, 0.001);   // Inspektor zeigt 3 Nachkommastellen
    // Linie: Länge und Winkel
    c.setTool(Tool::Line);
    c.setNumber("toolX", 0); c.setNumber("toolY", 0); c.setNumber("toolLen", 30); c.setNumber("toolAngle", 90);
    c.pressButton("toolCreate");
    r = selRect(c);
    NEARTOL(r.h, 30, 1e-9); NEARTOL(r.w, 0, 1e-9);
    // Klick legt die Form mit den Werkzeugmaßen mittig an der Stelle an
    c.setTool(Tool::Rect);
    c.setNumber("toolW", 40); c.setNumber("toolH", 10);
    c.pointerDown({300, 300}, 0.5, false);
    c.pointerUp();
    r = selRect(c);
    NEARTOL(r.x, 280, 1e-9); NEARTOL(r.y, 295, 1e-9);
}

TEST(reference_point_for_position_and_size) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.pointerDown({150, 80}, 1.0, false);           // Anhänger 40 × 80 bei 150/40
    c.setChoice("refPoint", 4);                     // Mitte
    double x = 0, y = 0;
    for (auto& f : c.inspector()) { if (f.key == "selX") x = f.value; if (f.key == "selY") y = f.value; }
    NEAR(x, 170); NEAR(y, 80);
    c.setNumberText("selX", "200");                 // Mitte auf 200
    Rect r = selRect(c);                            // selRect liest X/Y – jetzt Mittelpunkt
    NEAR(r.x, 200);
    c.setNumberText("selW", "20");                  // proportional um die Mitte
    for (auto& f : c.inspector()) { if (f.key == "selX") x = f.value; if (f.key == "selY") y = f.value; }
    NEAR(x, 200); NEAR(y, 80);                      // Mitte bleibt stehen
    c.setChoice("refPoint", 6);                     // unten links
    for (auto& f : c.inspector()) { if (f.key == "selX") x = f.value; if (f.key == "selY") y = f.value; }
    NEAR(x, 190); NEAR(y, 60);                      // 20 × 40 um die Mitte (200/80)
}

// ---------- Text ----------

TEST(font_outline_cap_height_and_alignment) {
    auto fonts = makeSystemFontProvider();
    CHECK(!fonts->families().empty());
    CHECK(!fonts->styles("Helvetica Neue").empty());
    TextSpec t;
    t.text = "HEFT";                         // nur gerade Großbuchstaben: Höhe = Versalhöhe
    t.capHeightMm = 10;
    auto out = fonts->outline(t);
    CHECK(!out.empty());
    Polyline all;
    for (auto& p : out) all.points.insert(all.points.end(), p.points.begin(), p.points.end());
    Rect b = all.bounds();
    NEARTOL(b.h, 10, 0.05);
    NEARTOL(b.y, 0, 0.05);                   // Grundlinie bei 0
    t.align = 1;
    auto centered = fonts->outline(t);
    Polyline c;
    for (auto& p : centered) c.points.insert(c.points.end(), p.points.begin(), p.points.end());
    NEARTOL(c.bounds().x + c.bounds().w / 2, 0, 0.6);   // mittig um 0 (Seitenabstand der Buchstaben)
    t.family = "Gibt Es Nicht";
    CHECK(fonts->outline(t).empty());
}

TEST(text_tool_edit_after_transform) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.newDocument();
    c.setTool(Tool::Text);
    c.pointerDown({50, 100}, 0.5, false);
    CHECK(c.hasSelection());
    CHECK(c.layerRows().size() == 1 && c.layerRows()[0].intent == "Gravieren");   // Text kommt in eine Gravurebene
    bool area = false;
    for (auto& f : c.inspector()) if (f.kind == Field::Kind::TextArea) { area = true; CHECK(f.focus); }
    CHECK(area);
    c.setString("textContent", "HEFT");
    Rect r = selRect(c);
    NEARTOL(r.h, 10, 0.05);
    double w = r.w;
    c.rotateSelection(90);                   // drehen – Text bleibt bearbeitbar
    r = selRect(c);
    NEARTOL(r.w, 10, 0.05); NEARTOL(r.h, w, 0.05);
    c.setString("textContent", "HEFTHEFT");  // doppelt so lang, weiterhin gedreht
    r = selRect(c);
    NEARTOL(r.w, 10, 0.05);
    CHECK(r.h > 1.9 * w);
    c.setNumber("textCap", 20);              // Schrifthöhe
    NEARTOL(selRect(c).w, 20, 0.1);
    c.undo();
    c.selectAll();                           // Widerrufen hebt die Auswahl auf
    NEARTOL(selRect(c).w, 10, 0.05);
}

TEST(text_saved_duplicated_converted) {
    AppController c(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    c.newDocument();
    c.setTool(Tool::Text);
    c.pointerDown({20, 20}, 0.5, false);
    c.setString("textContent", "MacBurn");
    c.setNumber("textCap", 8);
    std::string file = tempPath("text.macburn");
    CHECK(c.saveProject(file).ok);
    AppController d(std::string(MB_SOURCE_DIR) + "/profiles", "/nonexistent");
    CHECK(d.openFile(file).ok);
    d.selectAll();
    bool editable = false;
    for (auto& f : d.inspector()) if (f.kind == Field::Kind::TextArea) { editable = true; CHECK(f.text == "MacBurn"); }
    CHECK(editable);
    d.duplicateSelection();                  // Kopie ist ebenfalls Text
    d.setString("textContent", "Kopie");
    bool copyText = false;
    for (auto& f : d.inspector()) if (f.kind == Field::Kind::TextArea) copyText = f.text == "Kopie";
    CHECK(copyText);
    d.pressButton("textToPaths");
    bool still = false;
    for (auto& f : d.inspector()) if (f.kind == Field::Kind::TextArea) still = true;
    CHECK(!still);                           // jetzt normale Formen
    std::filesystem::remove(file);
}

int main() {
    for (auto& [name, fn] : tests) {
        int before = failures;
        fn();
        std::printf("%s %s\n", failures == before ? "ok    " : "FEHLER", name);
    }
    std::printf("\n%d Prüfungen, %d Fehler\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
