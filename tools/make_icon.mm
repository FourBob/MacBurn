// Erzeugt aus dem Logo (docs/logo/MacBurn-Logo.jpg) das App-Symbol und das README-Logo.
//
//   clang++ -std=c++20 -fobjc-arc -framework Cocoa tools/make_icon.mm -o build/make_icon
//   build/make_icon docs/logo/MacBurn-Logo.jpg resources
//   mv resources/MacBurn-Logo.png docs/logo/ && rm -r resources/MacBurn.iconset
//   iconutil -c icns resources/MacBurn.iconset -o resources/MacBurn.icns
//
// Das Bildzeichen (Flamme mit Strahl) wird automatisch freigestellt: alle nicht-weißen
// Pixel oberhalb des Schriftzugs. Das Symbol liegt auf einer weißen, abgerundeten Fläche
// im Raster der macOS-Symbole (824 von 1024 Punkten, Radius 185).

#import <Cocoa/Cocoa.h>
#include <algorithm>
#include <string>

struct Box { int x0, y0, x1, y1; };

// Begrenzung aller "farbigen" Pixel in den Zeilen [rowFrom, rowTo)
static Box inkBounds(const unsigned char* px, int w, int rowFrom, int rowTo, int stride) {
    Box b{w, rowTo, -1, -1};
    for (int y = rowFrom; y < rowTo; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned char* p = px + y * stride + x * 4;
            int minC = std::min({p[0], p[1], p[2]});
            if (minC < 200) {
                b.x0 = std::min(b.x0, x); b.x1 = std::max(b.x1, x);
                b.y0 = std::min(b.y0, y); b.y1 = std::max(b.y1, y);
            }
        }
    return b;
}

// Erste Zeile ab 'from', die nach einer leeren Lücke wieder Farbe enthält (Beginn des Schriftzugs)
static int textTop(const unsigned char* px, int w, int h, int stride, int symbolBottom) {
    for (int y = symbolBottom; y < h; ++y) {
        bool ink = false;
        for (int x = 0; x < w && !ink; ++x) {
            const unsigned char* p = px + y * stride + x * 4;
            ink = std::min({p[0], p[1], p[2]}) < 200;
        }
        if (!ink) {
            // Lücke gefunden – nächste Zeile mit Farbe ist der Schriftzug
            for (int z = y; z < h; ++z)
                for (int x = 0; x < w; ++x) {
                    const unsigned char* p = px + z * stride + x * 4;
                    if (std::min({p[0], p[1], p[2]}) < 200) return z;
                }
            return h;
        }
    }
    return h;
}

static bool writePNG(CGImageRef img, NSString* path) {
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:img];
    NSData* data = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return [data writeToFile:path atomically:YES];
}

