#include "doctor.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#include <fcntl.h>
#include <unistd.h>

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

void line(const QString &label, const QString &value)
{
    out() << QString::fromLatin1("%1 %2\n").arg(label.leftJustified(28, QLatin1Char('.')), value);
    out().flush();
}

QString yesNo(bool b)
{
    return b ? QStringLiteral("yes") : QStringLiteral("no");
}

} // namespace

int runDoctor()
{
    int problems = 0;
    QDBusConnection bus = QDBusConnection::sessionBus();

    out() << "== session ==\n";
    line(QStringLiteral("platform"), QGuiApplication::platformName());
    line(QStringLiteral("XDG_SESSION_TYPE"), QString::fromLocal8Bit(qgetenv("XDG_SESSION_TYPE")));
    line(QStringLiteral("session bus"), yesNo(bus.isConnected()));

    out() << "\n== KWin configuration (~/.config/kwinrc) ==\n";
    const QString kwinrc = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrc");
    QString imPath;
    if (QFile::exists(kwinrc)) {
        QSettings s(kwinrc, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("Wayland"));
        imPath = s.value(QStringLiteral("InputMethod")).toString();
        line(QStringLiteral("[Wayland] InputMethod"), imPath.isEmpty() ? QStringLiteral("(unset: no virtual keyboard selected)") : imPath);
        if (s.contains(QStringLiteral("VirtualKeyboardMode"))) {
            line(QStringLiteral("VirtualKeyboardMode"), s.value(QStringLiteral("VirtualKeyboardMode")).toString() + QStringLiteral(" (0=never 1=touch/pen 2=any input)"));
        } else {
            line(QStringLiteral("VirtualKeyboardEnabled"), s.value(QStringLiteral("VirtualKeyboardEnabled"), true).toString());
        }
    } else {
        line(QStringLiteral("kwinrc"), QStringLiteral("not found"));
    }
    if (!imPath.isEmpty()) {
        const bool exists = QFile::exists(imPath);
        line(QStringLiteral("desktop file exists"), yesNo(exists));
        if (!exists) {
            ++problems;
        } else {
            QSettings d(imPath, QSettings::IniFormat);
            d.beginGroup(QStringLiteral("Desktop Entry"));
            const QString exec = d.value(QStringLiteral("Exec")).toString();
            line(QStringLiteral("Exec"), exec);
            const QString program = exec.section(QLatin1Char(' '), 0, 0);
            const QString resolved = program.startsWith(QLatin1Char('/')) ? (QFile::exists(program) ? program : QString())
                                                                        : QStandardPaths::findExecutable(program);
            line(QStringLiteral("Exec resolves to"), resolved.isEmpty() ? QStringLiteral("NOT FOUND (KWin cannot start it)") : resolved);
            if (resolved.isEmpty()) {
                ++problems;
            }
            const bool isVkbd = program.contains(QLatin1String("vkbd"));
            line(QStringLiteral("selected keyboard is vkbd"), yesNo(isVkbd));
        }
    }

    out() << "\n== KWin virtual keyboard (org.kde.kwin.VirtualKeyboard) ==\n";
    QDBusInterface kwin(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualKeyboard"), QStringLiteral("org.kde.kwin.VirtualKeyboard"), bus);
    if (kwin.isValid()) {
        line(QStringLiteral("available"), kwin.property("available").toString());
        line(QStringLiteral("active"), kwin.property("active").toString());
        line(QStringLiteral("visible"), kwin.property("visible").toString());
        line(QStringLiteral("activeClientSupportsTextInput"), kwin.property("activeClientSupportsTextInput").toString());
        const QVariant mode = kwin.property("mode");
        if (mode.isValid()) {
            line(QStringLiteral("mode"), mode.toString());
        }
        const QVariant enabled = kwin.property("enabled");
        if (enabled.isValid()) {
            line(QStringLiteral("enabled"), enabled.toString());
        }
        if (!kwin.property("available").toBool()) {
            ++problems;
            out() << "   -> KWin reports no input method available: it did not start the configured keyboard.\n";
        }
    } else {
        line(QStringLiteral("interface"), QStringLiteral("not reachable (not a KWin Wayland session?)"));
    }

    out() << "\n== running vkbd instance ==\n";
    const QString service = QStringLiteral("org.vkbd.Keyboard");
    const bool running = bus.interface() && bus.interface()->isServiceRegistered(service);
    line(QStringLiteral("D-Bus name owned"), yesNo(running));
    if (running) {
        QDBusReply<uint> pid = bus.interface()->servicePid(service);
        line(QStringLiteral("pid"), pid.isValid() ? QString::number(pid.value()) : QStringLiteral("?"));
        QDBusInterface me(service, QStringLiteral("/"), service, bus);
        QDBusReply<QString> status = me.call(QStringLiteral("status"));
        line(QStringLiteral("status"), status.isValid() ? status.value() : QStringLiteral("(older instance, no status)"));
        if (status.isValid() && !status.value().contains(QLatin1String("launchedByKwin=yes"))) {
            out() << "   -> this instance was started by hand, not by KWin. Quit it (vkbd --quit) so KWin's own instance can run.\n";
            ++problems;
        }
    } else {
        out() << "   -> no vkbd is running. If KWin is configured to use it, check the log below for why it exited.\n";
        ++problems;
    }

    out() << "\n== backends ==\n";
    {
        const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        line(QStringLiteral("/dev/uinput writable"), yesNo(fd >= 0));
        if (fd >= 0) {
            ::close(fd);
        }
    }
    line(QStringLiteral("spectacle"), QStandardPaths::findExecutable(QStringLiteral("spectacle")).isEmpty() ? QStringLiteral("not found") : QStringLiteral("found"));

    out() << "\n== last log (~/.cache/vkbd/vkbd.log) ==\n";
    const QString logPath = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/vkbd/vkbd.log");
    QFile log(logPath);
    if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const int start = qMax(0, static_cast<int>(lines.size()) - 40);
        for (int i = start; i < lines.size(); ++i) {
            out() << "   " << lines[i] << '\n';
        }
    } else {
        out() << "   (no log yet)\n";
    }
    out() << "\n" << (problems ? QStringLiteral("%1 potential problem(s) found.").arg(problems) : QStringLiteral("No obvious problems found.")) << '\n';
    out().flush();
    return problems ? 1 : 0;
}
