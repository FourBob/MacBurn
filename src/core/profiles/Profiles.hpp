// MacBurn – Profile: Maschine → Material (wie Drucker → Filament).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "util/Json.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mb {

// Was mit einer Ebene passieren soll.
enum class Intent { Cut, Engrave, Photo, Mark };
const char* intentKey(Intent i);       // "cut", "engrave", …
const char* intentLabel(Intent i);     // "Schneiden", …
std::optional<Intent> intentFromKey(const std::string& key);
inline constexpr Intent kAllIntents[] = {Intent::Cut, Intent::Engrave, Intent::Photo, Intent::Mark};

struct WorkArea { double x = 400, y = 400; };

struct MachineVariant {
    std::string id, name;
    WorkArea area;
};

struct MachineProfile {
    std::string id;            // "mecpow/x3-pro"
    std::string vendor, model;
    std::vector<MachineVariant> variants;
    std::string powerClass;    // "diode-10w-445nm"
    double opticalPowerW = 0;
    double wavelengthNm = 0;
    double maxSpeedMmMin = 6000;
    double accelerationMmS2 = 1000; // GRBL $120/$121
    double travelSpeedMmMin = 3000; // Leerfahrten und Rahmen; schonender als der Eilgang
    double sMax = 1000;        // GRBL $30
    int baudRate = 115200;
    bool airAssistControllable = false;
    std::string focusHint;

    std::string displayName() const { return vendor + " " + model; }
};

// Einstellungen für eine Absicht. Fehlende Felder = nicht gesetzt.
struct IntentSettings {
    std::optional<double> speed;      // mm/min
    std::optional<double> power;      // % (max)
    std::optional<double> minPower;   // % (Graustufen)
    std::optional<int> passes;
    std::optional<double> lineIntervalMm;
    std::optional<double> dpi;
    std::optional<bool> airAssist;
    // Gemessene Leistung je Geschwindigkeit (aus dem Materialtest): [(mm/min, %), …]
    // Wird im Projekt nur die Geschwindigkeit geändert, folgt die Leistung dieser Kurve.
    std::vector<std::pair<double, double>> powerBySpeed;

    // Felder aus other überschreiben die eigenen, wo gesetzt.
    IntentSettings mergedWith(const IntentSettings& other) const;
    bool empty() const;
};

struct MaterialProfile {
    std::string id;            // gemeinsame Kennung, z. B. "plywood-poplar-3mm"
    std::string name, category;
    double thicknessMm = 0;
    std::string powerClass;    // gilt für eine Leistungsklasse …
    std::string machine;       // … oder für genau eine Maschine (Benutzerprofil)
    std::string source;        // "system" | "user"
    std::string note;          // Herkunft der Werte, Hinweise
    std::map<Intent, IntentSettings> intents;
};

// Getestet (eigener Materialtest) > Herstellerwert (Anleitung) > abgeleitet (Leistungsklasse)
enum class MaterialStatus { Tested, Vendor, Derived, Modified };
const char* statusGlyph(MaterialStatus s);   // ✓ ≈ ✎
const char* statusLabel(MaterialStatus s);

// Ergebnis von resolve(): vollständige Werte, die der Job-Compiler nutzt.
struct EffectiveSettings {
    bool possible = true;      // false, wenn das Material diese Absicht nicht kennt
    double speed = 1000, power = 0, minPower = 0;
    int passes = 1;
    double lineIntervalMm = 0.1, dpi = 254;
    bool airAssist = true;
};

double wattsOfPowerClass(const std::string& powerClass);
// Leistung für eine Geschwindigkeit aus der Kurve (log-log interpoliert; ein Punkt = gleiche Energie)
double powerForSpeed(const std::vector<std::pair<double, double>>& curve, double speed);

// Für Projektdateien und Benutzerprofile
MaterialProfile materialFromJson(const json::Value& v, const std::string& source);
json::Value materialToJson(const MaterialProfile& m);
IntentSettings settingsFromJson(const json::Value& v);
json::Value settingsToJson(const IntentSettings& s);   // "diode-10w-445nm" → 10

class ProfileStore {
public:
    // Lädt <root>/machines/**.json und <root>/materials/**.json.
    // Fehlerhafte Dateien werden übersprungen und in errors() gemeldet.
    void loadDirectory(const std::string& root, const std::string& source);

    const std::vector<MachineProfile>& machines() const { return machines_; }
    const std::vector<MaterialProfile>& materials() const { return materials_; }
    const std::vector<std::string>& errors() const { return errors_; }

    const MachineProfile* machine(const std::string& id) const;

    struct MaterialChoice {
        const MaterialProfile* material;
        MaterialStatus status;   // Tested, Vendor oder Derived
    };
    // Nur Materialien, die zur Maschine passen; Benutzerprofile dieser Maschine
    // ersetzen gleichnamige Systemprofile.
    std::vector<MaterialChoice> materialsFor(const MachineProfile& m) const;

    static EffectiveSettings resolve(const MachineProfile& machine,
                                     const MaterialProfile& material,
                                     Intent intent,
                                     const IntentSettings& overrides = {});

    // Für Tests und Programmcode ohne Dateien.
    void add(MachineProfile m) { machines_.push_back(std::move(m)); }
    void add(MaterialProfile m) { materials_.push_back(std::move(m)); }

private:
    std::vector<MachineProfile> machines_;
    std::vector<MaterialProfile> materials_;
    std::vector<std::string> errors_;
};

} // namespace mb
