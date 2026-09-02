#pragma once

#include "injector.h"

// Creates a virtual keyboard device through /dev/uinput. Works on X11 and on any
// Wayland compositor, and modifiers behave exactly like on a physical keyboard
// because the events travel the normal kernel -> libinput -> compositor path.
// Needs write access to /dev/uinput (see data/60-vkbd-uinput.rules).
class UinputInjector : public Injector
{
public:
    UinputInjector();
    ~UinputInjector() override;

    QString name() const override { return QStringLiteral("uinput"); }
    bool isReady() const override { return m_fd >= 0; }
    void key(int code, bool pressed) override;

    QString error() const { return m_error; }

private:
    void emitEvent(unsigned short type, unsigned short code, int value);

    int m_fd = -1;
    QString m_error;
};
