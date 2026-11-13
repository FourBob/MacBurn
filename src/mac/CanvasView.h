// SPDX-License-Identifier: GPL-3.0-or-later
#import <Cocoa/Cocoa.h>
#include "render/RenderList.hpp"

// Zeichnet die RenderList des Kerns. Vorerst mit CoreGraphics;
// Metal (über metal-cpp) ersetzt nur diese Datei.
@interface CanvasView : NSView
- (void)setRenderList:(const mb::RenderList&)list;
// Wird aufgerufen, wenn eine Datei auf die Fläche gezogen wird.
@property (nonatomic, copy) void (^onDropFile)(NSString* path);
// Pfeiltasten (dx, dy, shift), Esc, Löschtaste
@property (nonatomic, copy) void (^onKey)(int dx, int dy, BOOL shift, BOOL escape, BOOL del);
// Maus in mm: phase 0 = gedrückt, 1 = gezogen, 2 = losgelassen; tolerance in mm
// Bildschirmpunkte je mm (ändert sich mit Zoom und Fenstergröße)
@property (nonatomic, copy) void (^onViewScale)(double pxPerMm);
// Mausbewegung ohne gedrückte Taste (mm), Doppelklick (mm), Zeichen- und Eingabetaste
@property (nonatomic, copy) void (^onMove)(double x, double y);
@property (nonatomic, copy) void (^onDoubleClick)(double x, double y);
@property (nonatomic, copy) BOOL (^onCharacter)(NSString* chars);
@property (nonatomic, copy) BOOL (^onReturn)(void);
// Fadenkreuz statt Pfeil (Zeichenwerkzeug aktiv)
@property (nonatomic) BOOL crosshair;
@property (nonatomic, copy) void (^onPointer)(int phase, double x, double y, double tolerance, BOOL shift);
@end
