// MacBurn – kleine Helfer zwischen C++-Typen und Cocoa.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#import <Cocoa/Cocoa.h>
#include "document/Document.hpp"
#include <string>

inline NSString* NS(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
inline std::string STD(NSString* s) { return s ? std::string(s.UTF8String) : std::string(); }
inline NSColor* NSCol(const mb::Color& c) { return [NSColor colorWithSRGBRed:c.r green:c.g blue:c.b alpha:c.a]; }
