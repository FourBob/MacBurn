// SPDX-License-Identifier: GPL-3.0-or-later
#import "TrafficWindow.h"
#import "Bridge.hpp"

#include "app/AppController.hpp"

static const NSUInteger kMaxLines = 4000;

@implementation TrafficWindow {
    mb::AppController* _core;
    NSTextView* _text;
    NSButton* _showStatus;
    NSButton* _follow;
    NSTextField* _input;
    NSTextField* _hint;
    NSTimer* _timer;
    uint64_t _seq;
    NSUInteger _lines;
    NSDateFormatter* _clock;
    NSMutableArray<NSString*>* _history;   // gesendete Befehle (Pfeiltasten)
    NSInteger _historyPos;
}

- (instancetype)initWithCore:(mb::AppController*)core {
    self = [super initWithContentRect:NSMakeRect(0, 0, 620, 440)
                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                      NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                              backing:NSBackingStoreBuffered defer:NO];
    if (!self) return nil;
    _core = core;
    _history = [NSMutableArray array];
    self.title = @"Verbindungsverlauf";
    self.releasedWhenClosed = NO;
    self.delegate = self;
    self.minSize = NSMakeSize(420, 240);
    self.frameAutosaveName = @"Verbindungsverlauf";
    _clock = [[NSDateFormatter alloc] init];
    _clock.dateFormat = @"HH:mm:ss.SSS";

    NSView* root = self.contentView;

    _showStatus = [NSButton checkboxWithTitle:@"Statusabfragen zeigen" target:self action:@selector(reload:)];
    _follow = [NSButton checkboxWithTitle:@"Mitlaufen" target:nil action:nil];
    _follow.state = NSControlStateValueOn;
    NSButton* clear = [NSButton buttonWithTitle:@"Leeren" target:self action:@selector(clear:)];
    NSStackView* top = [NSStackView stackViewWithViews:@[_showStatus, _follow, [[NSView alloc] init], clear]];
    top.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    top.spacing = 14;
    [top.views[2] setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSScrollView* scroll = [NSTextView scrollableTextView];
    _text = scroll.documentView;
    _text.editable = NO;
    _text.richText = NO;
    _text.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    _text.textContainerInset = NSMakeSize(4, 4);
    scroll.hasHorizontalScroller = NO;

    _input = [NSTextField textFieldWithString:@""];
    _input.placeholderString = @"Befehl an den Laser, z. B. $$ oder $I – Eingabetaste sendet";
    _input.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    _input.target = self;
    _input.action = @selector(send:);
    _input.delegate = (id<NSTextFieldDelegate>)self;
    NSButton* send = [NSButton buttonWithTitle:@"Senden" target:self action:@selector(send:)];
    NSStackView* bottom = [NSStackView stackViewWithViews:@[_input, send]];
    bottom.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    [_input setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];

    _hint = [NSTextField labelWithString:@"→ an den Laser   ← vom Laser   ·   Befehle von Hand können den Kopf bewegen."];
    _hint.font = [NSFont systemFontOfSize:11];
    _hint.textColor = NSColor.secondaryLabelColor;

    for (NSView* v in @[top, scroll, bottom, _hint]) { v.translatesAutoresizingMaskIntoConstraints = NO; [root addSubview:v]; }
    [NSLayoutConstraint activateConstraints:@[
        [top.topAnchor constraintEqualToAnchor:root.topAnchor constant:10],
        [top.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:12],
        [top.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-12],
        [scroll.topAnchor constraintEqualToAnchor:top.bottomAnchor constant:8],
        [scroll.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [bottom.topAnchor constraintEqualToAnchor:scroll.bottomAnchor constant:8],
        [bottom.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:12],
        [bottom.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-12],
        [_hint.topAnchor constraintEqualToAnchor:bottom.bottomAnchor constant:6],
        [_hint.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:12],
        [_hint.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-12],
        [_hint.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-8],
    ]];
    [self center];
    return self;
}

- (void)makeKeyAndOrderFront:(id)sender {
    [super makeKeyAndOrderFront:sender];
    [self start];
    [self makeFirstResponder:_input];
}

- (void)start {
    if (_timer) return;
    [self reload:nil];
    __weak TrafficWindow* weak = self;
    _timer = [NSTimer scheduledTimerWithTimeInterval:0.25 repeats:YES block:^(NSTimer*) { [weak poll]; }];
}

- (void)windowWillClose:(NSNotification*)note {
    [_timer invalidate];   // geschlossen: kein Aufwand
    _timer = nil;
}

- (void)reload:(id)sender {
    _text.string = @"";
    _seq = 0;
    _lines = 0;
    [self poll];
}

- (void)clear:(id)sender {
    _core->clearTraffic();
    [self reload:nil];
}

- (NSColor*)colorFor:(const mb::grbl::TrafficEntry&)e {
    if (e.status) return NSColor.tertiaryLabelColor;
    if (e.outgoing) return NSColor.systemBlueColor;
    if (e.text.rfind("error:", 0) == 0 || e.text.rfind("ALARM:", 0) == 0) return NSColor.systemRedColor;
    if (e.text == "ok") return NSColor.secondaryLabelColor;
    return NSColor.labelColor;
}

- (void)poll {
    auto entries = _core->trafficSince(_seq);
    if (entries.empty()) return;
    _seq = entries.back().seq;
    bool withStatus = _showStatus.state == NSControlStateValueOn;
    NSDictionary* timeAttr = @{NSFontAttributeName: _text.font, NSForegroundColorAttributeName: NSColor.tertiaryLabelColor};
    NSMutableAttributedString* add = [[NSMutableAttributedString alloc] init];
    NSUInteger added = 0;
    for (const auto& e : entries) {
        if (e.status && !withStatus) continue;
        NSString* t = [_clock stringFromDate:[NSDate dateWithTimeIntervalSince1970:e.time]];
        [add appendAttributedString:[[NSAttributedString alloc] initWithString:[t stringByAppendingString:@"  "] attributes:timeAttr]];
        NSString* line = [NSString stringWithFormat:@"%@ %@\n", e.outgoing ? @"→" : @"←", NS(e.text)];
        [add appendAttributedString:[[NSAttributedString alloc] initWithString:line attributes:@{
            NSFontAttributeName: _text.font, NSForegroundColorAttributeName: [self colorFor:e]}]];
        ++added;
    }
    if (!added) return;
    NSTextStorage* store = _text.textStorage;
    [store beginEditing];
    [store appendAttributedString:add];
    _lines += added;
    if (_lines > kMaxLines) {   // ältere Zeilen vorne abschneiden
        NSString* s = store.string;
        NSUInteger drop = _lines - kMaxLines, pos = 0;
        for (NSUInteger i = 0; i < drop && pos < s.length; ++i) {
            NSRange nl = [s rangeOfString:@"\n" options:0 range:NSMakeRange(pos, s.length - pos)];
            if (nl.location == NSNotFound) break;
            pos = NSMaxRange(nl);
        }
        [store deleteCharactersInRange:NSMakeRange(0, pos)];
        _lines = kMaxLines;
    }
    [store endEditing];
    if (_follow.state == NSControlStateValueOn) [_text scrollRangeToVisible:NSMakeRange(store.length, 0)];
}

- (void)send:(id)sender {
    NSString* cmd = _input.stringValue;
    if (cmd.length == 0) return;
    std::string problem = _core->sendManualCommand(STD(cmd));
    if (!problem.empty()) {
        _hint.stringValue = NS(problem);
        _hint.textColor = NSColor.systemRedColor;
        NSBeep();
        return;
    }
    _hint.stringValue = @"→ an den Laser   ← vom Laser   ·   Befehle von Hand können den Kopf bewegen.";
    _hint.textColor = NSColor.secondaryLabelColor;
    if (![_history.lastObject isEqualToString:cmd]) [_history addObject:cmd];
    _historyPos = NSInteger(_history.count);
    _input.stringValue = @"";
}

// Pfeil hoch/runter: frühere Befehle
- (BOOL)control:(NSControl*)control textView:(NSTextView*)tv doCommandBySelector:(SEL)sel {
    if (control != _input || _history.count == 0) return NO;
    if (sel == @selector(moveUp:)) {
        _historyPos = MAX(0, _historyPos - 1);
    } else if (sel == @selector(moveDown:)) {
        _historyPos = MIN(NSInteger(_history.count), _historyPos + 1);
    } else return NO;
    _input.stringValue = _historyPos < NSInteger(_history.count) ? _history[NSUInteger(_historyPos)] : @"";
    return YES;
}

@end
