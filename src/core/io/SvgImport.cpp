// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/SvgImport.hpp"
#include "io/Xml.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace mb {

namespace {

// ---------------------------------------------------------------- Grundlagen

struct Mat {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
    // (*this) * o: erst o, dann this
    Mat operator*(const Mat& o) const {
        return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d,
                a * o.e + c * o.f + e, b * o.e + d * o.f + f};
    }
    double scale() const { return std::sqrt(std::abs(a * d - b * c)); }
    static Mat translate(double x, double y) { return {1, 0, 0, 1, x, y}; }
    static Mat scaling(double x, double y) { return {x, 0, 0, y, 0, 0}; }
    static Mat rotate(double deg) {
        double r = deg * M_PI / 180, cs = std::cos(r), sn = std::sin(r);
        return {cs, sn, -sn, cs, 0, 0};
    }
};

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    for (auto& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// Liest Zahlen wie in SVG üblich: "1.5.5" = 1.5, .5 · "10-5" = 10, -5 · Exponenten
class NumberReader {
public:
    explicit NumberReader(const std::string& s, size_t pos = 0) : s_(s), i_(pos) {}
    size_t pos() const { return i_; }
    bool atEnd() { skipSep(); return i_ >= s_.size(); }
    void skipSep() { while (i_ < s_.size() && (std::isspace(static_cast<unsigned char>(s_[i_])) || s_[i_] == ',')) ++i_; }
    bool peekNumber() {
        skipSep();
        if (i_ >= s_.size()) return false;
        char ch = s_[i_];
        return std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' || ch == '.';
    }
    bool number(double& out) {
        skipSep();
        size_t b = i_;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
        bool digits = false, dot = false;
        while (i_ < s_.size()) {
            char ch = s_[i_];
            if (std::isdigit(static_cast<unsigned char>(ch))) { digits = true; ++i_; }
            else if (ch == '.' && !dot) { dot = true; ++i_; }
            else break;
        }
        if (!digits) { i_ = b; return false; }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            size_t save = i_++;
            if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
            if (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_])))
                while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
            else i_ = save;
        }
        out = std::strtod(s_.substr(b, i_ - b).c_str(), nullptr);
        return true;
    }
    bool flag(bool& out) {   // Bogen-Flags dürfen ohne Trenner folgen: "a10 10 0 1120 20"
        skipSep();
        if (i_ < s_.size() && (s_[i_] == '0' || s_[i_] == '1')) { out = s_[i_++] == '1'; return true; }
        return false;
    }
    bool command(char& out) {
        skipSep();
        if (i_ < s_.size() && std::isalpha(static_cast<unsigned char>(s_[i_])) && s_[i_] != 'e' && s_[i_] != 'E') { out = s_[i_++]; return true; }
        return false;
    }

private:
    const std::string& s_;
    size_t i_;
};

std::vector<double> numbers(const std::string& s) {
    std::vector<double> v;
    NumberReader r(s);
    double x;
    while (r.number(x)) v.push_back(x);
    return v;
}

double mmPerUnit(const std::string& unit, double pxPerInch) {
    if (unit == "mm") return 1;
    if (unit == "cm") return 10;
    if (unit == "in") return 25.4;
    if (unit == "pt") return 25.4 / 72;
    if (unit == "pc") return 25.4 / 6;
    return 25.4 / pxPerInch;   // px oder ohne Einheit
}

// Länge in mm; nullopt bei Prozent oder leer
std::optional<double> lengthMm(const std::string* s, double pxPerInch) {
    if (!s) return std::nullopt;
    std::string t = trim(*s);
    if (t.empty() || t.back() == '%') return std::nullopt;
    NumberReader r(t);
    double v;
    if (!r.number(v)) return std::nullopt;
    return v * mmPerUnit(lower(trim(t.substr(r.pos()))), pxPerInch);
}

