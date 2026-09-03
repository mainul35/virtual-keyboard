#include "controller.h"
#include "doctor.h"
#include "injector.h"
#include "keyboardwidget.h"
#include "keymap.h"
#include "togglebutton.h"
#include "uinputinjector.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QStandardPaths>
#include <QWindow>

#include <cstdio>
#include <functional>
#include <linux/input-event-codes.h>
#include <memory>
#include <unistd.h>

#ifdef VKBD_HAVE_WAYLAND
#include "wayland/inputmethod.h"
#endif
#ifdef VKBD_HAVE_INPUT_PANEL
#include "wayland/inputpanelshell.h"
#endif
#ifdef VKBD_HAVE_LAYER_SHELL
#include <LayerShellQt/Window>
#endif
#ifdef VKBD_HAVE_SLIDE
#include "wayland/slide.h"
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

// Messages go to stderr (KWin forwards that to its journal) and to a log file
// so `vkbd --doctor` can show what the KWin-started instance did.
QFile *g_logFile = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const char *level = type == QtWarningMsg ? "W" : type == QtCriticalMsg || type == QtFatalMsg ? "E" : "I";
    const QByteArray line = QStringLiteral("%1 %2 %3\n")
                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")), QLatin1String(level), msg)
                                .toUtf8();
    fputs(line.constData(), stderr);
    fflush(stderr);
    if (g_logFile) {
        g_logFile->write(line);
        g_logFile->flush();
    }
    if (type == QtFatalMsg) {
        abort();
    }
}

void openLogFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/vkbd");
    QDir().mkpath(dir);
    auto *f = new QFile(dir + QStringLiteral("/vkbd.log"));
    if (f->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        g_logFile = f;
    } else {
        delete f;
    }
}

// Receives org.kde.KeyboardLayouts.layoutChanged so labels and the IM
// modifiers request follow the active xkb layout group.
class LayoutWatcher : public QObject
{
    Q_OBJECT
public:
    std::function<void(int)> onChange;
public Q_SLOTS:
    void layoutChanged(uint index)
    {
        if (onChange) {
            onChange(static_cast<int>(index));
        }
    }
};

} // namespace

