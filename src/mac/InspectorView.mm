// SPDX-License-Identifier: GPL-3.0-or-later
#import "InspectorView.h"
#import "Bridge.hpp"

@implementation InspectorView {
    mb::AppController* _core;
    NSStackView* _stack;
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
                view = [self row:NS(f.label) control:p unit:nil];
                break;
            }
            case mb::Field::Kind::Number: {
                NSTextField* t = [NSTextField textFieldWithString:@""];
                NSNumberFormatter* nf = [[NSNumberFormatter alloc] init];
                nf.numberStyle = NSNumberFormatterDecimalStyle;
                nf.maximumFractionDigits = f.step < 1 ? 2 : 0;
                nf.minimum = @(f.min); nf.maximum = @(f.max);
                t.formatter = nf;
                t.doubleValue = f.value;
                t.alignment = NSTextAlignmentRight;
                t.font = [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
                if (f.highlight) t.textColor = NSColor.controlAccentColor;
                t.identifier = key;
                t.target = self; t.action = @selector(number:);
                [t.widthAnchor constraintEqualToConstant:72].active = YES;
                NSStepper* st = [[NSStepper alloc] init];
                st.minValue = f.min; st.maxValue = f.max; st.increment = f.step; st.doubleValue = f.value;
                st.identifier = key;
                st.target = self; st.action = @selector(number:);
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
            case mb::Field::Kind::Button: {
                NSButton* b = [NSButton buttonWithTitle:NS(f.label) target:self action:@selector(button:)];
                b.identifier = key;
                b.controlSize = NSControlSizeSmall;
                view = b;
                break;
            }
        }
        if (!view) continue;
        [_stack addArrangedSubview:view];
        if (f.kind != mb::Field::Kind::Button) [self fill:view];
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
    NSStackView* row = [NSStackView stackViewWithViews:views];
    row.spacing = 6;
    return row;
}

- (void)choice:(NSPopUpButton*)p { _core->setChoice(STD(p.identifier), int(p.indexOfSelectedItem)); }
- (void)number:(NSControl*)c { _core->setNumber(STD(c.identifier), c.doubleValue); }
- (void)toggle:(NSSwitch*)s { _core->setToggle(STD(s.identifier), s.state == NSControlStateValueOn); }
- (void)button:(NSButton*)b { _core->pressButton(STD(b.identifier)); }

@end