// Länge in Benutzereinheiten (px); Einheiten werden umgerechnet
double userLength(const xml::Element& el, const char* key, double pxPerInch, double fallback = 0) {
    const std::string* s = el.attr(key);
    if (!s) return fallback;
    std::string t = trim(*s);
    NumberReader r(t);
    double v;
    if (!r.number(v)) return fallback;
    std::string unit = lower(trim(t.substr(r.pos())));
    if (unit.empty() || unit == "px" || unit == "%") return v;
    return v * mmPerUnit(unit, pxPerInch) / (25.4 / pxPerInch);
}

Mat parseTransform(const std::string& s) {
    Mat m;
    size_t i = 0;
    while (i < s.size()) {
        size_t open = s.find('(', i);
        if (open == std::string::npos) break;
        size_t close = s.find(')', open);
        if (close == std::string::npos) break;
        std::string name = trim(s.substr(i, open - i));
        while (!name.empty() && (name[0] == ',')) name = trim(name.substr(1));
        auto v = numbers(s.substr(open + 1, close - open - 1));
        Mat t;
        if (name == "matrix" && v.size() >= 6) t = {v[0], v[1], v[2], v[3], v[4], v[5]};
        else if (name == "translate" && !v.empty()) t = Mat::translate(v[0], v.size() > 1 ? v[1] : 0);
        else if (name == "scale" && !v.empty()) t = Mat::scaling(v[0], v.size() > 1 ? v[1] : v[0]);
        else if (name == "rotate" && !v.empty()) {
            t = Mat::rotate(v[0]);
            if (v.size() >= 3) t = Mat::translate(v[1], v[2]) * t * Mat::translate(-v[1], -v[2]);
        } else if (name == "skewX" && !v.empty()) t = {1, 0, std::tan(v[0] * M_PI / 180), 1, 0, 0};
        else if (name == "skewY" && !v.empty()) t = {1, std::tan(v[0] * M_PI / 180), 0, 1, 0, 0};
        m = m * t;
        i = close + 1;
    }
    return m;
}

// ---------------------------------------------------------------- Farben & Stil

struct Paint { bool none = true; std::string hex; };

std::string hex2(int v) {
    static const char* d = "0123456789abcdef";
    v = std::clamp(v, 0, 255);
    return {d[v >> 4], d[v & 15]};
}

std::optional<std::string> parseColor(const std::string& raw) {
    std::string s = lower(trim(raw));
    if (s.empty()) return std::nullopt;
    if (s[0] == '#') {
        std::string h = s.substr(1);
        if (h.size() == 3) h = {h[0], h[0], h[1], h[1], h[2], h[2]};
        if (h.size() >= 6) return "#" + h.substr(0, 6);
        return std::nullopt;
    }
    if (s.rfind("rgb", 0) == 0) {
        size_t o = s.find('('), c = s.find(')');
        if (o == std::string::npos || c == std::string::npos) return std::nullopt;
        std::string inner = s.substr(o + 1, c - o - 1);
        bool pct = inner.find('%') != std::string::npos;
        auto v = numbers(inner);
        if (v.size() < 3) return std::nullopt;
        auto ch = [&](double x) { return int(std::lround(pct ? x * 2.55 : x)); };
        return "#" + hex2(ch(v[0])) + hex2(ch(v[1])) + hex2(ch(v[2]));
    }
    static const std::map<std::string, std::string> named = {
        {"black", "#000000"}, {"white", "#ffffff"}, {"red", "#ff0000"}, {"green", "#008000"}, {"lime", "#00ff00"},
        {"blue", "#0000ff"}, {"yellow", "#ffff00"}, {"cyan", "#00ffff"}, {"aqua", "#00ffff"}, {"magenta", "#ff00ff"},
        {"fuchsia", "#ff00ff"}, {"gray", "#808080"}, {"grey", "#808080"}, {"silver", "#c0c0c0"}, {"orange", "#ffa500"},
        {"purple", "#800080"}, {"navy", "#000080"}, {"maroon", "#800000"}, {"olive", "#808000"}, {"teal", "#008080"},
        {"brown", "#a52a2a"}, {"pink", "#ffc0cb"}, {"darkgray", "#a9a9a9"}, {"lightgray", "#d3d3d3"}};
    auto it = named.find(s);
    if (it != named.end()) return it->second;
    return std::nullopt;
}

