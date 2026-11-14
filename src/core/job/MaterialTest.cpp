// SPDX-License-Identifier: GPL-3.0-or-later
#include "job/MaterialTest.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace mb {

namespace {

constexpr double kX0 = 6;        // Platz links für die Skalenbeschriftung
constexpr double kStripH = 5;        // Gravurstreifen
constexpr double kControlH = 3;      // Kontrollstreifen (nur zum Vergleichen)
constexpr double kSegmentMm = 1; // Auflösung des Verlaufs

struct Builder {
    Job job;
    std::vector<Polyline> preview;
    Vec2 pos{0, 0};
    double travel;

    void rapid(Vec2 p) {
        if ((p - pos).length() < 1e-9) return;
        job.travelLengthMm += (p - pos).length();
        job.moves.push_back({Move::Kind::Rapid, p, travel, 0});
        pos = p;
    }
    void burn(Vec2 p, double speed, double power) {
        job.burnLengthMm += (p - pos).length();
        job.moves.push_back({Move::Kind::Burn, p, speed, std::clamp(power, 0.0, 100.0)});
        pos = p;
    }
    // Linie von a nach b, unterteilt; speed/power als Funktion des Anteils t (0..1) entlang x
    void ramp(Vec2 a, Vec2 b, double x0, double len, const std::function<double(double)>& speed,
              const std::function<double(double)>& power) {
        rapid(a);
        int n = std::max(1, int(std::ceil(std::abs(b.x - a.x) / kSegmentMm)));
        for (int i = 1; i <= n; ++i) {
            Vec2 p = a + (b - a) * (double(i) / n);
            Vec2 mid = a + (b - a) * ((i - 0.5) / n);
            double t = std::clamp((mid.x - x0) / len, 0.0, 1.0);
            burn(p, speed(t), power(t));
        }
        preview.push_back({{a, b}, false});
    }
    void line(Vec2 a, Vec2 b, double speed, double power) {
        rapid(a);
        burn(b, speed, power);
        preview.push_back({{a, b}, false});
    }
};

// Skala unterhalb eines Streifens: Striche alle 10 (lang bei 0, 50, 100), Zahlen bei 0, 50, 100.
// Belegt y … y + 6 (Zahlen unten, Striche oben).
void scale(Builder& bld, const MaterialTestSpec& s, const FontProvider* fonts, double y) {
    double tickSpeed = s.labelSpeed / 2;   // Striche langsamer = dunkler, damit sie sicher sichtbar sind
    for (int v = 0; v <= 100; v += 10) {
        double x = kX0 + s.lengthMm * v / 100.0;
        double h = v % 50 == 0 ? 2.2 : 1.2;
        bld.line({x, y + 6}, {x, y + 6 - h}, tickSpeed, s.labelPower);
    }
    if (!fonts) return;
    for (int v = 0; v <= 100; v += 50) {
        TextSpec t;
        t.text = std::to_string(v);
        t.family = "Helvetica";
        t.style = "Bold";
        t.capHeightMm = 3;
        t.align = 1;
        t.m = Affine::translate(kX0 + s.lengthMm * v / 100.0, y + 0.3);
        auto glyphs = fonts->outline(t, 0.03);
        if (glyphs.empty()) continue;
        // Ziffern gefüllt gravieren (Löcher in 0, 6, 8 bleiben frei)
        std::vector<const Polyline*> ptrs;
        for (auto& g : glyphs) ptrs.push_back(&g);
        for (auto& [a, b] : JobBuilder::scanlineFill(ptrs, 0.15)) {
            bld.rapid(a);
            bld.burn(b, s.labelSpeed, s.labelPower);
        }
        for (auto& g : glyphs) bld.preview.push_back(g);
    }
}

} // namespace

double testEngravePower(const MaterialTestSpec& s, double r) {
    return s.powerMin + (s.powerMax - s.powerMin) * std::clamp(r, 0.0, 100.0) / 100.0;
}

double testCutSpeed(const MaterialTestSpec& s, double r) {
    return s.cutSpeedMax + (s.cutSpeedMin - s.cutSpeedMax) * std::clamp(r, 0.0, 100.0) / 100.0;
}

MaterialTestPlate buildMaterialTest(const MaterialTestSpec& s, const FontProvider* fonts) {
    Builder bld;
    bld.travel = s.travelSpeed;
    double L = s.lengthMm;
    double y = 0;

    if (s.cut) {
        // Skala unter der Leiter, dann der schmale Streifen (Längsseiten mit sinkender Geschwindigkeit)
        scale(bld, s, fonts, y);
        double y0 = y + 7, y1 = y0 + 4;
        auto speed = [&](double t) { return testCutSpeed(s, t * 100); };
        auto power = [&](double) { return s.cutPower; };
        for (int pass = 0; pass < std::max(1, s.cutPasses); ++pass) {
            bld.ramp({kX0, y0}, {kX0 + L, y0}, kX0, L, speed, power);
            bld.ramp({kX0, y1}, {kX0 + L, y1}, kX0, L, speed, power);
            // Enden langsam (sicher durch), damit der Streifen nur entlang der Leiter hängen bleiben kann
            bld.line({kX0 + L, y0}, {kX0 + L, y1}, s.cutSpeedMin, s.cutPower);
        }
        y = y1 + 5;
    }
    if (s.engrave) {
        scale(bld, s, fonts, y);
        double interval = std::max(0.02, s.lineIntervalMm);
        auto fill = [&](double ya, double h, double speed, double powerScale) {
            bool forward = true;
            for (double yy = ya + interval / 2; yy < ya + h; yy += interval) {
                Vec2 a{forward ? kX0 : kX0 + L, yy}, b{forward ? kX0 + L : kX0, yy};
                bld.rapid(a);
                int n = int(std::ceil(L / kSegmentMm));
                for (int i = 1; i <= n; ++i) {
                    Vec2 p = a + (b - a) * (double(i) / n);
                    Vec2 mid = a + (b - a) * ((i - 0.5) / n);
                    double t = std::clamp((mid.x - kX0) / L, 0.0, 1.0);
                    bld.burn(p, speed, testEngravePower(s, t * 100) * powerScale);
                }
                forward = !forward;
            }
            bld.preview.push_back(Polyline::rectangle(kX0, ya, L, h));
        };
        double ya = y + 7;
        fill(ya, kStripH, s.engraveSpeed, 1.0);
        // Kontrolle: gleiche Energie bei anderer Geschwindigkeit → Leistung im selben Verhältnis
        double f = std::clamp(s.controlSpeedFactor, 0.1, 1.0);
        fill(ya + kStripH + 2, kControlH, s.engraveSpeed * f, f);
        y = ya + kStripH + 2 + kControlH;
    }
    bld.rapid({0, 0});

    MaterialTestPlate plate;
    plate.job = std::move(bld.job);
    plate.preview = std::move(bld.preview);
    Polyline all;
    for (auto& p : plate.preview) all.points.insert(all.points.end(), p.points.begin(), p.points.end());
    plate.bounds = all.bounds();
    return plate;
}

} // namespace mb
