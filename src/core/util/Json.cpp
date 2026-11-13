// SPDX-License-Identifier: GPL-3.0-or-later
#include "util/Json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mb::json {

Value Value::boolean(bool b) { Value v; v.type_ = Type::Bool; v.bool_ = b; return v; }
Value Value::number(double d) { Value v; v.type_ = Type::Number; v.number_ = d; return v; }
Value Value::string(std::string s) { Value v; v.type_ = Type::String; v.string_ = std::move(s); return v; }
Value Value::array(std::vector<Value> a) { Value v; v.type_ = Type::Array; v.array_ = std::move(a); return v; }
Value Value::object(std::map<std::string, Value> o) { Value v; v.type_ = Type::Object; v.object_ = std::move(o); return v; }

bool Value::asBool(bool fallback) const { return type_ == Type::Bool ? bool_ : fallback; }
double Value::asNumber(double fallback) const { return type_ == Type::Number ? number_ : fallback; }
std::string Value::asString(const std::string& fallback) const { return type_ == Type::String ? string_ : fallback; }

const std::vector<Value>& Value::asArray() const {
    static const std::vector<Value> empty;
    return type_ == Type::Array ? array_ : empty;
}
const std::map<std::string, Value>& Value::asObject() const {
    static const std::map<std::string, Value> empty;
    return type_ == Type::Object ? object_ : empty;
}
const Value& Value::operator[](const std::string& key) const {
    static const Value null;
    if (type_ != Type::Object) return null;
    auto it = object_.find(key);
    return it == object_.end() ? null : it->second;
}
bool Value::has(const std::string& key) const {
    return type_ == Type::Object && object_.count(key) > 0;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& t) : s_(t) {}

    std::optional<Value> run(ParseError* err) {
        auto v = value();
        ws();
        if (v && pos_ != s_.size()) fail("Unerwartete Zeichen nach dem Ende");
        if (failed_) {
            if (err) *err = {msg_, pos_};
            return std::nullopt;
        }
        return v;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;
    bool failed_ = false;
    std::string msg_;

    void fail(const char* m) { if (!failed_) { failed_ = true; msg_ = m; } }
    void ws() { while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_; }
    bool eat(char c) { ws(); if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; } return false; }
    bool lit(const char* w) {
        size_t n = std::char_traits<char>::length(w);
        if (s_.compare(pos_, n, w) == 0) { pos_ += n; return true; }
        return false;
    }

    std::optional<Value> value() {
        ws();
        if (pos_ >= s_.size()) { fail("Unerwartetes Dateiende"); return std::nullopt; }
        char c = s_[pos_];
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') { auto str = string(); if (!str) return std::nullopt; return Value::string(*str); }
        if (lit("true")) return Value::boolean(true);
        if (lit("false")) return Value::boolean(false);
        if (lit("null")) return Value();
        return number();
    }

    std::optional<Value> number() {
        const char* begin = s_.c_str() + pos_;
        char* end = nullptr;
        double d = std::strtod(begin, &end);
        if (end == begin) { fail("Wert erwartet"); return std::nullopt; }
        pos_ += static_cast<size_t>(end - begin);
        return Value::number(d);
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }

    std::optional<unsigned> hex4() {
        if (pos_ + 4 > s_.size()) { fail("Unvollständige \\u-Sequenz"); return std::nullopt; }
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char h = s_[pos_++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= unsigned(h - '0');
            else if (h >= 'a' && h <= 'f') v |= unsigned(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= unsigned(h - 'A' + 10);
            else { fail("Ungültige \\u-Sequenz"); return std::nullopt; }
        }
        return v;
    }

    std::optional<std::string> string() {
        if (!eat('"')) { fail("Zeichenkette erwartet"); return std::nullopt; }
        std::string out;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) break;
            char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    auto cp = hex4(); if (!cp) return std::nullopt;
                    unsigned code = *cp;
                    if (code >= 0xD800 && code <= 0xDBFF && lit("\\u")) {
                        auto lo = hex4(); if (!lo) return std::nullopt;
                        code = 0x10000 + ((code - 0xD800) << 10) + (*lo - 0xDC00);
                    }
                    appendUtf8(out, code);
                    break;
                }
                default: fail("Ungültige Escape-Sequenz"); return std::nullopt;
            }
        }
        fail("Zeichenkette nicht abgeschlossen");
        return std::nullopt;
    }

    std::optional<Value> array() {
        eat('[');
        std::vector<Value> items;
        if (eat(']')) return Value::array(std::move(items));
        do {
            auto v = value(); if (!v) return std::nullopt;
            items.push_back(std::move(*v));
        } while (eat(','));
        if (!eat(']')) { fail("']' erwartet"); return std::nullopt; }
        return Value::array(std::move(items));
    }

    std::optional<Value> object() {
        eat('{');
        std::map<std::string, Value> members;
        if (eat('}')) return Value::object(std::move(members));
        do {
            ws();
            auto key = string(); if (!key) return std::nullopt;
            if (!eat(':')) { fail("':' erwartet"); return std::nullopt; }
            auto v = value(); if (!v) return std::nullopt;
            members[*key] = std::move(*v);
        } while (eat(','));
        if (!eat('}')) { fail("'}' erwartet"); return std::nullopt; }
        return Value::object(std::move(members));
    }
};

} // namespace

