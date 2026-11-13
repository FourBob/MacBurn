// SPDX-License-Identifier: GPL-3.0-or-later
#import "CanvasView.h"
#import "Bridge.hpp"
#include <algorithm>

@implementation CanvasView {
    mb::RenderList _list;
    CGFloat _zoom;      // 1 = Arbeitsfläche passt ins Fenster
    NSPoint _pan;       // Verschiebung in Punkten
}

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        _zoom = 1; _pan = NSZeroPoint;
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    }
    return self;
}

- (BOOL)acceptsFirstResponder { return YES; }

- (void)setRenderList:(const mb::RenderList&)list {
    _list = list;
    self.needsDisplay = YES;
}

// mm → Punkte. Ursprung vorne links = unten links im Fenster.
- (CGAffineTransform)transform {
    const auto& wa = _list.workArea;
    NSRect b = NSInsetRect(self.bounds, 24, 24);
    if (wa.w <= 0 || wa.h <= 0) return CGAffineTransformIdentity;
    CGFloat s = std::min(b.size.width / wa.w, b.size.height / wa.h) * _zoom;
    CGFloat ox = NSMidX(self.bounds) - wa.w * s / 2 + _pan.x;
    CGFloat oy = NSMidY(self.bounds) - wa.h * s / 2 + _pan.y;
    return CGAffineTransformMake(s, 0, 0, s, ox, oy);
}

- (void)drawRect:(NSRect)dirty {
    CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
    [[NSColor underPageBackgroundColor] setFill];
    NSRectFill(self.bounds);

    CGAffineTransform t = [self transform];
    const auto& wa = _list.workArea;
    CGRect bed = CGRectApplyAffineTransform(CGRectMake(wa.x, wa.y, wa.w, wa.h), t);

    // Arbeitsfläche mit Raster (10 mm fein, 50 mm kräftiger)
    [[NSColor textBackgroundColor] setFill];
    CGContextFillRect(ctx, bed);
    for (double mm = 10; mm < std::max(wa.w, wa.h); mm += 10) {
        [[NSColor.secondaryLabelColor colorWithAlphaComponent:(int(mm) % 50 == 0 ? 0.28 : 0.09)] setStroke];
        CGContextSetLineWidth(ctx, 0.5);
        if (mm < wa.w) {
            CGPoint a = CGPointApplyAffineTransform(CGPointMake(mm, 0), t), b = CGPointApplyAffineTransform(CGPointMake(mm, wa.h), t);
            CGContextStrokeLineSegments(ctx, (CGPoint[]){a, b}, 2);
        }
        if (mm < wa.h) {
            CGPoint a = CGPointApplyAffineTransform(CGPointMake(0, mm), t), b = CGPointApplyAffineTransform(CGPointMake(wa.w, mm), t);
            CGContextStrokeLineSegments(ctx, (CGPoint[]){a, b}, 2);
        }
    }
    [NSColor.separatorColor setStroke];
    CGContextStrokeRectWithWidth(ctx, bed, 1);

    // Striche aus dem Kern
    for (const auto& s : _list.strokes) {
        if (s.points.size() < 2) continue;
        CGMutablePathRef p = CGPathCreateMutable();
        CGPathMoveToPoint(p, &t, s.points[0].x, s.points[0].y);
        for (size_t i = 1; i < s.points.size(); ++i) CGPathAddLineToPoint(p, &t, s.points[i].x, s.points[i].y);
        if (s.closed) CGPathCloseSubpath(p);
        CGContextAddPath(ctx, p);
        NSColor* col = NSCol(s.color);
        BOOL dark = [self.effectiveAppearance bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]] == NSAppearanceNameDarkAqua;
        if (dark && std::max({s.color.r, s.color.g, s.color.b}) < 0.25f) col = [NSColor.labelColor colorWithAlphaComponent:s.color.a];
        CGContextSetStrokeColorWithColor(ctx, col.CGColor);
        CGContextSetLineWidth(ctx, s.width);
        CGFloat dash[] = {3, 3};
        CGContextSetLineDash(ctx, 0, s.dashed ? dash : nullptr, s.dashed ? 2 : 0);
        CGContextStrokePath(ctx);
        CGPathRelease(p);
    }

    // Umriss des Auftrags auf der Maschine
    if (_list.jobFrame) {
        const auto& f = *_list.jobFrame;
        CGRect r = CGRectApplyAffineTransform(CGRectMake(f.x, f.y, f.w, f.h), t);
        CGFloat dash[] = {6, 4};
        CGContextSetLineDash(ctx, 0, dash, 2);
        CGContextSetStrokeColorWithColor(ctx, NSColor.controlAccentColor.CGColor);
        CGContextStrokeRectWithWidth(ctx, r, 1);
        CGContextSetLineDash(ctx, 0, nullptr, 0);
    }
    // Laserkopf
    if (_list.head) {
        CGPoint h = CGPointApplyAffineTransform(CGPointMake(_list.head->x, _list.head->y), t);
        CGContextSetStrokeColorWithColor(ctx, NSColor.systemRedColor.CGColor);
        CGContextSetLineWidth(ctx, 1.5);
        CGContextStrokeEllipseInRect(ctx, CGRectMake(h.x - 7, h.y - 7, 14, 14));
        CGContextStrokeLineSegments(ctx, (CGPoint[]){{h.x - 12, h.y}, {h.x + 12, h.y}, {h.x, h.y - 12}, {h.x, h.y + 12}}, 4);
    }

    // Maßangabe
    NSString* label = [NSString stringWithFormat:@"%.0f %%  ·  %.0f × %.0f mm", _zoom * 100, wa.w, wa.h];
    [label drawAtPoint:NSMakePoint(10, 8) withAttributes:@{NSFontAttributeName: [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular],
                                                          NSForegroundColorAttributeName: NSColor.secondaryLabelColor}];
}

- (void)keyDown:(NSEvent*)e {
    if (!self.onKey) { [super keyDown:e]; return; }
    switch (e.keyCode) {
        case 123: self.onKey(-1, 0, NO); return;   // ←
        case 124: self.onKey(1, 0, NO); return;    // →
        case 125: self.onKey(0, -1, NO); return;   // ↓
        case 126: self.onKey(0, 1, NO); return;    // ↑
        case 53: self.onKey(0, 0, YES); return;    // Esc
        default: [super keyDown:e];
    }
}

// Drag & Drop aus dem Finder
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info {
    return [info.draggingPasteboard canReadObjectForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}]
        ? NSDragOperationCopy : NSDragOperationNone;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info {
    NSArray<NSURL*>* urls = [info.draggingPasteboard readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    if (!urls.count || !self.onDropFile) return NO;
    for (NSURL* u in urls) self.onDropFile(u.path);
    return YES;
}

// Trackpad: Zwei-Finger-Scroll verschiebt, Pinch zoomt.
- (void)scrollWheel:(NSEvent*)e {
    _pan.x += e.scrollingDeltaX;
    _pan.y -= e.scrollingDeltaY;
    self.needsDisplay = YES;
}

- (void)magnifyWithEvent:(NSEvent*)e {
    _zoom = std::clamp(_zoom * (1 + e.magnification), 0.2, 20.0);
    self.needsDisplay = YES;
}

- (void)smartMagnifyWithEvent:(NSEvent*)e {
    _zoom = 1; _pan = NSZeroPoint;
    self.needsDisplay = YES;
}

@end
