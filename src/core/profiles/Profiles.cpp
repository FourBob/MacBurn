// SPDX-License-Identifier: GPL-3.0-or-later
#include "profiles/Profiles.hpp"
#include "util/Json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

namespace mb {

const char* intentKey(Intent i) {
    switch (i) {
        case Intent::Cut: return "cut";
        case Intent::Engrave: return "engrave";
        case Intent::Photo: return "photo";
        case Intent::Mark: return "mark";
    }
    return "";
}
const char* intentLabel(Intent i) {
    switch (i) {
        case Intent::Cut: return "Schneiden";
        case Intent::Engrave: return "Gravieren";
        case Intent::Photo: return "Foto";
        case Intent::Mark: return "Markieren";
    }
    return "";
}
std::optional<Intent> intentFromKey(const std::string& key) {
    for (Intent i : kAllIntents) if (key == intentKey(i)) return i;
    return std::nullopt;
}
const char* statusGlyph(MaterialStatus s) {
    switch (s) {
        case MaterialStatus::Tested: return "✓";
        case MaterialStatus::Derived: return "≈";
        case MaterialStatus::Modified: return "✎";
    }
    return "";
}
const char* statusLabel(MaterialStatus s) {
    switch (s) {
        case MaterialStatus::Tested: return "getestet";
        case MaterialStatus::Derived: return "abgeleitet";
        case MaterialStatus::Modified: return "geändert";
    }
    return "";
}

IntentSettings IntentSettings::mergedWith(const IntentSettings& o) const {
    IntentSettings r = *this;
    if (o.speed) r.speed = o.speed;
    if (o.power) r.power = o.power;
    if (o.minPower) r.minPower = o.minPower;
    if (o.passes) r.passes = o.passes;
    if (o.lineIntervalMm) r.lineIntervalMm = o.lineIntervalMm;
    if (o.dpi) r.dpi = o.dpi;
    if (o.airAssist) r.airAssist = o.airAssist;
    return r;
}
bool IntentSettings::empty() const {
    return !speed && !power && !minPower && !passes && !lineIntervalMm && !dpi && !airAssist;
}

double wattsOfPowerClass(const std::string& pc) {
    static const std::regex re(R"((\d+(?:\.\d+)?)w)");
    std::smatch m;
    if (std::regex_search(pc, m, re)) return std::stod(m[1].str());
    return 0;
}

namespace {

MachineProfile machineFromJson(const json::Value& v) {
    MachineProfile m;
    m.id = v["id"].asString();
    m.vendor = v["vendor"].asString();
    m.model = v["model"].asString();
    for (const auto& var : v["variants"].asArray()) {
        MachineVariant mv;
        mv.id = var["id"].asString();
        mv.name = var["name"].asString();
        mv.area = {var["workArea"]["x"].asNumber(400), var["workArea"]["y"].asNumber(400)};
        m.variants.push_back(mv);
    }
    if (m.variants.empty()) m.variants.push_back({"standard", "Standard", {}});
    const auto& mods = v["laserModules"].asArray();
    if (!mods.empty()) {
        const auto& mod = mods.front();
        m.powerClass = mod["powerClass"].asString();
        m.opticalPowerW = mod["opticalPowerW"].asNumber();
        m.wavelengthNm = mod["wavelengthNm"].asNumber();
        m.focusHint = mod["focus"]["hint"].asString();
    }
    m.maxSpeedMmMin = v["limits"]["maxSpeedMmMin"].asNumber(6000);
    m.sMax = v["controller"]["sMax"].asNumber(1000);
    m.baudRate = int(v["controller"]["baudRate"].asNumber(115200));
    m.airAssistControllable = v["airAssist"]["control"].asString() == "gcode";
    return m;
}

IntentSettings settingsFromJson(const json::Value& v) {
    IntentSettings s;
    if (v.has("speed")) s.speed = v["speed"].asNumber();
    if (v.has("power")) s.power = v["power"].asNumber();
    if (v.has("minPower")) s.minPower = v["minPower"].asNumber();
    if (v.has("passes")) s.passes = int(v["passes"].asNumber(1));
    if (v.has("lineIntervalMm")) s.lineIntervalMm = v["lineIntervalMm"].asNumber();
    if (v.has("dpi")) s.dpi = v["dpi"].asNumber();
    if (v.has("airAssist")) s.airAssist = v["airAssist"].asBool();
    return s;
}

MaterialProfile materialFromJson(const json::Value& v, const std::string& source) {
    MaterialProfile m;
    m.id = v["id"].asString();
    m.name = v["name"].asString();
    m.category = v["category"].asString();
    m.thicknessMm = v["thicknessMm"].asNumber();
    m.powerClass = v["powerClass"].asString();
    m.machine = v["machine"].asString();
    m.source = source;
    for (const auto& [key, val] : v["intents"].asObject())
        if (auto i = intentFromKey(key)) m.intents[*i] = settingsFromJson(val);
    return m;
}

} // namespace

void ProfileStore::loadDirectory(const std::string& root, const std::string& source) {
    auto loadAll = [&](const fs::path& dir, bool machines) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) return;
        std::vector<fs::path> files;
        for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file() && it->path().extension() == ".json") files.push_back(it->path());
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            json::ParseError err;
            auto v = json::parseFile(f.string(), &err);
            if (!v || !v->isObject() || (*v)["id"].asString().empty()) {
                errors_.push_back(f.filename().string() + ": " + (v ? std::string("Feld 'id' fehlt") : err.message));
                continue;
            }
            if (machines) machines_.push_back(machineFromJson(*v));
            else materials_.push_back(materialFromJson(*v, source));
        }
    };
    loadAll(fs::path(root) / "machines", true);
    loadAll(fs::path(root) / "materials", false);
}

