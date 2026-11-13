// MacBurn – kleiner XML-Leser (reicht für SVG; keine DTD, keine Namensräume).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mb::xml {

struct Element {
    std::string name;                              // ohne Präfix-Auflösung, z. B. "svg", "xlink:href" als Attribut
    std::map<std::string, std::string> attributes;
    std::vector<std::unique_ptr<Element>> children;
    std::string text;                              // gesammelter Text inkl. CDATA (für <style>)

    const std::string* attr(const std::string& key) const;
    // Name ohne Namensraum-Präfix: "svg:path" → "path"
    std::string localName() const;
};

struct ParseError { std::string message; size_t offset = 0; };

std::unique_ptr<Element> parse(const std::string& text, ParseError* error = nullptr);

} // namespace mb::xml
