#include "layout.h"

#include <linux/input-event-codes.h>

namespace {

KeyDef ch(const char *label, const char *shift, int code, float w = 1.0f)
{
    KeyDef k;
    k.label = QString::fromUtf8(label);
    k.shiftLabel = QString::fromUtf8(shift);
    k.code = code;
    k.width = w;
    return k;
}

KeyDef letter(char c, int code)
{
    const QChar lower = QChar::fromLatin1(c);
    KeyDef k;
    k.label = QString(lower);
    k.shiftLabel = QString(lower.toUpper());
    k.code = code;
    return k;
}

// A symbol reached through Shift on a US layout, shown with a fixed label.
KeyDef sym(const char *label, int code, bool shift, float w = 1.0f)
{
    KeyDef k;
    k.label = QString::fromUtf8(label);
    k.code = code;
    k.width = w;
    k.withShift = shift;
    k.keymapLabel = false;
    return k;
}

KeyDef sp(const char *label, int code, float w = 1.0f)
{
    KeyDef k;
    k.label = QString::fromUtf8(label);
    k.code = code;
    k.width = w;
    k.kind = KeyKind::Special;
    return k;
}

KeyDef mod(const char *label, int code, float w = 1.0f)
{
    KeyDef k;
    k.label = QString::fromUtf8(label);
    k.code = code;
    k.width = w;
    k.kind = KeyKind::Modifier;
    return k;
}

KeyDef act(const char *label, KeyAction a, float w = 1.0f)
{
    KeyDef k;
    k.label = QString::fromUtf8(label);
    k.width = w;
    k.kind = KeyKind::Action;
    k.action = a;
    return k;
}

RowDef numberRow()
{
    return RowDef{{
        ch("1", "!", KEY_1), ch("2", "@", KEY_2), ch("3", "#", KEY_3), ch("4", "$", KEY_4),
        ch("5", "%", KEY_5), ch("6", "^", KEY_6), ch("7", "&", KEY_7), ch("8", "*", KEY_8),
        ch("9", "(", KEY_9), ch("0", ")", KEY_0),
    }};
}

// Esc Tab Ctrl Alt Fn  ← ↑ ↓ →  Del : the keys a terminal user misses most.
RowDef utilityRow()
{
    RowDef r;
    r.height = 0.8f;
    r.keys = {
        sp("Esc", KEY_ESC), sp("Tab", KEY_TAB),
        mod("Ctrl", KEY_LEFTCTRL), mod("Alt", KEY_LEFTALT),
        act("Fn", KeyAction::PageFn),
        sp("←", KEY_LEFT), sp("↑", KEY_UP), sp("↓", KEY_DOWN), sp("→", KEY_RIGHT),
        sp("Del", KEY_DELETE),
        act("⌄", KeyAction::Hide),
    };
    return r;
}

RowDef compactBottomRow(KeyAction pageKey, const char *pageLabel)
{
    // Space takes the middle half of the row so either thumb reaches it.
    return RowDef{{
        act(pageLabel, pageKey, 1.5f),
        sym(",", KEY_COMMA, false, 1.0f),
        sp("", KEY_SPACE, 5.0f),
        sym(".", KEY_DOT, false, 1.0f),
        sp("⏎", KEY_ENTER, 1.5f),
    }};
}

} // namespace

float PageDef::totalHeight() const
{
    float h = 0;
    for (const auto &r : rows) {
        h += r.height;
    }
    return h;
}

// ------------------------------------------------------------- full (PC) --

