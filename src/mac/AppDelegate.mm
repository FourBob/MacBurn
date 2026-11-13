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
    for (NSString* p in _pending) [_window openPath:p];
    _pending = nil;
    [NSApp activate];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app { return YES; }

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)app {
    if (!_window || !_window.isVisible || !_core->isModified()) return NSTerminateNow;
    [_window confirmUnsavedThen:^(BOOL proceed) { [NSApp replyToApplicationShouldTerminate:proceed]; }];
    return NSTerminateLater;
}

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
    [file addItem:item(@"Öffnen …", @selector(openDocument:), @"o")];
    [file addItem:NSMenuItem.separatorItem];
    [file addItem:item(@"Sichern", @selector(saveDocument:), @"s")];
    [file addItem:item(@"Sichern unter …", @selector(saveDocumentAs:), @"s", NSEventModifierFlagCommand | NSEventModifierFlagShift)];
    [file addItem:NSMenuItem.separatorItem];
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
    [edit addItem:item(@"Auswahl aufheben", @selector(deselectAllCore:), @"a", NSEventModifierFlagCommand | NSEventModifierFlagShift)];
    [edit addItem:NSMenuItem.separatorItem];
    [edit addItem:item(@"Duplizieren", @selector(duplicateCore:), @"d")];
    [edit addItem:item(@"Löschen", @selector(deleteCore:), @"")];

    NSMenu* object = [[NSMenu alloc] initWithTitle:@"Objekt"];
    [object addItem:item(@"Um 90° drehen", @selector(rotateCore:), @"")];
    [object addItem:item(@"Horizontal spiegeln", @selector(flipHCore:), @"")];
    [object addItem:item(@"Vertikal spiegeln", @selector(flipVCore:), @"")];
    [object addItem:NSMenuItem.separatorItem];
    [object addItem:item(@"Auf der Fläche zentrieren", @selector(centerCore:), @"")];

    NSMenu* view = [[NSMenu alloc] initWithTitle:@"Darstellung"];
    NSArray* phases = @[@"Gestalten", @"Vorbereiten", @"Lasern"];
    for (NSInteger i = 0; i < 3; ++i) {
        NSMenuItem* p = item(phases[i], @selector(showPhase:), [NSString stringWithFormat:@"%ld", i + 1]);
        p.tag = i;
        [view addItem:p];
    }

    NSMenu* laser = [[NSMenu alloc] initWithTitle:@"Laser"];
    [laser addItem:item(@"Verbinden", @selector(laserConnect:), @"k")];
    [laser addItem:item(@"Trennen", @selector(laserDisconnect:), @"k", NSEventModifierFlagCommand | NSEventModifierFlagShift)];
    [laser addItem:NSMenuItem.separatorItem];
    [laser addItem:item(@"Referenzfahrt", @selector(laserHome:), @"h", NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    [laser addItem:item(@"Rahmen abfahren", @selector(laserFrame:), @"r")];
    [laser addItem:NSMenuItem.separatorItem];
    NSMenuItem* p0 = item(@"Position wie auf der Fläche", @selector(laserPosition:), @"");
    p0.tag = 0;
    NSMenuItem* p1 = item(@"Position am Laserkopf", @selector(laserPosition:), @"");
    p1.tag = 1;
    [laser addItem:p0];
    [laser addItem:p1];
    NSMenu* anchors = [[NSMenu alloc] initWithTitle:@"Anker"];
    for (int i = 0; i < 9; ++i) {
        NSMenuItem* ai = item(NS(std::string("Anker ") + mb::anchorName(i)), @selector(laserAnchor:), @"");
        ai.tag = i;
        [anchors addItem:ai];
    }
    NSMenuItem* anchorItem = [[NSMenuItem alloc] initWithTitle:@"Anker" action:nil keyEquivalent:@""];
    anchorItem.submenu = anchors;
    [laser addItem:anchorItem];
    [laser addItem:NSMenuItem.separatorItem];
    [laser addItem:item(@"Stopp", @selector(laserStop:), @".")];

    NSMenu* window = [[NSMenu alloc] initWithTitle:@"Fenster"];
    [window addItem:item(@"Im Dock ablegen", @selector(performMiniaturize:), @"m")];
    NSApp.windowsMenu = window;

    for (NSMenu* m in @[app, file, edit, object, view, laser, window]) {
        NSMenuItem* top = [[NSMenuItem alloc] initWithTitle:m.title action:nil keyEquivalent:@""];
        top.submenu = m;
        [bar addItem:top];
    }
    NSApp.mainMenu = bar;
}

- (BOOL)validateMenuItem:(NSMenuItem*)mi {
    if (mi.action == @selector(undoCore:)) { mi.title = NS(_core->undoTitle()); return _core->canUndo(); }
    if (mi.action == @selector(redoCore:)) { mi.title = NS(_core->redoTitle()); return _core->canRedo(); }
    if (mi.action == @selector(duplicateCore:) || mi.action == @selector(deleteCore:) || mi.action == @selector(deselectAllCore:) ||
        mi.action == @selector(rotateCore:) || mi.action == @selector(flipHCore:) || mi.action == @selector(flipVCore:) ||
        mi.action == @selector(centerCore:))
        return _core->hasSelection() && _core->canEdit();
    if (mi.action == @selector(selectAll:)) return _core->canEdit();
    if (mi.action == @selector(laserConnect:)) return !_core->deviceConnected();
    if (mi.action == @selector(laserDisconnect:)) return _core->deviceConnected() && !_core->jobRunning();
    if (mi.action == @selector(laserHome:) || mi.action == @selector(laserFrame:)) return _core->deviceConnected() && !_core->jobRunning();
    if (mi.action == @selector(laserStop:)) return _core->deviceConnected();
    if (mi.action == @selector(laserPosition:)) { mi.state = mi.tag == _core->positionMode() ? NSControlStateValueOn : NSControlStateValueOff; return !_core->jobRunning(); }
    if (mi.action == @selector(laserAnchor:)) { mi.state = mi.tag == _core->anchor() ? NSControlStateValueOn : NSControlStateValueOff; return _core->positionMode() == 1 && !_core->jobRunning(); }
    if (mi.action == @selector(showPhase:)) { mi.state = mi.tag == NSInteger(_core->phase()) ? NSControlStateValueOn : NSControlStateValueOff; }
    return YES;
}

- (void)undoCore:(id)sender { _core->undo(); }
- (void)redoCore:(id)sender { _core->redo(); }
- (void)showPhase:(NSMenuItem*)sender { _core->setPhase(mb::Phase(sender.tag)); }


// Dateibefehle auch dann erreichbar, wenn das Fenster gerade nicht vorne ist
- (void)newDocumentCore:(id)sender { [_window newDocumentCore:sender]; }
- (void)openDocument:(id)sender { [_window openDocument:sender]; }
- (void)saveDocument:(id)sender { [_window saveDocument:sender]; }
- (void)saveDocumentAs:(id)sender { [_window saveDocumentAs:sender]; }

- (void)selectAll:(id)sender { _core->selectAll(); }
- (void)deselectAllCore:(id)sender { _core->clearSelection(); }
- (void)duplicateCore:(id)sender { _core->duplicateSelection(); }
- (void)deleteCore:(id)sender { _core->deleteSelection(); }
- (void)rotateCore:(id)sender { _core->rotateSelection(-90); }
- (void)flipHCore:(id)sender { _core->flipSelection(true); }
- (void)flipVCore:(id)sender { _core->flipSelection(false); }
- (void)centerCore:(id)sender { _core->centerSelection(); }

- (void)laserConnect:(id)sender { _core->setPhase(mb::Phase::Laser); _core->connect(); }
- (void)laserDisconnect:(id)sender { _core->disconnect(); }
- (void)laserHome:(id)sender { _core->home(); }
- (void)laserFrame:(id)sender { _core->frame(); }
- (void)laserStop:(id)sender { _core->stop(); }
- (void)laserPosition:(NSMenuItem*)sender { _core->setChoice("origin", int(sender.tag)); }
- (void)laserAnchor:(NSMenuItem*)sender { _core->setChoice("anchor", int(sender.tag)); }

- (void)importFile:(id)sender {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    UTType* svg = [UTType typeWithFilenameExtension:@"svg"];
    if (svg) panel.allowedContentTypes = @[svg];
    panel.allowsMultipleSelection = YES;
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r != NSModalResponseOK) return;
        for (NSURL* u in panel.URLs) [self->_window openPath:u.path];
    }];
}

// Datei auf das Dock-Symbol oder per „Öffnen mit“
- (BOOL)application:(NSApplication*)app openFile:(NSString*)path {
    if (_window) [_window openPath:path];
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
