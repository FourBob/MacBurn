// SPDX-License-Identifier: GPL-3.0-or-later
#import "MainWindow.h"
#import "CanvasView.h"
#import "InspectorView.h"
#import "Bridge.hpp"
#import <objc/runtime.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

static NSToolbarItemIdentifier const kPhase = @"phase";
static NSToolbarItemIdentifier const kEstimate = @"estimate";
static NSToolbarItemIdentifier const kPrimary = @"primary";

@implementation MainWindow {
    mb::AppController* _core;
    std::vector<mb::LayerRow> _rows;
    NSSegmentedControl* _phase;
    NSTextField* _estimate;
    NSButton* _primary;
    NSTextField* _machineKicker, *_machineSub, *_materialKicker, *_materialSub;
    NSPopUpButton* _machinePopup, *_materialPopup;
    NSTableView* _layers;
    CanvasView* _canvas;
    InspectorView* _inspector;
    NSTextField* _status;
    BOOL _refreshQueued;
}

- (instancetype)initWithCore:(mb::AppController*)core {
    self = [super initWithContentRect:NSMakeRect(0, 0, 1280, 800)
                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable |
                                      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskFullSizeContentView
                              backing:NSBackingStoreBuffered defer:NO];
    if (!self) return nil;
    _core = core;
    self.title = @"Unbenannt";
    self.subtitle = @"MacBurn";
    self.minSize = NSMakeSize(980, 600);
    self.releasedWhenClosed = NO;
    [self center];
    self.frameAutosaveName = @"MainWindow";
    self.delegate = self;

    NSToolbar* tb = [[NSToolbar alloc] initWithIdentifier:@"main"];
    tb.delegate = self;
    tb.displayMode = NSToolbarDisplayModeIconOnly;
    self.toolbar = tb;
    self.toolbarStyle = NSWindowToolbarStyleUnified;

    [self buildContent];

    __weak MainWindow* weakSelf = self;
    _core->setMainQueue([](std::function<void()> fn) { dispatch_async(dispatch_get_main_queue(), ^{ fn(); }); });
    _core->setOnChange([weakSelf] { [weakSelf queueRefresh]; });
    _core->setOnStatus([weakSelf] { [weakSelf refreshStatus]; });
    [self refresh];
    return self;
}

// ---------- Aufbau ----------

static NSTextField* small(NSString* s, BOOL secondary) {
    NSTextField* l = [NSTextField labelWithString:s];
    l.font = [NSFont systemFontOfSize:secondary ? 11 : 12 weight:secondary ? NSFontWeightRegular : NSFontWeightSemibold];
    if (secondary) l.textColor = NSColor.secondaryLabelColor;
    l.lineBreakMode = NSLineBreakByTruncatingTail;
    return l;
}

- (NSView*)cardWithKicker:(NSTextField*)kicker popup:(NSPopUpButton*)popup subtitle:(NSTextField*)sub {
    NSStackView* s = [NSStackView stackViewWithViews:@[kicker, popup, sub]];
    s.orientation = NSUserInterfaceLayoutOrientationVertical;
    s.alignment = NSLayoutAttributeLeading;
    s.spacing = 3;
    [popup.widthAnchor constraintEqualToAnchor:s.widthAnchor].active = YES;
    return s;
}

