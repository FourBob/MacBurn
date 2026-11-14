// MacBurn – Gerätesteuerung im AppController: Verbinden, Bewegen, Rahmen, Auftrag.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppController.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mb {

namespace {

constexpr double kJogSteps[] = {0.1, 1, 10, 50};
constexpr double kJogFeed = 3000;   // mm/min

std::string f3(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}

} // namespace

void AppController::log(const std::string& line) const {
    if (logPath_.empty()) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::path(logPath_).parent_path(), ec);
    if (fs::exists(logPath_, ec) && fs::file_size(logPath_, ec) > 2'000'000)   // alte Hälfte wegräumen
        fs::rename(logPath_, logPath_ + ".1", ec);
    std::ofstream out(logPath_, std::ios::app);
    std::time_t now = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    out << ts << "  " << line << "\n";
}

namespace {
std::string posText(Vec2 p) {
    char b[64];
    std::snprintf(b, sizeof b, "X %.2f Y %.2f", p.x, p.y);
    return b;
}
}

AppController::~AppController() {
    device_.reset();   // Thread beenden, bevor die übrigen Mitglieder verschwinden
}

void AppController::refreshPorts() {
    ports_ = grbl::listSerialPorts();
    // Der Treiber des Chip-Herstellers (wchusbserial) liefert zuverlässiger als der Apple-Treiber.
    std::stable_sort(ports_.begin(), ports_.end(), [](const std::string& a, const std::string& b) {
        return (a.find("wchusbserial") != std::string::npos) > (b.find("wchusbserial") != std::string::npos);
    });
    if (port_ >= int(ports_.size())) port_ = 0;
}

bool AppController::deviceIdle() const {
    return dev_.connected && !dev_.jobRunning && dev_.state.rfind("Idle", 0) == 0;
}

Rect AppController::outputBounds() const {
    Polyline all;
    for (const auto& s : doc_.shapes)
        if (s.layer < doc_.layers.size() && doc_.layers[s.layer].output)
            all.points.insert(all.points.end(), s.path.points.begin(), s.path.points.end());
    return all.bounds();
}

std::optional<Vec2> AppController::offsetFor() const {
    Rect b = outputBounds();
    Vec2 off{0, 0};
    if (origin_ == 1) {   // am Laserkopf: gewählter Ankerpunkt des Designs liegt unter dem Kopf
        if (!dev_.connected) return std::nullopt;
        off = headForPlacement() - anchorPoint(b, anchor_);
    }
    Rect wa = workArea();
    Rect moved{b.x + off.x, b.y + off.y, b.w, b.h};
    if (!wa.contains(moved)) return std::nullopt;
    return off;
}

PrimaryAction AppController::primaryAction() const {
    if (phase_ != Phase::Laser) return PrimaryAction::GoToLaser;
    if (dev_.connecting) return PrimaryAction::Connecting;
    if (!dev_.connected) return PrimaryAction::Connect;
    if (dev_.state.rfind("Alarm", 0) == 0) return PrimaryAction::Unlock;
    if (dev_.jobRunning) return dev_.state.rfind("Hold", 0) == 0 ? PrimaryAction::Resume : PrimaryAction::Pause;
    return PrimaryAction::StartJob;
}

// ---------- Befehle ----------

void AppController::connect() {
    if (dev_.connected || dev_.connecting) return;
    refreshPorts();
    if (ports_.empty()) {
        message_ = "Kein Laser gefunden. Bitte per USB anschließen und einschalten.";
        changed();
        return;
    }
    auto* m = machine();
    auto port = std::make_unique<grbl::SerialPort>();
    std::string err;
    const std::string& path = ports_[size_t(port_)];
    if (!port->open(path, m ? m->baudRate : 115200, &err)) {
        message_ = "Der Anschluss lässt sich nicht öffnen: " + err;
        changed();
        return;
    }
    port->setLines(false, false);   // ESP32-Steuerungen nicht im Reset halten
    dev_ = {};
    dev_.connecting = true;
    dev_.port = path;
    message_.clear();
    device_->attach(std::move(port));
    changed();
}

void AppController::disconnect() {
    lockedHead_.reset();
    device_->detach();
    dev_.connected = dev_.connecting = dev_.jobRunning = false;
    changed();
}

