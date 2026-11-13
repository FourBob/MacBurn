// SPDX-License-Identifier: GPL-3.0-or-later
#import "InspectorView.h"
#import "Bridge.hpp"
#include "util/NumberInput.hpp"

@implementation InspectorView {
    mb::AppController* _core;
    NSStackView* _stack;
    std::vector<mb::Field> _shown;   // nur neu bauen, wenn sich etwas geändert hat
}

- (instancetype)initWithCore:(mb::AppController*)core {
    if ((self = [super initWithFrame:NSZeroRect])) {
        _core = core;
        _stack = [NSStackView stackViewWithViews:@[]];
        _stack.orientation = NSUserInterfaceLayoutOrientationVertical;
        _stack.alignment = NSLayoutAttributeLeading;
        _stack.spacing = 10;
        _stack.edgeInsets = NSEdgeInsetsMake(16, 14, 16, 14);
        _stack.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:_stack];
        [NSLayoutConstraint activateConstraints:@[
            [_stack.topAnchor constraintEqualToAnchor:self.topAnchor],
            [_stack.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [_stack.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [_stack.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
        ]];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }   // Inhalt oben ausrichten

static NSTextField* label(NSString* s, NSFont* font, NSColor* color) {
    NSTextField* l = [NSTextField wrappingLabelWithString:s];
    l.font = font;
    l.textColor = color;
    return l;
}

- (void)fill:(NSView*)v {
    [v.widthAnchor constraintEqualToAnchor:_stack.widthAnchor constant:-28].active = YES;
}

- (void)showFields:(const std::vector<mb::Field>&)fields {
    if (fields == _shown && _stack.arrangedSubviews.count) return;
    _shown = fields;
    for (NSView* v in _stack.arrangedSubviews.copy) [v removeFromSuperview];

    for (const auto& f : fields) {
        NSString* key = NS(f.key);
        NSView* view = nil;
        switch (f.kind) {
            case mb::Field::Kind::Header:
                view = label(NS(f.label).uppercaseString, [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold], NSColor.secondaryLabelColor);
                break;
            case mb::Field::Kind::Info:
                view = label(NS(f.label), [NSFont systemFontOfSize:12], f.highlight ? NSColor.systemOrangeColor : NSColor.labelColor);
                break;
            case mb::Field::Kind::Choice: {
                NSPopUpButton* p = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
                for (const auto& o : f.options) [p addItemWithTitle:NS(o)];
                [p selectItemAtIndex:f.selected];
                p.identifier = key;
                p.target = self; p.action = @selector(choice:);
                p.enabled = f.enabled;
                view = [self row:NS(f.label) control:p unit:nil];
                break;
            }
            case mb::Field::Kind::Number: {
                // Freie Eingabe: Der Kern versteht Komma/Punkt, Rechnen (100/3) und Einheiten (2in, 1,5cm).
                NSTextField* t = [NSTextField textFieldWithString:NS(mb::formatNumber(f.value, f.step < 1 ? 3 : 0))];
                t.toolTip = @"Auch Rechnen möglich: 100/3, 25+12,5, 2in, 1,5cm";
                t.alignment = NSTextAlignmentRight;
                t.font = [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
                if (f.highlight) t.textColor = NSColor.controlAccentColor;
                t.identifier = key;
                t.target = self; t.action = @selector(numberText:);
                t.enabled = f.enabled;
                [t.widthAnchor constraintEqualToConstant:80].active = YES;
                NSStepper* st = [[NSStepper alloc] init];
                st.minValue = f.min; st.maxValue = f.max; st.increment = f.step; st.doubleValue = f.value;
                st.identifier = key;
                st.target = self; st.action = @selector(number:);
                st.enabled = f.enabled;
                NSStackView* ctl = [NSStackView stackViewWithViews:@[t, st]];
                ctl.spacing = 2;
                view = [self row:NS(f.label) control:ctl unit:NS(f.unit)];
                break;
            }
            case mb::Field::Kind::Toggle: {
                NSSwitch* s = [[NSSwitch alloc] init];
                s.state = f.on ? NSControlStateValueOn : NSControlStateValueOff;
                s.identifier = key;
                s.target = self; s.action = @selector(toggle:);
                view = [self row:NS(f.label) control:s unit:nil];
                break;
            }
            case mb::Field::Kind::JogPad: {
                // ▲ ◀ ▶ ▼ – Pfeiltasten auf der Zeichenfläche tun dasselbe
                NSGridView* g = [NSGridView gridViewWithNumberOfColumns:3 rows:3];
                NSString* titles[3][3] = {{@"", @"▲", @""}, {@"◀", @"", @"▶"}, {@"", @"▼", @""}};
                NSString* keys[3][3] = {{@"", @"jog+y", @""}, {@"jog-x", @"", @"jog+x"}, {@"", @"jog-y", @""}};
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c) {
                        if (!titles[r][c].length) continue;
                        NSButton* b = [NSButton buttonWithTitle:titles[r][c] target:self action:@selector(button:)];
                        b.identifier = keys[r][c];
                        b.enabled = f.enabled;
                        [b.widthAnchor constraintEqualToConstant:44].active = YES;
                        [g cellAtColumnIndex:c rowIndex:r].contentView = b;
                    }
                g.rowSpacing = 4; g.columnSpacing = 4;
                g.xPlacement = NSGridCellPlacementCenter;
                for (int c = 0; c < 3; ++c) [g columnAtIndex:c].width = 44;
                view = g;
                break;
            }
            case mb::Field::Kind::Anchor: {
                // 3 × 3 Punkte; der gewählte ist gefüllt
                NSGridView* g = [NSGridView gridViewWithNumberOfColumns:3 rows:3];
                for (int i = 0; i < 9; ++i) {
                    NSButton* b = [NSButton buttonWithTitle:(i == f.selected ? @"●" : @"○") target:self action:@selector(anchor:)];
                    b.bordered = NO;
                    b.tag = i;
                    b.identifier = key;
                    b.enabled = f.enabled;
                    b.font = [NSFont systemFontOfSize:15];
                    b.contentTintColor = i == f.selected ? NSColor.controlAccentColor : NSColor.secondaryLabelColor;
                    b.toolTip = NS(mb::anchorName(i));
                    [g cellAtColumnIndex:i % 3 rowIndex:i / 3].contentView = b;
                }
                g.rowSpacing = 0; g.columnSpacing = 6;
                for (int c = 0; c < 3; ++c) [g columnAtIndex:c].width = 20;
                [g setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
                view = [self row:NS(f.label) control:g unit:nil];
                break;
            }
            case mb::Field::Kind::TextArea: {
                // Mehrzeilig: ↩ übernimmt, ⌥↩ fügt eine neue Zeile ein
                NSTextField* t = [NSTextField textFieldWithString:NS(f.text)];
                t.usesSingleLineMode = NO;
                t.cell.wraps = YES;
                t.cell.scrollable = NO;
                t.lineBreakMode = NSLineBreakByWordWrapping;
                t.font = [NSFont systemFontOfSize:13];
                t.placeholderString = @"Text eingeben – ⌥↩ für neue Zeile";
                t.identifier = key;
                t.target = self; t.action = @selector(textArea:);
                [t.heightAnchor constraintGreaterThanOrEqualToConstant:52].active = YES;
                if (f.focus) dispatch_async(dispatch_get_main_queue(), ^{
                    [t.window makeFirstResponder:t];
                    [t.currentEditor selectAll:nil];
                });
                view = t;
                break;
            }
            case mb::Field::Kind::ButtonRow: {
                NSStackView* row = [NSStackView stackViewWithViews:@[]];
                row.spacing = 4;
                for (size_t i = 0; i < f.options.size(); ++i) {
                    NSString* opt = NS(f.options[i]);
                    NSArray* parts = [opt componentsSeparatedByString:@"|"];
                    NSString* tip = parts.count > 1 ? parts[1] : opt;
                    NSImage* img = [NSImage imageWithSystemSymbolName:parts[0] accessibilityDescription:tip];
                    NSButton* b = img ? [NSButton buttonWithImage:img target:self action:@selector(button:)]
                                      : [NSButton buttonWithTitle:tip target:self action:@selector(button:)];
                    b.identifier = [NSString stringWithFormat:@"%@:%zu", key, i];
                    b.toolTip = tip;
                    b.enabled = f.enabled;
                    b.controlSize = NSControlSizeSmall;
                    [row addArrangedSubview:b];
                }
                view = row;
                break;
            }
            case mb::Field::Kind::Progress: {
                NSProgressIndicator* p = [[NSProgressIndicator alloc] init];
                p.indeterminate = NO;
                p.minValue = 0; p.maxValue = 100; p.doubleValue = f.value;
                NSTextField* t = label([NSString stringWithFormat:@"%.0f %%  ·  %@", f.value, NS(f.unit)],
                                       [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular], NSColor.secondaryLabelColor);
                NSStackView* s = [NSStackView stackViewWithViews:@[p, t]];
                s.orientation = NSUserInterfaceLayoutOrientationVertical;
                s.alignment = NSLayoutAttributeLeading;
                [p.widthAnchor constraintEqualToAnchor:s.widthAnchor].active = YES;
                view = s;
                break;
            }
            case mb::Field::Kind::Button: {
                NSButton* b = [NSButton buttonWithTitle:NS(f.label) target:self action:@selector(button:)];
                b.identifier = key;
                b.controlSize = NSControlSizeSmall;
                b.enabled = f.enabled;
                if (f.highlight) { b.bezelColor = NSColor.systemRedColor; b.controlSize = NSControlSizeLarge; }
                view = b;
                break;
            }
        }
        if (!view) continue;
        [_stack addArrangedSubview:view];
        if (f.kind != mb::Field::Kind::Button && f.kind != mb::Field::Kind::JogPad && f.kind != mb::Field::Kind::ButtonRow) [self fill:view];
        if (f.kind == mb::Field::Kind::Header && view != _stack.arrangedSubviews.firstObject)
            [_stack setCustomSpacing:16 afterView:_stack.arrangedSubviews[_stack.arrangedSubviews.count - 2]];
    }
}

- (NSView*)row:(NSString*)title control:(NSView*)control unit:(NSString*)unit {
    NSTextField* l = [NSTextField labelWithString:title];
    l.font = [NSFont systemFontOfSize:12];
    [l.widthAnchor constraintEqualToConstant:104].active = YES;   // Werte stehen bündig untereinander
    NSMutableArray* views = [NSMutableArray arrayWithObjects:l, control, nil];
    if (unit.length) {
        NSTextField* u = [NSTextField labelWithString:unit];
        u.font = [NSFont systemFontOfSize:11];
        u.textColor = NSColor.secondaryLabelColor;
        [views addObject:u];
    }
    // Füller am Ende: Steuerelemente und Einheiten stehen in allen Zeilen bündig
    NSView* spacer = [[NSView alloc] init];
    [spacer setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    [views addObject:spacer];
    NSStackView* row = [NSStackView stackViewWithViews:views];
    row.spacing = 6;
    row.distribution = NSStackViewDistributionFill;
    return row;
}

- (void)textArea:(NSTextField*)t {
    _core->setString(STD(t.identifier), STD(t.stringValue));
    _shown.clear();
}
- (void)choice:(NSPopUpButton*)p { _core->setChoice(STD(p.identifier), int(p.indexOfSelectedItem)); }
- (void)number:(NSControl*)c { _core->setNumber(STD(c.identifier), c.doubleValue); }
- (void)numberText:(NSTextField*)t {
    bool ok = _core->setNumberText(STD(t.identifier), STD(t.stringValue));
    _shown.clear();   // Feld neu aufbauen: zeigt das Ergebnis (z. B. „33,333“ statt „100/3“) bzw. den alten Wert
    if (!ok) NSBeep();
}
- (void)toggle:(NSSwitch*)s { _core->setToggle(STD(s.identifier), s.state == NSControlStateValueOn); }
- (void)anchor:(NSButton*)b { _core->setChoice(STD(b.identifier), int(b.tag)); }
- (void)button:(NSButton*)b { _core->pressButton(STD(b.identifier)); }

@end
