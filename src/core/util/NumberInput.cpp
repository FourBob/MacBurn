// SPDX-License-Identifier: GPL-3.0-or-later
#include "util/NumberInput.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mb {

namespace {

// Rekursiver Abstieg: expr = term {(+|-) term}; term = factor {(*|/) factor}; factor = [+-] (zahl [einheit] | (expr))
class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    std::optional<double> run() {
        auto v = expr();
        ws();
        if (!v || i_ != s_.size() || !std::isfinite(*v)) return std::nullopt;
        return v;
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    void ws() { while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_; }
    bool eat(char c) { ws(); if (i_ < s_.size() && s_[i_] == c) { ++i_; return true; } return false; }

    std::optional<double> expr() {
        auto v = term();
        while (v) {
            if (eat('+')) { auto r = term(); if (!r) return std::nullopt; *v += *r; }
            else if (eat('-')) { auto r = term(); if (!r) return std::nullopt; *v -= *r; }
            else break;
        }
        return v;
    }
    std::optional<double> term() {
        auto v = factor();
        while (v) {
            if (eat('*') || eat('x') || eat('X')) { auto r = factor(); if (!r) return std::nullopt; *v *= *r; }
            else if (eat('/') || eat(':')) {
                auto r = factor();
                if (!r || std::abs(*r) < 1e-12) return std::nullopt;
                *v /= *r;
            } else break;
        }
        return v;
    }
    std::optional<double> factor() {
        if (eat('-')) { auto v = factor(); if (!v) return std::nullopt; return -*v; }
        if (eat('+')) return factor();
        if (eat('(')) {
            auto v = expr();
            if (!v || !eat(')')) return std::nullopt;
            return unit(*v);
        }
        ws();
        size_t b = i_;
        std::string num;
        bool dot = false;
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (std::isdigit(static_cast<unsigned char>(c))) num += c;
            else if ((c == ',' || c == '.') && !dot) { num += '.'; dot = true; }
            else break;
            ++i_;
        }
        if (num.empty() || num == ".") { i_ = b; return std::nullopt; }
        return unit(std::strtod(num.c_str(), nullptr));
    }
    // Einheit direkt hinter der Zahl: mm, cm, m, in, ", pt, °
    double unit(double v) {
        ws();
        auto starts = [&](const char* u) {
            size_t n = std::char_traits<char>::length(u);
            if (s_.compare(i_, n, u) != 0) return false;
            if (i_ + n < s_.size() && std::isalpha(static_cast<unsigned char>(s_[i_ + n]))) return false;
            i_ += n;
            return true;
        };
        if (starts("mm")) return v;
        if (starts("cm")) return v * 10;
        if (starts("in") || starts("\"") || starts("zoll") || starts("Zoll")) return v * 25.4;
        if (starts("pt")) return v * 25.4 / 72;
        if (starts("m")) return v * 1000;
        if (starts("°") || starts("%")) return v;
        return v;
    }
};

} // namespace

std::optional<double> parseNumberInput(const std::string& text) {
    if (text.find_first_not_of(" \t") == std::string::npos) return std::nullopt;
    return Parser(text).run();
}

std::string formatNumber(double v, int decimals) {
    if (std::abs(v) < 0.5 * std::pow(10.0, -decimals)) v = 0;
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    std::string s = b;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    for (auto& c : s) if (c == '.') c = ',';
    return s;
}

} // namespace mb
