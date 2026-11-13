// MacBurn – Schriften: Liste der Schriften und Umrisse für Text.
// Die Umsetzung für macOS nutzt CoreText (C-API); andere Systeme bekämen eine eigene Datei.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "document/Document.hpp"

#include <memory>
#include <string>
#include <vector>

namespace mb {

class FontProvider {
public:
    virtual ~FontProvider() = default;
    virtual std::vector<std::string> families() const = 0;
    virtual std::vector<std::string> styles(const std::string& family) const = 0;
    // Umrisse im Text-Koordinatensystem: mm, Grundlinie der ersten Zeile bei y = 0, y nach oben.
    // Leere Liste, wenn die Schrift fehlt oder der Text leer ist.
    virtual std::vector<Polyline> outline(const TextSpec& spec, double toleranceMm = 0.02) const = 0;
};

std::unique_ptr<FontProvider> makeSystemFontProvider();

} // namespace mb