struct Style {
    Paint fill{false, "#000000"};   // SVG-Vorgabe: schwarz gefüllt, ohne Kontur
    Paint stroke{true, ""};
    std::string color = "#000000";
    bool display = true;
    bool visible = true;
};

using Decls = std::vector<std::pair<std::string, std::string>>;

Decls parseDecls(const std::string& s) {
    Decls out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ';')) {
        auto c = part.find(':');
        if (c == std::string::npos) continue;
        out.push_back({lower(trim(part.substr(0, c))), trim(part.substr(c + 1))});
    }
    return out;
}

struct Context {
    SvgImportOptions opt;
    std::map<std::string, Decls> tagRules, classRules, idRules;
    std::map<std::string, const xml::Element*> ids;
    int texts = 0, images = 0, gradients = 0, others = 0;
    uint32_t parts = 0;

    struct LayerAcc { std::string key; int filled = 0, closed = 0, open = 0; size_t index = 0; };
    std::vector<LayerAcc> accs;
    std::vector<Shape> shapes;

    size_t layerFor(const std::string& key) {
        for (auto& a : accs) if (a.key == key) return a.index;
        accs.push_back({key, 0, 0, 0, accs.size()});
        return accs.back().index;
    }
};

void applyDecl(Style& st, const std::string& prop, const std::string& rawVal, Context& ctx) {
    std::string val = trim(rawVal);
    auto bang = val.find("!important");
    if (bang != std::string::npos) val = trim(val.substr(0, bang));
    std::string lv = lower(val);
    auto paint = [&](Paint& p) {
        if (lv == "none" || lv == "transparent") { p = {true, ""}; return; }
        if (lv == "currentcolor") { p = {false, st.color}; return; }
        if (lv.rfind("url(", 0) == 0) { ++ctx.gradients; p = {false, "#808080"}; return; }
        if (auto c = parseColor(val)) p = {false, *c};
    };
    if (prop == "fill") paint(st.fill);
    else if (prop == "stroke") paint(st.stroke);
    else if (prop == "color") { if (auto c = parseColor(val)) st.color = *c; }
    else if (prop == "display") st.display = lv != "none";
    else if (prop == "visibility") st.visible = lv != "hidden" && lv != "collapse";
    else if (prop == "fill-opacity" && std::strtod(lv.c_str(), nullptr) == 0 && !lv.empty()) st.fill = {true, ""};
    else if (prop == "stroke-opacity" && std::strtod(lv.c_str(), nullptr) == 0 && !lv.empty()) st.stroke = {true, ""};
    else if (prop == "opacity" && std::strtod(lv.c_str(), nullptr) == 0 && !lv.empty()) st.visible = false;
}

Style computeStyle(const xml::Element& el, const Style& parent, Context& ctx) {
    Style st = parent;
    st.display = true;   // nicht vererbt
    static const char* presentation[] = {"color", "fill", "stroke", "display", "visibility", "fill-opacity", "stroke-opacity", "opacity"};
    for (const char* p : presentation) if (auto v = el.attr(p)) applyDecl(st, p, *v, ctx);
    auto applyRules = [&](const std::map<std::string, Decls>& rules, const std::string& key) {
        auto it = rules.find(key);
        if (it != rules.end()) for (auto& [p, v] : it->second) applyDecl(st, p, v, ctx);
    };
    applyRules(ctx.tagRules, el.localName());
    if (auto cls = el.attr("class")) {
        std::stringstream ss(*cls);
        std::string c;
        while (ss >> c) applyRules(ctx.classRules, c);
    }
    if (auto id = el.attr("id")) applyRules(ctx.idRules, *id);
    if (auto s = el.attr("style")) for (auto& [p, v] : parseDecls(*s)) applyDecl(st, p, v, ctx);
    return st;
}

