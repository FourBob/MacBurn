// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/Project.hpp"
#include "util/Json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mb {

using json::Value;

std::string colorToHex(const Color& c) {
    auto ch = [](float f) { return std::clamp(int(std::lround(f * 255)), 0, 255); };
    char b[8];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", ch(c.r), ch(c.g), ch(c.b));
    return b;
}

Color colorFromHex(const std::string& hex) {
    if (hex.size() != 7 || hex[0] != '#') return {0, 0, 0, 1};
    auto h = [&](size_t i) { return float(std::strtol(hex.substr(i, 2).c_str(), nullptr, 16)) / 255.0f; };
    return {h(1), h(3), h(5), 1};
}

std::string writeProject(const ProjectData& p) {
    std::vector<Value> layers;
    for (const auto& l : p.doc.layers) {
        std::map<std::string, Value> o = {
            {"name", Value::string(l.name)}, {"color", Value::string(colorToHex(l.color))},
            {"intent", Value::string(intentKey(l.intent))}, {"output", Value::boolean(l.output)}};
        if (!l.colorKey.empty()) o["colorKey"] = Value::string(l.colorKey);
        if (!l.overrides.empty()) o["overrides"] = settingsToJson(l.overrides);
        layers.push_back(Value::object(std::move(o)));
    }
    std::vector<Value> shapes;
    for (const auto& s : p.doc.shapes) {
        std::vector<Value> pts;
        pts.reserve(s.path.points.size());
        for (const auto& pt : s.path.points) pts.push_back(Value::array({Value::number(pt.x), Value::number(pt.y)}));
        shapes.push_back(Value::object({
            {"name", Value::string(s.name)}, {"layer", Value::number(double(s.layer))},
            {"object", Value::number(s.object)}, {"part", Value::number(s.part)}, {"closed", Value::boolean(s.path.closed)},
            {"points", Value::array(std::move(pts))}}));
    }
    std::vector<Value> texts;
    for (const auto& [part, t] : p.doc.texts) {
        texts.push_back(Value::object({
            {"part", Value::number(part)}, {"text", Value::string(t.text)}, {"family", Value::string(t.family)},
            {"style", Value::string(t.style)}, {"capHeightMm", Value::number(t.capHeightMm)},
            {"align", Value::number(t.align)}, {"letterSpacingMm", Value::number(t.letterSpacingMm)},
            {"lineSpacing", Value::number(t.lineSpacing)},
            {"matrix", Value::array({Value::number(t.m.a), Value::number(t.m.b), Value::number(t.m.c),
                                     Value::number(t.m.d), Value::number(t.m.e), Value::number(t.m.f)})}}));
    }
    std::map<std::string, Value> laser = {
        {"machine", Value::string(p.machineId)}, {"variant", Value::string(p.variantId)},
        {"material", Value::string(p.materialId)},
        {"position", Value::string(p.position == 1 ? "laserHead" : "asPlaced")},
        {"anchor", Value::number(p.anchor)}};
    if (p.material) laser["materialValues"] = materialToJson(*p.material);
    return json::write(Value::object({
        {"format", Value::string("macburn")}, {"version", Value::number(kProjectFormatVersion)},
        {"laser", Value::object(std::move(laser))},
        {"layers", Value::array(std::move(layers))}, {"shapes", Value::array(std::move(shapes))},
        {"texts", Value::array(std::move(texts))}}));
}

std::optional<ProjectData> readProject(const std::string& text, std::string* error) {
    auto fail = [&](const std::string& m) -> std::optional<ProjectData> { if (error) *error = m; return std::nullopt; };
    json::ParseError perr;
    auto v = json::parse(text, &perr);
    if (!v || !v->isObject()) return fail("Die Datei ist beschädigt (" + perr.message + ").");
    if ((*v)["format"].asString() != "macburn") return fail("Das ist keine MacBurn-Projektdatei.");
    if ((*v)["version"].asNumber() > kProjectFormatVersion)
        return fail("Die Datei stammt aus einer neueren MacBurn-Version. Bitte MacBurn aktualisieren.");

    ProjectData p;
    for (const auto& l : (*v)["layers"].asArray()) {
        Layer layer;
        layer.name = l["name"].asString("Ebene");
        layer.color = colorFromHex(l["color"].asString("#000000"));
        layer.intent = intentFromKey(l["intent"].asString()).value_or(Intent::Mark);
        layer.output = l["output"].asBool(true);
        layer.colorKey = l["colorKey"].asString();
        if (l.has("overrides")) layer.overrides = settingsFromJson(l["overrides"]);
        p.doc.layers.push_back(layer);
    }
    for (const auto& s : (*v)["shapes"].asArray()) {
        Shape shape;
        shape.name = s["name"].asString();
        shape.layer = size_t(std::max(0.0, s["layer"].asNumber()));
        shape.object = uint32_t(std::max(0.0, s["object"].asNumber()));
        shape.part = uint32_t(std::max(0.0, s["part"].asNumber()));
        shape.path.closed = s["closed"].asBool();
        for (const auto& pt : s["points"].asArray()) {
            const auto& xy = pt.asArray();
            if (xy.size() >= 2) shape.path.points.push_back({xy[0].asNumber(), xy[1].asNumber()});
        }
        if (shape.layer >= p.doc.layers.size()) return fail("Die Datei ist beschädigt (Form ohne gültige Ebene).");
        if (shape.path.points.size() >= 2) p.doc.shapes.push_back(std::move(shape));
    }
    for (const auto& t : (*v)["texts"].asArray()) {
        TextSpec spec;
        spec.text = t["text"].asString();
        spec.family = t["family"].asString(spec.family);
        spec.style = t["style"].asString(spec.style);
        spec.capHeightMm = t["capHeightMm"].asNumber(spec.capHeightMm);
        spec.align = std::clamp(int(t["align"].asNumber(0)), 0, 2);
        spec.letterSpacingMm = t["letterSpacingMm"].asNumber(0);
        spec.lineSpacing = t["lineSpacing"].asNumber(1.2);
        const auto& m = t["matrix"].asArray();
        if (m.size() == 6) spec.m = {m[0].asNumber(), m[1].asNumber(), m[2].asNumber(), m[3].asNumber(), m[4].asNumber(), m[5].asNumber()};
        p.doc.texts[uint32_t(std::max(0.0, t["part"].asNumber()))] = spec;
    }
    p.doc.pruneTexts();
    const auto& laser = (*v)["laser"];
    p.machineId = laser["machine"].asString();
    p.variantId = laser["variant"].asString();
    p.materialId = laser["material"].asString();
    p.position = laser["position"].asString() == "laserHead" ? 1 : 0;
    p.anchor = std::clamp(int(laser["anchor"].asNumber(4)), 0, 8);
    if (laser.has("materialValues")) p.material = materialFromJson(laser["materialValues"], "project");
    return p;
}

} // namespace mb
