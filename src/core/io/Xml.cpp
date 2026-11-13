// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/Xml.hpp"

#include <cstdlib>

namespace mb::xml {

const std::string* Element::attr(const std::string& key) const {
    auto it = attributes.find(key);
    return it == attributes.end() ? nullptr : &it->second;
}

std::string Element::localName() const {
    auto c = name.find(':');
    return c == std::string::npos ? name : name.substr(c + 1);
}

namespace {

void appendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
}

std::string decodeEntities(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out += s[i]; continue; }
        std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            unsigned long cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')
                ? std::strtoul(ent.c_str() + 2, nullptr, 16) : std::strtoul(ent.c_str() + 1, nullptr, 10);
            appendUtf8(out, cp);
        } else { out += s.substr(i, semi - i + 1); }
        i = semi;
    }
    return out;
}

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    std::unique_ptr<Element> run(ParseError* err) {
        std::unique_ptr<Element> root;
        while (pos_ < s_.size() && !failed_) {
            skipMisc();
            if (pos_ >= s_.size()) break;
            if (s_[pos_] == '<' && !root) { root = element(); continue; }
            if (root) { skipMisc(); break; }
            fail("Kein Wurzelelement gefunden");
        }
        if (!root && !failed_) fail("Leeres Dokument");
        if (failed_) { if (err) *err = {msg_, pos_}; return nullptr; }
        return root;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;
    bool failed_ = false;
    std::string msg_;

    void fail(const char* m) { if (!failed_) { failed_ = true; msg_ = m; } }
    bool startsWith(const char* t) const { return s_.compare(pos_, std::char_traits<char>::length(t), t) == 0; }
    static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
    void ws() { while (pos_ < s_.size() && isSpace(s_[pos_])) ++pos_; }

    void skipUntil(const char* end) {
        size_t e = s_.find(end, pos_);
        if (e == std::string::npos) { fail("Unvollständiges Konstrukt"); pos_ = s_.size(); return; }
        pos_ = e + std::char_traits<char>::length(end);
    }

    // Kommentare, Verarbeitungsanweisungen, DOCTYPE (inkl. interner Teilmenge) und Leerraum
    void skipMisc() {
        for (;;) {
            ws();
            if (startsWith("<?")) skipUntil("?>");
            else if (startsWith("<!--")) skipUntil("-->");
            else if (startsWith("<!DOCTYPE") || startsWith("<!doctype")) {
                int depth = 0;
                while (pos_ < s_.size()) {
                    char c = s_[pos_++];
                    if (c == '[') ++depth;
                    else if (c == ']') --depth;
                    else if (c == '>' && depth <= 0) break;
                }
            } else return;
            if (failed_) return;
        }
    }

    std::string name() {
        size_t b = pos_;
        while (pos_ < s_.size() && !isSpace(s_[pos_]) && s_[pos_] != '>' && s_[pos_] != '/' && s_[pos_] != '=') ++pos_;
        return s_.substr(b, pos_ - b);
    }

    std::unique_ptr<Element> element() {
        auto el = std::make_unique<Element>();
        ++pos_;   // '<'
        el->name = name();
        if (el->name.empty()) { fail("Elementname fehlt"); return nullptr; }
        // Attribute
        for (;;) {
            ws();
            if (pos_ >= s_.size()) { fail("Unerwartetes Dateiende im Element"); return nullptr; }
            if (startsWith("/>")) { pos_ += 2; return el; }
            if (s_[pos_] == '>') { ++pos_; break; }
            std::string key = name();
            ws();
            if (pos_ >= s_.size() || s_[pos_] != '=') { el->attributes[key] = ""; continue; }
            ++pos_; ws();
            if (pos_ >= s_.size() || (s_[pos_] != '"' && s_[pos_] != '\'')) { fail("Attributwert ohne Anführungszeichen"); return nullptr; }
            char q = s_[pos_++];
            size_t e = s_.find(q, pos_);
            if (e == std::string::npos) { fail("Attributwert nicht abgeschlossen"); return nullptr; }
            el->attributes[key] = decodeEntities(s_.substr(pos_, e - pos_));
            pos_ = e + 1;
        }
        // Inhalt
        for (;;) {
            if (pos_ >= s_.size()) { fail("Element nicht geschlossen"); return nullptr; }
            if (startsWith("</")) {
                pos_ += 2;
                std::string closing = name();
                ws();
                if (closing != el->name) { fail("Schließendes Element passt nicht"); return nullptr; }
                if (pos_ < s_.size() && s_[pos_] == '>') ++pos_;
                return el;
            }
            if (startsWith("<![CDATA[")) {
                pos_ += 9;
                size_t e = s_.find("]]>", pos_);
                if (e == std::string::npos) { fail("CDATA nicht abgeschlossen"); return nullptr; }
                el->text += s_.substr(pos_, e - pos_);
                pos_ = e + 3;
                continue;
            }
            if (startsWith("<!--")) { skipUntil("-->"); continue; }
            if (startsWith("<?")) { skipUntil("?>"); continue; }
            if (s_[pos_] == '<') {
                auto child = element();
                if (!child) return nullptr;
                el->children.push_back(std::move(child));
                continue;
            }
            size_t e = s_.find('<', pos_);
            if (e == std::string::npos) e = s_.size();
            el->text += decodeEntities(s_.substr(pos_, e - pos_));
            pos_ = e;
        }
    }
};

} // namespace

std::unique_ptr<Element> parse(const std::string& text, ParseError* error) {
    return Parser(text).run(error);
}

} // namespace mb::xml