void parseStylesheet(const std::string& cssRaw, Context& ctx) {
    std::string css;
    for (size_t i = 0; i < cssRaw.size(); ++i) {   // Kommentare entfernen
        if (cssRaw.compare(i, 2, "/*") == 0) {
            size_t e = cssRaw.find("*/", i + 2);
            if (e == std::string::npos) break;
            i = e + 1;
        } else css += cssRaw[i];
    }
    size_t i = 0;
    while (i < css.size()) {
        size_t open = css.find('{', i);
        if (open == std::string::npos) break;
        size_t close = css.find('}', open);
        if (close == std::string::npos) break;
        Decls decls = parseDecls(css.substr(open + 1, close - open - 1));
        std::stringstream sels(css.substr(i, open - i));
        std::string sel;
        while (std::getline(sels, sel, ',')) {
            sel = trim(sel);
            if (sel.empty() || sel.find_first_of(" >+~:[") != std::string::npos) continue;   // nur einfache Selektoren
            auto& target = sel[0] == '.' ? ctx.classRules[sel.substr(1)]
                         : sel[0] == '#' ? ctx.idRules[sel.substr(1)] : ctx.tagRules[sel];
            target.insert(target.end(), decls.begin(), decls.end());
        }
        i = close + 1;
    }
}

void collect(const xml::Element& el, Context& ctx) {
    if (el.localName() == "style") parseStylesheet(el.text, ctx);
    if (auto id = el.attr("id")) ctx.ids[*id] = &el;
    for (auto& ch : el.children) collect(*ch, ctx);
}

// ---------------------------------------------------------------- Geometrie

class Builder {
public:
    Builder(const Mat& m, double tol) : m_(m), tol_(tol) {}

    void moveTo(Vec2 p) { finish(); cur_ = start_ = p; open_.points = {m_.apply(p)}; }
    void lineTo(Vec2 p) { ensureStarted(); cur_ = p; open_.points.push_back(m_.apply(p)); }
    void close() {
        if (open_.points.size() > 1) { open_.closed = true; finish(); }
        cur_ = start_;
    }
    void cubicTo(Vec2 c1, Vec2 c2, Vec2 p) {
        ensureStarted();
        flatten(m_.apply(cur_), m_.apply(c1), m_.apply(c2), m_.apply(p), 0);
        cur_ = p;
    }
    void quadTo(Vec2 q, Vec2 p) {
        cubicTo(cur_ + (q - cur_) * (2.0 / 3.0), p + (q - p) * (2.0 / 3.0), p);
    }
    // Ellipsenbogen in Benutzerkoordinaten, Winkel in Bogenmaß
    void ellipse(Vec2 c, double rx, double ry, double phi, double t0, double dt, bool startNew) {
        double r = std::max(rx, ry) * m_.scale();
        double step = r > tol_ ? 2 * std::acos(std::max(-1.0, 1 - tol_ / r)) : M_PI / 2;
        int n = std::clamp(int(std::ceil(std::abs(dt) / std::max(step, 1e-3))), 4, 2000);
        double cs = std::cos(phi), sn = std::sin(phi);
        auto pt = [&](double t) {
            double x = rx * std::cos(t), y = ry * std::sin(t);
            return Vec2{c.x + cs * x - sn * y, c.y + sn * x + cs * y};
        };
        if (startNew) moveTo(pt(t0));
        for (int i = 1; i <= n; ++i) lineTo(pt(t0 + dt * i / n));
    }
    void arcTo(double rx, double ry, double angleDeg, bool large, bool sweep, Vec2 p) {
        Vec2 p0 = cur_;
        if ((p - p0).length() < 1e-12) return;
        rx = std::abs(rx); ry = std::abs(ry);
        if (rx < 1e-12 || ry < 1e-12) { lineTo(p); return; }
        double phi = angleDeg * M_PI / 180, cs = std::cos(phi), sn = std::sin(phi);
        double dx = (p0.x - p.x) / 2, dy = (p0.y - p.y) / 2;
        double x1 = cs * dx + sn * dy, y1 = -sn * dx + cs * dy;
        double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
        if (lambda > 1) { double s = std::sqrt(lambda); rx *= s; ry *= s; }
        double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
        double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
        double coef = std::sqrt(std::max(0.0, num / den)) * (large == sweep ? -1 : 1);
        double cx1 = coef * rx * y1 / ry, cy1 = -coef * ry * x1 / rx;
        Vec2 c{cs * cx1 - sn * cy1 + (p0.x + p.x) / 2, sn * cx1 + cs * cy1 + (p0.y + p.y) / 2};
        auto angle = [](double ux, double uy, double vx, double vy) {
            return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
        };
        double t0 = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
        double dt = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
        if (!sweep && dt > 0) dt -= 2 * M_PI;
        if (sweep && dt < 0) dt += 2 * M_PI;
        ellipse(c, rx, ry, phi, t0, dt, false);
        cur_ = p;   // Rundungsfehler vermeiden
        open_.points.back() = m_.apply(p);
    }
    Vec2 current() const { return cur_; }

