// Zeichnet das Logo (docs/logo/MacBurn-Logo.jpg) als Vektorgrafik nach und legt daraus die
// Startvorlage an: ein Schild zum Ausschneiden (rot) mit graviertem Logo (blau gefüllt).
//
//   clang++ -std=c++20 -O2 -fobjc-arc -framework Cocoa tools/trace_logo.mm -o build/trace_logo
//   build/trace_logo docs/logo/MacBurn-Logo.jpg examples/MacBurn-Logo.svg
//
// Verfahren: Farbige Pixel (dunkelster Kanal < 170) gelten als Fläche. Die Grenzkanten zwischen
// Fläche und Hintergrund werden zu geschlossenen Umrissen verkettet (Löcher sind eigene Umrisse,
// gefüllt wird gerade-ungerade) und mit Douglas-Peucker vereinfacht.

#import <Cocoa/Cocoa.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

struct P { double x, y; };

static double segDist(P p, P a, P b) {
    double dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy;
    double t = l > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l, 0.0, 1.0) : 0;
    double qx = a.x + t * dx - p.x, qy = a.y + t * dy - p.y;
    return std::sqrt(qx * qx + qy * qy);
}

static void dp(const std::vector<P>& in, size_t a, size_t b, double tol, std::vector<char>& keep) {
    double best = -1; size_t idx = a;
    for (size_t i = a + 1; i < b; ++i) {
        double d = segDist(in[i], in[a], in[b]);
        if (d > best) { best = d; idx = i; }
    }
    if (best > tol) { keep[idx] = 1; dp(in, a, idx, tol, keep); dp(in, idx, b, tol, keep); }
}

// Geschlossenen Umriss vereinfachen: an den zwei am weitesten entfernten Punkten teilen
static std::vector<P> simplifyClosed(const std::vector<P>& pts, double tol) {
    size_t n = pts.size();
    if (n < 4) return pts;
    size_t far = 0; double fd = -1;
    for (size_t i = 1; i < n; ++i) {
        double d = std::hypot(pts[i].x - pts[0].x, pts[i].y - pts[0].y);
        if (d > fd) { fd = d; far = i; }
    }
    std::vector<P> ring(pts); ring.push_back(pts[0]);
    std::vector<char> keep(ring.size(), 0);
    keep[0] = keep[far] = keep[ring.size() - 1] = 1;
    dp(ring, 0, far, tol, keep);
    dp(ring, far, ring.size() - 1, tol, keep);
    std::vector<P> out;
    for (size_t i = 0; i + 1 < ring.size(); ++i) if (keep[i]) out.push_back(ring[i]);
    return out;
}

