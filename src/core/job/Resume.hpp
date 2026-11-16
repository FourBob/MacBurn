// MacBurn – Fortsetzen nach Abbruch: wo stand der Laser im G-Code, und ab wo wird sicher neu begonnen?
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "geometry/Geometry.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mb {

// Ziel jeder Zeile (absolut, G90) und ob dort gebrannt wird. Zeilen ohne Bewegung behalten das letzte Ziel.
struct GcodeTrack {
    std::vector<Vec2> to;
    std::vector<bool> motion;   // Zeile bewegt den Kopf
    std::vector<bool> burn;     // … und der Laser ist dabei an
    std::vector<double> feed;   // gültiger Vorschub nach dieser Zeile
    bool dynamicPower = true;   // M4 (sonst M3)
    bool air = false;           // M8 kommt vor
};
GcodeTrack trackGcode(const std::vector<std::string>& lines);

// Welche Zeile wird gerade ausgeführt? Erste Zeile ab `from` (bis `to`, ausschließlich), deren Strecke
// durch `pos` geht. Gibt `from` zurück, wenn keine passt.
size_t locateExecuting(const GcodeTrack& t, size_t from, size_t to, Vec2 pos, double tolMm = 0.15);

// Sicherer Neubeginn: die letzte Leerfahrt an oder vor `executing` – also der Anfang der Kontur
// (Schneiden) bzw. der Zeile (Gravieren), die gerade lief.
size_t restartLine(const GcodeTrack& t, size_t executing);

// Vorspann (Einheiten, Absolutmaße, Lasermodus, Luft, Vorschub) und Rest ab `restart`.
std::vector<std::string> resumeLines(const std::vector<std::string>& lines, const GcodeTrack& t, size_t restart,
                                     size_t* preambleCount = nullptr);

// Umriss der noch zu brennenden Strecken ab `restart`.
Rect remainingBounds(const GcodeTrack& t, size_t restart);
// Noch zu brennende Strecken als Linienzüge (für die Vorschau).
std::vector<std::vector<Vec2>> remainingPaths(const GcodeTrack& t, size_t restart);

} // namespace mb
