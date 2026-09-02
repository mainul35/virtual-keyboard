#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <memory>

struct xkb_context;
struct xkb_keymap;

// Thin wrapper around an xkbcommon keymap. Used for two things:
//  * drawing the label that a keycode really produces in the user's layout
//  * knowing which modifier bit each modifier key corresponds to (Wayland IM path)
class Keymap
{
public:
    ~Keymap();

    // Keymap text as sent by the compositor (wl_keyboard.keymap, XKB_V1 format).
    static std::shared_ptr<Keymap> fromString(const QByteArray &text);
    // Best effort: reads Plasma's ~/.config/kxkbrc, then XKB_DEFAULT_* env, then "us".
    static std::shared_ptr<Keymap> fromSystemConfig();

    // Label for an evdev keycode at the given layout group and shift level
    // (empty when the key produces nothing printable).
    QString label(int evdevCode, int group, bool shifted) const;
    int groupCount() const;

    uint32_t shiftMask() const { return m_shift; }
    uint32_t capsMask() const { return m_caps; }
    uint32_t ctrlMask() const { return m_ctrl; }
    uint32_t altMask() const { return m_alt; }
    uint32_t metaMask() const { return m_meta; }
    uint32_t altGrMask() const { return m_altGr; }

private:
    Keymap() = default;
    void resolveModifiers();

    xkb_context *m_ctx = nullptr;
    xkb_keymap *m_map = nullptr;
    uint32_t m_shift = 1u << 0;
    uint32_t m_caps = 1u << 1;
    uint32_t m_ctrl = 1u << 2;
    uint32_t m_alt = 1u << 3;
    uint32_t m_meta = 1u << 6;
    uint32_t m_altGr = 1u << 7;
};
