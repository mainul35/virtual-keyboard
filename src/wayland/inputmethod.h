#pragma once

#include "injector.h"

#include <QObject>
#include <QtWaylandClient/QWaylandClientExtension>

#include "qwayland-input-method-unstable-v1.h"

#include <cstdint>
#include <memory>

class Keymap;
struct wl_keyboard;
struct wl_keyboard_listener;

// One zwp_input_method_context_v1: exists while a text field has focus (or
// while KWin was asked to force-activate the keyboard). Grabs the keyboard so
// we learn the real keymap and layout group; physical keys are passed through.
class InputMethodContext : public QObject, public QtWayland::zwp_input_method_context_v1
{
    Q_OBJECT
public:
    explicit InputMethodContext(struct ::zwp_input_method_context_v1 *id, QObject *parent = nullptr);
    ~InputMethodContext() override;

    uint32_t serial() const { return m_serial; }
    int group() const { return m_group; }
    std::shared_ptr<Keymap> keymap() const { return m_keymap; }

    void sendKey(uint32_t evdevCode, bool pressed);
    void sendModifiers(uint32_t depressed, uint32_t latched, uint32_t locked);

Q_SIGNALS:
    void keymapChanged();
    void groupChanged(int group);

protected:
    void zwp_input_method_context_v1_commit_state(uint32_t serial) override;

private:
    friend struct ContextTrampolines;
    static const wl_keyboard_listener s_keyboardListener;
    void handleKeymap(uint32_t format, int fd, uint32_t size);
    void handleKey(uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
    void handleModifiers(uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);

    struct ::wl_keyboard *m_keyboard = nullptr;
    uint32_t m_serial = 0;
    int m_group = 0;
    std::shared_ptr<Keymap> m_keymap;

    bool m_modsKnown = false;
    uint32_t m_lastDepressed = 0;
    uint32_t m_lastLatched = 0;
    uint32_t m_lastLocked = 0;
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
// the compositor's xkb modifier state so Ctrl/Alt/Shift combos work everywhere.
class InputMethodInjector : public QObject, public Injector
{
    Q_OBJECT
public:
    explicit InputMethodInjector(InputMethod *im, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("wayland-im"); }
    bool isReady() const override;
    void key(int code, bool pressed) override;

private:
    uint32_t maskFor(int code) const;

    InputMethod *m_im;
    uint32_t m_depressed = 0;
    uint32_t m_locked = 0;
};
