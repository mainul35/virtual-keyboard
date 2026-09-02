#pragma once

class QWindow;

// Ask KWin's "Sliding Popups" effect to animate the window in and out from
// the bottom edge (org_kde_kwin_slide protocol, the same thing
// KWindowEffects::slideWindow does for Maliit). Safe to call repeatedly; it
// must be called once the window has a Wayland surface, i.e. after show().
void applySlideFromBottom(QWindow *window);
