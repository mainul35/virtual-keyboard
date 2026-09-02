#pragma once

#include <QString>
#include <vector>

// Static description of the keyboard pages. Sizes are in "units": every row is
// stretched to the full panel width, so only the ratios between keys matter.

enum class KeyKind {
    Char,      // produces text; label can be taken from the real xkb keymap
    Special,   // Enter, Backspace, arrows, F-keys ... fixed label
    Modifier,  // Shift/Ctrl/Alt/Meta: tap = latch, double tap = lock, hold = hold
    Lock,      // Caps Lock: real toggle key
    Action,    // does something inside the app instead of sending a key
};

enum class KeyAction {
    None,
    Hide,
    PageFn,
    PageMain,
    Screenshot, // capture the screen to the clipboard (hides the panel first)
};

struct KeyDef {
    QString label;
    QString shiftLabel;      // label shown when Shift is active (Char keys only)
    int code = 0;            // evdev keycode (KEY_* from linux/input-event-codes.h)
    float width = 1.0f;
    KeyKind kind = KeyKind::Char;
    KeyAction action = KeyAction::None;
};

struct RowDef {
    std::vector<KeyDef> keys;
    float height = 1.0f;
};

struct PageDef {
    std::vector<RowDef> rows;
    float totalHeight() const;
};

namespace Layout {
PageDef mainPage(bool withFunctionRow);
PageDef fnPage();
bool isModifierCode(int code);
bool isLetterCode(int code);
}
