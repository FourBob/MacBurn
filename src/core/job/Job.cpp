// SPDX-License-Identifier: GPL-3.0-or-later
#include "job/Job.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace mb {

namespace {

int intentOrder(Intent i) {
    switch (i) {
        case Intent::Mark: return 0;
        case Intent::Photo: return 1;
        case Intent::Engrave: return 2;
        case Intent::Cut: return 3;
    }
    return 4;
}

struct Emitter {
    Job& job;
    Vec2 pos{0, 0};

    void rapid(Vec2 p) {
        if ((p - pos).length() < 1e-6) return;
        job.travelLengthMm += (p - pos).length();
        job.moves.push_back({Move::Kind::Rapid, p, 0, 0});
        pos = p;
    }
    void burn(Vec2 p, double speed, double power) {
        job.burnLengthMm += (p - pos).length();
        job.moves.push_back({Move::Kind::Burn, p, speed, power});
        pos = p;
    }
};

} // namespace

std::vector<std::pair<Vec2, Vec2>> JobBuilder::scanlineFill(const std::vector<const Polyline*>& shapes, double interval) {
    std::vector<std::pair<Vec2, Vec2>> lines;
    if (shapes.empty() || interval <= 0) return lines;
    double y0 = 1e300, y1 = -1e300;
    for (auto* s : shapes) {
        if (!s->closed) continue;
        Rect b = s->bounds();
        y0 = std::min(y0, b.y); y1 = std::max(y1, b.y + b.h);
    }
    bool forward = true;
    for (double y = y0 + interval / 2; y < y1; y += interval) {
        std::vector<double> xs;
        for (auto* s : shapes) {
            if (!s->closed) continue;
            const auto& p = s->points;
            for (size_t i = 0; i < p.size(); ++i) {
                Vec2 a = p[i], b = p[(i + 1) % p.size()];
                if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y))
                    xs.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
            }
        }
        std::sort(xs.begin(), xs.end());
        std::vector<std::pair<Vec2, Vec2>> row;
        for (size_t i = 0; i + 1 < xs.size(); i += 2) row.push_back({{xs[i], y}, {xs[i + 1], y}});
        if (!forward) {
            std::reverse(row.begin(), row.end());
            for (auto& seg : row) std::swap(seg.first, seg.second);
        }
        lines.insert(lines.end(), row.begin(), row.end());
        if (!row.empty()) forward = !forward;
    }
    return lines;
}

Job JobBuilder::build(const Document& doc) const {
    Job job;
    Emitter em{job};

    std::vector<size_t> layerOrder(doc.layers.size());
    for (size_t i = 0; i < layerOrder.size(); ++i) layerOrder[i] = i;
    std::stable_sort(layerOrder.begin(), layerOrder.end(), [&](size_t a, size_t b) {
        return intentOrder(doc.layers[a].intent) < intentOrder(doc.layers[b].intent);
    });

    for (size_t li : layerOrder) {
        const Layer& layer = doc.layers[li];
        if (!layer.output) continue;
        EffectiveSettings s = ProfileStore::resolve(machine_, material_, layer.intent, layer.overrides);
        if (!s.possible) continue;

        std::vector<const Polyline*> paths;
        for (const auto& sh : doc.shapes) if (sh.layer == li) paths.push_back(&sh.path);
        if (paths.empty()) continue;

        if (layer.intent == Intent::Engrave || layer.intent == Intent::Photo) {
            for (auto& [a, b] : scanlineFill(paths, s.lineIntervalMm)) {
                em.rapid(a);
                em.burn(b, s.speed, s.power);
            }
            continue;
        }

        // Schneiden/Markieren: kleine (innere) Konturen zuerst.
        std::stable_sort(paths.begin(), paths.end(), [](const Polyline* a, const Polyline* b) {
            Rect ra = a->bounds(), rb = b->bounds();
            return ra.w * ra.h < rb.w * rb.h;
        });
        for (int pass = 0; pass < s.passes; ++pass) {
            for (auto* p : paths) {
                if (p->points.empty()) continue;
                em.rapid(p->points.front());
                for (size_t i = 1; i < p->points.size(); ++i) em.burn(p->points[i], s.speed, s.power);
                if (p->closed) em.burn(p->points.front(), s.speed, s.power);
            }
        }
    }
    em.rapid({0, 0});
    return job;
}

Vec2 anchorPoint(const Rect& r, int anchor) {
    anchor = std::clamp(anchor, 0, 8);
    double fx = (anchor % 3) / 2.0;
    double fy = 1.0 - (anchor / 3) / 2.0;   // obere Zeile = hinten = größtes y
    return {r.x + fx * r.w, r.y + fy * r.h};
}

const char* anchorName(int anchor) {
    static const char* names[] = {"oben links", "oben Mitte", "oben rechts", "links Mitte", "Mitte",
                                  "rechts Mitte", "unten links", "unten Mitte", "unten rechts"};
    return names[std::clamp(anchor, 0, 8)];
}

