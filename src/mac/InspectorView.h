// SPDX-License-Identifier: GPL-3.0-or-later
#import <Cocoa/Cocoa.h>
#include "app/AppController.hpp"
#include <vector>

// Generischer Baukasten: baut Bedienelemente aus der Feldliste des Kerns.
// Neue Inspektor-Inhalte entstehen im Kern, nicht hier.
@interface InspectorView : NSView
- (instancetype)initWithCore:(mb::AppController*)core;
- (void)showFields:(const std::vector<mb::Field>&)fields;
@end
