#include "windowtracker.h"

#include <QDebug>
#include <QTimer>

namespace {
constexpr uint32_t kStateActive = 0x1; // org_kde_plasma_window_management.state.active
constexpr int kRequestedVersion = 16;
}

class WindowTracker::Window : public QtWayland::org_kde_plasma_window
{
public:
    Window(WindowTracker *tracker, struct ::org_kde_plasma_window *object, const QString &id)
        : QtWayland::org_kde_plasma_window(object)
        , m_tracker(tracker)
        , m_id(id)
    {
    }

    ~Window() override
    {
        if (object()) {
            if (version() >= 4) {
                destroy();
            } else {
                wl_proxy_destroy(reinterpret_cast<struct wl_proxy *>(object()));
            }
        }
    }

protected:
    void org_kde_plasma_window_state_changed(uint32_t flags) override
    {
        const bool active = flags & kStateActive;
        if (active != m_active) {
            m_active = active;
            m_tracker->windowActiveChanged(m_id, active);
        }
    }

    void org_kde_plasma_window_unmapped() override
    {
        m_tracker->windowUnmapped(m_id);
    }

private:
    WindowTracker *m_tracker;
    QString m_id;
    bool m_active = false;
};

WindowTracker::WindowTracker()
    : QWaylandClientExtensionTemplate<WindowTracker>(kRequestedVersion)
{
}

WindowTracker::~WindowTracker()
{
    qDeleteAll(m_windows);
    m_windows.clear();
}

void WindowTracker::org_kde_plasma_window_management_window(uint32_t id)
{
    // Legacy event (compositors older than protocol version 13).
    if (QtWayland::org_kde_plasma_window_management::version() >= 13) {
        return; // window_with_uuid follows
    }
    addWindow(get_window(id), QString::number(id));
}

void WindowTracker::org_kde_plasma_window_management_window_with_uuid(uint32_t id, const QString &uuid)
{
    Q_UNUSED(id);
    addWindow(get_window_by_uuid(uuid), uuid);
}

void WindowTracker::addWindow(struct ::org_kde_plasma_window *object, const QString &id)
{
    if (!object || m_windows.contains(id)) {
        return;
    }
    m_windows.insert(id, new Window(this, object, id));
}

void WindowTracker::windowActiveChanged(const QString &id, bool active)
{
    if (active) {
        if (m_active != id) {
            m_active = id;
            Q_EMIT activeWindowChanged(m_active);
        }
    } else if (m_active == id) {
        m_active.clear();
        Q_EMIT activeWindowChanged(m_active);
    }
}

void WindowTracker::windowUnmapped(const QString &id)
{
    Window *w = m_windows.take(id);
    if (!w) {
        return;
    }
    if (m_active == id) {
        m_active.clear();
        Q_EMIT activeWindowChanged(m_active);
    }
    // Do not delete from inside the event handler.
    QTimer::singleShot(0, this, [w] { delete w; });
}
