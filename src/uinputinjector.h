#pragma once

#include "injector.h"

// Creates a virtual keyboard device through /dev/uinput. Works on X11 and on any
// Wayland compositor, and modifiers behave exactly like on a physical keyboard
// because the events travel the normal kernel -> libinput -> compositor path.
// Needs write access to /dev/uinput (see data/60-vkbd-uinput.rules).
//
// The device is created lazily on first use so that, while KWin drives the
// keyboard through the input-method protocol, no extra keyboard device exists.
class UinputInjector : public Injector
{
public:
    UinputInjector() = default;
    ~UinputInjector() override;

    QString name() const override { return QStringLiteral("uinput"); }
    bool isReady() const override;
    void key(int code, bool pressed) override;

    // Try to create the device now (used by --self-test); returns success.
    bool open();
    bool isOpen() const { return m_fd >= 0; }
    QString error() const { return m_error; }

private:
    void emitEvent(unsigned short type, unsigned short code, int value);

    int m_fd = -1;
    bool m_attempted = false;
    QString m_error;
};
