// SPDX-License-Identifier: GPL-3.0-or-later
#include "job/Resume.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace mb {

GcodeTrack trackGcode(const std::vector<std::string>& lines) {
    GcodeTrack t;
    Vec2 pos{0, 0};
    int motionMode = 0;   // 0 = G0, 1 = G1
    bool laserOn = false;
    double s = 0, f = 0;
    for (const auto& raw : lines) {
        bool hasX = false, hasY = false;
        double x = pos.x, y = pos.y;
        for (size_t i = 0; i < raw.size();) {
            char c = char(std::toupper((unsigned char)raw[i]));
            if (c == ';' || c == '(') break;
            if (!std::isalpha((unsigned char)c)) { ++i; continue; }
            char* end = nullptr;
            double v = std::strtod(raw.c_str() + i + 1, &end);
            size_t next = size_t(end - raw.c_str());
            if (next == i + 1) { ++i; continue; }   // Buchstabe ohne Zahl ($H, $J= …)
            switch (c) {
                case 'G': if (v == 0) motionMode = 0; else if (v == 1) motionMode = 1; break;
                case 'M':
                    if (v == 3) { laserOn = true; t.dynamicPower = false; }
                    else if (v == 4) { laserOn = true; t.dynamicPower = true; }
                    else if (v == 5) laserOn = false;
                    else if (v == 8) t.air = true;
                    break;
                case 'X': x = v; hasX = true; break;
                case 'Y': y = v; hasY = true; break;
                case 'S': s = v; break;
                case 'F': f = v; break;
                default: break;
            }
            i = next;
        }
        bool moves = hasX || hasY;
        pos = {x, y};
        t.to.push_back(pos);
        t.motion.push_back(moves);
        t.burn.push_back(moves && motionMode == 1 && laserOn && s > 0);
        t.feed.push_back(f);
    }
    return t;
}

size_t locateExecuting(const GcodeTrack& t, size_t from, size_t to, Vec2 pos, double tol) {
    to = std::min(to, t.to.size());
    for (size_t i = from; i < to; ++i) {
        if (!t.motion[i]) continue;
        Vec2 a = i > 0 ? t.to[i - 1] : t.to[i];
        if (distanceToSegment(pos, a, t.to[i]) <= tol) return i;
    }
    return from;
}

size_t restartLine(const GcodeTrack& t, size_t executing) {
    if (t.to.empty()) return 0;
    executing = std::min(executing, t.to.size() - 1);
    for (size_t i = executing + 1; i-- > 0;)
        if (t.motion[i] && !t.burn[i]) return i;   // Leerfahrt zum Anfang der Kontur/Zeile
    return 0;
}

std::vector<std::string> resumeLines(const std::vector<std::string>& lines, const GcodeTrack& t, size_t restart,
                                     size_t* preambleCount) {
    std::vector<std::string> out = {"G21", "G90", t.dynamicPower ? "M4 S0" : "M3 S0"};
    if (t.air) out.push_back("M8");
    double f = restart > 0 && restart - 1 < t.feed.size() ? t.feed[restart - 1] : 0;
    if (f > 0) { char b[32]; std::snprintf(b, sizeof b, "F%.0f", f); out.push_back(b); }
    if (preambleCount) *preambleCount = out.size();
    for (size_t i = restart; i < lines.size(); ++i) out.push_back(lines[i]);
    return out;
}

Rect remainingBounds(const GcodeTrack& t, size_t restart) {
    double x0 = std::numeric_limits<double>::max(), y0 = x0, x1 = -x0, y1 = -x0;
    bool any = false;
    for (size_t i = restart; i < t.to.size(); ++i) {
        if (!t.burn[i]) continue;
        for (Vec2 p : {i > 0 ? t.to[i - 1] : t.to[i], t.to[i]}) {
            x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
        }
        any = true;
    }
    return any ? Rect{x0, y0, x1 - x0, y1 - y0} : Rect{};
}

std::vector<std::vector<Vec2>> remainingPaths(const GcodeTrack& t, size_t restart) {
    std::vector<std::vector<Vec2>> out;
    bool open = false;
    for (size_t i = restart; i < t.to.size(); ++i) {
        if (!t.motion[i]) continue;
        if (!t.burn[i]) { open = false; continue; }
        if (!open) { out.push_back({i > 0 ? t.to[i - 1] : t.to[i]}); open = true; }
        out.back().push_back(t.to[i]);
    }
    return out;
}

} // namespace mb
