#pragma once

#include <QObject>
#include <QTimer>
#include <functional>

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
    // Free-form "key=value ..." text reported by status() (set from main).
    void setStatusInfo(const QString &info) { m_statusInfo = info; }
    // Called right after the panel window is shown (used to attach the
    // compositor slide animation once the surface exists).
    void setAfterShowHook(std::function<void()> hook) { m_afterShow = std::move(hook); }
    // Identifier of Plasma's active toplevel window (empty = none / unknown).
    // With it, losing the text field to a popup keeps the keyboard, while
    // switching to another application hides it.
    void setActiveWindowProvider(std::function<QString()> provider) { m_activeWindow = std::move(provider); }
    void activeWindowChanged(const QString &id);
    // While held (our own tray menu is open) the keyboard never auto-hides.
    void holdOpen(bool hold);

public Q_SLOTS:
    Q_SCRIPTABLE void show();
    Q_SCRIPTABLE void hide();
    Q_SCRIPTABLE void toggle();
    Q_SCRIPTABLE bool isVisible() const;
    Q_SCRIPTABLE QString status() const;
    Q_SCRIPTABLE void quit();
    // Hide the panel, capture the screen to the clipboard with Spectacle,
    // then bring the panel back so Ctrl+V works right away.
    Q_SCRIPTABLE void screenshot();
    // Send Ctrl+C / Ctrl+V / Ctrl+A to the focused window without showing the
    // keyboard (for the tray menu, launchers and shortcuts). A short delay lets
    // the menu close and focus return to the application first.
    Q_SCRIPTABLE void copy();
    Q_SCRIPTABLE void paste();
    Q_SCRIPTABLE void selectAll();

    void imActivated();
    void imDeactivated();

private:
    bool kwinPanelVisible() const;
    void kwinForceActivate();
    void kwinDeactivate();
    void finishScreenshot();
    void showPanel();
    void sendChordSoon(int modifierCode, int code);
    void scheduleAutoHide();
    void autoHideNow();

    KeyboardWidget *m_keyboard;
    Mode m_mode;
    QString m_statusInfo;
    std::function<void()> m_afterShow;
    std::function<QString()> m_activeWindow;
    QString m_activeWindowAtShow;
    bool m_imActive = false;
    bool m_holdOpen = false;
    QTimer m_hideTimer;
    QTimer m_screenshotTimeout;
    bool m_screenshotInProgress = false;
    bool m_wasVisibleBeforeScreenshot = false;
};
