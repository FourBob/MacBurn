// MacBurn – Projektdatei (.macburn): lesbares JSON.
// Enthält Formen, Ebenen, Projekt-Abweichungen, Maschine, Material, Position –
// und eine Kopie der Materialwerte, damit das Projekt auch ohne das Profil öffnet.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "document/Document.hpp"
#include "profiles/Profiles.hpp"

#include <optional>
#include <string>

namespace mb {

struct ProjectData {
    Document doc;
    std::string machineId, variantId, materialId;
    std::optional<MaterialProfile> material;   // Kopie der Werte beim Sichern
    int position = 0;                           // 0 = wie auf der Fläche, 1 = am Laserkopf
    int anchor = 4;
};

inline constexpr int kProjectFormatVersion = 1;

std::string writeProject(const ProjectData& p);
std::optional<ProjectData> readProject(const std::string& text, std::string* error = nullptr);

std::string colorToHex(const Color& c);
Color colorFromHex(const std::string& hex);

} // namespace mb
