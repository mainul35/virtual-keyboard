#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QtWaylandClient/QWaylandClientExtension>

#include "qwayland-plasma-window-management.h"

// Follows which toplevel window Plasma considers active, through KWin's
// org_kde_plasma_window_management protocol. Used to tell "focus moved to a
// popup or the desktop" from "the user switched to another application"
// when deciding whether to hide the keyboard.
class WindowTracker : public QWaylandClientExtensionTemplate<WindowTracker>, public QtWayland::org_kde_plasma_window_management
{
    Q_OBJECT
public:
    WindowTracker();
    ~WindowTracker() override;

    using QWaylandClientExtensionTemplate<WindowTracker>::initialize;

    // Identifier of the active toplevel, empty when none is active (desktop,
    // panel popups, lock screen ...).
    QString activeWindow() const { return m_active; }

Q_SIGNALS:
    void activeWindowChanged(const QString &id);

protected:
    void org_kde_plasma_window_management_window(uint32_t id) override;
    void org_kde_plasma_window_management_window_with_uuid(uint32_t id, const QString &uuid) override;

private:
    class Window;
    friend class Window;
    void addWindow(struct ::org_kde_plasma_window *object, const QString &id);
    void windowActiveChanged(const QString &id, bool active);
    void windowUnmapped(const QString &id);

    QHash<QString, Window *> m_windows;
    QString m_active;
};
