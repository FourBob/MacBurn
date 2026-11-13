// SPDX-License-Identifier: GPL-3.0-or-later
#import <Cocoa/Cocoa.h>
#include "render/RenderList.hpp"

// Zeichnet die RenderList des Kerns. Vorerst mit CoreGraphics;
// Metal (über metal-cpp) ersetzt nur diese Datei.
@interface CanvasView : NSView
- (void)setRenderList:(const mb::RenderList&)list;
// Wird aufgerufen, wenn eine Datei auf die Fläche gezogen wird.
@property (nonatomic, copy) void (^onDropFile)(NSString* path);
// Pfeiltasten (dx, dy) und Esc (0, 0 mit escape = YES)
@property (nonatomic, copy) void (^onKey)(int dx, int dy, BOOL escape);
@end