static double area(const std::vector<P>& p) {
    double a = 0;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) a += (p[j].x + p[i].x) * (p[j].y - p[i].y);
    return a / 2;
}

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc < 3) { fprintf(stderr, "Aufruf: trace_logo <logo.jpg> <vorlage.svg>\n"); return 1; }
        NSImage* img = [[NSImage alloc] initWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]];
        if (!img) { fprintf(stderr, "Logo nicht lesbar\n"); return 1; }
        CGImageRef cg = [img CGImageForProposedRect:nullptr context:nil hints:nil];
        int w = (int)CGImageGetWidth(cg), h = (int)CGImageGetHeight(cg);
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef ctx = CGBitmapContextCreate(nullptr, w, h, 8, 0, cs,
            (uint32_t)kCGImageAlphaPremultipliedLast | (uint32_t)kCGBitmapByteOrder32Big);
        CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cg);
        const unsigned char* px = (const unsigned char*)CGBitmapContextGetData(ctx);
        size_t stride = CGBitmapContextGetBytesPerRow(ctx);

        std::vector<char> ink(size_t(w) * size_t(h));
        int x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const unsigned char* p = px + size_t(y) * stride + size_t(x) * 4;
                bool on = std::min({p[0], p[1], p[2]}) < 170;
                ink[size_t(y) * size_t(w) + size_t(x)] = on;
                if (on) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
            }
        auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && ink[size_t(y) * size_t(w) + size_t(x)]; };

        // Grenzkanten, Fläche jeweils rechts (y nach unten): oben →, rechts ↓, unten ←, links ↑
        struct E { int ax, ay, bx, by; bool used = false; };
        std::vector<E> edges;
        std::multimap<long, size_t> from;
        auto key = [&](int x, int y) { return long(y) * long(w + 1) + long(x); };
        auto add = [&](int ax, int ay, int bx, int by) { from.insert({key(ax, ay), edges.size()}); edges.push_back({ax, ay, bx, by}); };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (!at(x, y)) continue;
                if (!at(x, y - 1)) add(x, y, x + 1, y);
                if (!at(x + 1, y)) add(x + 1, y, x + 1, y + 1);
                if (!at(x, y + 1)) add(x + 1, y + 1, x, y + 1);
                if (!at(x - 1, y)) add(x, y + 1, x, y);
            }

        std::vector<std::vector<P>> loops;
        for (size_t s = 0; s < edges.size(); ++s) {
            if (edges[s].used) continue;
            std::vector<P> loop;
            size_t cur = s;
            while (true) {
                E& e = edges[cur];
                e.used = true;
                loop.push_back({double(e.ax), double(e.ay)});
                int dx = e.bx - e.ax, dy = e.by - e.ay;
                // An Kreuzungspunkten rechts abbiegen, damit diagonal berührende Pixel getrennt bleiben
                size_t next = SIZE_MAX; int bestRank = 9;
                auto range = from.equal_range(key(e.bx, e.by));
                for (auto it = range.first; it != range.second; ++it) {
                    E& c = edges[it->second];
                    if (c.used && it->second != s) continue;
                    int cx = c.bx - c.ax, cy = c.by - c.ay;
                    int cross = dx * cy - dy * cx;   // > 0: rechts (y nach unten)
                    int rank = cross > 0 ? 0 : cross == 0 ? 1 : 2;
                    if (rank < bestRank) { bestRank = rank; next = it->second; }
                }
                if (next == SIZE_MAX || next == s) break;
                cur = next;
            }
            if (loop.size() >= 8) loops.push_back(loop);
        }

        // Maßstab: 0,1 mm je Pixel, Logo mittig auf einem Schild 76 × 96 mm
        const double mmPerPx = 0.1, plateW = 76, plateH = 96, radius = 6;
        double cx = (x0 + x1 + 1) / 2.0, cy = (y0 + y1 + 1) / 2.0;
        auto mx = [&](double x) { return plateW / 2 + (x - cx) * mmPerPx; };
        auto my = [&](double y) { return plateH / 2 + (y - cy) * mmPerPx; };

        FILE* f = fopen(argv[2], "w");
        if (!f) { fprintf(stderr, "Vorlage nicht schreibbar\n"); return 1; }
        fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                   "<!-- MacBurn-Startvorlage: Schild %.0f × %.0f mm mit graviertem Logo.\n"
                   "     Rot = Schneiden, Blau gefüllt = Gravieren. Erzeugt mit tools/trace_logo.mm. -->\n"
                   "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0fmm\" height=\"%.0fmm\" viewBox=\"0 0 %.0f %.0f\">\n",
                plateW, plateH, plateW, plateH, plateW, plateH);
        fprintf(f, "  <rect id=\"Schild\" x=\"0\" y=\"0\" width=\"%.0f\" height=\"%.0f\" rx=\"%.0f\" fill=\"none\" stroke=\"#ff0000\" stroke-width=\"0.1\"/>\n",
                plateW, plateH, radius);
        fprintf(f, "  <path id=\"Logo\" fill=\"#0000ff\" fill-rule=\"evenodd\" d=\"");
        size_t kept = 0, points = 0;
        for (auto& l : loops) {
            auto s = simplifyClosed(l, 0.75);
            if (s.size() < 3 || std::abs(area(s)) < 6) continue;   // Krümel weglassen
            ++kept; points += s.size();
            fprintf(f, "M%.2f %.2f", mx(s[0].x), my(s[0].y));
            for (size_t i = 1; i < s.size(); ++i) fprintf(f, "L%.2f %.2f", mx(s[i].x), my(s[i].y));
            fprintf(f, "Z");
        }
        fprintf(f, "\"/>\n</svg>\n");
        fclose(f);
        printf("Logo %d×%d px → %.1f × %.1f mm, %zu Umrisse, %zu Punkte\n",
               x1 - x0 + 1, y1 - y0 + 1, (x1 - x0 + 1) * mmPerPx, (y1 - y0 + 1) * mmPerPx, kept, points);
        CGContextRelease(ctx);
        CGColorSpaceRelease(cs);
        return 0;
    }
}