double estimateSeconds(const Job& job, double rapidSpeedMmMin, double accel, double junctionDeviation) {
    // Nachbildung der GRBL-Bahnplanung: Kurvengeschwindigkeit aus der Kurvenabweichung ($11),
    // dann Vorwärts- (Beschleunigen) und Rückwärtsdurchgang (Bremsen), Zeit je Abschnitt als Trapez.
    struct Seg { Vec2 dir; double len, vmax, entry = 0; };
    std::vector<Seg> segs;
    Vec2 pos{0, 0};
    for (const auto& m : job.moves) {
        Vec2 d = m.to - pos;
        double len = d.length();
        pos = m.to;
        double v = (m.kind == Move::Kind::Rapid ? rapidSpeedMmMin : m.speed) / 60.0;
        if (len < 1e-9 || v <= 0) continue;
        segs.push_back({d * (1.0 / len), len, v});
    }
    if (segs.empty() || accel <= 0) return 0;
    // Höchste Einfahrgeschwindigkeit an jeder Ecke
    std::vector<double> vj(segs.size() + 1, 0.0);   // vj[i] = Geschwindigkeit am Anfang von segs[i]; Ende = 0
    for (size_t i = 1; i < segs.size(); ++i) {
        double cosT = -(segs[i - 1].dir.x * segs[i].dir.x + segs[i - 1].dir.y * segs[i].dir.y);   // Winkel zwischen Richtungen
        double limit = std::min(segs[i - 1].vmax, segs[i].vmax);
        if (cosT < -0.999999) { vj[i] = limit; continue; }        // gerade weiter
        if (cosT > 0.999999) { vj[i] = 0; continue; }             // Umkehr
        double sinHalf = std::sqrt(0.5 * (1 - cosT));
        double v = std::sqrt(accel * junctionDeviation * sinHalf / (1 - sinHalf));
        vj[i] = std::min(v, limit);
    }
    // Rückwärts: rechtzeitig bremsen können
    for (size_t i = segs.size(); i-- > 0;) vj[i] = std::min(vj[i], std::sqrt(vj[i + 1] * vj[i + 1] + 2 * accel * segs[i].len));
    // Vorwärts: nur so schnell, wie beschleunigt werden kann
    for (size_t i = 0; i < segs.size(); ++i) vj[i + 1] = std::min(vj[i + 1], std::sqrt(vj[i] * vj[i] + 2 * accel * segs[i].len));
    double t = 0;
    for (size_t i = 0; i < segs.size(); ++i) {
        double v0 = vj[i], v1 = vj[i + 1], vm = segs[i].vmax, L = segs[i].len;
        double dAcc = (vm * vm - v0 * v0) / (2 * accel), dDec = (vm * vm - v1 * v1) / (2 * accel);
        if (dAcc + dDec <= L) {
            t += (vm - v0) / accel + (vm - v1) / accel + (L - dAcc - dDec) / vm;
        } else {
            // Dreieck: Spitzengeschwindigkeit wird nicht erreicht
            double vp = std::sqrt((2 * accel * L + v0 * v0 + v1 * v1) / 2);
            t += (vp - v0) / accel + (vp - v1) / accel;
        }
    }
    return t;
}

std::string writeGcode(const Job& job, const GcodeOptions& opt) {
    std::ostringstream g;
    char buf[96];
    g << "; MacBurn\nG21\nG90\n" << (opt.dynamicPower ? "M4" : "M3") << " S0\n";
    if (opt.airAssistGcode) g << "M8\n";
    double lastF = -1, lastS = -1;
    for (const auto& m : job.moves) {
        if (m.kind == Move::Kind::Rapid) {
            if (opt.travelSpeed > 0) {
                // Schonende Leerfahrt: normaler Fahrbefehl, Laser aus (S0)
                std::snprintf(buf, sizeof buf, "G1 X%.3f Y%.3f F%.0f S0\n", m.to.x + opt.offset.x, m.to.y + opt.offset.y, opt.travelSpeed);
                lastF = opt.travelSpeed;
            } else {
                std::snprintf(buf, sizeof buf, "G0 X%.3f Y%.3f S0\n", m.to.x + opt.offset.x, m.to.y + opt.offset.y);
            }
            lastS = 0;
            g << buf;
            continue;
        }
        double s = std::round(m.power / 100.0 * opt.sMax);
        std::string line = "G1 X" + std::string(buf, std::snprintf(buf, sizeof buf, "%.3f", m.to.x + opt.offset.x))
                         + " Y" + std::string(buf, std::snprintf(buf, sizeof buf, "%.3f", m.to.y + opt.offset.y));
        if (m.speed != lastF) { std::snprintf(buf, sizeof buf, " F%.0f", m.speed); line += buf; lastF = m.speed; }
        if (s != lastS) { std::snprintf(buf, sizeof buf, " S%.0f", s); line += buf; lastS = s; }
        g << line << "\n";
    }
    if (opt.airAssistGcode) g << "M9\n";
    g << "M5\n";
    return g.str();
}

} // namespace mb
