// MacBurn – Fortsetzen nach Abbruch: Sicherungspunkt während des Auftrags, Neubeginn am Anfang der Kontur.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppController.hpp"
#include "util/Json.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace mb {

namespace {
double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string nowText() {
    std::time_t now = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof ts, "%d.%m. %H:%M", std::localtime(&now));
    return ts;
}
std::string mm(Vec2 p) {
    char b[64];
    std::snprintf(b, sizeof b, "X %.1f Y %.1f", p.x, p.y);
    return b;
}
} // namespace

std::string AppController::resumeDir() const {
    if (userProfilesDir_.empty() || userProfilesDir_ == "/nonexistent") return {};
    return (std::filesystem::path(userProfilesDir_).parent_path() / "resume").string();
}

void AppController::beginCheckpoint(std::vector<std::string> lines) {
    if (lines.empty()) { discardResume(); return; }
    Checkpoint c;
    c.title = title_;
    c.material = material() ? material()->name : "";
    c.startedAt = nowText();
    c.track = trackGcode(lines);
    c.lines = std::move(lines);
    c.homedAtStart = dev_.homed;
    c.running = true;
    checkpoint_ = std::move(c);
    saveCheckpoint(true);
}

void AppController::updateCheckpoint() {
    if (!checkpoint_ || !checkpoint_->running) return;
    auto& c = *checkpoint_;
    // bestätigt = angenommen, nicht unbedingt ausgeführt. dev_.done zählt bei einem fortgesetzten Auftrag schon
    // den ganzen Auftrag – hier darum die Zählung des Geräts selbst.
    long acked = long(c.deviceAcked) + c.deviceBase;
    if (acked < 0) acked = 0;
    // Vorrat der Steuerung (Empfangs- und Planungspuffer) ist begrenzt: weiter zurück kann die
    // ausgeführte Zeile nicht liegen.
    size_t floor = size_t(std::max<long>(0, acked - 48));
    size_t from = std::max(c.executing, floor);
    size_t to = std::min(c.lines.size(), size_t(acked) + 1);
    if (from < to) c.executing = locateExecuting(c.track, from, to, workPos());
    else c.executing = std::max(c.executing, from);
    double now = steadySeconds();
    if (now - c.savedAt >= 1.0) saveCheckpoint(false);
}

void AppController::saveCheckpoint(bool withLines) {
    if (!checkpoint_) return;
    checkpoint_->savedAt = steadySeconds();
    std::string dir = resumeDir();
    if (dir.empty()) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const auto& c = *checkpoint_;
    if (withLines) {
        std::ofstream g(fs::path(dir) / "auftrag.gcode", std::ios::binary | std::ios::trunc);
        for (const auto& l : c.lines) g << l << "\n";
    }
    // erst vollständig schreiben, dann ersetzen – ein Stromausfall mitten im Schreiben hinterlässt die alte Datei
    auto tmp = fs::path(dir) / "stand.json.tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << json::write(json::Value::object({
            {"title", json::Value::string(c.title)},
            {"material", json::Value::string(c.material)},
            {"startedAt", json::Value::string(c.startedAt)},
            {"homedAtStart", json::Value::boolean(c.homedAtStart)},
            {"executing", json::Value::number(double(c.executing))},
            {"lines", json::Value::number(double(c.lines.size()))},
        }));
    }
    fs::rename(tmp, fs::path(dir) / "stand.json", ec);
}

void AppController::loadCheckpoint() {
    std::string dir = resumeDir();
    if (dir.empty()) return;
    namespace fs = std::filesystem;
    auto v = json::parseFile((fs::path(dir) / "stand.json").string());
    if (!v) return;
    std::ifstream g(fs::path(dir) / "auftrag.gcode");
    if (!g) return;
    Checkpoint c;
    for (std::string l; std::getline(g, l);) c.lines.push_back(l);
    if (c.lines.empty() || double(c.lines.size()) != (*v)["lines"].asNumber()) return;   // unvollständig
    c.title = (*v)["title"].asString();
    c.material = (*v)["material"].asString();
    c.startedAt = (*v)["startedAt"].asString();
    c.homedAtStart = (*v)["homedAtStart"].asBool();
    c.executing = std::min(c.lines.size() - 1, size_t((*v)["executing"].asNumber()));
    c.track = trackGcode(c.lines);
    checkpoint_ = std::move(c);
}

void AppController::discardResume() {
    checkpoint_.reset();
    std::string dir = resumeDir();
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    changed();
}

void AppController::resumeJob() {
    if (!resumeAvailable() || !deviceIdle()) return;
    auto& c = *checkpoint_;
    if (c.homedAtStart && !dev_.homed) {
        message_ = "Erst eine Referenzfahrt – dann stimmt die Position wieder genau.";
        changed();
        return;
    }
    size_t r = restartIndex();
    size_t pre = 0;
    auto lines = resumeLines(c.lines, c.track, r, &pre);
    c.deviceBase = long(r) - long(pre);
    c.deviceAcked = 0;
    c.executing = r;
    c.running = true;
    saveCheckpoint(false);
    size_t pct = c.lines.empty() ? 0 : r * 100 / c.lines.size();
    log("AUFTRAG FORTGESETZT „" + c.title + "“ · ab Zeile " + std::to_string(r + 1) + " von " + std::to_string(c.lines.size()) +
        " (" + std::to_string(pct) + " %) · Start bei " + mm(c.track.to[r]) + (dev_.homed ? " · referenziert" : " · OHNE Referenzfahrt"));
    pointerOff("Fortsetzen", false);
    lockedHead_.reset();
    device_->startJob(std::move(lines));
    dev_.jobRunning = true;
    dev_.done = dev_.total = 0;
    message_ = "Auftrag wird fortgesetzt.";
    changed();
}