static CGContextRef makeContext(int w, int h) {
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(nullptr, w, h, 8, 0, cs,
        (uint32_t)kCGImageAlphaPremultipliedLast | (uint32_t)kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(cs);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    return ctx;
}

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc < 3) { fprintf(stderr, "Aufruf: make_icon <logo.jpg> <ausgabeordner>\n"); return 1; }
        NSString* src = [NSString stringWithUTF8String:argv[1]];
        NSString* out = [NSString stringWithUTF8String:argv[2]];
        NSImage* img = [[NSImage alloc] initWithContentsOfFile:src];
        if (!img) { fprintf(stderr, "Logo nicht lesbar\n"); return 1; }
        CGImageRef cg = [img CGImageForProposedRect:nullptr context:nil hints:nil];
        int w = (int)CGImageGetWidth(cg), h = (int)CGImageGetHeight(cg);

        // In ein RGBA-Raster mit bekannter Anordnung zeichnen (Zeile 0 = oben)
        CGContextRef raster = makeContext(w, h);
        CGContextDrawImage(raster, CGRectMake(0, 0, w, h), cg);
        const unsigned char* px = (const unsigned char*)CGBitmapContextGetData(raster);
        int stride = (int)CGBitmapContextGetBytesPerRow(raster);

        // Symbol: von oben bis zur Lücke vor dem Schriftzug
        Box all = inkBounds(px, w, 0, h, stride);
        // Symbolunterkante: erste leere Zeile nach Beginn der Farbe
        int y = all.y0;
        for (; y < h; ++y) {
            bool ink = false;
            for (int x = 0; x < w && !ink; ++x) {
                const unsigned char* p = px + y * stride + x * 4;
                ink = std::min({p[0], p[1], p[2]}) < 200;
            }
            if (!ink) break;
        }
        // Der Leuchtpunkt hat einen weichen Schein – kleine Lücken überbrücken
        int symbolBottom = y;
        int text = textTop(px, w, h, stride, symbolBottom);
        while (text - symbolBottom < 20 && text < h) {
            int yy = text;
            for (; yy < h; ++yy) {
                bool ink = false;
                for (int x = 0; x < w && !ink; ++x) {
                    const unsigned char* p = px + yy * stride + x * 4;
                    ink = std::min({p[0], p[1], p[2]}) < 200;
                }
                if (!ink) break;
            }
            symbolBottom = yy;
            text = textTop(px, w, h, stride, symbolBottom);
        }
        Box sym = inkBounds(px, w, all.y0, symbolBottom, stride);
        printf("Bild %dx%d, Symbol x %d–%d y %d–%d, Schriftzug ab y %d\n",
               w, h, sym.x0, sym.x1, sym.y0, sym.y1, text);

        // README-Logo: Symbol und Schriftzug mit etwas Rand, weißer Hintergrund bleibt
        {
            int pad = 40;
            CGRect r = CGRectMake(std::max(0, all.x0 - pad), std::max(0, all.y0 - pad),
                                  std::min(w, all.x1 + pad) - std::max(0, all.x0 - pad),
                                  std::min(h, all.y1 + pad) - std::max(0, all.y0 - pad));
            CGImageRef crop = CGImageCreateWithImageInRect(cg, r);
            writePNG(crop, [out stringByAppendingPathComponent:@"MacBurn-Logo.png"]);
            CGImageRelease(crop);
        }

        // Symbol quadratisch ausschneiden (zentriert, etwas Luft)
        int sw = sym.x1 - sym.x0 + 1, sh = sym.y1 - sym.y0 + 1;
        int side = std::max(sw, sh);
        int cx = (sym.x0 + sym.x1) / 2, cy = (sym.y0 + sym.y1) / 2;
        CGRect sq = CGRectMake(cx - side / 2, cy - side / 2, side, side);
        CGImageRef symbol = CGImageCreateWithImageInRect(cg, sq);

        NSString* set = [out stringByAppendingPathComponent:@"MacBurn.iconset"];
        [[NSFileManager defaultManager] createDirectoryAtPath:set withIntermediateDirectories:YES
                                                   attributes:nil error:nil];
        struct { int px; const char* name; } sizes[] = {
            {16, "icon_16x16"}, {32, "icon_16x16@2x"}, {32, "icon_32x32"}, {64, "icon_32x32@2x"},
            {128, "icon_128x128"}, {256, "icon_128x128@2x"}, {256, "icon_256x256"},
            {512, "icon_256x256@2x"}, {512, "icon_512x512"}, {1024, "icon_512x512@2x"}};
        for (auto& s : sizes) {
            double k = s.px / 1024.0;
            CGContextRef ctx = makeContext(s.px, s.px);
            // Grundfläche im macOS-Raster mit leichtem Schatten
            CGRect body = CGRectMake(100 * k, 100 * k, 824 * k, 824 * k);
            CGPathRef shape = CGPathCreateWithRoundedRect(body, 185 * k, 185 * k, nullptr);
            CGContextSaveGState(ctx);
            CGContextSetShadowWithColor(ctx, CGSizeMake(0, -10 * k), 24 * k,
                                        CGColorCreateGenericGray(0, 0.30));
            CGContextAddPath(ctx, shape);
            CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
            CGContextFillPath(ctx);
            CGContextRestoreGState(ctx);
            // Symbol auf die Fläche, mit Innenabstand
            CGContextSaveGState(ctx);
            CGContextAddPath(ctx, shape);
            CGContextClip(ctx);
            double inset = 120 * k;
            CGContextDrawImage(ctx, CGRectInset(body, inset, inset), symbol);
            CGContextRestoreGState(ctx);
            CGPathRelease(shape);
            CGImageRef res = CGBitmapContextCreateImage(ctx);
            writePNG(res, [set stringByAppendingPathComponent:
                               [NSString stringWithFormat:@"%s.png", s.name]]);
            CGImageRelease(res);
            CGContextRelease(ctx);
        }
        CGImageRelease(symbol);
        CGContextRelease(raster);
        return 0;
    }
}