const MachineProfile* ProfileStore::machine(const std::string& id) const {
    for (const auto& m : machines_) if (m.id == id) return &m;
    return nullptr;
}

std::vector<ProfileStore::MaterialChoice> ProfileStore::materialsFor(const MachineProfile& mc) const {
    std::vector<MaterialChoice> out;
    // Zuerst Benutzerprofile genau dieser Maschine.
    for (const auto& m : materials_)
        if (m.machine == mc.id) out.push_back({&m, MaterialStatus::Tested});
    // Dann Klassenprofile gleicher Lasertechnik, sofern nicht schon getestet vorhanden.
    auto tech = [](const std::string& pc) { return pc.substr(0, pc.find('-')); };
    for (const auto& m : materials_) {
        if (!m.machine.empty() || m.powerClass.empty()) continue;
        if (tech(m.powerClass) != tech(mc.powerClass)) continue;
        bool shadowed = std::any_of(out.begin(), out.end(), [&](const MaterialChoice& c) { return c.material->id == m.id; });
        if (!shadowed) out.push_back({&m, MaterialStatus::Derived});
    }
    std::stable_sort(out.begin(), out.end(), [](const MaterialChoice& a, const MaterialChoice& b) {
        if (a.material->category != b.material->category) return a.material->category < b.material->category;
        return a.material->name < b.material->name;
    });
    return out;
}

EffectiveSettings ProfileStore::resolve(const MachineProfile& machine, const MaterialProfile& material,
                                        Intent intent, const IntentSettings& overrides) {
    EffectiveSettings e;
    auto it = material.intents.find(intent);
    if (it == material.intents.end()) {
        e.possible = false;
        return e;
    }
    IntentSettings base = it->second;

    // Klassenprofil auf andere Leistung umrechnen: Geschwindigkeit skaliert linear mit der Leistung.
    if (material.machine.empty()) {
        double from = wattsOfPowerClass(material.powerClass);
        double to = machine.opticalPowerW > 0 ? machine.opticalPowerW : wattsOfPowerClass(machine.powerClass);
        if (from > 0 && to > 0 && std::abs(from - to) > 0.01 && base.speed)
            base.speed = std::round(*base.speed * to / from / 10.0) * 10.0;
    }

    IntentSettings s = base.mergedWith(overrides);
    e.speed = std::min(s.speed.value_or(1000), machine.maxSpeedMmMin);
    e.power = std::clamp(s.power.value_or(0), 0.0, 100.0);
    e.minPower = std::clamp(s.minPower.value_or(0), 0.0, e.power);
    e.passes = std::max(1, s.passes.value_or(1));
    e.lineIntervalMm = s.lineIntervalMm.value_or(0.1);
    e.dpi = s.dpi.value_or(254);
    e.airAssist = s.airAssist.value_or(true);
    return e;
}

} // namespace mb
