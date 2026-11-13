// SPDX-License-Identifier: GPL-3.0-or-later
#import <Cocoa/Cocoa.h>
#include "app/AppController.hpp"

// Hauptfenster: Werkzeugleiste, Seitenleiste (Maschine → Material, Ebenen),
// Zeichenfläche, Inspektor, Statusleiste. Zeigt nur an, was der Kern liefert.
@interface MainWindow : NSWindow <NSToolbarDelegate, NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate>
- (instancetype)initWithCore:(mb::AppController*)core;
- (void)openPath:(NSString*)path;   // .macburn öffnen oder SVG importieren
// Fragt bei ungesicherten Änderungen nach; then(YES) = weitermachen
- (void)confirmUnsavedThen:(void (^)(BOOL proceed))then;
- (void)newDocumentCore:(id)sender;
- (void)openDocument:(id)sender;
- (void)saveDocument:(id)sender;
- (void)saveDocumentAs:(id)sender;
@end
