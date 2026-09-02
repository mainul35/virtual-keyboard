#include "inputpanelshell.h"

#include <QDebug>
#include <QWindow>

#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

InputPanelShellIntegration::InputPanelShellIntegration()
    : QtWaylandClient::QWaylandShellIntegrationTemplate<InputPanelShellIntegration>(1)
{
}

InputPanelShellIntegration::~InputPanelShellIntegration() = default;

QtWaylandClient::QWaylandShellSurface *InputPanelShellIntegration::createShellSurface(QtWaylandClient::QWaylandWindow *window)
{
    if (!isActive()) {
        return nullptr;
    }
    struct ::zwp_input_panel_surface_v1 *surface = get_input_panel_surface(window->wlSurface());
    return new InputPanelSurface(surface, window);
}

InputPanelSurface::InputPanelSurface(struct ::zwp_input_panel_surface_v1 *object, QtWaylandClient::QWaylandWindow *window)
    : QtWaylandClient::QWaylandShellSurface(window)
    , QtWayland::zwp_input_panel_surface_v1(object)
{
    window->applyConfigureWhenPossible();
}

InputPanelSurface::~InputPanelSurface()
{
    zwp_input_panel_surface_v1_destroy(object());
}

void InputPanelSurface::applyConfigure()
{
    QtWaylandClient::QWaylandScreen *screen = window()->waylandScreen();
    if (!screen) {
        qWarning() << "vkbd: no Wayland screen for the input panel surface";
        return;
    }
    set_toplevel(screen->output(), position_center_bottom);
    window()->display()->handleWindowActivated(window());
}

bool initInputPanelIntegration(QWindow *window)
{
    if (!window) {
        return false;
    }
    window->create();
    auto *waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow *>(window->handle());
    if (!waylandWindow) {
        return false;
    }

    static InputPanelShellIntegration *integration = nullptr;
    if (!integration) {
        integration = new InputPanelShellIntegration();
        if (!integration->initialize(waylandWindow->display())) {
            delete integration;
            integration = nullptr;
            qWarning() << "vkbd: compositor did not offer zwp_input_panel_v1 on this connection";
            return false;
        }
    }
    waylandWindow->setShellIntegration(integration);
    return true;
}