PageDef Layout::fullPage(bool withFunctionRow)
{
    PageDef page;

    if (withFunctionRow) {
        RowDef fn;
        fn.height = 0.72f;
        fn.keys = {
            sp("Esc", KEY_ESC),
            sp("F1", KEY_F1), sp("F2", KEY_F2), sp("F3", KEY_F3), sp("F4", KEY_F4),
            sp("F5", KEY_F5), sp("F6", KEY_F6), sp("F7", KEY_F7), sp("F8", KEY_F8),
            sp("F9", KEY_F9), sp("F10", KEY_F10), sp("F11", KEY_F11), sp("F12", KEY_F12),
            act("PrtSc", KeyAction::Screenshot),
            sp("Del", KEY_DELETE),
            act("⌄", KeyAction::Hide),
        };
        page.rows.push_back(fn);
    }

    page.rows.push_back(RowDef{{
        ch("`", "~", KEY_GRAVE),
        ch("1", "!", KEY_1), ch("2", "@", KEY_2), ch("3", "#", KEY_3), ch("4", "$", KEY_4),
        ch("5", "%", KEY_5), ch("6", "^", KEY_6), ch("7", "&", KEY_7), ch("8", "*", KEY_8),
        ch("9", "(", KEY_9), ch("0", ")", KEY_0), ch("-", "_", KEY_MINUS), ch("=", "+", KEY_EQUAL),
        sp("⌫", KEY_BACKSPACE, 2.0f),
    }});

    page.rows.push_back(RowDef{{
        sp("Tab", KEY_TAB, 1.5f),
        letter('q', KEY_Q), letter('w', KEY_W), letter('e', KEY_E), letter('r', KEY_R), letter('t', KEY_T),
        letter('y', KEY_Y), letter('u', KEY_U), letter('i', KEY_I), letter('o', KEY_O), letter('p', KEY_P),
        ch("[", "{", KEY_LEFTBRACE), ch("]", "}", KEY_RIGHTBRACE),
        ch("\\", "|", KEY_BACKSLASH, 1.5f),
    }});

    page.rows.push_back(RowDef{{
        sp("Del", KEY_DELETE, 1.75f),
        letter('a', KEY_A), letter('s', KEY_S), letter('d', KEY_D), letter('f', KEY_F), letter('g', KEY_G),
        letter('h', KEY_H), letter('j', KEY_J), letter('k', KEY_K), letter('l', KEY_L),
        ch(";", ":", KEY_SEMICOLON), ch("'", "\"", KEY_APOSTROPHE),
        sp("Enter", KEY_ENTER, 2.25f),
    }});

    page.rows.push_back(RowDef{{
        mod("Shift", KEY_LEFTSHIFT, 3.0f),
        letter('z', KEY_Z), letter('x', KEY_X), letter('c', KEY_C), letter('v', KEY_V), letter('b', KEY_B),
        letter('n', KEY_N), letter('m', KEY_M),
        ch(",", "<", KEY_COMMA), ch(".", ">", KEY_DOT), ch("/", "?", KEY_SLASH),
        sp("↑", KEY_UP, 2.0f),
    }});

    RowDef bottom{{
        mod("Ctrl", KEY_LEFTCTRL, 1.25f),
        mod("Meta", KEY_LEFTMETA, 1.25f),
        mod("Alt", KEY_LEFTALT, 1.25f),
        sp("", KEY_SPACE, 6.25f),
        mod("AltGr", KEY_RIGHTALT, 1.0f),
        act("Fn", KeyAction::PageFn, 1.0f),
        sp("←", KEY_LEFT), sp("↓", KEY_DOWN), sp("→", KEY_RIGHT),
    }};
    if (!withFunctionRow) {
        // The hide key normally lives in the function row.
        bottom.keys[3].width = 5.25f;
        bottom.keys.push_back(act("⌄", KeyAction::Hide));
    }
    page.rows.push_back(bottom);

    return page;
}

// -------------------------------------------------------- compact (phone) --

PageDef Layout::compactPage()
{
    PageDef page;
    page.rows.push_back(utilityRow());
    page.rows.push_back(numberRow());

    page.rows.push_back(RowDef{{
        letter('q', KEY_Q), letter('w', KEY_W), letter('e', KEY_E), letter('r', KEY_R), letter('t', KEY_T),
        letter('y', KEY_Y), letter('u', KEY_U), letter('i', KEY_I), letter('o', KEY_O), letter('p', KEY_P),
    }});

    RowDef home{{
        letter('a', KEY_A), letter('s', KEY_S), letter('d', KEY_D), letter('f', KEY_F), letter('g', KEY_G),
        letter('h', KEY_H), letter('j', KEY_J), letter('k', KEY_K), letter('l', KEY_L),
    }};
    home.leftPad = 0.5f;
    home.rightPad = 0.5f;
    page.rows.push_back(home);

    page.rows.push_back(RowDef{{
        mod("⇧", KEY_LEFTSHIFT, 1.5f),
        letter('z', KEY_Z), letter('x', KEY_X), letter('c', KEY_C), letter('v', KEY_V), letter('b', KEY_B),
        letter('n', KEY_N), letter('m', KEY_M),
        sp("⌫", KEY_BACKSPACE, 1.5f),
    }});

    page.rows.push_back(compactBottomRow(KeyAction::PageSymbols, "?123"));
    return page;
}

PageDef Layout::symbolsPage()
{
    PageDef page;
    page.rows.push_back(utilityRow());
    page.rows.push_back(numberRow());

    page.rows.push_back(RowDef{{
        sym("@", KEY_2, true), sym("#", KEY_3, true), sym("$", KEY_4, true), sym("%", KEY_5, true),
        sym("&", KEY_7, true), sym("-", KEY_MINUS, false), sym("+", KEY_EQUAL, true),
        sym("(", KEY_9, true), sym(")", KEY_0, true), sym("/", KEY_SLASH, false),
    }});

    page.rows.push_back(RowDef{{
        act("=\\<", KeyAction::PageSymbols2, 1.5f),
        sym("*", KEY_8, true), sym("\"", KEY_APOSTROPHE, true), sym("'", KEY_APOSTROPHE, false),
        sym(":", KEY_SEMICOLON, true), sym(";", KEY_SEMICOLON, false),
        sym("!", KEY_1, true), sym("?", KEY_SLASH, true),
        sp("⌫", KEY_BACKSPACE, 1.5f),
    }});

    page.rows.push_back(compactBottomRow(KeyAction::PageMain, "ABC"));
    return page;
}

