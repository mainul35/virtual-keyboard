#include "controller.h"
#include "injector.h"
#include "keyboardwidget.h"
#include "keymap.h"
#include "togglebutton.h"
#include "uinputinjector.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDebug>
#include <QScreen>
#include <QSettings>
#include <QWindow>

#include <linux/input-event-codes.h>
#include <memory>

#ifdef VKBD_HAVE_WAYLAND
#include "wayland/inputmethod.h"
#endif
#ifdef VKBD_HAVE_INPUT_PANEL
#include "wayland/inputpanelshell.h"
#endif
#ifdef VKBD_HAVE_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

namespace {

const auto kService = QStringLiteral("org.vkbd.Keyboard");
const auto kInterface = QStringLiteral("org.vkbd.Keyboard");

QSize panelSizeFor(const QScreen *screen, double heightFraction)
{
    const QSize g = screen ? screen->geometry().size() : QSize(1280, 800);
    double f = heightFraction;
    if (f <= 0.0 || f > 0.9) {
        f = g.height() > g.width() ? 0.36 : 0.42;
    }
    return QSize(g.width(), qRound(g.height() * f));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("vkbd"));
    QCoreApplication::setApplicationName(QStringLiteral("vkbd"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VKBD_VERSION));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Lightweight on-screen keyboard for Plasma with arrows, Ctrl/Alt/Meta, Del, Esc and F-keys.\n"
        "Configure it as the virtual keyboard in System Settings > Keyboard > Virtual Keyboard,\n"
        "or run it standalone. Settings file: ~/.config/vkbd/vkbd.conf"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        {QStringLiteral("show"), QStringLiteral("Show the running keyboard (starts it if needed).")},
        {QStringLiteral("hide"), QStringLiteral("Hide the running keyboard.")},
        {QStringLiteral("toggle"), QStringLiteral("Toggle the running keyboard (starts it if needed).")},
        {QStringLiteral("backend"), QStringLiteral("Key injection backend: auto, im (KWin input method), uinput."), QStringLiteral("name"), QStringLiteral("auto")},
        {QStringLiteral("shell"), QStringLiteral("Panel window role: auto, input-panel, layer-shell, plain."), QStringLiteral("name"), QStringLiteral("auto")},
        {QStringLiteral("height"), QStringLiteral("Panel height as a fraction of the screen height (e.g. 0.4)."), QStringLiteral("fraction")},
        {QStringLiteral("no-fn-row"), QStringLiteral("Hide the Esc/F1-F12/Del row on the main page.")},
        {QStringLiteral("toggle-button"), QStringLiteral("Always show the floating show/hide button.")},
        {QStringLiteral("self-test"), QStringLiteral("Report which input backends work, press/release Shift once, and exit.")},
        {QStringLiteral("render"), QStringLiteral("Render the keyboard at WIDTHxHEIGHT to a PNG file and exit (layout preview)."), QStringLiteral("file[:WxH]")},
    });
    parser.process(app);

    QSettings settings;
    const QString backend = parser.isSet(QStringLiteral("backend")) && parser.value(QStringLiteral("backend")) != QLatin1String("auto")
        ? parser.value(QStringLiteral("backend"))
        : settings.value(QStringLiteral("backend"), QStringLiteral("auto")).toString();
    const QString shell = parser.isSet(QStringLiteral("shell")) && parser.value(QStringLiteral("shell")) != QLatin1String("auto")
        ? parser.value(QStringLiteral("shell"))
        : settings.value(QStringLiteral("shell"), QStringLiteral("auto")).toString();
    const double heightFraction = parser.isSet(QStringLiteral("height"))
        ? parser.value(QStringLiteral("height")).toDouble()
        : settings.value(QStringLiteral("height"), 0.0).toDouble();
    const bool fnRow = parser.isSet(QStringLiteral("no-fn-row")) ? false : settings.value(QStringLiteral("fnRow"), true).toBool();
    const bool wantToggleButton = parser.isSet(QStringLiteral("toggle-button")) || settings.value(QStringLiteral("toggleButton"), false).toBool();

    // ---- layout preview: no compositor needed (QT_QPA_PLATFORM=offscreen works)
    if (parser.isSet(QStringLiteral("render"))) {
        const QString spec = parser.value(QStringLiteral("render"));
        QString file = spec;
        QSize size(1280, 800);
        const int colon = spec.lastIndexOf(QLatin1Char(':'));
        if (colon > 0) {
            const QStringList wh = spec.mid(colon + 1).split(QLatin1Char('x'));
            if (wh.size() == 2) {
                size = QSize(wh[0].toInt(), wh[1].toInt());
                file = spec.left(colon);
            }
        }
        InjectorRouter none;
        KeyboardWidget preview(&none);
        preview.setFunctionRowVisible(fnRow);
        preview.setKeymap(Keymap::fromSystemConfig());
        preview.resize(panelSizeFor(nullptr, heightFraction).height() > 0 ? QSize(size.width(), qRound(size.height() * (heightFraction > 0 ? heightFraction : 0.42))) : size);
        const bool ok = preview.grab().save(file);
        qInfo().noquote() << (ok ? "vkbd: wrote" : "vkbd: failed to write") << file << preview.size();
        return ok ? 0 : 1;
    }

    // ---- client mode: talk to an already running instance ------------------
    const bool wantShow = parser.isSet(QStringLiteral("show"));
    const bool wantHide = parser.isSet(QStringLiteral("hide"));
    const bool wantToggle = parser.isSet(QStringLiteral("toggle"));
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (wantShow || wantHide || wantToggle) {
        QDBusInterface running(kService, QStringLiteral("/"), kInterface, bus);
        if (running.isValid()) {
            running.call(wantHide ? QStringLiteral("hide") : wantShow ? QStringLiteral("show") : QStringLiteral("toggle"));
            return 0;
        }
        if (wantHide) {
            return 0;
        }
        // Not running yet: fall through and start, then show.
    }

    if (!bus.registerService(kService)) {
        qWarning() << "vkbd: another instance is already running (or the session bus is unavailable)";
        return 1;
    }

    const bool wayland = app.platformName().startsWith(QLatin1String("wayland"));

    // ---- key injection backends --------------------------------------------
    std::unique_ptr<UinputInjector> uinput;
    if (backend != QLatin1String("im")) {
        uinput = std::make_unique<UinputInjector>();
        if (!uinput->isReady()) {
            qInfo().noquote() << "vkbd: uinput backend unavailable:" << uinput->error()
                              << "(install data/60-vkbd-uinput.rules and add yourself to the 'input' group to enable it)";
        }
    }