int main(int argc, char **argv)
{
    // KWin starts its input method with WAYLAND_SOCKET pointing at a private
    // connection. libwayland unsets the variable while connecting, so look
    // before QApplication is constructed.
    const bool launchedByKwin = qEnvironmentVariableIsSet("WAYLAND_SOCKET");

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
        {QStringLiteral("quit"), QStringLiteral("Stop the running keyboard.")},
        {QStringLiteral("copy"), QStringLiteral("Send Ctrl+C to the focused window (via the running keyboard).")},
        {QStringLiteral("paste"), QStringLiteral("Send Ctrl+V to the focused window (via the running keyboard).")},
        {QStringLiteral("doctor"), QStringLiteral("Print diagnostics about the KWin integration and exit.")},
        {QStringLiteral("backend"), QStringLiteral("Key injection backend: auto, im (KWin input method), uinput."), QStringLiteral("name"), QStringLiteral("auto")},
        {QStringLiteral("shell"), QStringLiteral("Panel window role: auto, input-panel, layer-shell, plain."), QStringLiteral("name"), QStringLiteral("auto")},
        {QStringLiteral("height"), QStringLiteral("Panel height as a fraction of the screen height (e.g. 0.4)."), QStringLiteral("fraction")},
        {QStringLiteral("layout"), QStringLiteral("Key layout: auto (compact in portrait, full in landscape), compact, full."), QStringLiteral("name"), QStringLiteral("auto")},
        {QStringLiteral("no-fn-row"), QStringLiteral("Hide the Esc/F1-F12/Del row of the full layout.")},
        {QStringLiteral("toggle-button"), QStringLiteral("Always show the floating show/hide button.")},
        {QStringLiteral("no-tray"), QStringLiteral("Do not add an icon to the system tray.")},
        {QStringLiteral("self-test"), QStringLiteral("Report which input backends work, press/release Shift once, and exit.")},
        {QStringLiteral("render"), QStringLiteral("Render the keyboard at WIDTHxHEIGHT to a PNG file and exit (layout preview)."), QStringLiteral("file[:WxH]")},
    });
    parser.process(app);

    if (parser.isSet(QStringLiteral("doctor"))) {
        return runDoctor();
    }

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
    const QString layoutName = parser.isSet(QStringLiteral("layout")) && parser.value(QStringLiteral("layout")) != QLatin1String("auto")
        ? parser.value(QStringLiteral("layout"))
        : settings.value(QStringLiteral("layout"), QStringLiteral("auto")).toString();
    const KeyboardWidget::LayoutMode layoutMode = layoutName == QLatin1String("compact") ? KeyboardWidget::LayoutMode::Compact
        : layoutName == QLatin1String("full")                                          ? KeyboardWidget::LayoutMode::Full
                                                                                       : KeyboardWidget::LayoutMode::Auto;
    std::shared_ptr<Keymap> systemKeymap = Keymap::fromSystemConfig();
    const bool wantToggleButton = parser.isSet(QStringLiteral("toggle-button")) || settings.value(QStringLiteral("toggleButton"), false).toBool();
    const bool wantTray = !parser.isSet(QStringLiteral("no-tray")) && settings.value(QStringLiteral("tray"), true).toBool();

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
        preview.setLayoutMode(layoutMode);
        preview.setKeymap(systemKeymap);
        preview.resize(QSize(size.width(), qRound(size.height() * (heightFraction > 0 ? heightFraction : 0.42))));
        const bool ok = preview.grab().save(file);
        qInfo().noquote() << (ok ? "vkbd: wrote" : "vkbd: failed to write") << file << preview.size();
        return ok ? 0 : 1;
    }

    // ---- client mode: talk to an already running instance ------------------
    const bool wantShow = parser.isSet(QStringLiteral("show"));
    const bool wantHide = parser.isSet(QStringLiteral("hide"));
    const bool wantToggle = parser.isSet(QStringLiteral("toggle"));
    const bool wantQuit = parser.isSet(QStringLiteral("quit"));
    const bool wantCopy = parser.isSet(QStringLiteral("copy"));
    const bool wantPaste = parser.isSet(QStringLiteral("paste"));
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (wantShow || wantHide || wantToggle || wantQuit || wantCopy || wantPaste) {
        QDBusInterface running(kService, QStringLiteral("/"), kInterface, bus);
        if (running.isValid()) {
            const QString method = wantQuit ? QStringLiteral("quit")
                : wantHide                  ? QStringLiteral("hide")
                : wantCopy                  ? QStringLiteral("copy")
                : wantPaste                 ? QStringLiteral("paste")
                : wantShow                  ? QStringLiteral("show")
                                            : QStringLiteral("toggle");
            running.call(method);
            return 0;
        }
        if (wantHide || wantQuit || wantCopy || wantPaste) {
            qWarning() << "vkbd: not running";
            return 1;
        }
        // Not running yet: fall through and start, then show.
    }

    qInstallMessageHandler(messageHandler);

    // ---- single instance --------------------------------------------------
    // The instance KWin starts must win: a leftover manual instance would
    // otherwise make KWin's one exit and the keyboard would never appear.
    bool haveName = false;
    if (bus.isConnected() && bus.interface()) {
        const auto queue = launchedByKwin ? QDBusConnectionInterface::ReplaceExistingService : QDBusConnectionInterface::DontQueueService;
        QDBusReply<QDBusConnectionInterface::RegisterServiceReply> reply = bus.interface()->registerService(kService, queue, QDBusConnectionInterface::AllowReplacement);
        haveName = reply.isValid() && reply.value() == QDBusConnectionInterface::ServiceRegistered;
    }
    if (!haveName) {
        if (!launchedByKwin) {
            qWarning() << "vkbd: another instance is already running; use --show/--hide/--toggle/--quit to control it";
            return 1;
        }
        qWarning() << "vkbd: could not own the D-Bus name; continuing without D-Bus control";
    }
    // Only the instance that is going to run owns the log file, so a refused
    // duplicate never wipes the log of the real one.
    openLogFile();
    qInfo().noquote() << "vkbd" << VKBD_VERSION << "pid" << getpid() << (launchedByKwin ? "started by KWin (input-method socket)" : "started standalone")
                      << "platform" << app.platformName();
    QDBusServiceWatcher nameWatcher(kService, bus, QDBusServiceWatcher::WatchForOwnerChange);
    QObject::connect(&nameWatcher, &QDBusServiceWatcher::serviceOwnerChanged, &app,
                     [&](const QString &, const QString &, const QString &newOwner) {
                         if (haveName && !newOwner.isEmpty() && newOwner != bus.baseService()) {
                             qInfo() << "vkbd: replaced by another instance (KWin started one), quitting";
                             haveName = false;
                             app.quit();
                         }
                     });

    const bool wayland = app.platformName().startsWith(QLatin1String("wayland"));

    // ---- key injection backends --------------------------------------------
    // uinput is preferred: its events take the same path as a physical
    // keyboard, so modifiers, key repeat and global shortcuts behave exactly.
    std::unique_ptr<UinputInjector> uinput;
    if (backend != QLatin1String("im")) {
        uinput = std::make_unique<UinputInjector>();
        if (!uinput->open()) {
            qInfo().noquote() << "vkbd: uinput backend unavailable:" << uinput->error()
                              << "(install data/60-vkbd-uinput.rules and add yourself to the 'input' group, then log in again)";
        }
    }