    std::vector<Polyline> take() { finish(); return std::move(done_); }

private:
    Mat m_;
    double tol_;
    Vec2 cur_, start_;
    Polyline open_;
    std::vector<Polyline> done_;

    void ensureStarted() { if (open_.points.empty()) { start_ = cur_; open_.points = {m_.apply(cur_)}; } }
    void finish() {
        if (open_.points.size() > 1) done_.push_back(open_);
        open_ = {};
    }
    void flatten(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int depth) {
        auto distToChord = [&](Vec2 q) {
            Vec2 d = p3 - p0;
            double len = d.length();
            if (len < 1e-12) return (q - p0).length();
            return std::abs((q.x - p0.x) * d.y - (q.y - p0.y) * d.x) / len;
        };
        if (depth > 16 || std::max(distToChord(p1), distToChord(p2)) <= tol_) {
            open_.points.push_back(p3);
            return;
        }
        Vec2 a = (p0 + p1) * 0.5, b = (p1 + p2) * 0.5, c = (p2 + p3) * 0.5;
        Vec2 ab = (a + b) * 0.5, bc = (b + c) * 0.5, mid = (ab + bc) * 0.5;
        flatten(p0, a, ab, mid, depth + 1);
        flatten(mid, bc, c, p3, depth + 1);
    }
};

void parsePath(const std::string& d, Builder& b) {
    NumberReader r(d);
    char cmd = 0;
    Vec2 lastCtrl;
    char lastCmd = 0;
    double v[7];
    auto read = [&](int n) {
        for (int i = 0; i < n; ++i) if (!r.number(v[i])) return false;
        return true;
    };
    while (!r.atEnd()) {
        char c;
        if (r.command(c)) cmd = c;
        else if (!cmd || !r.peekNumber()) break;   // unbekanntes Zeichen
        bool rel = std::islower(static_cast<unsigned char>(cmd));
        char up = char(std::toupper(static_cast<unsigned char>(cmd)));
        Vec2 cur = b.current();
        Vec2 o = rel ? cur : Vec2{0, 0};
        switch (up) {
            case 'M':
                if (!read(2)) return;
                b.moveTo(o + Vec2{v[0], v[1]});
                cmd = rel ? 'l' : 'L';   // weitere Paare sind lineto
                break;
            case 'L': if (!read(2)) return; b.lineTo(o + Vec2{v[0], v[1]}); break;
            case 'H': if (!read(1)) return; b.lineTo({(rel ? cur.x : 0) + v[0], cur.y}); break;
            case 'V': if (!read(1)) return; b.lineTo({cur.x, (rel ? cur.y : 0) + v[0]}); break;
            case 'C': {
                if (!read(6)) return;
                Vec2 c1 = o + Vec2{v[0], v[1]}, c2 = o + Vec2{v[2], v[3]}, p = o + Vec2{v[4], v[5]};
                b.cubicTo(c1, c2, p); lastCtrl = c2; break;
            }
            case 'S': {
                if (!read(4)) return;
                Vec2 c1 = (lastCmd == 'C' || lastCmd == 'S') ? cur * 2 - lastCtrl : cur;
                Vec2 c2 = o + Vec2{v[0], v[1]}, p = o + Vec2{v[2], v[3]};
                b.cubicTo(c1, c2, p); lastCtrl = c2; break;
            }
            case 'Q': {
                if (!read(4)) return;
                Vec2 q = o + Vec2{v[0], v[1]}, p = o + Vec2{v[2], v[3]};
                b.quadTo(q, p); lastCtrl = q; break;
            }
            case 'T': {
                if (!read(2)) return;
                Vec2 q = (lastCmd == 'Q' || lastCmd == 'T') ? cur * 2 - lastCtrl : cur;
                b.quadTo(q, o + Vec2{v[0], v[1]}); lastCtrl = q; break;
            }
            case 'A': {
                bool large, sweep;
                if (!r.number(v[0]) || !r.number(v[1]) || !r.number(v[2]) || !r.flag(large) || !r.flag(sweep) || !r.number(v[3]) || !r.number(v[4])) return;
                b.arcTo(v[0], v[1], v[2], large, sweep, o + Vec2{v[3], v[4]});
                break;
            }
            case 'Z': b.close(); break;
            default: return;
        }
        lastCmd = up;
        if (up == 'Z') cmd = 0;   // nach Z muss ein Befehl folgen
    }
}

