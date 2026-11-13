// MacBurn – Job-Pipeline: Dokument → geräteneutrale Bewegungsliste → G-Code.
// Vorschau, Zeitschätzung und Export nutzen dieselbe Liste.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "document/Document.hpp"
#include "profiles/Profiles.hpp"

#include <string>
#include <vector>

namespace mb {

struct Move {
    enum class Kind { Rapid, Burn } kind = Kind::Rapid;
    Vec2 to;
    double speed = 0;   // mm/min
    double power = 0;   // %
};

struct Job {
    std::vector<Move> moves;
    double burnLengthMm = 0, travelLengthMm = 0;
};

class JobBuilder {
public:
    JobBuilder(const MachineProfile& machine, const MaterialProfile& material)
        : machine_(machine), material_(material) {}

    // Reihenfolge: Markieren, Gravieren, Schneiden; beim Schneiden innen vor außen.
    Job build(const Document& doc) const;

    // Füllung geschlossener Konturen (gerade-ungerade-Regel), Zeilen in y-Richtung, bidirektional.
    static std::vector<std::pair<Vec2, Vec2>> scanlineFill(const std::vector<const Polyline*>& shapes, double interval);

private:
    const MachineProfile& machine_;
    const MaterialProfile& material_;
};

// Ankerpunkt eines Rechtecks: 0 = oben links … 4 = Mitte … 8 = unten rechts (zeilenweise, y nach hinten).
Vec2 anchorPoint(const Rect& r, int anchor);
const char* anchorName(int anchor);

// Zeitschätzung mit Beschleunigung (Trapezprofil je Segment), Ergebnis in Sekunden.
double estimateSeconds(const Job& job, double rapidSpeedMmMin, double accelMmS2 = 1000);

struct GcodeOptions {
    double sMax = 1000;
    bool dynamicPower = true;      // M4 statt M3
    bool airAssistGcode = false;   // M8/M9 ausgeben
    Vec2 offset;                   // Verschiebung aller Koordinaten (Jobursprung)
    double travelSpeed = 0;        // > 0: Leerfahrten als G1 mit S0 und diesem Vorschub statt G0
};
std::string writeGcode(const Job& job, const GcodeOptions& opt);

} // namespace mb
