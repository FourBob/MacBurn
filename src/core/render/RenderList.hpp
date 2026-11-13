// MacBurn – was die Zeichenfläche zeigen soll, unabhängig von Metal oder CoreGraphics.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "document/Document.hpp"
#include "job/Job.hpp"

#include <optional>
#include <vector>

namespace mb {

enum class Phase { Design, Prepare, Laser };

struct Stroke {
    std::vector<Vec2> points;
    bool closed = false;
    Color color;
    float width = 1.0f;    // Punkte auf dem Bildschirm
    bool dashed = false;
};

struct RenderList {
    Rect workArea;
    std::vector<Stroke> strokes;
    std::optional<Vec2> head;       // Laserkopf (nur verbunden)
    std::optional<Rect> jobFrame;   // Umriss des Auftrags auf der Maschine
    std::optional<Rect> selection;  // Rahmen um die Auswahl
    std::vector<Vec2> handles;      // 8 Größen-Anfasser, dann der Dreh-Anfasser (mm)
};

// Gestalten: Ebenenfarben. Vorbereiten: Brennfarbe + Leerfahrten. Lasern: Brennfarbe.
RenderList buildRenderList(const Document& doc, const Job& job, Phase phase, Rect workArea);

} // namespace mb
