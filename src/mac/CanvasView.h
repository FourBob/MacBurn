// SPDX-License-Identifier: GPL-3.0-or-later
#import <Cocoa/Cocoa.h>
#include "render/RenderList.hpp"

// Zeichnet die RenderList des Kerns. Vorerst mit CoreGraphics;
// Metal (über metal-cpp) ersetzt nur diese Datei.
@interface CanvasView : NSView
- (void)setRenderList:(const mb::RenderList&)list;
@end