void AppController::jog(int dx, int dy) {
    if (!deviceIdle() && dev_.state.rfind("Jog", 0) != 0) return;
    lockedHead_.reset();   // Kopf wird bewegt: Vorschau folgt wieder dem Kopf
    double step = kJogSteps[std::clamp(jogStep_, 0, 3)];
    Rect wa = workArea();
    Vec2 p = workPos();
    // Nicht über die Arbeitsfläche hinaus fahren.
    double tx = std::clamp(p.x + dx * step, 0.0, wa.w), ty = std::clamp(p.y + dy * step, 0.0, wa.h);
    double mx = tx - p.x, my = ty - p.y;
    if (std::abs(mx) < 1e-6 && std::abs(my) < 1e-6) {
        message_ = "Rand der Arbeitsfläche erreicht.";
        changed();
        return;
    }
    device_->sendLine("$J=G91 G21 X" + f3(mx) + " Y" + f3(my) + " F" + f3(kJogFeed));
}

void AppController::home() {
    if (!dev_.connected || dev_.jobRunning) return;
    lockedHead_.reset();
    log("REFERENZFAHRT");
    message_ = "Referenzfahrt …";
    device_->sendLine("$H");
    changed();
}

void AppController::unlock() {
    if (!dev_.connected || dev_.jobRunning) return;
    lockedHead_.reset();
    device_->sendLine("$X");
    message_ = "Entsperrt. Position ist erst nach einer Referenzfahrt wieder sicher.";
    changed();
}

void AppController::frame() {
    if (!deviceIdle()) return;
    if (test_) {
        auto off = testOffset();
        if (!off) { message_ = "Von hier aus ragt die Testplatte über die Arbeitsfläche."; changed(); return; }
        lockedHead_ = headForPlacement();
        Rect b = testPlate().bounds;
        double x0 = b.x + off->x, y0 = b.y + off->y, x1 = x0 + b.w, y1 = y0 + b.h;
        Vec2 back = *lockedHead_;   // zurück an den Ausgangspunkt
        std::string feed = " F" + f3(machine() ? machine()->travelSpeedMmMin : 3000) + " S0";
        for (const auto& l : std::vector<std::string>{"M5", "G90",
                 "G1 X" + f3(x0) + " Y" + f3(y0) + feed, "G1 X" + f3(x1) + " Y" + f3(y0) + feed,
                 "G1 X" + f3(x1) + " Y" + f3(y1) + feed, "G1 X" + f3(x0) + " Y" + f3(y1) + feed,
                 "G1 X" + f3(x0) + " Y" + f3(y0) + feed, "G1 X" + f3(back.x) + " Y" + f3(back.y) + feed})
            device_->sendLine(l);
        message_ = "Rahmen der Testplatte wird abgefahren (Laser aus).";
        changed();
        return;
    }
    auto off = offsetFor();
    if (off && origin_ == 1) lockedHead_ = headForPlacement();
    if (!off) {
        message_ = origin_ == 1 ? "Von dieser Kopfposition aus ragt das Design über die Arbeitsfläche. Kopf verschieben oder anderen Anker wählen."
                                : "Das Design passt an dieser Stelle nicht auf die Arbeitsfläche.";
        changed();
        return;
    }
    Rect b = outputBounds();
    double x0 = b.x + off->x, y0 = b.y + off->y, x1 = x0 + b.w, y1 = y0 + b.h;
    Vec2 back = headForPlacement();   // zurück an den Ausgangspunkt, nicht mitten in den Rahmen
    // Laser aus (M5, S0); Fahrgeschwindigkeit aus dem Profil statt Eilgang – verhindert Schrittverluste
    std::string feed = " F" + f3(machine() ? machine()->travelSpeedMmMin : 3000) + " S0";
    std::vector<std::string> lines = {"M5", "G90",
        "G1 X" + f3(x0) + " Y" + f3(y0) + feed, "G1 X" + f3(x1) + " Y" + f3(y0) + feed,
        "G1 X" + f3(x1) + " Y" + f3(y1) + feed, "G1 X" + f3(x0) + " Y" + f3(y1) + feed,
        "G1 X" + f3(x0) + " Y" + f3(y0) + feed, "G1 X" + f3(back.x) + " Y" + f3(back.y) + feed};
    for (auto& l : lines) device_->sendLine(l);   // Laser bleibt aus
    log("RAHMEN " + posText(Vec2{x0, y0}) + " bis " + posText(Vec2{x1, y1}));
    message_ = "Rahmen wird abgefahren (Laser aus).";
    changed();
}