// ---------------------------------------------------------------- Durchlauf

void emit(std::vector<Polyline> polys, const Style& st, const std::string& name, Context& ctx) {
    if (!st.visible || polys.empty()) return;
    bool filled = !st.fill.none, stroked = !st.stroke.none;
    if (!filled && !stroked) return;
    auto area = [](const Polyline& p) {
        double a = 0;
        for (size_t i = 0; i < p.points.size(); ++i) {
            const Vec2& u = p.points[i];
            const Vec2& v = p.points[(i + 1) % p.points.size()];
            a += u.x * v.y - v.x * u.y;
        }
        return std::abs(a) / 2;
    };
    uint32_t part = ++ctx.parts;   // alle Teilpfade eines Elements gehören zusammen
    for (auto& p : polys) {
        // Linien ohne Fläche können nicht gefüllt werden (SVG füllt standardmäßig schwarz).
        bool fillThis = filled && p.points.size() > 2 && area(p) > 1e-6;
        if (!fillThis && !stroked) continue;
        size_t li = ctx.layerFor(fillThis ? st.fill.hex : st.stroke.hex);
        auto& acc = ctx.accs[li];
        if (fillThis) { p.closed = true; ++acc.filled; }
        else if (p.closed) ++acc.closed;
        else ++acc.open;
        ctx.shapes.push_back({name, li, std::move(p), 0, part});
    }
}

