#pragma once

#include <QString>

// A virtual absolute-position mouse through /dev/uinput. libinput classifies
// a device with ABS_X/ABS_Y plus BTN_LEFT as an "absolute mouse" (like a VM
// tablet), and the compositor maps its coordinates onto the screen. Used to
// replay a finger gesture as a real pointer drag for text selection.
class PointerInjector
{
public:
    PointerInjector() = default;
    ~PointerInjector();

    bool open();
    void close();
    bool isOpen() const { return m_fd >= 0; }
    QString error() const { return m_error; }

    // Normalised screen coordinates, 0..1 in both axes.
    void moveTo(double nx, double ny);
    void leftButton(bool down);

private:
    void emitEvent(unsigned short type, unsigned short code, int value);

    int m_fd = -1;
    QString m_error;
};
