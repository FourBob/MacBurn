// MacBurn – SVG-Import: Formen werden zu Polylinien in mm, Farben zu Ebenen.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "document/Document.hpp"

#include <string>
#include <vector>

namespace mb {

struct SvgImportOptions {
    double pxPerInch = 96;      // Inkscape, Browser, Figma. Ältere Illustrator-Dateien: 72.
    double toleranceMm = 0.02;  // maximale Abweichung beim Annähern von Kurven
};

struct SvgImportResult {
    bool ok = false;
    std::string error;                  // nur gesetzt, wenn ok == false
    std::vector<Layer> layers;          // eine Ebene je Farbe, Absicht geschätzt
    std::vector<Shape> shapes;          // shape.layer verweist in layers
    std::vector<std::string> warnings;  // z. B. übersprungene Texte und Bilder
    Rect page;                          // Seitengröße in mm
};

// Koordinaten: Ursprung vorne links, y nach hinten (Seite unten links = Maschinenursprung).
SvgImportResult importSvg(const std::string& svgText, const SvgImportOptions& options = {});
SvgImportResult importSvgFile(const std::string& path, const SvgImportOptions& options = {});

} // namespace mb
