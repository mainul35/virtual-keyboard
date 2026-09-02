#include "inputmethod.h"

#include "keymap.h"

#include <QDebug>

#include <wayland-client-protocol.h>

#include <chrono>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

uint32_t nowMs()
{
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace

// C-style wl_keyboard listener callbacks that forward into the context.
struct ContextTrampolines {
    static void keymap(void *data, wl_keyboard *, uint32_t format, int32_t fd, uint32_t size);
    static void enter(void *, wl_keyboard *, uint32_t, wl_surface *, wl_array *) {}
    static void leave(void *, wl_keyboard *, uint32_t, wl_surface *) {}
    static void key(void *data, wl_keyboard *, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
    static void modifiers(void *data, wl_keyboard *, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
    static void repeatInfo(void *, wl_keyboard *, int32_t, int32_t) {}
};

const wl_keyboard_listener InputMethodContext::s_keyboardListener = {
    ContextTrampolines::keymap,
    ContextTrampolines::enter,
    ContextTrampolines::leave,
    ContextTrampolines::key,
    ContextTrampolines::modifiers,
    ContextTrampolines::repeatInfo,
};

// ------------------------------------------------------- InputMethodContext --

InputMethodContext::InputMethodContext(struct ::zwp_input_method_context_v1 *id, QObject *parent)
    : QObject(parent)
    , QtWayland::zwp_input_method_context_v1(id)
{
    m_keyboard = grab_keyboard();
    if (m_keyboard) {
        wl_keyboard_add_listener(m_keyboard, &s_keyboardListener, this);
    }
}

InputMethodContext::~InputMethodContext()
{
    if (m_keyboard) {
        wl_keyboard_destroy(m_keyboard);
        m_keyboard = nullptr;
    }
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

void InputMethodContext::sendModifiers(uint32_t depressed, uint32_t latched, uint32_t locked)
{
    m_modsKnown = true;
    m_lastDepressed = depressed;
    m_lastLatched = latched;
    m_lastLocked = locked;
    modifiers(m_serial, depressed, latched, locked, static_cast<uint32_t>(m_group));
}

void InputMethodContext::handleKeymap(uint32_t format, int fd, uint32_t size)
{
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) {
        ::close(fd);
        return;
    }
    void *mem = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mem == MAP_FAILED) {
        qWarning() << "vkbd: could not map keymap";
        return;
    }
    const QByteArray text(static_cast<const char *>(mem), static_cast<qsizetype>(size));
    munmap(mem, size);

    auto km = Keymap::fromString(text.left(text.indexOf('\0') >= 0 ? text.indexOf('\0') : text.size()));
    if (km) {
        m_keymap = std::move(km);
        Q_EMIT keymapChanged();
    }
}

void InputMethodContext::handleKey(uint32_t serial, uint32_t time, uint32_t keycode, uint32_t state)
{
    // A physical keyboard key arrived while we hold the grab: pass it on.
    key(serial, time, keycode, state);
}

void InputMethodContext::handleModifiers(uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
    const int g = static_cast<int>(group);
    if (g != m_group) {
        m_group = g;
        Q_EMIT groupChanged(g);
    }
    // Echoes of our own modifier updates come back through the grab; only
    // forward genuinely new state (physical keyboard) to avoid ping-pong.
    if (m_modsKnown && depressed == m_lastDepressed && latched == m_lastLatched && locked == m_lastLocked) {
        return;
    }
    m_modsKnown = true;
    m_lastDepressed = depressed;
    m_lastLatched = latched;
    m_lastLocked = locked;
    modifiers(serial, depressed, latched, locked, group);
}

void ContextTrampolines::keymap(void *data, wl_keyboard *, uint32_t format, int32_t fd, uint32_t size)
{
    static_cast<InputMethodContext *>(data)->handleKeymap(format, fd, size);
}

void ContextTrampolines::key(void *data, wl_keyboard *, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    static_cast<InputMethodContext *>(data)->handleKey(serial, time, key, state);
}

void ContextTrampolines::modifiers(void *data, wl_keyboard *, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
    static_cast<InputMethodContext *>(data)->handleModifiers(serial, depressed, latched, locked, group);
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

InputMethodInjector::InputMethodInjector(InputMethod *im, QObject *parent)
    : QObject(parent)
    , m_im(im)
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
    const auto km = m_im->context() ? m_im->context()->keymap() : nullptr;
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
    ctx->sendKey(static_cast<uint32_t>(code), pressed);

    const uint32_t mask = maskFor(code);
    if (mask) {
        if (pressed) {
            m_depressed |= mask;
        } else {
            m_depressed &= ~mask;
        }
        ctx->sendModifiers(m_depressed, 0, m_locked);
    } else if (code == KEY_CAPSLOCK && pressed) {
        const auto km = ctx->keymap();
        m_locked ^= km ? km->capsMask() : (1u << 1);
        ctx->sendModifiers(m_depressed, 0, m_locked);
    }
}
