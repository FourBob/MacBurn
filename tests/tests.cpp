// MacBurn – Kerntests ohne Fenster und ohne Hardware.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppController.hpp"
#include "device/Grbl.hpp"
#include "device/GrblDevice.hpp"
#include "io/SvgImport.hpp"
#include "job/Job.hpp"
#include "profiles/Profiles.hpp"
#include "util/Json.hpp"

#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <thread>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; std::printf("  FEHLER %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b) CHECK(std::abs(double(a) - double(b)) < 1e-6)

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

int main() {
    for (auto& [name, fn] : tests) {
        int before = failures;
        fn();
        std::printf("%s %s\n", failures == before ? "ok    " : "FEHLER", name);
    }
    std::printf("\n%d Prüfungen, %d Fehler\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
