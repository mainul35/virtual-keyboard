#include "controller.h"

#include "keyboardwidget.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QProcess>
#include <QStandardPaths>
#include <QVariant>

#include <linux/input-event-codes.h>

namespace {
const auto kKwinService = QStringLiteral("org.kde.KWin");
const auto kKwinPath = QStringLiteral("/VirtualKeyboard");
const auto kKwinIface = QStringLiteral("org.kde.kwin.VirtualKeyboard");
// Give the compositor a frame or two to actually unmap the panel before capturing.
constexpr int kScreenshotHideDelayMs = 350;
constexpr int kScreenshotTimeoutMs = 8000;
}

Controller::Controller(KeyboardWidget *keyboard, Mode mode, QObject *parent)
    : QObject(parent)
    , m_keyboard(keyboard)
    , m_mode(mode)
{
    // Focus hops between two text fields produce deactivate+activate back to
    // back; a short grace period avoids the panel flashing.
    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(250);
    connect(&m_hideTimer, &QTimer::timeout, this, [this] {
        if (!m_screenshotInProgress) {
            m_keyboard->hide();
        }
    });
    m_screenshotTimeout.setSingleShot(true);
    m_screenshotTimeout.setInterval(kScreenshotTimeoutMs);
    connect(&m_screenshotTimeout, &QTimer::timeout, this, &Controller::finishScreenshot);
}

void Controller::screenshot()
{
    if (m_screenshotInProgress) {
        return;
    }
    const QString spectacle = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
    if (spectacle.isEmpty()) {
        // No Spectacle: send the real Print key and let the session's own
        // shortcut handle it (works on the uinput path).
        qWarning() << "vkbd: spectacle not found, sending Print key instead";
        m_keyboard->tapKey(KEY_SYSRQ);
        return;
    }

    m_screenshotInProgress = true;
    m_wasVisibleBeforeScreenshot = isVisible();
    m_keyboard->releaseAll();
    m_keyboard->hide(); // just unmap; keep the input-method context alive

    QTimer::singleShot(kScreenshotHideDelayMs, this, [this, spectacle] {
        auto *proc = new QProcess(this);
        // -b: no GUI, -f: full screen, -c: copy image to clipboard, -n: quiet
        proc->setProgram(spectacle);
        proc->setArguments({QStringLiteral("-b"), QStringLiteral("-f"), QStringLiteral("-c")});
        connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
            if (code != 0) {
                qWarning() << "vkbd: spectacle exited with" << code;
            }
            proc->deleteLater();
            finishScreenshot();
        });
        connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError err) {
            qWarning() << "vkbd: could not run spectacle:" << err;
            proc->deleteLater();
            finishScreenshot();
        });
        m_screenshotTimeout.start();
        proc->start();
    });
}

void Controller::finishScreenshot()
{
    if (!m_screenshotInProgress) {
        return;
    }
    m_screenshotInProgress = false;
    m_screenshotTimeout.stop();
    if (m_wasVisibleBeforeScreenshot) {
        m_keyboard->show();
        if (m_mode == Mode::InputPanel) {
            kwinForceActivate(); // make sure KWin shows the re-mapped panel
        }
    }
}

bool Controller::kwinPanelVisible() const
{
    QDBusInterface kwin(kKwinService, kKwinPath, kKwinIface, QDBusConnection::sessionBus());
    if (!kwin.isValid()) {
        return m_keyboard->isVisible();
    }
    return kwin.property("visible").toBool();
}

void Controller::kwinForceActivate()
{
    QDBusInterface kwin(kKwinService, kKwinPath, kKwinIface, QDBusConnection::sessionBus());
    if (!kwin.isValid()) {
        qWarning() << "vkbd: KWin virtual keyboard D-Bus interface not reachable";
        return;
    }
    kwin.call(QDBus::NoBlock, QStringLiteral("forceActivate"));
}

void Controller::kwinDeactivate()
{
    QDBusInterface kwin(kKwinService, kKwinPath, kKwinIface, QDBusConnection::sessionBus());
    if (!kwin.isValid()) {
        qWarning() << "vkbd: KWin virtual keyboard D-Bus interface not reachable";
        return;
    }
    kwin.setProperty("active", false);
}

void Controller::show()
{
    m_hideTimer.stop();
    switch (m_mode) {
    case Mode::InputPanel:
        if (!m_keyboard->isVisible()) {
            m_keyboard->show();
        }
        kwinForceActivate();
        break;
    case Mode::LayerShell:
    case Mode::Plain:
        m_keyboard->show();
        break;
    }
}

void Controller::hide()
{
    m_hideTimer.stop();
    m_keyboard->releaseAll();
    // Unmapping the surface is what actually removes the panel from the screen.
    // In input-panel mode also deactivate the context, so that tapping the
    // same text field again re-activates it and brings the keyboard back.
    m_keyboard->hide();
    if (m_mode == Mode::InputPanel) {
        kwinDeactivate();
    }
}

void Controller::toggle()
{
    if (isVisible()) {
        hide();
    } else {
        show();
    }
}

bool Controller::isVisible() const
{
    if (m_mode == Mode::InputPanel) {
        return kwinPanelVisible();
    }
    return m_keyboard->isVisible();
}

QString Controller::status() const
{
    const char *mode = m_mode == Mode::InputPanel ? "input-panel" : m_mode == Mode::LayerShell ? "layer-shell" : "plain";
    return QStringLiteral("shell=%1 visible=%2 %3").arg(QLatin1String(mode), isVisible() ? QStringLiteral("yes") : QStringLiteral("no"), m_statusInfo);
}

void Controller::quit()
{
    m_keyboard->releaseAll();
    QCoreApplication::quit();
}

void Controller::imActivated()
{
    m_hideTimer.stop();
    switch (m_mode) {
    case Mode::InputPanel:
        // KWin decides whether the panel is shown; we only need to be mapped.
        if (!m_keyboard->isVisible()) {
            m_keyboard->show();
        }
        break;
    case Mode::LayerShell:
    case Mode::Plain:
        m_keyboard->show();
        break;
    }
}

void Controller::imDeactivated()
{
    m_keyboard->releaseAll();
    // Unmap once focus has really left a text field. KWin does not hide an
    // input panel on its own when focus moves to a window without text input
    // (the desktop, a file manager ...); Maliit and plasma-keyboard unmap too,
    // and KWin re-shows the surface when it is mapped again on activation.
    m_hideTimer.start();
}
