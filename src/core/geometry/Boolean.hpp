// MacBurn – Boolesche Operationen und Versatz auf Polylinien (über Clipper2).
// Jede Eingabe-„Form“ ist eine Liste geschlossener Polylinien, die nach der
// Gerade-ungerade-Regel gefüllt sind (Löcher wie beim Buchstaben O).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "geometry/Geometry.hpp"

#include <vector>

namespace mb::geo {

using Region = std::vector<Polyline>;   // eine Fläche, ggf. mit Löchern

Region unite(const std::vector<Region>& regions);
Region subtract(const Region& base, const std::vector<Region>& cutters);
Region intersect(const std::vector<Region>& regions);
// Zerlegt überlappende Flächen in alle getrennten Teilflächen
std::vector<Region> divide(const std::vector<Region>& regions);

// Versatz: > 0 nach außen, < 0 nach innen; runde Ecken.
// Offene Linien werden zu einer Kontur mit runden Enden (Breite = 2 × |distance|).
Region offset(const std::vector<Polyline>& paths, double distance, double tolerance = 0.01);

double area(const Region& r);   // Fläche in mm² (Löcher abgezogen)

} // namespace mb::geo