PageDef Layout::symbols2Page()
{
    PageDef page;
    page.rows.push_back(utilityRow());
    page.rows.push_back(numberRow());

    page.rows.push_back(RowDef{{
        sym("~", KEY_GRAVE, true), sym("`", KEY_GRAVE, false), sym("^", KEY_6, true), sym("_", KEY_MINUS, true),
        sym("=", KEY_EQUAL, false), sym("{", KEY_LEFTBRACE, true), sym("}", KEY_RIGHTBRACE, true),
        sym("[", KEY_LEFTBRACE, false), sym("]", KEY_RIGHTBRACE, false), sym("\\", KEY_BACKSLASH, false),
    }});

    page.rows.push_back(RowDef{{
        act("?123", KeyAction::PageSymbols, 1.5f),
        sym("|", KEY_BACKSLASH, true), sym("<", KEY_COMMA, true), sym(">", KEY_DOT, true),
        sym("-", KEY_MINUS, false), sym("+", KEY_EQUAL, true), sym("*", KEY_8, true), sym("/", KEY_SLASH, false),
        sp("⌫", KEY_BACKSPACE, 1.5f),
    }});

    page.rows.push_back(compactBottomRow(KeyAction::PageMain, "ABC"));
    return page;
}

// ------------------------------------------------------------------- Fn --

PageDef Layout::fnPage()
{
    PageDef page;

    page.rows.push_back(RowDef{{
        sp("Esc", KEY_ESC),
        sp("Home", KEY_HOME), sp("End", KEY_END), sp("PgUp", KEY_PAGEUP), sp("PgDn", KEY_PAGEDOWN),
        sp("Ins", KEY_INSERT), sp("Del", KEY_DELETE),
        act("PrtSc", KeyAction::Screenshot), sp("ScrLk", KEY_SCROLLLOCK), sp("Pause", KEY_PAUSE), sp("Menu", KEY_COMPOSE),
    }});

    page.rows.push_back(RowDef{{
        sp("F1", KEY_F1), sp("F2", KEY_F2), sp("F3", KEY_F3), sp("F4", KEY_F4),
        sp("F5", KEY_F5), sp("F6", KEY_F6), sp("F7", KEY_F7), sp("F8", KEY_F8),
        sp("F9", KEY_F9), sp("F10", KEY_F10), sp("F11", KEY_F11), sp("F12", KEY_F12),
    }});

    page.rows.push_back(RowDef{{
        sp("Vol−", KEY_VOLUMEDOWN), sp("Vol+", KEY_VOLUMEUP), sp("Mute", KEY_MUTE),
        sp("Bright−", KEY_BRIGHTNESSDOWN), sp("Bright+", KEY_BRIGHTNESSUP),
        sp("⏮", KEY_PREVIOUSSONG), sp("⏯", KEY_PLAYPAUSE), sp("⏭", KEY_NEXTSONG),
        sp("Tab", KEY_TAB), sp("NumLk", KEY_NUMLOCK),
    }});

    page.rows.push_back(RowDef{{
        mod("Shift", KEY_LEFTSHIFT, 1.5f),
        mod("Ctrl", KEY_LEFTCTRL, 1.5f),
        mod("Alt", KEY_LEFTALT, 1.5f),
        mod("Meta", KEY_LEFTMETA, 1.5f),
        sp("⌫", KEY_BACKSPACE, 1.5f),
        sp("↑", KEY_UP, 1.5f),
        sp("⏎", KEY_ENTER, 2.0f),
    }});

    page.rows.push_back(RowDef{{
        act("abc", KeyAction::PageMain, 1.5f),
        sp("", KEY_SPACE, 4.5f),
        act("Sel", KeyAction::SelectMode, 1.5f),
        sp("←", KEY_LEFT), sp("↓", KEY_DOWN), sp("→", KEY_RIGHT),
        act("⌄", KeyAction::Hide, 1.5f),
    }});

    return page;
}

bool Layout::isModifierCode(int code)
{
    switch (code) {
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
    case KEY_LEFTALT:
    case KEY_RIGHTALT:
    case KEY_LEFTMETA:
    case KEY_RIGHTMETA:
        return true;
    default:
        return false;
    }
}

bool Layout::isLetterCode(int code)
{
    switch (code) {
    case KEY_Q: case KEY_W: case KEY_E: case KEY_R: case KEY_T: case KEY_Y: case KEY_U:
    case KEY_I: case KEY_O: case KEY_P: case KEY_A: case KEY_S: case KEY_D: case KEY_F:
    case KEY_G: case KEY_H: case KEY_J: case KEY_K: case KEY_L: case KEY_Z: case KEY_X:
    case KEY_C: case KEY_V: case KEY_B: case KEY_N: case KEY_M:
        return true;
    default:
        return false;
    }
}