- (void)buildContent {
    // Seitenleiste
    _machineKicker = small(@"MASCHINE", YES);
    _machinePopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _machinePopup.target = self; _machinePopup.action = @selector(machineChanged:);
    _machineSub = small(@"", YES);
    _materialKicker = small(@"MATERIAL", YES);
    _materialPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _materialPopup.target = self; _materialPopup.action = @selector(materialChanged:);
    _materialSub = small(@"", YES);
    NSTextField* arrow = small(@"↓", YES);

    NSTextField* layersTitle = small(@"EBENEN", YES);
    _layers = [[NSTableView alloc] init];
    NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:@"layer"];
    [_layers addTableColumn:col];
    _layers.headerView = nil;
    _layers.rowHeight = 40;
    _layers.style = NSTableViewStyleSourceList;
    _layers.dataSource = self;
    _layers.delegate = self;
    NSScrollView* layerScroll = [[NSScrollView alloc] init];
    layerScroll.documentView = _layers;
    layerScroll.drawsBackground = NO;
    layerScroll.hasVerticalScroller = YES;

    NSStackView* side = [NSStackView stackViewWithViews:@[
        [self cardWithKicker:_machineKicker popup:_machinePopup subtitle:_machineSub], arrow,
        [self cardWithKicker:_materialKicker popup:_materialPopup subtitle:_materialSub], layersTitle, layerScroll]];
    side.orientation = NSUserInterfaceLayoutOrientationVertical;
    side.alignment = NSLayoutAttributeLeading;
    side.spacing = 8;
    side.edgeInsets = NSEdgeInsetsMake(12, 12, 12, 12);
    [side setCustomSpacing:18 afterView:side.arrangedSubviews[2]];
    for (NSView* v in side.arrangedSubviews) if (v != arrow && v != layersTitle)
        [v.widthAnchor constraintEqualToAnchor:side.widthAnchor constant:-24].active = YES;

    NSVisualEffectView* sideBg = [[NSVisualEffectView alloc] init];
    sideBg.material = NSVisualEffectMaterialSidebar;
    [self pin:side into:sideBg top:52];

    // Mitte und rechts
    _canvas = [[CanvasView alloc] initWithFrame:NSZeroRect];
    __weak MainWindow* weakSelf = self;
    _canvas.onDropFile = ^(NSString* path) { [weakSelf openPath:path]; };
    mb::AppController* core = _core;
    _canvas.onKey = ^(int dx, int dy, BOOL shift, BOOL escape, BOOL del) {
        if (escape) {
            if (core->jobRunning()) core->stop();
            else core->clearSelection();
        } else if (del) {
            core->deleteSelection();
        } else if (core->phase() == mb::Phase::Laser && core->deviceConnected()) {
            core->jog(dx, dy);
        } else {
            double step = shift ? 10 : 1;
            core->nudge(dx * step, dy * step);
        }
    };
    _canvas.onPointer = ^(int phase, double x, double y, double tol, BOOL shift) {
        if (phase == 0) core->pointerDown({x, y}, tol, shift);
        else if (phase == 1) core->pointerDrag({x, y});
        else core->pointerUp();
    };
    _inspector = [[InspectorView alloc] initWithCore:_core];
    NSScrollView* inspScroll = [[NSScrollView alloc] init];
    inspScroll.drawsBackground = NO;
    inspScroll.hasVerticalScroller = YES;
    NSClipView* clip = [[NSClipView alloc] init];
    clip.drawsBackground = NO;
    inspScroll.contentView = clip;
    _inspector.translatesAutoresizingMaskIntoConstraints = NO;
    inspScroll.documentView = _inspector;
    [_inspector.widthAnchor constraintEqualToAnchor:clip.widthAnchor].active = YES;
    NSVisualEffectView* inspBg = [[NSVisualEffectView alloc] init];
    inspBg.material = NSVisualEffectMaterialSidebar;
    [self pin:inspScroll into:inspBg top:52];

    NSSplitView* split = [[NSSplitView alloc] init];
    split.vertical = YES;
    split.dividerStyle = NSSplitViewDividerStyleThin;
    [split addArrangedSubview:sideBg];
    [split addArrangedSubview:_canvas];
    [split addArrangedSubview:inspBg];
    [sideBg.widthAnchor constraintGreaterThanOrEqualToConstant:230].active = YES;
    [inspBg.widthAnchor constraintGreaterThanOrEqualToConstant:280].active = YES;
    [split setHoldingPriority:NSLayoutPriorityDefaultHigh forSubviewAtIndex:0];
    [split setHoldingPriority:NSLayoutPriorityDefaultHigh forSubviewAtIndex:2];

    // Statusleiste
    _status = small(@"", YES);
    NSView* statusBar = [[NSView alloc] init];
    _status.translatesAutoresizingMaskIntoConstraints = NO;
    [statusBar addSubview:_status];

    NSView* root = [[NSView alloc] init];
    for (NSView* v in @[split, statusBar]) { v.translatesAutoresizingMaskIntoConstraints = NO; [root addSubview:v]; }
    [NSLayoutConstraint activateConstraints:@[
        [split.topAnchor constraintEqualToAnchor:root.topAnchor],
        [split.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [split.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [split.bottomAnchor constraintEqualToAnchor:statusBar.topAnchor],
        [statusBar.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [statusBar.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [statusBar.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
        [statusBar.heightAnchor constraintEqualToConstant:26],
        [_status.leadingAnchor constraintEqualToAnchor:statusBar.leadingAnchor constant:12],
        [_status.trailingAnchor constraintLessThanOrEqualToAnchor:statusBar.trailingAnchor constant:-12],
        [_status.centerYAnchor constraintEqualToAnchor:statusBar.centerYAnchor],
    ]];
    self.contentView = root;
    [split setPosition:250 ofDividerAtIndex:0];
    [split setPosition:1280 - 300 ofDividerAtIndex:1];
}

- (void)pin:(NSView*)child into:(NSView*)parent top:(CGFloat)top {
    child.translatesAutoresizingMaskIntoConstraints = NO;
    [parent addSubview:child];
    [NSLayoutConstraint activateConstraints:@[
        [child.topAnchor constraintEqualToAnchor:parent.topAnchor constant:top],
        [child.leadingAnchor constraintEqualToAnchor:parent.leadingAnchor],
        [child.trailingAnchor constraintEqualToAnchor:parent.trailingAnchor],
        [child.bottomAnchor constraintEqualToAnchor:parent.bottomAnchor],
    ]];
}

// ---------- Werkzeugleiste ----------

- (NSArray<NSToolbarItemIdentifier>*)toolbarDefaultItemIdentifiers:(NSToolbar*)tb {
    return @[NSToolbarFlexibleSpaceItemIdentifier, kPhase, NSToolbarFlexibleSpaceItemIdentifier, kEstimate, kPrimary];
}
- (NSArray<NSToolbarItemIdentifier>*)toolbarAllowedItemIdentifiers:(NSToolbar*)tb {
    return [self toolbarDefaultItemIdentifiers:tb];
}

- (NSToolbarItem*)toolbar:(NSToolbar*)tb itemForItemIdentifier:(NSToolbarItemIdentifier)ident willBeInsertedIntoToolbar:(BOOL)flag {
    NSToolbarItem* it = [[NSToolbarItem alloc] initWithItemIdentifier:ident];
    if ([ident isEqual:kPhase]) {
        _phase = [NSSegmentedControl segmentedControlWithLabels:@[@"Gestalten", @"Vorbereiten", @"Lasern"]
                                                   trackingMode:NSSegmentSwitchTrackingSelectOne
                                                         target:self action:@selector(phaseChanged:)];
        _phase.selectedSegment = 0;
        it.view = _phase;
        it.label = @"Phase";
    } else if ([ident isEqual:kEstimate]) {
        _estimate = [NSTextField labelWithString:@"⏱ 00:00"];
        _estimate.font = [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightMedium];
        _estimate.toolTip = @"Geschätzte Laufzeit";
        it.view = _estimate;
        it.label = @"Laufzeit";
    } else if ([ident isEqual:kPrimary]) {
        _primary = [NSButton buttonWithTitle:@"Weiter zum Lasern" target:self action:@selector(primaryPressed:)];
        _primary.bezelColor = NSColor.controlAccentColor;
        // Keine Eingabetaste: ↩ bestätigt Werte im Inspektor und darf nicht zum Lasern springen.
        it.view = _primary;
        it.label = @"Lasern";
    }
    return it;
}

// ---------- Aktionen → Kern ----------

- (void)phaseChanged:(NSSegmentedControl*)s { _core->setPhase(mb::Phase(s.selectedSegment)); }
- (void)machineChanged:(NSPopUpButton*)p { _core->selectMachine(int(p.indexOfSelectedItem)); }
- (void)materialChanged:(NSPopUpButton*)p { _core->selectMaterial(int(p.selectedItem.tag)); }
- (void)primaryPressed:(id)sender {
    switch (_core->primaryAction()) {
        case mb::PrimaryAction::GoToLaser: _core->setPhase(mb::Phase::Laser); break;
        case mb::PrimaryAction::Connect: _core->connect(); break;
        case mb::PrimaryAction::Connecting: break;
        case mb::PrimaryAction::StartJob: [self showPreflight]; break;
        case mb::PrimaryAction::Pause:
        case mb::PrimaryAction::Resume: _core->pauseOrResume(); break;
        case mb::PrimaryAction::Unlock: _core->unlock(); break;
    }
}

// ---------- Startprüfung ----------

- (void)showPreflight {
    auto checks = _core->preflight();
    NSPanel* sheet = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 460, 100) styleMask:NSWindowStyleMaskTitled
                                                  backing:NSBackingStoreBuffered defer:NO];
    NSStackView* stack = [NSStackView stackViewWithViews:@[]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 10;
    stack.edgeInsets = NSEdgeInsetsMake(20, 22, 20, 22);

    NSTextField* title = [NSTextField labelWithString:@"Bereit zum Lasern?"];
    title.font = [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];
    [stack addArrangedSubview:title];

    NSMutableArray<NSButton*>* boxes = [NSMutableArray array];
    BOOL blocked = NO;
    NSButton* start = [NSButton buttonWithTitle:@"Starten" target:nil action:nil];
    for (const auto& c : checks) {
        NSTextField* detail = [NSTextField wrappingLabelWithString:NS(c.detail)];
        detail.font = [NSFont systemFontOfSize:11];
        detail.textColor = NSColor.secondaryLabelColor;
        NSView* head;
        if (c.automatic) {
            NSTextField* t = [NSTextField labelWithString:[NSString stringWithFormat:@"%@  %@", c.ok ? @"✓" : @"⚠", NS(c.text)]];
            t.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
            t.textColor = c.ok ? NSColor.labelColor : NSColor.systemOrangeColor;
            head = t;
            // Nicht bereit oder außerhalb der Fläche: Start nicht möglich. Material-Hinweise nur warnen.
            if (!c.ok && (c.text.rfind("Design", 0) == 0 || c.text.rfind("Laser", 0) == 0)) blocked = YES;
        } else {
            NSButton* box = [NSButton checkboxWithTitle:NS(c.text) target:nil action:nil];
            box.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
            [boxes addObject:box];
            head = box;
        }
        NSStackView* row = [NSStackView stackViewWithViews:@[head, detail]];
        row.orientation = NSUserInterfaceLayoutOrientationVertical;
        row.alignment = NSLayoutAttributeLeading;
        row.spacing = 2;
        [detail.widthAnchor constraintLessThanOrEqualToConstant:410].active = YES;
        [stack addArrangedSubview:row];
    }

    NSButton* cancel = [NSButton buttonWithTitle:@"Abbrechen" target:nil action:nil];
    cancel.keyEquivalent = @"\033";
    start.keyEquivalent = @"\r";
    start.enabled = NO;
    NSStackView* buttons = [NSStackView stackViewWithViews:@[cancel, start]];
    [stack addArrangedSubview:buttons];
    [buttons.trailingAnchor constraintEqualToAnchor:stack.trailingAnchor constant:-22].active = YES;

    // Starten erst, wenn alles abgehakt ist
    void (^update)(void) = ^{
        BOOL all = !blocked;
        for (NSButton* b in boxes) all = all && b.state == NSControlStateValueOn;
        start.enabled = all;
    };
    for (NSButton* b in boxes) { b.target = self; b.action = @selector(preflightToggled:); }
    objc_setAssociatedObject(self, "preflightUpdate", update, OBJC_ASSOCIATION_COPY);

    stack.translatesAutoresizingMaskIntoConstraints = NO;
    sheet.contentView = [[NSView alloc] init];
    [sheet.contentView addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:sheet.contentView.topAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:sheet.contentView.bottomAnchor],
        [stack.leadingAnchor constraintEqualToAnchor:sheet.contentView.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:sheet.contentView.trailingAnchor],
        [stack.widthAnchor constraintEqualToConstant:460],
    ]];

    __weak NSPanel* weakSheet = sheet;
    __weak MainWindow* weakSelf = self;
    cancel.target = self; cancel.action = @selector(preflightCancel:);
    start.target = self; start.action = @selector(preflightStart:);
    objc_setAssociatedObject(self, "preflightSheet", sheet, OBJC_ASSOCIATION_RETAIN);
    [self beginSheet:sheet completionHandler:^(NSModalResponse r) {
        MainWindow* strongSelf = weakSelf;
        if (!strongSelf) return;
        objc_setAssociatedObject(strongSelf, "preflightSheet", nil, OBJC_ASSOCIATION_RETAIN);
        if (r == NSModalResponseOK) strongSelf->_core->startJob();
        (void)weakSheet;
    }];
}

- (void)preflightToggled:(id)sender {
    void (^update)(void) = objc_getAssociatedObject(self, "preflightUpdate");
    if (update) update();
}
- (void)preflightCancel:(id)sender {
    NSWindow* s = objc_getAssociatedObject(self, "preflightSheet");
    if (s) [self endSheet:s returnCode:NSModalResponseCancel];
}
- (void)preflightStart:(id)sender {
    NSWindow* s = objc_getAssociatedObject(self, "preflightSheet");
    if (s) [self endSheet:s returnCode:NSModalResponseOK];
}

- (void)tableViewSelectionDidChange:(NSNotification*)n {
    NSInteger row = _layers.selectedRow;
    if (row >= 0 && size_t(row) < _rows.size() && !_rows[size_t(row)].selected) _core->selectLayer(int(row));
}

// ---------- Dateien ----------

- (void)showError:(NSString*)title text:(const std::string&)text {
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = title;
    a.informativeText = NS(text);
    [a beginSheetModalForWindow:self completionHandler:nil];
}

- (void)openPath:(NSString*)path {
    void (^go)(void) = ^{
        auto r = self->_core->openFile(STD(path));
        if (r.ok) [NSDocumentController.sharedDocumentController noteNewRecentDocumentURL:[NSURL fileURLWithPath:path]];
        else [self showError:[NSString stringWithFormat:@"„%@“ lässt sich nicht öffnen", path.lastPathComponent] text:r.message];
    };
    if ([path.pathExtension.lowercaseString isEqual:@"macburn"])
        [self confirmUnsavedThen:^(BOOL proceed) { if (proceed) go(); }];
    else go();   // SVG kommt ins aktuelle Projekt
}

- (UTType*)projectType {
    return [UTType typeWithIdentifier:@"org.macburn.project"] ?: [UTType typeWithFilenameExtension:@"macburn"];
}

- (void)saveThen:(void (^)(BOOL saved))then asNew:(BOOL)asNew {
    if (!asNew && !_core->documentPath().empty()) {
        auto r = _core->saveProject(_core->documentPath());
        if (!r.ok) [self showError:@"Das Projekt lässt sich nicht sichern" text:r.message];
        if (then) then(r.ok);
        return;
    }
    NSSavePanel* panel = [NSSavePanel savePanel];
    if (UTType* t = [self projectType]) panel.allowedContentTypes = @[t];
    panel.nameFieldStringValue = [NS(_core->documentTitle()) stringByAppendingPathExtension:@"macburn"];
    [panel beginSheetModalForWindow:self completionHandler:^(NSModalResponse resp) {
        if (resp != NSModalResponseOK) { if (then) then(NO); return; }
        auto r = self->_core->saveProject(STD(panel.URL.path));
        if (r.ok) [NSDocumentController.sharedDocumentController noteNewRecentDocumentURL:panel.URL];
        else [self showError:@"Das Projekt lässt sich nicht sichern" text:r.message];
        if (then) then(r.ok);
    }];
}

- (void)confirmUnsavedThen:(void (^)(BOOL proceed))then {
    if (!_core->isModified()) { then(YES); return; }
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = [NSString stringWithFormat:@"Änderungen an „%@“ sichern?", NS(_core->documentTitle())];
    a.informativeText = @"Ohne Sichern gehen die Änderungen verloren.";
    [a addButtonWithTitle:@"Sichern …"];
    [a addButtonWithTitle:@"Abbrechen"];
    [a addButtonWithTitle:@"Nicht sichern"];
    a.buttons[2].hasDestructiveAction = YES;
    [a beginSheetModalForWindow:self completionHandler:^(NSModalResponse r) {
        if (r == NSAlertFirstButtonReturn) [self saveThen:then asNew:NO];
        else if (r == NSAlertThirdButtonReturn) then(YES);
        else then(NO);
    }];
}

// Menübefehle (über die Responder-Kette)
- (void)newDocumentCore:(id)sender {
    [self confirmUnsavedThen:^(BOOL proceed) { if (proceed) self->_core->newDocument(); }];
}
- (void)openDocument:(id)sender {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    if (UTType* t = [self projectType]) [types addObject:t];
    if (UTType* svg = [UTType typeWithFilenameExtension:@"svg"]) [types addObject:svg];
    panel.allowedContentTypes = types;
    [panel beginSheetModalForWindow:self completionHandler:^(NSModalResponse r) {
        if (r == NSModalResponseOK) [self openPath:panel.URL.path];
    }];
}
- (void)saveDocument:(id)sender { [self saveThen:nil asNew:NO]; }
- (void)saveDocumentAs:(id)sender { [self saveThen:nil asNew:YES]; }

- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (!_core->isModified()) return YES;
    [self confirmUnsavedThen:^(BOOL proceed) {
        if (proceed) [self close];
    }];
    return NO;
}

