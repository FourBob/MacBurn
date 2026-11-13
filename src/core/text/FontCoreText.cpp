// SPDX-License-Identifier: GPL-3.0-or-later
#include "text/Font.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace mb {

namespace {

template <typename T>
struct CF {   // CoreFoundation-Objekt mit automatischem Freigeben
    T ref = nullptr;
    explicit CF(T r = nullptr) : ref(r) {}
    ~CF() { if (ref) CFRelease(ref); }
    CF(const CF&) = delete;
    CF& operator=(const CF&) = delete;
    operator T() const { return ref; }
};

CFStringRef cfString(const std::string& s) {
    return CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(s.data()), CFIndex(s.size()), kCFStringEncodingUTF8, false);
}

std::string toStd(CFStringRef s) {
    if (!s) return {};
    CFIndex len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
    std::string out(size_t(len), '\0');
    if (!CFStringGetCString(s, out.data(), len, kCFStringEncodingUTF8)) return {};
    out.resize(std::char_traits<char>::length(out.c_str()));
    return out;
}

// Schrift aus Familie + Schnitt; Größe in mm (1 Einheit = 1 mm)
CTFontRef makeFont(const std::string& family, const std::string& style, double size) {
    CF<CFStringRef> fam(cfString(family)), sty(cfString(style));
    const void* keys[] = {kCTFontFamilyNameAttribute, kCTFontStyleNameAttribute};
    const void* vals[] = {fam.ref, sty.ref};
    CF<CFDictionaryRef> attrs(CFDictionaryCreate(nullptr, keys, vals, style.empty() ? 1 : 2,
                                                 &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    CF<CTFontDescriptorRef> desc(CTFontDescriptorCreateWithAttributes(attrs));
    CTFontRef font = CTFontCreateWithFontDescriptor(desc, size, nullptr);
    if (!font) return nullptr;
    // CoreText weicht bei unbekannten Namen still auf eine Ersatzschrift aus – das erkennen.
    CF<CFStringRef> got(CTFontCopyFamilyName(font));
    if (toStd(got) != family) { CFRelease(font); return nullptr; }
    return font;
}

struct Flattener {
    std::vector<Polyline>* out;
    Polyline current;
    Vec2 last, start;
    double tol;
    Affine m;

    void finish(bool close) {
        if (current.points.size() > 1) {
            current.closed = close;
            if (close && (current.points.back() - current.points.front()).length() < 1e-9) current.points.pop_back();
            if (current.points.size() > (close ? 2u : 1u)) out->push_back(current);
        }
        current = {};
    }
    void add(Vec2 p) { current.points.push_back(m.apply(p)); last = p; }
    void cubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int depth) {
        auto dist = [&](Vec2 q) {
            Vec2 d = p3 - p0;
            double len = d.length();
            return len < 1e-12 ? (q - p0).length() : std::abs((q.x - p0.x) * d.y - (q.y - p0.y) * d.x) / len;
        };
        if (depth > 14 || std::max(dist(p1), dist(p2)) <= tol) { add(p3); return; }
        Vec2 a = (p0 + p1) * 0.5, b = (p1 + p2) * 0.5, c = (p2 + p3) * 0.5;
        Vec2 ab = (a + b) * 0.5, bc = (b + c) * 0.5, mid = (ab + bc) * 0.5;
        cubic(p0, a, ab, mid, depth + 1);
        cubic(mid, bc, c, p3, depth + 1);
    }

    static void apply(void* info, const CGPathElement* e) {
        auto* f = static_cast<Flattener*>(info);
        auto P = [&](int i) { return Vec2{e->points[i].x, e->points[i].y}; };
        switch (e->type) {
            case kCGPathElementMoveToPoint: f->finish(true); f->start = P(0); f->add(P(0)); break;
            case kCGPathElementAddLineToPoint: f->add(P(0)); break;
            case kCGPathElementAddQuadCurveToPoint: {
                Vec2 p0 = f->last, q = P(0), p = P(1);
                f->cubic(p0, p0 + (q - p0) * (2.0 / 3), p + (q - p) * (2.0 / 3), p, 0);
                break;
            }
            case kCGPathElementAddCurveToPoint: f->cubic(f->last, P(0), P(1), P(2), 0); break;
            case kCGPathElementCloseSubpath: f->finish(true); break;
        }
    }
};

class CoreTextProvider : public FontProvider {
public:
    std::vector<std::string> families() const override {
        std::vector<std::string> out;
        CF<CFArrayRef> names(CTFontManagerCopyAvailableFontFamilyNames());
        for (CFIndex i = 0; i < CFArrayGetCount(names); ++i) {
            std::string n = toStd(static_cast<CFStringRef>(CFArrayGetValueAtIndex(names, i)));
            if (!n.empty() && n[0] != '.') out.push_back(n);   // Systemschriften mit Punkt ausblenden
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    std::vector<std::string> styles(const std::string& family) const override {
        std::vector<std::string> out;
        CF<CFStringRef> fam(cfString(family));
        const void* keys[] = {kCTFontFamilyNameAttribute};
        const void* vals[] = {fam.ref};
        CF<CFDictionaryRef> attrs(CFDictionaryCreate(nullptr, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CF<CTFontDescriptorRef> desc(CTFontDescriptorCreateWithAttributes(attrs));
        CF<CFArrayRef> matches(CTFontDescriptorCreateMatchingFontDescriptors(desc, nullptr));
        if (!matches.ref) return out;
        for (CFIndex i = 0; i < CFArrayGetCount(matches); ++i) {
            auto d = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(matches, i));
            CF<CFStringRef> s(static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(d, kCTFontStyleNameAttribute)));
            std::string n = toStd(s);
            if (!n.empty() && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        }
        return out;
    }

    std::vector<Polyline> outline(const TextSpec& spec, double tol) const override {
        std::vector<Polyline> out;
        if (spec.text.empty() || spec.capHeightMm <= 0) return out;
        // Erst mit Größe 1 die Großbuchstabenhöhe messen, dann so skalieren, dass sie capHeightMm ergibt
        CF<CTFontRef> probe(makeFont(spec.family, spec.style, 100));
        if (!probe.ref) return out;
        double cap = CTFontGetCapHeight(probe);
        if (cap <= 0) cap = CTFontGetAscent(probe) * 0.7;
        double size = 100 * spec.capHeightMm / cap;
        CF<CTFontRef> font(makeFont(spec.family, spec.style, size));
        if (!font.ref) return out;
        double lineHeight = (CTFontGetAscent(font) + CTFontGetDescent(font)) * spec.lineSpacing;

        std::vector<std::string> lines;
        std::stringstream ss(spec.text);
        for (std::string l; std::getline(ss, l);) lines.push_back(l);
        if (!spec.text.empty() && spec.text.back() == '\n') lines.push_back("");

        CF<CFNumberRef> kern(CFNumberCreate(nullptr, kCFNumberDoubleType, &spec.letterSpacingMm));
        for (size_t li = 0; li < lines.size(); ++li) {
            if (lines[li].empty()) continue;
            CF<CFStringRef> str(cfString(lines[li]));
            if (!str.ref) continue;
            const void* keys[] = {kCTFontAttributeName, kCTKernAttributeName};
            const void* vals[] = {font.ref, kern.ref};
            CF<CFDictionaryRef> attrs(CFDictionaryCreate(nullptr, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
            CF<CFAttributedStringRef> astr(CFAttributedStringCreate(nullptr, str, attrs));
            CF<CTLineRef> line(CTLineCreateWithAttributedString(astr));
            double width = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
            double x0 = spec.align == 1 ? -width / 2 : spec.align == 2 ? -width : 0;
            double y0 = -double(li) * lineHeight;

            CFArrayRef runs = CTLineGetGlyphRuns(line);
            for (CFIndex r = 0; r < CFArrayGetCount(runs); ++r) {
                auto run = static_cast<CTRunRef>(CFArrayGetValueAtIndex(runs, r));
                auto runFont = static_cast<CTFontRef>(CFDictionaryGetValue(CTRunGetAttributes(run), kCTFontAttributeName));
                CFIndex n = CTRunGetGlyphCount(run);
                std::vector<CGGlyph> glyphs(static_cast<size_t>(n));
                std::vector<CGPoint> pos(static_cast<size_t>(n));
                CTRunGetGlyphs(run, CFRangeMake(0, 0), glyphs.data());
                CTRunGetPositions(run, CFRangeMake(0, 0), pos.data());
                for (CFIndex g = 0; g < n; ++g) {
                    CF<CGPathRef> path(CTFontCreatePathForGlyph(runFont ? runFont : font.ref, glyphs[size_t(g)], nullptr));
                    if (!path.ref) continue;   // Leerzeichen
                    Flattener f{&out, {}, {}, {}, tol, spec.m * Affine::translate(x0 + pos[size_t(g)].x, y0 + pos[size_t(g)].y)};
                    CGPathApply(path, &f, &Flattener::apply);
                    f.finish(true);
                }
            }
        }
        return out;
    }
};

} // namespace

std::unique_ptr<FontProvider> makeSystemFontProvider() { return std::make_unique<CoreTextProvider>(); }

} // namespace mb