#ifdef VKBD_HAVE_WAYLAND
    std::unique_ptr<InputMethod> im;
    std::unique_ptr<InputMethodInjector> imInjector;
    if (wayland && backend != QLatin1String("uinput")) {
        im = std::make_unique<InputMethod>();
        im->initialize();
        if (im->isActive()) {
            imInjector = std::make_unique<InputMethodInjector>(im.get(), systemKeymap);
            qInfo() << "vkbd: bound zwp_input_method_v1, running as KWin's input method";
        } else {
            if (launchedByKwin) {
                qWarning() << "vkbd: started by KWin but zwp_input_method_v1 is not offered on this connection";
            }
            im.reset();
        }
    }
#endif

    InjectorRouter router;
    if (uinput && uinput->isOpen()) {
        router.addBackend(uinput.get());
    }
#ifdef VKBD_HAVE_WAYLAND
    if (imInjector) {
        router.addBackend(imInjector.get());
    }
#endif

    if (parser.isSet(QStringLiteral("self-test"))) {
        qInfo().noquote() << "platform:" << app.platformName();
        qInfo().noquote() << "uinput:  " << (uinput ? (uinput->open() ? QStringLiteral("ready") : uinput->error()) : QStringLiteral("disabled"));
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
    keyboard.setLayoutMode(layoutMode);
    keyboard.setKeymap(systemKeymap);

    // Follow Plasma's active keyboard layout (group) for labels and modifiers.
    LayoutWatcher layoutWatcher;
    layoutWatcher.onChange = [&](int group) {
        keyboard.setLayoutGroup(group);
#ifdef VKBD_HAVE_WAYLAND
        if (imInjector) {
            imInjector->setLayoutGroup(group);
        }
#endif
    };
    {
        QDBusInterface layouts(QStringLiteral("org.kde.keyboard"), QStringLiteral("/Layouts"), QStringLiteral("org.kde.KeyboardLayouts"), bus);
        if (layouts.isValid()) {
            QDBusReply<uint> current = layouts.call(QStringLiteral("getLayout"));
            if (current.isValid()) {
                layoutWatcher.layoutChanged(current.value());
            }
            bus.connect(QStringLiteral("org.kde.keyboard"), QStringLiteral("/Layouts"), QStringLiteral("org.kde.KeyboardLayouts"),
                        QStringLiteral("layoutChanged"), &layoutWatcher, SLOT(layoutChanged(uint)));
        }
    }

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

    bool haveIm = false;
#ifdef VKBD_HAVE_WAYLAND
    haveIm = im != nullptr;
#endif

    if (wayland) {
        keyboard.winId(); // create the QWindow so a shell role can be attached before it is shown
#ifdef VKBD_HAVE_INPUT_PANEL
        const bool panelWanted = shell == QLatin1String("input-panel") || (shell == QLatin1String("auto") && haveIm);
        if (panelWanted) {
            if (initInputPanelIntegration(keyboard.windowHandle())) {
                mode = Controller::Mode::InputPanel;
                qInfo() << "vkbd: using the input-panel surface role";
            } else {
                qWarning() << "vkbd: input-panel role unavailable, falling back to layer-shell";
            }
        }
#else
        if (haveIm) {
            qWarning() << "vkbd: built without the input-panel role (Qt private headers missing); using layer-shell";
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
    controller.setStatusInfo(QStringLiteral("launchedByKwin=%1 im=%2 backendPreference=%3 pid=%4")
                                 .arg(launchedByKwin ? QStringLiteral("yes") : QStringLiteral("no"),
                                      haveIm ? QStringLiteral("bound") : QStringLiteral("none"),
                                      backend,
                                      QString::number(getpid())));
    if (haveName) {
        bus.registerObject(QStringLiteral("/"), &controller, QDBusConnection::ExportScriptableSlots);
    }
#ifdef VKBD_HAVE_SLIDE
    if (wayland) {
        controller.setAfterShowHook([&] { applySlideFromBottom(keyboard.windowHandle()); });
    }
#endif
    QObject::connect(&keyboard, &KeyboardWidget::hideRequested, &controller, &Controller::hide);
    QObject::connect(&keyboard, &KeyboardWidget::screenshotRequested, &controller, &Controller::screenshot);

#ifdef VKBD_HAVE_WAYLAND
    if (im) {
        QObject::connect(im.get(), &InputMethod::activated, &controller, [&] {
            qInfo() << "vkbd: input method context activated";
            controller.imActivated();
        });
        QObject::connect(im.get(), &InputMethod::deactivated, &controller, [&] {
            qInfo() << "vkbd: input method context deactivated";
            controller.imDeactivated();
        });
    }
#endif

    // ---- system tray icon: bring the keyboard up even on the desktop --------
    std::unique_ptr<QSystemTrayIcon> tray;
    std::unique_ptr<QMenu> trayMenu;
    if (wantTray && QSystemTrayIcon::isSystemTrayAvailable()) {
        QIcon icon = QIcon::fromTheme(QStringLiteral("input-keyboard-virtual"), QIcon::fromTheme(QStringLiteral("input-keyboard")));
        if (icon.isNull()) {
            QPixmap pm(64, 64);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Qt::white, 4));
            p.drawRoundedRect(QRectF(6, 16, 52, 32), 6, 6);
            for (int i = 0; i < 4; ++i) {
                p.drawPoint(QPointF(16 + i * 11, 27));
                p.drawPoint(QPointF(16 + i * 11, 37));
            }
            icon = QIcon(pm);
        }
        tray = std::make_unique<QSystemTrayIcon>(icon);
        tray->setToolTip(QStringLiteral("vkbd on-screen keyboard: click to show or hide"));
        trayMenu = std::make_unique<QMenu>();
        trayMenu->addAction(QStringLiteral("Show keyboard"), &controller, &Controller::show);
        trayMenu->addAction(QStringLiteral("Hide keyboard"), &controller, &Controller::hide);
        trayMenu->addSeparator();
        trayMenu->addAction(QStringLiteral("Copy  (Ctrl+C)"), &controller, &Controller::copy);
        trayMenu->addAction(QStringLiteral("Paste  (Ctrl+V)"), &controller, &Controller::paste);
        trayMenu->addAction(QStringLiteral("Select all  (Ctrl+A)"), &controller, &Controller::selectAll);
        trayMenu->addAction(QStringLiteral("Take screenshot to clipboard"), &controller, &Controller::screenshot);
        trayMenu->addSeparator();
        trayMenu->addAction(QStringLiteral("Quit vkbd"), &controller, &Controller::quit);
        tray->setContextMenu(trayMenu.get());
        QObject::connect(tray.get(), &QSystemTrayIcon::activated, &controller, [&](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::MiddleClick) {
                controller.toggle();
            }
        });
        tray->show();
    }

    std::unique_ptr<ToggleButton> toggleButton;
    if (wantToggleButton || !haveIm) {
        toggleButton = std::make_unique<ToggleButton>();
        toggleButton->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
        toggleButton->setAttribute(Qt::WA_ShowWithoutActivating);
        QObject::connect(toggleButton.get(), &ToggleButton::clicked, &controller, &Controller::toggle);
#ifdef VKBD_HAVE_LAYER_SHELL
        if (wayland) {
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
    // With an input-method connection the panel stays unmapped until KWin
    // activates us for a text field; otherwise it is shown right away.
    if (!haveIm || wantShow || wantToggle) {
        controller.show();
    }

    qInfo().noquote() << "vkbd: ready, shell ="
                      << (mode == Controller::Mode::InputPanel ? "input-panel" : mode == Controller::Mode::LayerShell ? "layer-shell" : "plain")
                      << ", backends =" << (uinput && uinput->isOpen() ? "uinput" : "") << (haveIm ? "wayland-im" : "")
                      << ", layout =" << layoutName;

    const int rc = app.exec();
    qInfo() << "vkbd: exiting with" << rc;
    return rc;
}

#include "main.moc"