void AppController::frameRemainder() {
    if (!resumeAvailable() || !deviceIdle()) return;
    const auto& c = *checkpoint_;
    if (c.homedAtStart && !dev_.homed) {
        message_ = "Erst eine Referenzfahrt – sonst stimmt der Rahmen nicht.";
        changed();
        return;
    }
    restOfCheckpoint();
    Rect b = c.restBounds;
    if (b.w <= 0 && b.h <= 0) {
        message_ = "Vom unterbrochenen Auftrag ist nichts mehr zu brennen.";
        changed();
        return;
    }
    pointerOff("Rahmen", false);
    Vec2 back = workPos();
    char f[32];
    std::snprintf(f, sizeof f, " F%.0f S0", machine() ? machine()->travelSpeedMmMin : 3000.0);
    auto go = [&](double x, double y) { char b2[64]; std::snprintf(b2, sizeof b2, "G1 X%.3f Y%.3f", x, y); return std::string(b2) + f; };
    for (const auto& l : std::vector<std::string>{"M5", "G90", go(b.x, b.y), go(b.x + b.w, b.y), go(b.x + b.w, b.y + b.h),
                                                  go(b.x, b.y + b.h), go(b.x, b.y), go(back.x, back.y)})
        device_->sendLine(l);
    log("RAHMEN REST " + mm({b.x, b.y}) + " bis " + mm({b.x + b.w, b.y + b.h}));
    message_ = "Rahmen des Rests wird abgefahren (Laser aus).";
    changed();
}

const AppController::Checkpoint& AppController::restOfCheckpoint() const {
    const auto& c = *checkpoint_;
    size_t r = restartIndex();
    if (c.restFor != r) {
        c.restPaths = remainingPaths(c.track, r);
        c.restBounds = remainingBounds(c.track, r);
        c.restFor = r;
    }
    return c;
}

void AppController::addResumeFields(std::vector<Field>& f) const {
    if (!resumeAvailable()) return;
    const auto& c = *checkpoint_;
    size_t r = restartIndex();
    size_t pct = c.lines.empty() ? 0 : c.executing * 100 / c.lines.size();
    Field h{Field::Kind::Header, "", "Unterbrochener Auftrag"};
    f.push_back(h);
    f.push_back({Field::Kind::Info, "", "„" + c.title + "“ · " + c.material + " · gestartet " + c.startedAt +
                                        " · unterbrochen bei " + std::to_string(pct) + " %"});
    if (restOfCheckpoint().restPaths.empty())
        f.push_back({Field::Kind::Info, "", "Es ist nichts mehr zu brennen – der Auftrag war praktisch fertig."});
    else if (r < c.track.to.size())
        f.push_back({Field::Kind::Info, "", "Neubeginn am Anfang der Kontur bzw. Zeile, die gerade lief: " + mm(c.track.to[r]) +
                                            ". Orange auf der Fläche: was noch fehlt."});
    bool needHome = c.homedAtStart && !dev_.homed;
    if (!dev_.connected) {
        f.push_back({Field::Kind::Info, "", "Zum Fortsetzen verbinden. Das Werkstück nicht verschieben."});
    } else if (needHome) {
        Field w{Field::Kind::Info, "", "Vor dem Auftrag lief eine Referenzfahrt. Jetzt erst wieder referenzieren – dann stimmt die Position genau, auch nach einem Stromausfall."};
        w.highlight = true;
        f.push_back(w);
        Field home{Field::Kind::Button, "home", "Referenzfahrt"};
        home.enabled = deviceIdle() || dev_.state.rfind("Alarm", 0) == 0;
        f.push_back(home);
    } else if (!c.homedAtStart) {
        Field w{Field::Kind::Info, "", "⚠ Vor dem Auftrag lief keine Referenzfahrt. Genau passt es nur, wenn der Laser nicht aus war (kein Stromausfall) und das Werkstück nicht bewegt wurde. Erst den Rahmen prüfen."};
        w.highlight = true;
        f.push_back(w);
    }
    Field fr{Field::Kind::Button, "resumeFrame", "Rahmen des Rests abfahren (Laser aus)"};
    fr.enabled = dev_.connected && deviceIdle() && !needHome;
    f.push_back(fr);
    Field go{Field::Kind::Button, "resumeJob", "Fortsetzen"};
    go.enabled = dev_.connected && deviceIdle() && !needHome;
    go.highlight = go.enabled;
    f.push_back(go);
    f.push_back({Field::Kind::Button, "resumeDiscard", "Verwerfen"});
}

} // namespace mb
