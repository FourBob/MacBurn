// MacBurn – Zahleneingabe wie in guten Mac-Apps: Komma oder Punkt, Rechnen, Einheiten.
// „100/3“, „25+12,5“, „2in“, „1,5 cm“, „(10+2)*3“ → Wert in mm (bzw. ohne Einheit).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>

namespace mb {

std::optional<double> parseNumberInput(const std::string& text);

// Für die Anzeige: höchstens `decimals` Nachkommastellen, deutsches Komma, ohne überflüssige Nullen
std::string formatNumber(double v, int decimals = 3);

} // namespace mb
