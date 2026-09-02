# vkbd – a lightweight on-screen keyboard for Plasma tablets

`vkbd` is a virtual keyboard for KDE Plasma (Wayland first, X11 supported) written
for low-end touch hardware such as an Intel Atom tablet with 4 GB of RAM. It gives
you everything the stock Maliit keyboard leaves out:

* **Arrow keys**, Home/End/PgUp/PgDn, Insert, **Delete**, **Esc**
* **Ctrl, Alt, AltGr, Meta and Shift** with tap-to-latch, double-tap-to-lock and
  hold-and-type behaviour, so Ctrl+C, Alt+Tab, Ctrl+Alt+T, Shift+arrows all work
* **F1–F12** on the main page, plus PrtSc/ScrLk/Pause/Menu, volume and brightness
  keys on the Fn page
* Caps Lock, Tab, a full US-PC layout; key labels follow your real xkb layout
* Same workflow as Maliit: it pops up when a text field gets focus and hides when
  focus leaves, and it never steals focus from the app you are typing into

It is deliberately small: a single custom-painted `QWidget`, no QML engine, no
GPU effects, no animations. Idle CPU is zero and the process shares Qt's
libraries with the rest of Plasma.

## How it works

| Piece | What it does |
| --- | --- |
| **KWin integration** | When selected as Plasma's virtual keyboard, KWin starts `vkbd` on a private Wayland socket and speaks `zwp_input_method_v1` to it: activate/deactivate on focus changes, and key/modifier requests to type. `vkbd` also asks for the input-panel surface role (`zwp_input_panel_v1`) so KWin docks it at the bottom, shows/hides it and pushes the focused window up like it does for Maliit. |
| **Key injection, IM path** | Keys are sent as raw evdev keycodes over the input-method context (`key` + `modifiers` requests). They reach the focused app as ordinary `wl_keyboard` events with the right modifier state, which is why Ctrl/Alt combos and F-keys work in Qt, GTK, Electron and Xwayland apps alike. |
| **Key injection, uinput path** | If there is no input-method context (X11 session, keyboard launched by hand, or an app that does not implement text-input so KWin never activates the IM) `vkbd` types through a virtual `/dev/uinput` keyboard instead. Both paths are compiled in; the right one is chosen per keystroke. |
| **Standalone window** | Without KWin's input-method socket the panel is a `layer-shell` surface (Wayland) or a focus-less tool window (X11), plus a small floating button to show/hide it. `vkbd --toggle` does the same from a panel launcher or shortcut. |

## Building

`./install.sh` installs these for you on apt, dnf, pacman and zypper based
distros. Dependencies (names for Debian/Ubuntu/neon; Fedora and Arch equivalents
in brackets):

```
cmake ninja-build g++ pkg-config
qt6-base-dev qt6-base-private-dev                         [qt6-qtbase-private-devel / qt6-base]
qt6-wayland-dev qt6-wayland-private-dev                   [qt6-qtwayland-devel / qt6-wayland]
libwayland-dev libxkbcommon-dev
liblayershellqtinterface-dev                              [layer-shell-qt-devel / layer-shell-qt]
```

The input-panel role uses Qt's private `QtWaylandClient` headers (the same thing
`maliit-keyboard` and `plasma-keyboard` do). If your distro does not ship them the
build prints a warning and falls back to layer-shell, which still works with
KWin's input-method socket; you only lose the "push the window up" behaviour.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

`./install.sh` does the build, installs to `/usr/local`, sets up the uinput udev
rule, adds you to the `input` group and registers vkbd with KWin.

## Setting it up on Plasma Wayland

1. Install (see above), then log out and back in once so the `input` group takes
   effect (only needed for the uinput fallback).
2. **System Settings → Keyboard → Virtual Keyboard** (Plasma 6) and pick
   *vkbd Virtual Keyboard*. `install.sh` does this for you with:
   ```
   kwriteconfig6 --file kwinrc --group Wayland --key InputMethod /usr/share/applications/vkbd.desktop
   qdbus6 org.kde.KWin /KWin reconfigure
   ```
3. Tap any text field. The keyboard appears; the ⌄ key hides it, tapping a field
   brings it back. For apps that do not support text-input (some Electron builds,
   X11 apps) use the Plasma system-tray keyboard icon, `vkbd --toggle`, or pin the
   *Toggle vkbd Keyboard* launcher to your panel.

The Plasma tray icon's "enable/disable virtual keyboard" toggle and KWin's
`org.kde.kwin.VirtualKeyboard` D-Bus interface keep working, because to KWin
`vkbd` is just another input method.

## Standalone / X11

Run `vkbd`. It shows a floating keyboard button at the bottom-right; tap it (or
run `vkbd --toggle`) to show/hide the panel. Typing goes through `/dev/uinput`,
so the udev rule and `input` group membership are required in this mode.

## Options

```
vkbd [--show|--hide|--toggle] [--backend auto|im|uinput] [--shell auto|input-panel|layer-shell|plain]
     [--height 0.42] [--no-fn-row] [--toggle-button]
vkbd --self-test                       # which backend works here? sends one Shift press/release
vkbd --render preview.png:1280x800     # draw the layout to a PNG (QT_QPA_PLATFORM=offscreen works)
```

The same keys can be set permanently in `~/.config/vkbd/vkbd.conf`:

```ini
[General]
height=0.42        ; fraction of the screen height (default 0.42 landscape, 0.36 portrait)
fnRow=true         ; Esc/F1–F12/Del row on the main page
toggleButton=false ; always show the floating show/hide button
backend=auto
shell=auto
```

## Using the modifiers

* **Tap** Shift/Ctrl/Alt/Meta: latched for the next key (outlined).
* **Tap twice**: locked until tapped again (filled).
* **Hold** with one finger and tap keys with another: behaves like a physical key.
* Held keys auto-repeat (arrows, Backspace) because the app receives a real key
  press and release.

## Layout of the source

```
src/keyboardwidget.*     the panel: painting, touch handling, modifier logic
src/layout.*             key definitions for the main and Fn pages
src/keymap.*             xkbcommon wrapper for layout-aware labels and modifier masks
src/injector.*           backend interface and per-key routing
src/uinputinjector.*     /dev/uinput backend
src/wayland/inputmethod.*     zwp_input_method_v1 client, keyboard grab, IM backend
src/wayland/inputpanelshell.* zwp_input_panel_v1 surface role (Qt private API)
src/controller.*         show/hide policy and the org.vkbd.Keyboard D-Bus interface
src/main.cpp             wiring, CLI, window roles
data/                    desktop entries and the udev rule
protocols/               input-method-unstable-v1.xml (vendored from wayland-protocols)
```

## Troubleshooting

* *Keys do nothing*: run `vkbd` from a terminal; it prints which backend it uses.
  "backend = none" means KWin did not activate the IM and `/dev/uinput` is not
  writable. Check `ls -l /dev/uinput` and `id` for the `input` group.
* *Keyboard never appears when tapping a field*: make sure the field is in an app
  that supports Wayland text-input and that KWin's virtual keyboard is enabled in
  the tray icon. `KWIN_IM_SHOW_ALWAYS=1` in KWin's environment forces it to show
  for mouse input too.
* *Wrong characters*: labels come from the keymap KWin sends; in uinput mode they
  come from `~/.config/kxkbrc`. The keys themselves always send keycodes, exactly
  like a physical US-PC keyboard, so the active xkb layout decides the character.
