#include "uinputinjector.h"

#include <QDebug>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

UinputInjector::UinputInjector()
{
    m_fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        m_error = QStringLiteral("cannot open /dev/uinput: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return;
    }

    auto fail = [this](const char *what) {
        m_error = QStringLiteral("%1 failed: %2").arg(QLatin1String(what), QString::fromLocal8Bit(strerror(errno)));
        ::close(m_fd);
        m_fd = -1;
    };

    if (ioctl(m_fd, UI_SET_EVBIT, EV_KEY) < 0) {
        fail("UI_SET_EVBIT");
        return;
    }
    // Plain keyboard keys only. Deliberately stop before the BTN_* range so
    // libinput never mistakes the device for a mouse or joystick.
    for (int code = 1; code <= KEY_MICMUTE; ++code) {
        ioctl(m_fd, UI_SET_KEYBIT, code);
    }

    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x5642;  // "VB"
    setup.id.product = 0x4b42; // "KB"
    setup.id.version = 1;
    strncpy(setup.name, "vkbd virtual keyboard", UINPUT_MAX_NAME_SIZE - 1);

    if (ioctl(m_fd, UI_DEV_SETUP, &setup) < 0) {
        fail("UI_DEV_SETUP");
        return;
    }
    if (ioctl(m_fd, UI_DEV_CREATE) < 0) {
        fail("UI_DEV_CREATE");
        return;
    }
}

UinputInjector::~UinputInjector()
{
    if (m_fd >= 0) {
        ioctl(m_fd, UI_DEV_DESTROY);
        ::close(m_fd);
    }
}

void UinputInjector::emitEvent(unsigned short type, unsigned short code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    gettimeofday(&ev.time, nullptr);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (::write(m_fd, &ev, sizeof(ev)) != static_cast<ssize_t>(sizeof(ev))) {
        qWarning() << "vkbd: uinput write failed:" << strerror(errno);
    }
}

void UinputInjector::key(int code, bool pressed)
{
    if (m_fd < 0) {
        return;
    }
    emitEvent(EV_KEY, static_cast<unsigned short>(code), pressed ? 1 : 0);
    emitEvent(EV_SYN, SYN_REPORT, 0);
}