void walk(const xml::Element& el, const Mat& parentM, const Style& parentSt, Context& ctx, int depth) {
    if (depth > 64) return;
    static const std::set<std::string> skip = {"defs", "clipPath", "mask", "symbol", "marker", "metadata", "title", "desc",
        "style", "script", "pattern", "linearGradient", "radialGradient", "filter", "namedview"};
    std::string tag = el.localName();
    if (skip.count(tag)) return;
    Style st = computeStyle(el, parentSt, ctx);
    if (!st.display) return;
    Mat m = parentM;
    if (auto t = el.attr("transform")) m = m * parseTransform(*t);
    const double ppi = ctx.opt.pxPerInch;
    std::string name = el.attr("id") ? *el.attr("id") : tag;
    Builder b(m, ctx.opt.toleranceMm);

    if (tag == "g" || tag == "a" || tag == "switch" || tag == "svg") {
        if (tag == "svg" && depth > 0)
            m = m * Mat::translate(userLength(el, "x", ppi), userLength(el, "y", ppi));
        for (auto& ch : el.children) walk(*ch, m, st, ctx, depth + 1);
        return;
    }
    if (tag == "use") {
        const std::string* href = el.attr("href");
        if (!href) href = el.attr("xlink:href");
        if (href && !href->empty() && (*href)[0] == '#') {
            auto it = ctx.ids.find(href->substr(1));
            if (it != ctx.ids.end()) {
                Mat um = m * Mat::translate(userLength(el, "x", ppi), userLength(el, "y", ppi));
                const xml::Element& target = *it->second;
                if (target.localName() == "symbol")
                    for (auto& ch : target.children) walk(*ch, um, st, ctx, depth + 1);
                else walk(target, um, st, ctx, depth + 1);
            }
        }
        return;
    }
    if (tag == "path") {
        if (auto d = el.attr("d")) parsePath(*d, b);
    } else if (tag == "rect") {
        double x = userLength(el, "x", ppi), y = userLength(el, "y", ppi);
        double w = userLength(el, "width", ppi), h = userLength(el, "height", ppi);
        if (w <= 0 || h <= 0) return;
        double rx = userLength(el, "rx", ppi, -1), ry = userLength(el, "ry", ppi, -1);
        if (rx < 0) rx = ry < 0 ? 0 : ry;
        if (ry < 0) ry = rx;
        rx = std::min(rx, w / 2); ry = std::min(ry, h / 2);
        if (rx <= 0 || ry <= 0) {
            b.moveTo({x, y}); b.lineTo({x + w, y}); b.lineTo({x + w, y + h}); b.lineTo({x, y + h}); b.close();
        } else {
            b.moveTo({x + rx, y});
            b.lineTo({x + w - rx, y});
            b.ellipse({x + w - rx, y + ry}, rx, ry, 0, -M_PI / 2, M_PI / 2, false);
            b.lineTo({x + w, y + h - ry});
            b.ellipse({x + w - rx, y + h - ry}, rx, ry, 0, 0, M_PI / 2, false);
            b.lineTo({x + rx, y + h});
            b.ellipse({x + rx, y + h - ry}, rx, ry, 0, M_PI / 2, M_PI / 2, false);
            b.lineTo({x, y + ry});
            b.ellipse({x + rx, y + ry}, rx, ry, 0, M_PI, M_PI / 2, false);
            b.close();
        }
    } else if (tag == "circle" || tag == "ellipse") {
        double cx = userLength(el, "cx", ppi), cy = userLength(el, "cy", ppi);
        double rx = tag == "circle" ? userLength(el, "r", ppi) : userLength(el, "rx", ppi);
        double ry = tag == "circle" ? rx : userLength(el, "ry", ppi);
        if (rx <= 0 || ry <= 0) return;
        b.ellipse({cx, cy}, rx, ry, 0, 0, 2 * M_PI, true);
        b.close();
    } else if (tag == "line") {
        b.moveTo({userLength(el, "x1", ppi), userLength(el, "y1", ppi)});
        b.lineTo({userLength(el, "x2", ppi), userLength(el, "y2", ppi)});
    } else if (tag == "polyline" || tag == "polygon") {
        auto v = el.attr("points") ? numbers(*el.attr("points")) : std::vector<double>{};
        for (size_t i = 0; i + 1 < v.size(); i += 2) {
            if (i == 0) b.moveTo({v[0], v[1]});
            else b.lineTo({v[i], v[i + 1]});
        }
        if (tag == "polygon") b.close();
    } else if (tag == "text") {
        ++ctx.texts; return;
    } else if (tag == "image") {
        ++ctx.images; return;
    } else {
        if (tag != "foreignObject" && !el.children.empty())
            for (auto& ch : el.children) walk(*ch, m, st, ctx, depth + 1);
        else if (tag == "foreignObject") ++ctx.others;
        return;
    }
    emit(b.take(), st, name, ctx);
}

std::string colorName(const std::string& hex) {
    static const std::map<std::string, std::string> names = {
        {"#000000", "Schwarz"}, {"#ffffff", "Weiß"}, {"#ff0000", "Rot"}, {"#00ff00", "Grün"}, {"#008000", "Grün"},
        {"#0000ff", "Blau"}, {"#ffff00", "Gelb"}, {"#00ffff", "Cyan"}, {"#ff00ff", "Magenta"}, {"#808080", "Grau"},
        {"#ffa500", "Orange"}, {"#800080", "Lila"}};
    auto it = names.find(hex);
    return it != names.end() ? it->second : hex;
}

Color toColor(const std::string& hex) {
    auto h = [&](size_t i) { return float(std::strtol(hex.substr(i, 2).c_str(), nullptr, 16)) / 255.0f; };
    if (hex.size() != 7) return {0, 0, 0, 1};
    return {h(1), h(3), h(5), 1};
}

} // namespace

