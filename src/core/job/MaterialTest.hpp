// MacBurn – Materialtest mit Verläufen statt Kästchen.
//
// Testplatte (Ursprung unten links, ca. 90 × 40 mm):
//   oben:   Kontrollstreifen (3 mm) – halbe Geschwindigkeit, gleiche Energie wie der Gravurstreifen
//   Mitte:  Gravurstreifen (5 mm) – Leistung steigt stufenlos von powerMin auf powerMax
//   Skala 0–100 unter dem Gravurstreifen
//   unten:  Schnitt-Leiter – schmaler Streifen, Geschwindigkeit sinkt stufenlos von cutSpeedMax auf cutSpeedMin
//   Skala 0–100 unter der Leiter
// Abgelesen wird jeweils ein Skalenwert (0–100); daraus entstehen die Materialwerte.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "job/Job.hpp"
#include "text/Font.hpp"

namespace mb {

struct MaterialTestSpec {
    bool engrave = true, cut = true;
    double engraveSpeed = 3000;        // mm/min
    double powerMin = 5, powerMax = 100;
    double lineIntervalMm = 0.1;
    double controlSpeedFactor = 0.5;   // Kontrollstreifen: diese Geschwindigkeit × Faktor
    double cutPower = 100;
    double cutSpeedMax = 1000, cutSpeedMin = 100;
    int cutPasses = 1;
    // Skala und Zahlen werden graviert (gefüllt) – mit den Gravurwerten des Materials, damit sie lesbar sind
    double labelPower = 60, labelSpeed = 3000;
    double lengthMm = 80;
    double travelSpeed = 3000;
};

struct MaterialTestPlate {
    Job job;                          // fertige Bewegungen (Ursprung unten links = 0/0)
    std::vector<Polyline> preview;    // Umrisse für die Anzeige
    Rect bounds;
};

MaterialTestPlate buildMaterialTest(const MaterialTestSpec& spec, const FontProvider* fonts);

// Auswertung der abgelesenen Skalenwerte (0–100)
double testEngravePower(const MaterialTestSpec& s, double reading);   // bei engraveSpeed
double testCutSpeed(const MaterialTestSpec& s, double reading);       // Geschwindigkeit, bei der es gerade noch durchgeht

} // namespace mb