// ---------- Anzeige ----------

- (void)queueRefresh {
    if (_refreshQueued) return;
    _refreshQueued = YES;
    dispatch_async(dispatch_get_main_queue(), ^{ self->_refreshQueued = NO; [self refresh]; });
}

// Nur Statusleiste und Zeichenfläche (häufige Positionsmeldungen)
- (void)refreshStatus {
    _status.stringValue = NS(_core->statusText());
    if (_core->phase() == mb::Phase::Laser) [_canvas setRenderList:_core->renderList()];
}

- (void)refresh {
    self.title = NS(_core->documentTitle());
    self.representedURL = _core->documentPath().empty() ? nil : [NSURL fileURLWithPath:NS(_core->documentPath())];
    self.documentEdited = _core->isModified();
    _phase.selectedSegment = NSInteger(_core->phase());
    _estimate.stringValue = [@"⏱ " stringByAppendingString:NS(_core->estimateText())];
    _primary.title = NS(_core->primaryActionTitle());

    auto mc = _core->machineCard();
    [_machinePopup removeAllItems];
    for (const auto& i : _core->machineItems()) {
        [_machinePopup addItemWithTitle:NS(i.title)];
        if (i.selected) [_machinePopup selectItem:_machinePopup.lastItem];
    }
    _machineSub.stringValue = NS(mc.subtitle);

    auto matc = _core->materialCard();
    [_materialPopup removeAllItems];
    NSInteger idx = 0;
    for (const auto& i : _core->materialItems()) {
        if (i.separatorBefore) [_materialPopup.menu addItem:NSMenuItem.separatorItem];
        NSString* title = i.detail.empty() ? NS(i.title) : [NSString stringWithFormat:@"%@  %@", NS(i.title), NS(i.detail)];
        NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
        mi.tag = idx++;
        [_materialPopup.menu addItem:mi];
        if (i.selected) [_materialPopup selectItem:mi];
    }
    _materialSub.stringValue = NS(matc.subtitle);
    _materialSub.textColor = matc.subtitle.find("✓") != std::string::npos ? NSColor.systemGreenColor
                           : matc.subtitle.find("◇") != std::string::npos ? NSColor.systemTealColor
                           : matc.subtitle.find("✎") != std::string::npos ? NSColor.controlAccentColor : NSColor.systemOrangeColor;

    _rows = _core->layerRows();
    [_layers reloadData];
    for (size_t i = 0; i < _rows.size(); ++i)
        if (_rows[i].selected) [_layers selectRowIndexes:[NSIndexSet indexSetWithIndex:i] byExtendingSelection:NO];

    [_canvas setRenderList:_core->renderList()];
    [_inspector showFields:_core->inspector()];
    _status.stringValue = NS(_core->statusText());
}

