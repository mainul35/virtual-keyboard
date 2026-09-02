#pragma once

#include "injector.h"

#include <QObject>
#include <QtWaylandClient/QWaylandClientExtension>

#include "qwayland-input-method-unstable-v1.h"

#include <cstdint>
#include <memory>

class Keymap;

// One zwp_input_method_context_v1: exists while a text field has focus (or
// while KWin was asked to force-activate the keyboard).
//
// We deliberately do not grab the keyboard: with a grab in place KWin diverts
// every physical (and uinput) key event to the input method, which would make
// the uinput backend depend on this process forwarding keys again.
class InputMethodContext : public QObject, public QtWayland::zwp_input_method_context_v1
{
    Q_OBJECT
public:
    explicit InputMethodContext(struct ::zwp_input_method_context_v1 *id, QObject *parent = nullptr);
    ~InputMethodContext() override;

    uint32_t serial() const { return m_serial; }

    void sendKey(uint32_t evdevCode, bool pressed);
    void sendModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);

protected:
    void zwp_input_method_context_v1_commit_state(uint32_t serial) override;

private:
    uint32_t m_serial = 0;
};

// The zwp_input_method_v1 global. Only present on the private socket KWin hands
// to the configured input method, so isActive() doubles as "launched by KWin".
class InputMethod : public QWaylandClientExtensionTemplate<InputMethod>, public QtWayland::zwp_input_method_v1
{
    Q_OBJECT
public:
    InputMethod();
    ~InputMethod() override;

    // Bind synchronously instead of waiting for the queued auto-initialisation,
    // so isActive() can be checked right after construction.
    using QWaylandClientExtensionTemplate<InputMethod>::initialize;

    InputMethodContext *context() const { return m_context.get(); }

Q_SIGNALS:
    void activated();
    void deactivated();

protected:
    void zwp_input_method_v1_activate(struct ::zwp_input_method_context_v1 *id) override;
    void zwp_input_method_v1_deactivate(struct ::zwp_input_method_context_v1 *context) override;

private:
    std::unique_ptr<InputMethodContext> m_context;
};

// Types keys through the active context: a raw evdev keycode goes to the
// focused surface as a wl_keyboard.key, and modifier keys additionally update
// the compositor's xkb modifier state. Used when /dev/uinput is not available.
class InputMethodInjector : public QObject, public Injector
{
    Q_OBJECT
public:
    InputMethodInjector(InputMethod *im, std::shared_ptr<Keymap> keymap, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("wayland-im"); }
    bool isReady() const override;
    void key(int code, bool pressed) override;

    // Current xkb layout group (asked from Plasma over D-Bus), so that the
    // modifiers request does not accidentally switch the layout.
    void setLayoutGroup(int group) { m_group = group; }

private:
    uint32_t maskFor(int code) const;

    InputMethod *m_im;
    std::shared_ptr<Keymap> m_keymap;
    uint32_t m_depressed = 0;
    uint32_t m_locked = 0;
    int m_group = 0;
};
