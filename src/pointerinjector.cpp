#include "pointerinjector.h"

#include <QDebug>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
constexpr int kAbsMax = 65535;
}

PointerInjector::~PointerInjector()
{
    close();
}

bool PointerInjector::open()
{
    if (m_fd >= 0) {
        return true;
    }
    m_fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        m_error = QStringLiteral("cannot open /dev/uinput: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    auto fail = [this](const char *what) {
        m_error = QStringLiteral("%1 failed: %2").arg(QLatin1String(what), QString::fromLocal8Bit(strerror(errno)));
        ::close(m_fd);
        m_fd = -1;
        return false;
    };

    if (ioctl(m_fd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(m_fd, UI_SET_KEYBIT, BTN_LEFT) < 0) {
        return fail("UI_SET_KEYBIT");
    }
    if (ioctl(m_fd, UI_SET_EVBIT, EV_ABS) < 0 || ioctl(m_fd, UI_SET_ABSBIT, ABS_X) < 0 || ioctl(m_fd, UI_SET_ABSBIT, ABS_Y) < 0) {
        return fail("UI_SET_ABSBIT");
    }

    struct uinput_abs_setup abs;
    for (unsigned short code : {static_cast<unsigned short>(ABS_X), static_cast<unsigned short>(ABS_Y)}) {
        memset(&abs, 0, sizeof(abs));
        abs.code = code;
        abs.absinfo.minimum = 0;
        abs.absinfo.maximum = kAbsMax;
        abs.absinfo.resolution = 0;
        if (ioctl(m_fd, UI_ABS_SETUP, &abs) < 0) {
            return fail("UI_ABS_SETUP");
        }
    }

    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x5642;
    setup.id.product = 0x4d53; // "MS"
    setup.id.version = 1;
    strncpy(setup.name, "vkbd virtual pointer", UINPUT_MAX_NAME_SIZE - 1);
    if (ioctl(m_fd, UI_DEV_SETUP, &setup) < 0) {
        return fail("UI_DEV_SETUP");
    }
    if (ioctl(m_fd, UI_DEV_CREATE) < 0) {
        return fail("UI_DEV_CREATE");
    }
    qInfo() << "vkbd: created virtual pointer device";
    return true;
}

void PointerInjector::close()
{
    if (m_fd >= 0) {
        ioctl(m_fd, UI_DEV_DESTROY);
        ::close(m_fd);
        m_fd = -1;
    }
}

void PointerInjector::emitEvent(unsigned short type, unsigned short code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    gettimeofday(&ev.time, nullptr);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (::write(m_fd, &ev, sizeof(ev)) != static_cast<ssize_t>(sizeof(ev))) {
        qWarning() << "vkbd: pointer write failed:" << strerror(errno);
    }
}

void PointerInjector::moveTo(double nx, double ny)
{
    if (m_fd < 0) {
        return;
    }
    const int x = static_cast<int>(std::clamp(nx, 0.0, 1.0) * kAbsMax);
    const int y = static_cast<int>(std::clamp(ny, 0.0, 1.0) * kAbsMax);
    emitEvent(EV_ABS, ABS_X, x);
    emitEvent(EV_ABS, ABS_Y, y);
    emitEvent(EV_SYN, SYN_REPORT, 0);
}

void PointerInjector::leftButton(bool down)
{
    if (m_fd < 0) {
        return;
    }
    emitEvent(EV_KEY, BTN_LEFT, down ? 1 : 0);
    emitEvent(EV_SYN, SYN_REPORT, 0);
}