// ---------- Ebenenliste ----------

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tv { return NSInteger(_rows.size()); }

- (NSView*)tableView:(NSTableView*)tv viewForTableColumn:(NSTableColumn*)col row:(NSInteger)row {
    const auto& r = _rows[size_t(row)];
    NSView* swatch = [[NSView alloc] init];
    swatch.wantsLayer = YES;
    swatch.layer.backgroundColor = NSCol(r.color).CGColor;
    swatch.layer.cornerRadius = 3;
    [swatch.widthAnchor constraintEqualToConstant:12].active = YES;
    [swatch.heightAnchor constraintEqualToConstant:12].active = YES;

    NSTextField* name = [NSTextField labelWithString:NS(r.name)];
    name.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
    if (!r.output) name.textColor = NSColor.tertiaryLabelColor;
    NSTextField* detail = [NSTextField labelWithString:[NSString stringWithFormat:@"%@ · %@%@", NS(r.intent), NS(r.values), r.modified ? @"  ✎" : @""]];
    detail.font = [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular];
    detail.textColor = r.modified ? NSColor.controlAccentColor : NSColor.secondaryLabelColor;

    NSStackView* text = [NSStackView stackViewWithViews:@[name, detail]];
    text.orientation = NSUserInterfaceLayoutOrientationVertical;
    text.alignment = NSLayoutAttributeLeading;
    text.spacing = 1;
    NSStackView* cell = [NSStackView stackViewWithViews:@[swatch, text]];
    cell.spacing = 8;
    cell.edgeInsets = NSEdgeInsetsMake(0, 4, 0, 4);
    return cell;
}

@end
