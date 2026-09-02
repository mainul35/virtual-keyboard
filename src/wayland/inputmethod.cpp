#include "inputmethod.h"

#include "keymap.h"

#include <QDebug>

#include <wayland-client-protocol.h>

#include <chrono>
#include <linux/input-event-codes.h>

namespace {

uint32_t nowMs()
{
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace

// ------------------------------------------------------- InputMethodContext --

InputMethodContext::InputMethodContext(struct ::zwp_input_method_context_v1 *id, QObject *parent)
    : QObject(parent)
    , QtWayland::zwp_input_method_context_v1(id)
{
}

InputMethodContext::~InputMethodContext()
{
    if (object()) {
        destroy();
    }
}

void InputMethodContext::zwp_input_method_context_v1_commit_state(uint32_t serial)
{
    m_serial = serial;
}

void InputMethodContext::sendKey(uint32_t evdevCode, bool pressed)
{
    key(m_serial, nowMs(), evdevCode, pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
}

void InputMethodContext::sendModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
    modifiers(m_serial, depressed, latched, locked, group);
}

// -------------------------------------------------------------- InputMethod --

InputMethod::InputMethod()
    : QWaylandClientExtensionTemplate<InputMethod>(1)
{
}

InputMethod::~InputMethod()
{
    m_context.reset();
}

void InputMethod::zwp_input_method_v1_activate(struct ::zwp_input_method_context_v1 *id)
{
    // Owned by the unique_ptr only (no QObject parent) to avoid a double delete.
    m_context = std::make_unique<InputMethodContext>(id);
    Q_EMIT activated();
}

void InputMethod::zwp_input_method_v1_deactivate(struct ::zwp_input_method_context_v1 *context)
{
    if (m_context && m_context->object() == context) {
        m_context.reset();
        Q_EMIT deactivated();
    } else {
        // A context we never adopted (should not happen); release it anyway.
        zwp_input_method_context_v1_destroy(context);
    }
}

// ------------------------------------------------------ InputMethodInjector --

InputMethodInjector::InputMethodInjector(InputMethod *im, std::shared_ptr<Keymap> keymap, QObject *parent)
    : QObject(parent)
    , m_im(im)
    , m_keymap(std::move(keymap))
{
    connect(m_im, &InputMethod::deactivated, this, [this] {
        m_depressed = 0;
    });
}

bool InputMethodInjector::isReady() const
{
    return m_im && m_im->context() != nullptr;
}

uint32_t InputMethodInjector::maskFor(int code) const
{
    const auto &km = m_keymap;
    switch (code) {
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
        return km ? km->shiftMask() : (1u << 0);
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
        return km ? km->ctrlMask() : (1u << 2);
    case KEY_LEFTALT:
        return km ? km->altMask() : (1u << 3);
    case KEY_RIGHTALT:
        return km ? km->altGrMask() : (1u << 7);
    case KEY_LEFTMETA:
    case KEY_RIGHTMETA:
        return km ? km->metaMask() : (1u << 6);
    default:
        return 0;
    }
}

void InputMethodInjector::key(int code, bool pressed)
{
    InputMethodContext *ctx = m_im->context();
    if (!ctx) {
        return;
    }
    const uint32_t mask = maskFor(code);
    const uint32_t group = static_cast<uint32_t>(m_group);

    if (mask) {
        // Modifier key: update the compositor's xkb state before the release
        // of a normal key that follows, exactly like a physical keyboard.
        ctx->sendKey(static_cast<uint32_t>(code), pressed);
        if (pressed) {
            m_depressed |= mask;
        } else {
            m_depressed &= ~mask;
        }
        ctx->sendModifiers(m_depressed, 0, m_locked, group);
        return;
    }

    if (code == KEY_CAPSLOCK) {
        ctx->sendKey(static_cast<uint32_t>(code), pressed);
        if (pressed) {
            m_locked ^= m_keymap ? m_keymap->capsMask() : (1u << 1);
            ctx->sendModifiers(m_depressed, 0, m_locked, group);
        }
        return;
    }

    ctx->sendKey(static_cast<uint32_t>(code), pressed);
}
