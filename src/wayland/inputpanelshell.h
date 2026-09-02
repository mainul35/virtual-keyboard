#pragma once

// Gives our QWindow the zwp_input_panel_surface_v1 role, which is how KWin
// recognises "this is the on-screen keyboard": it places the surface at the
// bottom of the screen, never gives it focus, shows/hides it together with the
// input-method context and pushes the focused window up so the text cursor
// stays visible. This mirrors what maliit-keyboard and plasma-keyboard do and
// relies on Qt's private QtWaylandClient API, so it is optional at build time.

#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>

#include "qwayland-input-method-unstable-v1.h"

class QWindow;

class InputPanelShellIntegration : public QtWaylandClient::QWaylandShellIntegrationTemplate<InputPanelShellIntegration>,
                                   public QtWayland::zwp_input_panel_v1
{
public:
    InputPanelShellIntegration();
    ~InputPanelShellIntegration() override;

    QtWaylandClient::QWaylandShellSurface *createShellSurface(QtWaylandClient::QWaylandWindow *window) override;
};

class InputPanelSurface : public QtWaylandClient::QWaylandShellSurface, public QtWayland::zwp_input_panel_surface_v1
{
public:
    InputPanelSurface(struct ::zwp_input_panel_surface_v1 *object, QtWaylandClient::QWaylandWindow *window);
    ~InputPanelSurface() override;

    void applyConfigure() override;
};

// Returns false when the window is not a Wayland window or the compositor did
// not offer zwp_input_panel_v1 (i.e. we were not launched by KWin as its IM).
bool initInputPanelIntegration(QWindow *window);
