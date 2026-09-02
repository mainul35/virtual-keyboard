#include "slide.h"

#include <QDebug>
#include <QHash>
#include <QWindow>
#include <QtWaylandClient/QWaylandClientExtension>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

#include "qwayland-slide.h"

namespace {

class SlideManager : public QWaylandClientExtensionTemplate<SlideManager>, public QtWayland::org_kde_kwin_slide_manager
{
public:
    SlideManager()
        : QWaylandClientExtensionTemplate<SlideManager>(1)
    {
    }
    using QWaylandClientExtensionTemplate<SlideManager>::initialize;
};

class Slide : public QtWayland::org_kde_kwin_slide
{
public:
    explicit Slide(struct ::org_kde_kwin_slide *obj)
        : QtWayland::org_kde_kwin_slide(obj)
    {
    }
    ~Slide()
    {
        release();
    }
};

struct Applied {
    ::wl_surface *surface = nullptr;
    Slide *slide = nullptr;
};

} // namespace

void applySlideFromBottom(QWindow *window)
{
    static SlideManager *manager = nullptr;
    static bool triedManager = false;
    static QHash<QWindow *, Applied> applied;

    if (!window) {
        return;
    }
    auto *waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow *>(window->handle());
    if (!waylandWindow) {
        return;
    }
    ::wl_surface *surface = waylandWindow->wlSurface();
    if (!surface) {
        return;
    }

    if (!triedManager) {
        triedManager = true;
        manager = new SlideManager();
        manager->initialize();
        if (!manager->isActive()) {
            qInfo() << "vkbd: compositor does not offer org_kde_kwin_slide, no slide animation";
            delete manager;
            manager = nullptr;
        }
    }
    if (!manager) {
        return;
    }

    Applied &a = applied[window];
    if (a.surface == surface && a.slide) {
        return; // already set up for this surface
    }
    delete a.slide;
    a.slide = new Slide(manager->create(surface));
    a.surface = surface;
    a.slide->set_location(QtWayland::org_kde_kwin_slide::location_bottom);
    a.slide->set_offset(0);
    a.slide->commit();
}
