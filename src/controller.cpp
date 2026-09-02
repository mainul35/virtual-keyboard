#include "controller.h"

#include "keyboardwidget.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QVariant>

namespace {
const auto kKwinService = QStringLiteral("org.kde.KWin");
const auto kKwinPath = QStringLiteral("/VirtualKeyboard");
const auto kKwinIface = QStringLiteral("org.kde.kwin.VirtualKeyboard");
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
        m_keyboard->hide();
    });
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
    switch (m_mode) {
    case Mode::InputPanel:
        kwinDeactivate();
        break;
    case Mode::LayerShell:
    case Mode::Plain:
        m_keyboard->hide();
        break;
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
    if (m_mode != Mode::InputPanel) {
        m_hideTimer.start();
    }
}