std::vector<PreflightCheck> AppController::preflight() const {
    std::vector<PreflightCheck> c;
    auto* m = machine();
    auto off = test_ ? testOffset() : offsetFor();
    if (test_) {
        c.push_back({"Testplatte liegt auf der Arbeitsfläche", off ? "Linke untere Ecke am Laserkopf." : "Kopf verschieben.", true, bool(off)});
        c.push_back({"Laser bereit", deviceIdle() ? "Verbunden" : "Nicht bereit: " + grbl::describeState(dev_.state), true, deviceIdle()});
        c.push_back({"Fokus eingestellt", m && !m->focusHint.empty() ? m->focusHint : "Laser auf die Materialoberfläche fokussieren.", false});
        if (m && !m->airAssistControllable) c.push_back({"Luftpumpe eingeschaltet", "Wird an der Pumpe geschaltet.", false});
        c.push_back({"Schutzbrille auf oder Abdeckung geschlossen", "Auch reflektiertes Laserlicht schädigt die Augen.", false});
        c.push_back({"Ich bleibe während des Tests beim Gerät", "Der Schnittstreifen kann sich lösen.", false});
        return c;
    }
    c.push_back({"Design liegt auf der Arbeitsfläche",
                 off ? (origin_ == 0 ? std::string("Position: wie auf der Fläche")
                                     : std::string("Position: am Laserkopf, Anker ") + anchorName(anchor_))
                     : "Bitte Design verschieben oder Position ändern.", true, bool(off)});
    c.push_back({"Laser bereit", deviceIdle() ? "Verbunden, Zustand: " + grbl::describeState(dev_.state) : "Nicht bereit: " + grbl::describeState(dev_.state),
                 true, deviceIdle()});
    auto st = materialStatus();
    c.push_back({std::string("Material: ") + (material() ? material()->name : "keins"),
                 st == MaterialStatus::Tested ? "Auf dieser Maschine getestet." :
                 st == MaterialStatus::Vendor ? "Werte aus der Anleitung des Herstellers." :
                 st == MaterialStatus::Modified ? "Mit Änderungen im Projekt." : "Nur abgeleitet. Erst ein Materialtest gibt sichere Werte.",
                 true, st != MaterialStatus::Derived});
    for (size_t i = 0; i < doc_.layers.size(); ++i) {
        const auto& l = doc_.layers[i];
        if (!l.output || l.intent != Intent::Cut) continue;
        int open = 0;
        for (const auto& s : doc_.shapes) if (s.layer == i && !s.path.closed) ++open;
        if (open) c.push_back({"Ebene „" + l.name + "“ schneidet offene Linien",
                               std::to_string(open) + " offene Linie(n) – Teile fallen eventuell nicht heraus.", true, false});
    }
    c.push_back({"Fokus eingestellt", m && !m->focusHint.empty() ? m->focusHint : "Laser auf die Materialoberfläche fokussieren.", false});
    if (m && !m->airAssistControllable) c.push_back({"Luftpumpe eingeschaltet", "Wird an der Pumpe geschaltet, nicht von MacBurn.", false});
    c.push_back({"Schutzbrille auf oder Abdeckung geschlossen", "Auch reflektiertes Laserlicht schädigt die Augen.", false});
    c.push_back({"Ich bleibe während des Auftrags beim Gerät", "Holz und Pappe können sich entzünden.", false});
    return c;
}