#ifdef VKBD_HAVE_WAYLAND
    std::unique_ptr<InputMethod> im;
    std::unique_ptr<InputMethodInjector> imInjector;
    if (wayland && backend != QLatin1String("uinput")) {
        im = std::make_unique<InputMethod>();
        im->initialize();
        if (im->isActive()) {
            imInjector = std::make_unique<InputMethodInjector>(im.get());
            qInfo() << "vkbd: running as KWin's input method";
        } else {
            im.reset();
        }
    }
#endif

    InjectorRouter router;
#ifdef VKBD_HAVE_WAYLAND
    if (imInjector) {
        router.addBackend(imInjector.get());
    }
#endif
    if (uinput && uinput->isReady()) {
        router.addBackend(uinput.get());
    }
    if (!router.hasBackends()) {
        qWarning() << "vkbd: no working input backend. Either configure vkbd as the virtual keyboard in"
                   << "System Settings (Wayland) or grant access to /dev/uinput. Keys will be ignored.";
    }

    if (parser.isSet(QStringLiteral("self-test"))) {
        qInfo().noquote() << "platform:" << app.platformName();
        qInfo().noquote() << "uinput:  " << (uinput ? (uinput->isReady() ? QStringLiteral("ready") : uinput->error()) : QStringLiteral("disabled"));
#ifdef VKBD_HAVE_WAYLAND
        qInfo().noquote() << "kwin-im: " << (im ? QStringLiteral("bound (launched by KWin)") : QStringLiteral("not available on this connection"));
#else
        qInfo().noquote() << "kwin-im:  not built";
#endif
        qInfo().noquote() << "selected:" << router.name();
        if (router.isReady()) {
            router.key(KEY_LEFTSHIFT, true);
            router.key(KEY_LEFTSHIFT, false);
            qInfo().noquote() << "sent Shift press/release through" << router.name();
        }
        return router.isReady() ? 0 : 2;
    }

    // ---- the panel ----------------------------------------------------------
    KeyboardWidget keyboard(&router);
    keyboard.setWindowTitle(QStringLiteral("vkbd"));
    keyboard.setFunctionRowVisible(fnRow);
    keyboard.setKeymap(Keymap::fromSystemConfig());

    Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus;
    if (!wayland) {
        flags |= Qt::Tool | Qt::WindowStaysOnTopHint;
    }
    keyboard.setWindowFlags(flags);
    keyboard.setAttribute(Qt::WA_ShowWithoutActivating);

    Controller::Mode mode = Controller::Mode::Plain;
#ifdef VKBD_HAVE_LAYER_SHELL
    LayerShellQt::Window *layerWindow = nullptr;
