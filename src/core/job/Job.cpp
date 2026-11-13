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

double estimateSeconds(const Job& job, double rapidSpeedMmMin, double accel) {
    double t = 0;
    Vec2 pos{0, 0};
    for (const auto& m : job.moves) {
        double d = (m.to - pos).length();
        pos = m.to;
        if (d <= 0) continue;
        double v = (m.kind == Move::Kind::Rapid ? rapidSpeedMmMin : m.speed) / 60.0;
        if (v <= 0) continue;
        // Beschleunigen und Bremsen: Trapez oder Dreieck.
        double dAcc = v * v / accel;
        t += d >= dAcc ? d / v + v / accel : 2.0 * std::sqrt(d / accel);
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
            std::snprintf(buf, sizeof buf, "G0 X%.3f Y%.3f S0\n", m.to.x, m.to.y);
            lastS = 0;
            g << buf;
            continue;
        }
        double s = std::round(m.power / 100.0 * opt.sMax);
        std::string line = "G1 X" + std::string(buf, std::snprintf(buf, sizeof buf, "%.3f", m.to.x))
                         + " Y" + std::string(buf, std::snprintf(buf, sizeof buf, "%.3f", m.to.y));
        if (m.speed != lastF) { std::snprintf(buf, sizeof buf, " F%.0f", m.speed); line += buf; lastF = m.speed; }
        if (s != lastS) { std::snprintf(buf, sizeof buf, " S%.0f", s); line += buf; lastS = s; }
        g << line << "\n";
    }
    if (opt.airAssistGcode) g << "M9\n";
    g << "M5\nG0 X0 Y0\n";
    return g.str();
}

} // namespace mb