void AppController::startJob(bool forTest) {
    if (!deviceIdle()) return;
    if (forTest != test_.has_value()) {
        // Prüffenster gehörte zu einem anderen Auftrag (Modus inzwischen gewechselt)
        message_ = "Die Startprüfung gehörte zu einem anderen Auftrag. Bitte erneut auf „Lasern …“ klicken.";
        changed();
        return;
    }
    if (test_) {
        auto off = testOffset();
        auto* m = machine();
        if (!off || !m) return;
        lockedHead_ = headForPlacement();
        GcodeOptions o;
        o.sMax = m->sMax;
        o.airAssistGcode = m->airAssistControllable;
        o.offset = *off;
        o.travelSpeed = m->travelSpeedMmMin;
        std::vector<std::string> lines;
        std::istringstream in(writeGcode(testPlate().job, o));
        for (std::string l; std::getline(in, l);) lines.push_back(l);
        log("MATERIALTEST START · " + test_->name + " · " + std::to_string(lines.size()) + " Zeilen · " + posText(*off));
        device_->startJob(std::move(lines));
        dev_.jobRunning = true;
        dev_.done = dev_.total = 0;
        message_ = "Materialtest läuft.";
        changed();
        return;
    }
    auto off = offsetFor();
    auto* m = machine();
    if (!off || !m || !material()) return;
    jobOffset_ = *off;
    Vec2 startHead = headForPlacement();
    if (origin_ == 1) lockedHead_ = startHead;
    GcodeOptions o;
    o.sMax = m->sMax;
    o.airAssistGcode = m->airAssistControllable;
    o.offset = *off;
    o.travelSpeed = m->travelSpeedMmMin;
    std::vector<std::string> lines;
    std::istringstream in(writeGcode(job_, o));
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    if (origin_ == 1)   // am Laserkopf: danach wieder dorthin, wo der Kopf stand
        lines.push_back("G1 X" + f3(startHead.x) + " Y" + f3(startHead.y) + " F" + f3(m->travelSpeedMmMin) + " S0");
    {
        std::string layers;
        for (const auto& l : doc_.layers) {
            if (!l.output) continue;
            auto st = ProfileStore::resolve(*m, *material(), l.intent, l.overrides);
            char b[160];
            std::snprintf(b, sizeof b, "%s%s=%s %.0f mm/min %.0f %% ×%d", layers.empty() ? "" : ", ", l.name.c_str(),
                          intentLabel(l.intent), st.speed, st.power, st.passes);
            layers += b;
        }
        log("AUFTRAG START „" + title_ + "“ · " + std::to_string(lines.size()) + " Zeilen · Material " + material()->name +
            " · Position " + (origin_ == 1 ? "am Laserkopf " + posText(startHead) : std::string("wie auf der Fläche")) + " · " + layers);
    }
    device_->startJob(std::move(lines));
    dev_.jobRunning = true;
    dev_.done = 0;
    dev_.total = 0;
    message_ = "Auftrag läuft.";
    changed();
}

void AppController::pauseOrResume() {
    if (!dev_.jobRunning) return;
    bool held = dev_.state.rfind("Hold", 0) == 0;
    log(held ? "FORTSETZEN" : "PAUSE");
    device_->realtime(held ? grbl::GrblDevice::kResume : grbl::GrblDevice::kFeedHold);
    message_ = held ? "Fortgesetzt." : "Angehalten. Der Laser ist aus, solange pausiert.";
    changed();
}

void AppController::stop() {
    log(dev_.jobRunning ? "STOPP durch Benutzer" : "STOPP (Joggen abbrechen)");
    if (dev_.jobRunning) device_->abortJob("Vom Benutzer gestoppt.");
    else if (dev_.connected) device_->realtime(grbl::GrblDevice::kJogCancel);
}

// ---------- Ereignisse vom Gerät (Main-Thread) ----------

