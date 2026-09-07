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
// Time for a tray menu to close and keyboard focus to return to the app.
constexpr int kChordDelayMs = 400;
// Auto-hide grace periods after the text field lost focus: quick when another
// application became active, generous when focus went to a popup/the desktop
// (no active toplevel) or stayed in the same window, so menus opened from the
// panel or our tray icon do not make the keyboard vanish.
constexpr int kHideAfterAppSwitchMs = 250;
constexpr int kHideAfterFocusLossMs = 2500;
}

Controller::Controller(KeyboardWidget *keyboard, Mode mode, QObject *parent)
    : QObject(parent)
    , m_keyboard(keyboard)
    , m_mode(mode)
{
    // Focus hops between two text fields produce deactivate+activate back to
    // back; a short grace period avoids the panel flashing.
    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, &Controller::autoHideNow);
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
    // Bring it back afterwards if it was on screen, or if a text field is
    // still active (the tray menu may have hidden it moments ago).
    m_wasVisibleBeforeScreenshot = isVisible() || m_imActive;
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

void Controller::sendChordSoon(int modifierCode, int code)
{
    QTimer::singleShot(kChordDelayMs, this, [this, modifierCode, code] {
        m_keyboard->tapChord(modifierCode, code);
    });
}

void Controller::copy()
{
    sendChordSoon(KEY_LEFTCTRL, KEY_C);
}

void Controller::paste()
{
    sendChordSoon(KEY_LEFTCTRL, KEY_V);
}

void Controller::selectAll()
{
    sendChordSoon(KEY_LEFTCTRL, KEY_A);
}

void Controller::finishScreenshot()
{
    if (!m_screenshotInProgress) {
        return;
    }
    m_screenshotInProgress = false;
    m_screenshotTimeout.stop();
    if (m_wasVisibleBeforeScreenshot) {
        showPanel();
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

void Controller::showPanel()
{
    if (!m_keyboard->isVisible()) {
        m_keyboard->show();
    }
    if (m_afterShow) {
        m_afterShow();
    }
}

void Controller::setAutoShowSuppressed(bool suppressed)
{
    if (m_autoShowSuppressed == suppressed) {
        return;
    }
    m_autoShowSuppressed = suppressed;
    if (suppressed) {
        // A keyboard was plugged in: put the panel away unless the user
        // asked for it explicitly.
        if (!m_shownExplicitly && m_keyboard->isVisible()) {
            m_keyboard->releaseAll();
            m_keyboard->hide();
        }
    } else if (m_imActive) {
        showPanel();
    }
}

void Controller::show()
{
    m_hideTimer.stop();
    m_shownExplicitly = true;
    showPanel();
    if (m_mode == Mode::InputPanel) {
        // forceActivate makes KWin activate us even if the focused window has
        // no text field (desktop, file manager). The first call triggers the
        // activation while our surface may still be unmapped; a second one
        // once it is mapped makes KWin actually show the panel.
        kwinForceActivate();
        QTimer::singleShot(300, this, [this] {
            if (!m_hideTimer.isActive()) {
                kwinForceActivate();
            }
        });
    }
}

void Controller::hide()
{
    m_hideTimer.stop();
    m_shownExplicitly = false;
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
    m_imActive = true;
    m_hideTimer.stop();
    m_activeWindowAtShow = m_activeWindow ? m_activeWindow() : QString();
    if (m_autoShowSuppressed && !m_shownExplicitly) {
        return; // a physical keyboard is connected; stay out of the way
    }
    // In input-panel mode KWin decides whether the mapped panel is shown.
    showPanel();
}

void Controller::imDeactivated()
{
    m_imActive = false;
    m_keyboard->releaseAll();
    // Unmap once focus has really left a text field. KWin does not hide an
    // input panel on its own when focus moves to a window without text input
    // (the desktop, a file manager ...); Maliit and plasma-keyboard unmap too,
    // and KWin re-shows the surface when it is mapped again on activation.
    scheduleAutoHide();
}

void Controller::activeWindowChanged(const QString &id)
{
    // Another application became active while no text field is focused.
    if (!m_imActive && m_keyboard->isVisible() && !id.isEmpty() && id != m_activeWindowAtShow) {
        m_hideTimer.start(kHideAfterAppSwitchMs);
    }
}

void Controller::holdOpen(bool hold)
{
    m_holdOpen = hold;
    if (hold) {
        m_hideTimer.stop();
        // Safety net: a menu whose close notification never arrives must not
        // pin the keyboard on screen forever.
        QTimer::singleShot(30000, this, [this] {
            if (m_holdOpen) {
                holdOpen(false);
            }
        });
    } else if (!m_imActive && m_keyboard->isVisible()) {
        scheduleAutoHide();
    }
}

void Controller::scheduleAutoHide()
{
    if (m_holdOpen) {
        return;
    }
    const QString now = m_activeWindow ? m_activeWindow() : QString();
    const bool switchedApp = !now.isEmpty() && now != m_activeWindowAtShow;
    m_hideTimer.start(switchedApp ? kHideAfterAppSwitchMs : kHideAfterFocusLossMs);
}

void Controller::autoHideNow()
{
    m_shownExplicitly = false;
    if (m_imActive || m_holdOpen || m_screenshotInProgress) {
        return;
    }
    m_keyboard->hide();
}
