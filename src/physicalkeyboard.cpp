#include "physicalkeyboard.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>

#include <cstring>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

constexpr unsigned short kVendorVkbd = 0x5642;
constexpr unsigned short kVendorKeyd = 0x0fac;

// SMBIOS chassis type: 30 tablet, 31 convertible, 32 detachable. On those,
// an internal i8042 ("AT Translated Set 2") keyboard is a phantom device that
// exists whether or not a keyboard dock is attached; docks come over USB,
// Bluetooth or I2C, so the internal one is ignored there.
bool tabletLikeChassis()
{
    static int cached = -1;
    if (cached < 0) {
        cached = 0;
        QFile f(QStringLiteral("/sys/class/dmi/id/chassis_type"));
        if (f.open(QIODevice::ReadOnly)) {
            const int type = f.readAll().trimmed().toInt();
            cached = (type == 30 || type == 31 || type == 32) ? 1 : 0;
        }
    }
    return cached == 1;
}

bool hasBit(const unsigned long *bits, int code)
{
    const size_t word = code / (8 * sizeof(unsigned long));
    const size_t bit = code % (8 * sizeof(unsigned long));
    return (bits[word] >> bit) & 1UL;
}

// A device counts as a real keyboard when it has (almost) all letter keys,
// Space, Enter, Backspace and Shift, a plausible total number of keys, no
// touch/pen/absolute axes, sits on a hardware bus and is not a known virtual
// device. ACPI hotkey devices often advertise every key code there is; the
// upper bound on the key count filters those out.
bool isPhysicalKeyboard(int fd, QString *name, QString *details)
{
    char nameBuf[128] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof(nameBuf) - 1), nameBuf) < 0) {
        return false;
    }
    const QString devName = QString::fromLocal8Bit(nameBuf);
    if (name) {
        *name = devName;
    }

    struct input_id id;
    memset(&id, 0, sizeof(id));
    if (ioctl(fd, EVIOCGID, &id) < 0) {
        return false;
    }
    if (details) {
        *details = QStringLiteral("bus 0x%1").arg(id.bustype, 2, 16, QLatin1Char('0'));
    }
    if (id.bustype == BUS_VIRTUAL || id.bustype == BUS_HOST) {
        return false; // uinput devices, ACPI / platform hotkey devices
    }
    if (id.bustype == BUS_I8042 && tabletLikeChassis()) {
        return false; // phantom internal keyboard of a tablet / detachable
    }
    if (id.vendor == kVendorVkbd || id.vendor == kVendorKeyd) {
        return false;
    }
    if (devName.contains(QLatin1String("virtual"), Qt::CaseInsensitive)) {
        return false;
    }

    unsigned long evBits[(EV_MAX + 1) / (8 * sizeof(unsigned long)) + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) < 0 || !hasBit(evBits, EV_KEY)) {
        return false;
    }
    if (hasBit(evBits, EV_ABS)) {
        return false; // touchscreens, tablets, absolute pointers
    }
    unsigned long keyBits[(KEY_MAX + 1) / (8 * sizeof(unsigned long)) + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0) {
        return false;
    }
    if (hasBit(keyBits, BTN_TOUCH) || hasBit(keyBits, BTN_TOOL_PEN) || hasBit(keyBits, BTN_LEFT)) {
        return false;
    }
    int total = 0;
    for (int code = 0; code <= KEY_MAX; ++code) {
        if (hasBit(keyBits, code)) {
            ++total;
        }
    }
    if (details) {
        *details += QStringLiteral(", %1 keys").arg(total);
    }
    if (total < 40 || total > 400) {
        return false; // too few for a keyboard, or "declares everything"
    }

    static const int letters[] = {KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
                                  KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
                                  KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M};
    int found = 0;
    for (int code : letters) {
        if (hasBit(keyBits, code)) {
            ++found;
        }
    }
    return found >= 20 && hasBit(keyBits, KEY_SPACE) && hasBit(keyBits, KEY_ENTER)
        && hasBit(keyBits, KEY_BACKSPACE) && hasBit(keyBits, KEY_LEFTSHIFT);
}

} // namespace

QStringList PhysicalKeyboardWatcher::scan(const QStringList &ignore)
{
    QStringList names;
    const QDir dir(QStringLiteral("/dev/input"));
    const QStringList nodes = dir.entryList({QStringLiteral("event*")}, QDir::System | QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &node : nodes) {
        const QByteArray path = dir.filePath(node).toLocal8Bit();
        const int fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        QString name;
        QString details;
        if (isPhysicalKeyboard(fd, &name, &details) && !ignore.contains(name, Qt::CaseInsensitive)) {
            names.append(QStringLiteral("%1 (%2)").arg(name, details));
        }
        ::close(fd);
    }
    return names;
}

PhysicalKeyboardWatcher::PhysicalKeyboardWatcher(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(700);
    connect(&m_debounce, &QTimer::timeout, this, &PhysicalKeyboardWatcher::rescan);

    m_watcher = new QFileSystemWatcher(this);
    if (QDir(QStringLiteral("/dev/input")).exists()) {
        m_watcher->addPath(QStringLiteral("/dev/input"));
    }
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        // Device nodes appear before udev has fixed their permissions; scan
        // once soon and once more a little later.
        m_debounce.start();
        QTimer::singleShot(2500, this, &PhysicalKeyboardWatcher::rescan);
    });

    m_names = scan(m_ignore);
}

void PhysicalKeyboardWatcher::rescan()
{
    const QStringList now = scan(m_ignore);
    if (now == m_names) {
        return;
    }
    const bool was = !m_names.isEmpty();
    m_names = now;
    if (was != present()) {
        qInfo() << "vkbd: physical keyboard" << (present() ? "connected:" : "disconnected") << m_names;
        Q_EMIT presenceChanged(present());
    }
}