void AppController::handleDevice(const grbl::DeviceEvent& e) {
    using K = grbl::DeviceEvent::Kind;
    switch (e.kind) {
        case K::Connected:
            dev_.connecting = false;
            dev_.connected = true;
            message_ = "Verbunden mit " + dev_.port;
            log("VERBUNDEN " + dev_.port + " · Zustand " + e.status.state + " · " + posText(e.status.machinePos) +
                (e.status.pins.empty() ? "" : " · Eingänge " + e.status.pins));
            [[fallthrough]];
        case K::Status: {
            bool stateChanged = dev_.state != e.status.state;
            if (e.status.workOffset) dev_.wco = *e.status.workOffset;
            dev_.machinePos = e.status.workPosReported ? e.status.machinePos + dev_.wco : e.status.machinePos;
            if (stateChanged || e.status.pins != lastPins_)
                log("ZUSTAND " + e.status.state + " · " + posText(workPos()) +
                    (e.status.pins.empty() ? "" : " · Eingänge " + e.status.pins) +
                    (dev_.jobRunning ? " · Auftrag " + std::to_string(dev_.done) + "/" + std::to_string(dev_.total) : ""));
            lastPins_ = e.status.pins;
            dev_.state = e.status.state;
            if (stateChanged || e.kind == K::Connected) changed();
            else if (onStatus_) onStatus_();
            return;
        }
        case K::Disconnected:
            log("GETRENNT" + (e.text.empty() ? std::string() : " · " + e.text));
            dev_.connected = dev_.connecting = dev_.jobRunning = false;
            if (!e.text.empty()) message_ = e.text;
            else message_ = "Getrennt.";
            break;
        case K::Identity:
            if (e.text.rfind("VER:", 0) == 0) {
                std::string v = e.text.substr(4);
                auto dot = v.find('.', 2);   // "1.1h.20231023…" → "1.1h"
                dev_.identity = "GRBL " + v.substr(0, dot) + (dev_.identity.empty() ? "" : " · " + dev_.identity);
            } else if (e.text.rfind("PRODUCER:", 0) == 0) {
                dev_.identity += (dev_.identity.empty() ? "" : " · ") + e.text.substr(9);
            }
            break;
        case K::Setting:
            dev_.settings[e.code] = e.value;
            return;   // kein Neuaufbau nötig
        case K::Alarm:
            log("ALARM " + std::to_string(e.code) + " · " + e.text + " · " + posText(workPos()) +
                (lastPins_.empty() ? "" : " · Eingänge " + lastPins_) +
                (dev_.jobRunning ? " · Auftrag " + std::to_string(dev_.done) + "/" + std::to_string(dev_.total) : ""));
            dev_.lastProblem = e.text;
            message_ = "Alarm: " + e.text;
            break;
        case K::Error:
            log("FEHLER " + std::to_string(e.code) + " · " + grbl::describeError(e.code) + " · Zeile: " + e.text);
            message_ = grbl::describeError(e.code);
            break;
        case K::Message:
            log("MELDUNG " + e.text);
            // Startmeldungen der Steuerung (Board, WLAN-Daten) gehören nicht in die Statusleiste.
            if (e.text.find("Caution: Unlocked") != std::string::npos || e.text.rfind("Using machine", 0) == 0 ||
                e.text.rfind("Mode=", 0) == 0) return;
            message_ = e.text;
            break;
        case K::Progress: {
            size_t oldPct = dev_.total ? dev_.done * 100 / dev_.total : 0;
            dev_.done = e.done;
            dev_.total = e.total;
            size_t pct = dev_.total ? dev_.done * 100 / dev_.total : 0;
            if (pct == oldPct) return;   // nur bei vollem Prozent neu aufbauen
            if (pct / 10 != oldPct / 10) log("FORTSCHRITT " + std::to_string(pct) + " % · " + posText(workPos()));
            break;
        }
        case K::JobFinished:
            log("AUFTRAG FERTIG");
            dev_.jobRunning = false;
            if (test_) { test_->ran = true; message_ = "Test fertig. Jetzt die Skalen ablesen und unten eintragen."; break; }
            message_ = "Auftrag fertig.";
            break;
        case K::JobAborted:
            log("AUFTRAG ABGEBROCHEN · " + e.text + " · " + posText(workPos()) + " · nach " + std::to_string(dev_.done) +
                "/" + std::to_string(dev_.total) + " Zeilen");
            dev_.jobRunning = false;
            message_ = "Auftrag abgebrochen: " + e.text + " Laser ist aus. Vor dem nächsten Auftrag bitte eine Referenzfahrt machen.";
            break;
    }
    changed();
}

// ---------- Inspektor im Lasern-Modus ----------

