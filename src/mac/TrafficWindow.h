// MacBurn – Fenster „Verbindungsverlauf“: was zwischen MacBurn und dem Laser hin- und hergeht.
// SPDX-License-Identifier: GPL-3.0-or-later
#import <Cocoa/Cocoa.h>

namespace mb { class AppController; }

@interface TrafficWindow : NSWindow <NSWindowDelegate>
- (instancetype)initWithCore:(mb::AppController*)core;
@end