SvgImportResult importSvg(const std::string& text, const SvgImportOptions& opt) {
    SvgImportResult res;
    xml::ParseError perr;
    auto root = xml::parse(text, &perr);
    if (!root) { res.error = "Die Datei ist kein gültiges XML: " + perr.message; return res; }
    if (root->localName() != "svg") { res.error = "Die Datei ist kein SVG."; return res; }

    Context ctx;
    ctx.opt = opt;
    collect(*root, ctx);

    // Seitengröße und Benutzereinheit
    double pxmm = 25.4 / opt.pxPerInch;
    auto vb = root->attr("viewBox") ? numbers(*root->attr("viewBox")) : std::vector<double>{};
    auto wMm = lengthMm(root->attr("width"), opt.pxPerInch), hMm = lengthMm(root->attr("height"), opt.pxPerInch);
    double s = pxmm, vbx = 0, vby = 0, pageW = 0, pageH = 0;
    if (vb.size() == 4 && vb[2] > 0 && vb[3] > 0) {
        vbx = vb[0]; vby = vb[1];
        pageW = wMm ? *wMm : (hMm ? *hMm * vb[2] / vb[3] : vb[2] * pxmm);
        pageH = hMm ? *hMm : (wMm ? *wMm * vb[3] / vb[2] : vb[3] * pxmm);
        s = std::min(pageW / vb[2], pageH / vb[3]);
    } else {
        pageW = wMm.value_or(0);
        pageH = hMm.value_or(0);
    }
    Mat rootM = Mat::scaling(s, s) * Mat::translate(-vbx, -vby);

    Style base;
    for (auto& ch : root->children) walk(*ch, rootM, computeStyle(*root, base, ctx), ctx, 1);

    if (ctx.shapes.empty()) {
        res.error = ctx.texts || ctx.images
            ? "Die Datei enthält keine Vektorformen, nur Text oder Bilder. Text bitte vorher in Pfade umwandeln."
            : "Die Datei enthält keine sichtbaren Formen.";
        return res;
    }

    // Ohne Seitenhöhe: Unterkante der Formen als Bezug
    if (pageH <= 0 || pageW <= 0) {
        double maxX = 0, maxY = 0;
        for (auto& sh : ctx.shapes) { Rect r = sh.path.bounds(); maxX = std::max(maxX, r.x + r.w); maxY = std::max(maxY, r.y + r.h); }
        if (pageW <= 0) pageW = maxX;
        if (pageH <= 0) pageH = maxY;
    }
    // SVG: y nach unten → Maschine: y nach hinten
    for (auto& sh : ctx.shapes) for (auto& p : sh.path.points) p.y = pageH - p.y;
    res.page = {0, 0, pageW, pageH};

    for (auto& a : ctx.accs) {
        Layer l;
        l.colorKey = a.key;
        l.name = colorName(a.key);
        l.color = toColor(a.key);
        // Geschlossene Formen (auch gefüllte) werden geschnitten – Gravieren wählt man bewusst (Farbe Blau).
        // Sicherheit zuerst: Eine Ebene mit offenen Linien wird nie automatisch geschnitten.
        l.intent = a.open > 0 ? Intent::Mark : Intent::Cut;
        res.layers.push_back(l);
    }
    res.shapes = std::move(ctx.shapes);

    if (ctx.texts) res.warnings.push_back(std::to_string(ctx.texts) + (ctx.texts == 1 ? " Text wurde" : " Texte wurden") + " übersprungen. Text bitte im Zeichenprogramm in Pfade umwandeln.");
    if (ctx.images) res.warnings.push_back(std::to_string(ctx.images) + (ctx.images == 1 ? " eingebettetes Bild wurde" : " eingebettete Bilder wurden") + " übersprungen.");
    if (ctx.gradients) res.warnings.push_back("Verläufe und Muster wurden als graue Flächen übernommen.");
    res.ok = true;
    return res;
}

SvgImportResult importSvgFile(const std::string& path, const SvgImportOptions& opt) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { SvgImportResult r; r.error = "Die Datei lässt sich nicht lesen."; return r; }
    std::stringstream ss;
    ss << in.rdbuf();
    return importSvg(ss.str(), opt);
}

} // namespace mb