void AppController::addDeviceFields(std::vector<Field>& f) const {
    f.push_back({Field::Kind::Header, "", "Verbindung"});
    if (!dev_.connected && !dev_.connecting) {
        Field port{Field::Kind::Choice, "port", "Anschluss"};
        for (auto& p : ports_) port.options.push_back(p.substr(p.find_last_of('/') + 1));
        if (port.options.empty()) port.options.push_back("kein Laser gefunden");
        port.selected = port_;
        port.enabled = !ports_.empty();
        f.push_back(port);
        f.push_back({Field::Kind::Button, "refreshPorts", "Anschlüsse neu suchen"});
        Field c{Field::Kind::Button, "connect", "Verbinden"};
        c.enabled = !ports_.empty();
        f.push_back(c);
    } else {
        f.push_back({Field::Kind::Info, "", dev_.connecting ? "Verbinde mit " + dev_.port + " …" : "Verbunden: " + dev_.port});
        if (!dev_.identity.empty()) f.push_back({Field::Kind::Info, "", dev_.identity});
        Field d{Field::Kind::Button, "disconnect", "Trennen"};
        d.enabled = !dev_.jobRunning;
        f.push_back(d);
    }
    if (!dev_.connected) {
        if (test_) return;   // Materialtest hat eigene Felder
        f.push_back({Field::Kind::Header, "", "Auftrag"});
        f.push_back({Field::Kind::Info, "", "Geschätzte Laufzeit: " + estimateText()});
        return;
    }

    bool idle = deviceIdle();
    bool alarm = dev_.state.rfind("Alarm", 0) == 0;
    f.push_back({Field::Kind::Header, "", "Bewegen"});
    if (alarm) {
        Field a{Field::Kind::Info, "", dev_.lastProblem.empty() ? "Alarm: Die Steuerung ist gesperrt." : dev_.lastProblem};
        a.highlight = true;
        f.push_back(a);
    }
    bool jogging = dev_.state.rfind("Jog", 0) == 0;
    Field pad{Field::Kind::JogPad, "jog", ""};
    pad.enabled = idle || jogging;   // mehrere Klicks hintereinander erlauben
    f.push_back(pad);
    Field step{Field::Kind::Choice, "jogStep", "Schrittweite", {"0,1 mm", "1 mm", "10 mm", "50 mm"}};
    step.selected = jogStep_;
    f.push_back(step);
    Field h{Field::Kind::Button, "home", "Referenzfahrt"};
    h.enabled = !dev_.jobRunning;
    f.push_back(h);
    if (alarm) f.push_back({Field::Kind::Button, "unlock", "Entsperren ohne Referenzfahrt"});

    if (test_) return;   // Materialtest hat eigene Felder
    f.push_back({Field::Kind::Header, "", "Auftrag"});
    Field origin{Field::Kind::Choice, "origin", "Position", {"Wie auf der Fläche", "Am Laserkopf"}};
    origin.selected = origin_;
    origin.enabled = !dev_.jobRunning;
    f.push_back(origin);
    if (origin_ == 1) {
        if (lockedHead_) f.push_back({Field::Kind::Info, "", "📍 Position festgelegt (Rahmen/Auftrag). Pfeiltasten lösen sie wieder."});
        Field a{Field::Kind::Anchor, "anchor", "Anker"};
        a.selected = anchor_;
        a.enabled = !dev_.jobRunning;
        f.push_back(a);
        f.push_back({Field::Kind::Info, "", std::string("Der Punkt „") + anchorName(anchor_) + "“ des Designs liegt unter dem Laserkopf. Kopf mit den Pfeiltasten verschieben, dann Rahmen abfahren."});
    }
    auto off = offsetFor();
    if (!off) {
        Field w{Field::Kind::Info, "", origin_ == 1 ? "⚠ Von hier aus ragt das Design über die Arbeitsfläche." : "⚠ Das Design passt so nicht auf die Arbeitsfläche."};
        w.highlight = true;
        f.push_back(w);
    }
    if (dev_.jobRunning) {
        Field p{Field::Kind::Progress, "progress", "Fortschritt"};
        p.value = dev_.total ? double(dev_.done) * 100.0 / double(dev_.total) : 0;
        p.unit = std::to_string(dev_.done) + " / " + std::to_string(dev_.total) + " Zeilen";
        f.push_back(p);
        Field s{Field::Kind::Button, "stop", "Stopp (Esc)"};
        s.highlight = true;
        f.push_back(s);
    } else {
        f.push_back({Field::Kind::Info, "", "Geschätzte Laufzeit: " + estimateText()});
        Field fr{Field::Kind::Button, "frame", "Rahmen abfahren (Laser aus)"};
        fr.enabled = idle && off.has_value();
        f.push_back(fr);
    }
}

} // namespace mb