#endif

    if (wayland) {
        keyboard.winId(); // create the QWindow so a shell role can be attached before it is shown
#ifdef VKBD_HAVE_INPUT_PANEL
        const bool panelWanted = shell == QLatin1String("input-panel") || (shell == QLatin1String("auto") && im);
        if (panelWanted && initInputPanelIntegration(keyboard.windowHandle())) {
            mode = Controller::Mode::InputPanel;
            qInfo() << "vkbd: using the input-panel surface role";
        }
#endif
#ifdef VKBD_HAVE_LAYER_SHELL
        if (mode == Controller::Mode::Plain && shell != QLatin1String("plain")) {
            layerWindow = LayerShellQt::Window::get(keyboard.windowHandle());
            layerWindow->setScope(QStringLiteral("vkbd"));
            layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
            layerWindow->setAnchors(LayerShellQt::Window::Anchors({LayerShellQt::Window::AnchorBottom, LayerShellQt::Window::AnchorLeft, LayerShellQt::Window::AnchorRight}));
            layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
            layerWindow->setActivateOnShow(false);
            mode = Controller::Mode::LayerShell;
            qInfo() << "vkbd: using a layer-shell surface";
        }
#endif
    }

    auto applySize = [&] {
        QScreen *screen = keyboard.windowHandle() && keyboard.windowHandle()->screen() ? keyboard.windowHandle()->screen() : app.primaryScreen();
        const QSize size = panelSizeFor(screen, heightFraction);
        keyboard.resize(size);
#ifdef VKBD_HAVE_LAYER_SHELL
        if (layerWindow) {
            layerWindow->setExclusiveZone(size.height());
        }
#endif
        if (mode == Controller::Mode::Plain && screen) {
            const QRect g = screen->geometry();
            keyboard.move(g.left(), g.bottom() - size.height() + 1);
        }
    };
    applySize();
    auto watchScreen = [&](QScreen *screen) {
        if (screen) {
            QObject::connect(screen, &QScreen::geometryChanged, &keyboard, [&](const QRect &) { applySize(); });
        }
    };
    watchScreen(app.primaryScreen());
    QObject::connect(&app, &QGuiApplication::primaryScreenChanged, &keyboard, [&](QScreen *s) {
        watchScreen(s);
        applySize();
    });
    if (keyboard.windowHandle()) {
        QObject::connect(keyboard.windowHandle(), &QWindow::screenChanged, &keyboard, [&](QScreen *s) {
            watchScreen(s);
            applySize();
        });
    }

    // ---- glue ----------------------------------------------------------------
    Controller controller(&keyboard, mode);
    bus.registerObject(QStringLiteral("/"), &controller, QDBusConnection::ExportScriptableSlots);
    QObject::connect(&keyboard, &KeyboardWidget::hideRequested, &controller, &Controller::hide);
    QObject::connect(&keyboard, &KeyboardWidget::screenshotRequested, &controller, &Controller::screenshot);

#ifdef VKBD_HAVE_WAYLAND
    if (im) {
        QObject::connect(im.get(), &InputMethod::activated, &controller, [&] {
            InputMethodContext *ctx = im->context();
            QObject::connect(ctx, &InputMethodContext::keymapChanged, &keyboard, [&] {
                if (im->context() && im->context()->keymap()) {
                    keyboard.setKeymap(im->context()->keymap());
                }
            });
            QObject::connect(ctx, &InputMethodContext::groupChanged, &keyboard, &KeyboardWidget::setLayoutGroup);
            controller.imActivated();
        });
        QObject::connect(im.get(), &InputMethod::deactivated, &controller, &Controller::imDeactivated);
    }
#endif

    std::unique_ptr<ToggleButton> toggleButton;
    bool haveIm = false;
#ifdef VKBD_HAVE_WAYLAND
    haveIm = im != nullptr;
#endif
    if (wantToggleButton || !haveIm) {
        toggleButton = std::make_unique<ToggleButton>();
        toggleButton->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
        toggleButton->setAttribute(Qt::WA_ShowWithoutActivating);
        QObject::connect(toggleButton.get(), &ToggleButton::clicked, &controller, &Controller::toggle);
#ifdef VKBD_HAVE_LAYER_SHELL
        if (wayland && mode == Controller::Mode::LayerShell) {
            toggleButton->winId();
            auto *lw = LayerShellQt::Window::get(toggleButton->windowHandle());
            lw->setScope(QStringLiteral("vkbd-toggle"));
            lw->setLayer(LayerShellQt::Window::LayerOverlay);
            lw->setAnchors(LayerShellQt::Window::Anchors({LayerShellQt::Window::AnchorBottom, LayerShellQt::Window::AnchorRight}));
            lw->setMargins(QMargins(0, 0, 12, 12));
            lw->setExclusiveZone(0);
            lw->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
            lw->setActivateOnShow(false);
        }
#endif
        if (!wayland && app.primaryScreen()) {
            const QRect g = app.primaryScreen()->availableGeometry();
            toggleButton->move(g.right() - toggleButton->width() - 12, g.bottom() - toggleButton->height() - 12);
        }
        toggleButton->show();
    }

    // ---- initial visibility -------------------------------------------------
    switch (mode) {
    case Controller::Mode::InputPanel:
        keyboard.show(); // mapped; KWin decides when it is actually visible
        break;
    case Controller::Mode::LayerShell:
    case Controller::Mode::Plain:
        if (!haveIm || wantShow || wantToggle) {
            keyboard.show();
        }
        break;
    }
    if ((wantShow || wantToggle) && mode == Controller::Mode::InputPanel) {
        controller.show();
    }

    qInfo().noquote() << "vkbd: backend =" << router.name() << ", shell ="
                      << (mode == Controller::Mode::InputPanel ? "input-panel" : mode == Controller::Mode::LayerShell ? "layer-shell" : "plain");

    return app.exec();
}