namespace {

void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += char(c);
        }
    }
    out += '"';
}

void writeNumber(std::string& out, double d) {
    if (!std::isfinite(d)) { out += "0"; return; }
    char b[48];
    std::snprintf(b, sizeof b, "%.4f", d);
    std::string s = b;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    out += s;
}

// Kurze Zahlenlisten (z. B. Punkte [x, y]) bleiben in einer Zeile.
bool isFlat(const Value& v) {
    if (!v.isArray() || v.asArray().size() > 8) return false;
    for (const auto& e : v.asArray()) if (e.isArray() || e.isObject()) return false;
    return true;
}

void writeValue(std::string& out, const Value& v, bool pretty, int indent) {
    auto nl = [&](int lvl) { if (pretty) { out += '\n'; out.append(size_t(lvl) * 2, ' '); } };
    switch (v.type()) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Value::Type::Number: writeNumber(out, v.asNumber()); break;
        case Value::Type::String: writeString(out, v.asString()); break;
        case Value::Type::Array: {
            const auto& a = v.asArray();
            if (a.empty()) { out += "[]"; break; }
            bool flat = isFlat(v);
            out += '[';
            for (size_t i = 0; i < a.size(); ++i) {
                if (i) out += flat && pretty ? ", " : ",";
                if (!flat) nl(indent + 1);
                writeValue(out, a[i], pretty && !flat, indent + 1);
            }
            if (!flat) nl(indent);
            out += ']';
            break;
        }
        case Value::Type::Object: {
            const auto& o = v.asObject();
            if (o.empty()) { out += "{}"; break; }
            out += '{';
            bool first = true;
            for (const auto& [k, val] : o) {
                if (!first) out += ',';
                first = false;
                nl(indent + 1);
                writeString(out, k);
                out += pretty ? ": " : ":";
                writeValue(out, val, pretty, indent + 1);
            }
            nl(indent);
            out += '}';
            break;
        }
    }
}

} // namespace

std::string write(const Value& v, bool pretty) {
    std::string out;
    writeValue(out, v, pretty, 0);
    if (pretty) out += '\n';
    return out;
}

std::optional<Value> parse(const std::string& text, ParseError* error) {
    return Parser(text).run(error);
}

std::optional<Value> parseFile(const std::string& path, ParseError* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { if (error) *error = {"Datei nicht lesbar: " + path, 0}; return std::nullopt; }
    std::stringstream ss; ss << in.rdbuf();
    return parse(ss.str(), error);
}

} // namespace mb::json
