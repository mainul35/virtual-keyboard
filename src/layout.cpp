#include "layout.h"

#include <linux/input-event-codes.h>

namespace {

KeyDef ch(const char *label, const char *shift, int code, float w = 1.0f)
{
    return KeyDef{QString::fromUtf8(label), QString::fromUtf8(shift), code, w, KeyKind::Char, KeyAction::None};
}

KeyDef letter(char c, int code)
{
    const QChar lower = QChar::fromLatin1(c);
    return KeyDef{QString(lower), QString(lower.toUpper()), code, 1.0f, KeyKind::Char, KeyAction::None};
}

KeyDef sp(const char *label, int code, float w = 1.0f)
{
    return KeyDef{QString::fromUtf8(label), {}, code, w, KeyKind::Special, KeyAction::None};
}

KeyDef mod(const char *label, int code, float w = 1.0f)
{
    return KeyDef{QString::fromUtf8(label), {}, code, w, KeyKind::Modifier, KeyAction::None};
}

KeyDef lock(const char *label, int code, float w = 1.0f)
{
    return KeyDef{QString::fromUtf8(label), {}, code, w, KeyKind::Lock, KeyAction::None};
}

KeyDef act(const char *label, KeyAction a, float w = 1.0f)
{
    return KeyDef{QString::fromUtf8(label), {}, 0, w, KeyKind::Action, a};
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

PageDef Layout::mainPage(bool withFunctionRow)
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
        lock("Caps", KEY_CAPSLOCK, 1.75f),
        letter('a', KEY_A), letter('s', KEY_S), letter('d', KEY_D), letter('f', KEY_F), letter('g', KEY_G),
        letter('h', KEY_H), letter('j', KEY_J), letter('k', KEY_K), letter('l', KEY_L),
        ch(";", ":", KEY_SEMICOLON), ch("'", "\"", KEY_APOSTROPHE),
        sp("Enter", KEY_ENTER, 2.25f),
    }});

    page.rows.push_back(RowDef{{
        mod("Shift", KEY_LEFTSHIFT, 2.0f),
        letter('z', KEY_Z), letter('x', KEY_X), letter('c', KEY_C), letter('v', KEY_V), letter('b', KEY_B),
        letter('n', KEY_N), letter('m', KEY_M),
        ch(",", "<", KEY_COMMA), ch(".", ">", KEY_DOT), ch("/", "?", KEY_SLASH),
        mod("Shift", KEY_RIGHTSHIFT, 1.75f),
        sp("↑", KEY_UP, 1.25f),
    }});

    page.rows.push_back(RowDef{{
        mod("Ctrl", KEY_LEFTCTRL, 1.25f),
        mod("Meta", KEY_LEFTMETA, 1.25f),
        mod("Alt", KEY_LEFTALT, 1.25f),
        sp("", KEY_SPACE, 5.25f),
        mod("AltGr", KEY_RIGHTALT, 1.0f),
        act("Fn", KeyAction::PageFn, 1.0f),
        sp("←", KEY_LEFT), sp("↓", KEY_DOWN), sp("→", KEY_RIGHT),
        act("⌄", KeyAction::Hide, 1.0f),
    }});

    return page;
}

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
        sp("Enter", KEY_ENTER, 1.5f),
        sp("⌫", KEY_BACKSPACE, 1.5f),
        sp("↑", KEY_UP),
        sp("", KEY_SPACE, 2.0f),
    }});

    page.rows.push_back(RowDef{{
        act("abc", KeyAction::PageMain, 1.5f),
        sp("", KEY_SPACE, 6.0f),
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
