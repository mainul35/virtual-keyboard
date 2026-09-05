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
    PageSymbols,  // "?123"
    PageSymbols2, // "=\<"
    Screenshot,   // capture the screen to the clipboard (hides the panel first)
    SelectMode,   // hold Shift so the next tap in the app extends the selection
};

struct KeyDef {
    QString label;
    QString shiftLabel;      // label shown when Shift is active (Char keys only)
    int code = 0;            // evdev keycode (KEY_* from linux/input-event-codes.h)
    float width = 1.0f;
    KeyKind kind = KeyKind::Char;
    KeyAction action = KeyAction::None;
    bool withShift = false;  // symbol that lives on the shifted level of `code`
    bool keymapLabel = true; // derive the label from the active xkb keymap
};

struct RowDef {
    std::vector<KeyDef> keys;
    float height = 1.0f;
    float leftPad = 0.0f;  // empty space (units) before the first key
    float rightPad = 0.0f; // empty space (units) after the last key
};

struct PageDef {
    std::vector<RowDef> rows;
    float totalHeight() const;
};

namespace Layout {
// Full PC layout (15 keys per row): comfortable in landscape.
PageDef fullPage(bool withFunctionRow);
// Phone-style layout (10 keys per row) for portrait: letters here, symbols
// behind "?123" like Android/iOS, plus a utility row with Esc/Tab/Ctrl/Alt/arrows.
PageDef compactPage();
PageDef symbolsPage();
PageDef symbols2Page();
// Function keys, navigation cluster, media keys.
PageDef fnPage();

bool isModifierCode(int code);
bool isLetterCode(int code);
}
