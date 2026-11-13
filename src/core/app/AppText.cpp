// MacBurn – Text: anlegen, bearbeiten (bleibt nach Verschieben/Drehen/Skalieren editierbar), in Pfade umwandeln.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppController.hpp"
#include "util/NumberInput.hpp"

#include <algorithm>
#include <cmath>

namespace mb {

const FontProvider& AppController::fonts() const {
    if (!fonts_) fonts_ = makeSystemFontProvider();
    return *fonts_;
}

const std::vector<std::string>& AppController::families() const {
    if (familyCache_.empty()) familyCache_ = fonts().families();
    return familyCache_;
}

std::optional<uint32_t> AppController::selectedTextPart() const {
    if (selection_.size() != 1) return std::nullopt;
    std::optional<uint32_t> part;
    for (const auto& s : doc_.shapes) {
        if (!selection_.count(s.object)) continue;
        if (part && *part != s.part) return std::nullopt;   // mehrere Teile: keine reine Textauswahl
        part = s.part;
    }
    if (part && doc_.texts.count(*part)) return part;
    return std::nullopt;
}

size_t AppController::textLayer() {
    // Text wird meist graviert: gewählte Ebene, wenn sie graviert oder markiert, sonst die erste Gravurebene.
    if (layer_ >= 0 && layer_ < int(doc_.layers.size())) {
        Intent i = doc_.layers[size_t(layer_)].intent;
        if (i == Intent::Engrave || i == Intent::Mark) return size_t(layer_);
    }
    for (size_t i = 0; i < doc_.layers.size(); ++i)
        if (doc_.layers[i].intent == Intent::Engrave) return i;
    Layer l;
    l.name = "Gravur";
    l.color = {0.14f, 0.39f, 0.92f, 1};
    l.intent = Intent::Engrave;
    l.colorKey = "#2463eb";
    doc_.layers.push_back(l);
    return doc_.layers.size() - 1;
}

bool AppController::regenerateText(Document& d, uint32_t part, const FontProvider& fonts) {
    auto it = d.texts.find(part);
    if (it == d.texts.end()) return false;
    auto outlines = fonts.outline(it->second);
    if (outlines.empty()) return false;   // Schrift fehlt oder Text leer: alte Buchstaben behalten
    size_t layer = 0;
    uint32_t object = 0;
    size_t insertAt = d.shapes.size();
    for (size_t i = 0; i < d.shapes.size(); ++i)
        if (d.shapes[i].part == part) { layer = d.shapes[i].layer; object = d.shapes[i].object; insertAt = std::min(insertAt, i); }
    d.shapes.erase(std::remove_if(d.shapes.begin(), d.shapes.end(), [&](const Shape& s) { return s.part == part; }), d.shapes.end());
    std::vector<Shape> glyphs;
    for (auto& p : outlines) glyphs.push_back({"Text", layer, std::move(p), object, part});
    insertAt = std::min(insertAt, d.shapes.size());
    d.shapes.insert(d.shapes.begin() + long(insertAt), glyphs.begin(), glyphs.end());
    return true;
}

void AppController::createText(Vec2 at) {
    if (!canEdit()) return;
    TextSpec spec = textTemplate_;
    spec.m = Affine::translate(at);   // Grundlinie der ersten Zeile beginnt am Klickpunkt
    auto outlines = fonts().outline(spec);
    if (outlines.empty()) {
        // Schrift nicht vorhanden: auf eine überall vorhandene ausweichen
        spec.family = "Helvetica";
        spec.style = "Bold";
        outlines = fonts().outline(spec);
        if (outlines.empty()) { message_ = "Die Schrift lässt sich nicht laden."; changed(); return; }
    }
    Document before = doc_;
    size_t layer = textLayer();
    Document after = doc_;
    doc_ = before;
    uint32_t obj = after.nextObjectId(), part = after.nextPartId();
    for (auto& p : outlines) after.shapes.push_back({"Text", layer, std::move(p), obj, part});
    after.texts[part] = spec;
    undo_.perform(doc_, {"Text anlegen", [after](Document& d) { d = after; }, [before](Document& d) { d = before; }});
    isSample_ = false;
    selection_ = {obj};
    layer_ = int(layer);
    tool_ = Tool::Select;
    focusText_ = true;   // Textfeld im Inspektor gleich zum Tippen bereit
    message_.clear();
    changed();
}

void AppController::editText(const std::function<void(TextSpec&)>& fn, const std::string& label, bool keepCenter) {
    if (auto part = selectedTextPart()) {
        TextSpec old = doc_.texts[*part];
        TextSpec spec = old;
        fn(spec);
        if (spec == old) return;
        Document trial = doc_;
        trial.texts[*part] = spec;
        if (!regenerateText(trial, *part, fonts())) {
            message_ = spec.text.empty() ? "Ein Text braucht mindestens ein Zeichen." : "Die Schrift lässt sich nicht laden.";
            changed();
            return;
        }
        if (keepCenter) {
            Rect b0 = doc_.bounds(selection_), b1 = trial.bounds(selection_);
            trial.transform(selection_, Affine::translate(b0.x + b0.w / 2 - (b1.x + b1.w / 2), b0.y + b0.h / 2 - (b1.y + b1.h / 2)));
        }
        editDoc(label, [&](Document& d) { d = trial; });
        textTemplate_ = spec;   // nächster Text bekommt dieselben Einstellungen
        textTemplate_.m = {};
        return;
    }
    fn(textTemplate_);   // kein Text ausgewählt: Vorlage für den nächsten Text
    changed();
}

void AppController::convertTextToPaths() {
    auto part = selectedTextPart();
    if (!part) return;
    uint32_t p = *part;
    editDoc("In Pfade umwandeln", [p](Document& d) { d.texts.erase(p); });
}

void AppController::addTextFields(std::vector<Field>& f) const {
    auto part = selectedTextPart();
    if (!part && tool_ != Tool::Text) return;
    const TextSpec& t = part ? doc_.texts.at(*part) : textTemplate_;
    f.push_back({Field::Kind::Header, "", part ? "Text" : "Werkzeug: Text"});
    if (part) {
        Field area{Field::Kind::TextArea, "textContent", ""};
        area.text = t.text;
        area.focus = focusText_;
        f.push_back(area);
    } else {
        f.push_back({Field::Kind::Info, "", "Auf die Fläche klicken: Der Text beginnt dort (Grundlinie). Danach gleich lostippen."});
    }
    Field fam{Field::Kind::Choice, "textFamily", "Schrift"};
    const auto& fams = families();
    fam.options = fams;
    auto it = std::find(fams.begin(), fams.end(), t.family);
    if (it == fams.end()) { fam.options.insert(fam.options.begin(), t.family + " (fehlt)"); fam.selected = 0; }
    else fam.selected = int(it - fams.begin());
    f.push_back(fam);
    Field sty{Field::Kind::Choice, "textStyle", "Schnitt"};
    sty.options = fonts().styles(t.family);
    auto si = std::find(sty.options.begin(), sty.options.end(), t.style);
    if (si == sty.options.end()) { sty.options.insert(sty.options.begin(), t.style); sty.selected = 0; }
    else sty.selected = int(si - sty.options.begin());
    f.push_back(sty);
    auto num = [&](const char* key, const char* label, double v, double lo, double hi, double step, const char* unit) {
        Field x{Field::Kind::Number, key, label};
        x.value = v; x.min = lo; x.max = hi; x.step = step; x.unit = unit;
        f.push_back(x);
    };
    num("textCap", "Höhe (Großbuchst.)", t.capHeightMm, 0.5, 500, 0.5, "mm");
    num("textSpacing", "Zeichenabstand", t.letterSpacingMm, -20, 50, 0.1, "mm");
    num("textLine", "Zeilenabstand", std::round(t.lineSpacing * 100), 50, 400, 5, "%");
    Field al{Field::Kind::ButtonRow, "textAlign", ""};
    al.options = {"text.alignleft|Linksbündig", "text.aligncenter|Zentriert", "text.alignright|Rechtsbündig"};
    f.push_back(al);
    if (part) f.push_back({Field::Kind::Button, "textToPaths", "In Pfade umwandeln"});
}

} // namespace mb
