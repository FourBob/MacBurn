// MacBurn – App-Loop und Menüleiste. Menübefehle gehen direkt an den Kern.
// SPDX-License-Identifier: GPL-3.0-or-later
#import "AppDelegate.h"
#import "MainWindow.h"
#import "Bridge.hpp"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "app/AppController.hpp"
#include <memory>

@implementation AppDelegate {
    std::unique_ptr<mb::AppController> _core;
    MainWindow* _window;
    NSMutableArray<NSString*>* _pending;   // Dateien, die vor dem Fenster ankommen
}

- (void)applicationDidFinishLaunching:(NSNotification*)note {
    NSString* system = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"profiles"];
    NSURL* support = [NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
    NSString* user = [[support.path stringByAppendingPathComponent:@"MacBurn"] stringByAppendingPathComponent:@"profiles"];
    _core = std::make_unique<mb::AppController>(STD(system), STD(user));

    [self buildMenu];
    _window = [[MainWindow alloc] initWithCore:_core.get()];
    [_window makeKeyAndOrderFront:nil];
    for (NSString* p in _pending) [_window importPath:p];
    _pending = nil;
    [NSApp activate];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app { return YES; }

// ---------- Menü ----------

static NSMenuItem* item(NSString* title, SEL action, NSString* key, NSEventModifierFlags mods = NSEventModifierFlagCommand) {
    NSMenuItem* i = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
    i.keyEquivalentModifierMask = mods;
    return i;
}

- (void)buildMenu {
    NSMenu* bar = [[NSMenu alloc] init];

    NSMenu* app = [[NSMenu alloc] initWithTitle:@"MacBurn"];
    [app addItem:item(@"Über MacBurn", @selector(orderFrontStandardAboutPanel:), @"")];
    [app addItem:NSMenuItem.separatorItem];
    [app addItem:item(@"MacBurn ausblenden", @selector(hide:), @"h")];
    [app addItem:item(@"MacBurn beenden", @selector(terminate:), @"q")];

    NSMenu* file = [[NSMenu alloc] initWithTitle:@"Ablage"];
    [file addItem:item(@"Neu", @selector(newDocumentCore:), @"n")];
    [file addItem:item(@"Importieren …", @selector(importFile:), @"i")];
    [file addItem:NSMenuItem.separatorItem];
    [file addItem:item(@"G-Code exportieren …", @selector(exportGcode:), @"e", NSEventModifierFlagCommand | NSEventModifierFlagShift)];
    [file addItem:NSMenuItem.separatorItem];
    [file addItem:item(@"Fenster schließen", @selector(performClose:), @"w")];

    NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Bearbeiten"];
    [edit addItem:item(@"Widerrufen", @selector(undoCore:), @"z")];
    [edit addItem:item(@"Wiederholen", @selector(redoCore:), @"z", NSEventModifierFlagCommand | NSEventModifierFlagShift)];
    [edit addItem:NSMenuItem.separatorItem];
    [edit addItem:item(@"Ausschneiden", @selector(cut:), @"x")];
    [edit addItem:item(@"Kopieren", @selector(copy:), @"c")];
    [edit addItem:item(@"Einsetzen", @selector(paste:), @"v")];
    [edit addItem:item(@"Alles auswählen", @selector(selectAll:), @"a")];

    NSMenu* view = [[NSMenu alloc] initWithTitle:@"Darstellung"];
    NSArray* phases = @[@"Gestalten", @"Vorbereiten", @"Lasern"];
    for (NSInteger i = 0; i < 3; ++i) {
        NSMenuItem* p = item(phases[i], @selector(showPhase:), [NSString stringWithFormat:@"%ld", i + 1]);
        p.tag = i;
        [view addItem:p];
    }

    NSMenu* window = [[NSMenu alloc] initWithTitle:@"Fenster"];
    [window addItem:item(@"Im Dock ablegen", @selector(performMiniaturize:), @"m")];
    NSApp.windowsMenu = window;

    for (NSMenu* m in @[app, file, edit, view, window]) {
        NSMenuItem* top = [[NSMenuItem alloc] initWithTitle:m.title action:nil keyEquivalent:@""];
        top.submenu = m;
        [bar addItem:top];
    }
    NSApp.mainMenu = bar;
}

- (BOOL)validateMenuItem:(NSMenuItem*)mi {
    if (mi.action == @selector(undoCore:)) { mi.title = NS(_core->undoTitle()); return _core->canUndo(); }
    if (mi.action == @selector(redoCore:)) { mi.title = NS(_core->redoTitle()); return _core->canRedo(); }
    if (mi.action == @selector(showPhase:)) { mi.state = mi.tag == NSInteger(_core->phase()) ? NSControlStateValueOn : NSControlStateValueOff; }
    return YES;
}

- (void)undoCore:(id)sender { _core->undo(); }
- (void)redoCore:(id)sender { _core->redo(); }
- (void)showPhase:(NSMenuItem*)sender { _core->setPhase(mb::Phase(sender.tag)); }

- (void)newDocumentCore:(id)sender { _core->newDocument(); }

- (void)importFile:(id)sender {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    UTType* svg = [UTType typeWithFilenameExtension:@"svg"];
    if (svg) panel.allowedContentTypes = @[svg];
    panel.allowsMultipleSelection = YES;
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r != NSModalResponseOK) return;
        for (NSURL* u in panel.URLs) [self->_window importPath:u.path];
    }];
}

// Datei auf das Dock-Symbol oder per „Öffnen mit“
- (BOOL)application:(NSApplication*)app openFile:(NSString*)path {
    if (_window) [_window importPath:path];
    else { if (!_pending) _pending = [NSMutableArray array]; [_pending addObject:path]; }
    return YES;
}

- (void)exportGcode:(id)sender {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = @"Auftrag.gcode";
    UTType* gcode = [UTType typeWithFilenameExtension:@"gcode"];
    if (gcode) panel.allowedContentTypes = @[gcode];
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r != NSModalResponseOK) return;
        NSString* text = NS(self->_core->exportGcode());
        NSError* err = nil;
        if (![text writeToURL:panel.URL atomically:YES encoding:NSUTF8StringEncoding error:&err])
            [[NSAlert alertWithError:err] beginSheetModalForWindow:self->_window completionHandler:nil];
    }];
}

@end
