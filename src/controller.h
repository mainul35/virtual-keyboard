#pragma once

#include <QObject>
#include <QTimer>

class KeyboardWidget;

// Owns the show/hide policy and exposes it on D-Bus as org.vkbd.Keyboard so
// that `vkbd --toggle` (or any launcher / shortcut) can drive a running instance.
class Controller : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.vkbd.Keyboard")
public:
    enum class Mode {
        InputPanel, // KWin owns visibility (zwp_input_panel_v1 role)
        LayerShell, // we own visibility, panel is a layer-shell surface
        Plain,      // X11 or a plain Wayland window
    };

    Controller(KeyboardWidget *keyboard, Mode mode, QObject *parent = nullptr);

    Mode mode() const { return m_mode; }

public Q_SLOTS:
    Q_SCRIPTABLE void show();
    Q_SCRIPTABLE void hide();
    Q_SCRIPTABLE void toggle();
    Q_SCRIPTABLE bool isVisible() const;
    // Hide the panel, capture the screen to the clipboard with Spectacle,
    // then bring the panel back so Ctrl+V works right away.
    Q_SCRIPTABLE void screenshot();

    void imActivated();
    void imDeactivated();

private:
    bool kwinPanelVisible() const;
    void kwinForceActivate();
    void kwinDeactivate();
    void finishScreenshot();

    KeyboardWidget *m_keyboard;
    Mode m_mode;
    QTimer m_hideTimer;
    QTimer m_screenshotTimeout;
    bool m_screenshotInProgress = false;
    bool m_wasVisibleBeforeScreenshot = false;
};
