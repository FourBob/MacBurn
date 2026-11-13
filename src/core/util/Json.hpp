// MacBurn – minimaler JSON-Leser für Profile und Projektdateien.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mb::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    static Value boolean(bool b);
    static Value number(double d);
    static Value string(std::string s);
    static Value array(std::vector<Value> a);
    static Value object(std::map<std::string, Value> o);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }

    // Zugriff mit Rückfallwert – Profile sind oft unvollständig.
    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    std::string asString(const std::string& fallback = {}) const;
    const std::vector<Value>& asArray() const;
    const std::map<std::string, Value>& asObject() const;

    // Objektzugriff; liefert Null-Wert, wenn der Schlüssel fehlt.
    const Value& operator[](const std::string& key) const;
    bool has(const std::string& key) const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::map<std::string, Value> object_;
};

struct ParseError {
    std::string message;
    size_t offset = 0;
};

// Liefert den Wert oder setzt error.
std::optional<Value> parse(const std::string& text, ParseError* error = nullptr);
std::optional<Value> parseFile(const std::string& path, ParseError* error = nullptr);

} // namespace mb::json
