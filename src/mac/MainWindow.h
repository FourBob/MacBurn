// SPDX-License-Identifier: GPL-3.0-or-later
#import <Cocoa/Cocoa.h>
#include "app/AppController.hpp"

// Hauptfenster: Werkzeugleiste, Seitenleiste (Maschine → Material, Ebenen),
// Zeichenfläche, Inspektor, Statusleiste. Zeigt nur an, was der Kern liefert.
@interface MainWindow : NSWindow <NSToolbarDelegate, NSTableViewDataSource, NSTableViewDelegate>
- (instancetype)initWithCore:(mb::AppController*)core;
- (void)importPath:(NSString*)path;
@end
