# vkbd – a lightweight on-screen keyboard for Plasma tablets

`vkbd` is a virtual keyboard for KDE Plasma (Wayland first, X11 supported) written
for low-end touch hardware such as an Intel Atom tablet with 4 GB of RAM. It gives
you everything the stock Maliit keyboard leaves out:

* **Arrow keys**, Home/End/PgUp/PgDn, Insert, **Delete**, **Esc**
* **Ctrl, Alt, AltGr, Meta and Shift** with tap-to-latch, double-tap-to-lock and
  hold-and-type behaviour, so Ctrl+C, Alt+Tab, Ctrl+Alt+T, Shift+arrows all work
* **F1–F12** on the main page, plus ScrLk/Pause/Menu, volume and brightness
  keys on the Fn page
* **PrtSc** takes a full-screen screenshot with Spectacle and puts it on the
  clipboard, hiding the keyboard for the capture and bringing it back so you can
  Ctrl+V straight away (falls back to the real Print key if Spectacle is missing)
* Two layouts: a phone-style compact one for portrait (10 keys per row, a
  `?123` symbols page like Android/iOS, a utility row with Esc/Tab/Ctrl/Alt/
  arrows/Del, and a wide centred space bar) and the full PC layout for
  landscape. Key labels follow your real xkb layout.
* A **Sel** key (on the Fn page) for selecting text by touch: tap where the
  selection should start, press Fn then Sel, tap where it should end. Sel holds Shift for you so the
  second tap is a Shift+click, which browsers, editors, terminals and most
  Qt/GTK apps treat as "extend the selection to here". Press Sel again to end.
* A key preview bubble: while a character key is held, an enlarged copy of its
  label pops up above it (Android/iOS style); `keyPreview=false` or
  `--no-preview` turns it off.
* A short click sound on every key press (libcanberra, the freedesktop
  event-sound library used by Plasma and GNOME alike; `sound=false` or
  `--no-sound` turns it off, `soundVolume=-8` sets the level in dB). No
  vibration: x86 tablets expose no haptic device to Linux.
* Same workflow as Maliit: it pops up when a text field gets focus and hides when
  focus leaves, and it never steals focus from the app you are typing into.
  Popups and menus (including vkbd's own tray menu) do not make it disappear:
  it only hides after a short grace period without a text field, or when you
  hide it yourself

It is deliberately small: a single custom-painted `QWidget`, no QML engine, no
GPU effects, no animations. Idle CPU is zero and the process shares Qt's
libraries with the rest of Plasma.

## How it works

| Piece | What it does |
| --- | --- |
| **KWin integration** | When selected as Plasma's virtual keyboard, KWin starts `vkbd` on a private Wayland socket and speaks `zwp_input_method_v1` to it: activate/deactivate on focus changes, and key/modifier requests to type. `vkbd` also asks for the input-panel surface role (`zwp_input_panel_v1`) so KWin docks it at the bottom, shows/hides it and pushes the focused window up like it does for Maliit. |
| **Key injection, uinput path (preferred)** | Keys are typed through a virtual `/dev/uinput` keyboard. They take exactly the path of a physical keyboard through libinput and KWin, so modifiers, key repeat, global shortcuts (Alt+Tab, Meta) and Xwayland apps all behave. Needs the udev rule from `install.sh`. |
| **Key injection, IM path (fallback)** | Without `/dev/uinput` access, keys are sent as raw evdev keycodes over the input-method context (`key` + `modifiers` requests) and reach the focused app as `wl_keyboard` events. This only works while KWin has activated the keyboard for a text field. |
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
3. Tap any text field. The keyboard appears; the hide key puts it away, tapping
   a field brings it back.
4. vkbd adds its own keyboard icon to the system tray. Click it to bring the
   keyboard up anywhere, also on the desktop or in apps without text-input
   support. Its menu (long-press or right-click) offers **Copy, Paste and
   Select all**, which send the real Ctrl+C / Ctrl+V / Ctrl+A to the focused
   window without opening the keyboard: select text in a PDF or web page, open
   the menu, tap Copy. It also has Show, Hide, "Take screenshot to clipboard"
   and Quit. `vkbd --copy`, `--paste`, `--toggle` and the *Toggle vkbd Keyboard*
   launcher do the same from a panel launcher or a custom shortcut. `--no-tray`
   or `tray=false` in the config file removes the icon; `toggleButton=true`
   adds a floating one-tap keyboard bubble at the bottom-right instead.

The Plasma tray icon's "enable/disable virtual keyboard" toggle and KWin's
`org.kde.kwin.VirtualKeyboard` D-Bus interface keep working, because to KWin
`vkbd` is just another input method.

## Standalone / X11

Run `vkbd`. It shows a floating keyboard button at the bottom-right; tap it (or
run `vkbd --toggle`) to show/hide the panel. Typing goes through `/dev/uinput`,
so the udev rule and `input` group membership are required in this mode.

## Options

```
vkbd [--show|--hide|--toggle|--quit|--copy|--paste] [--backend auto|im|uinput] [--shell auto|input-panel|layer-shell|plain]
     [--layout auto|compact|full] [--height 0.42] [--no-fn-row] [--toggle-button] [--no-tray] [--no-sound] [--no-preview]
vkbd --doctor                          # diagnose the KWin integration
vkbd --self-test                       # which backend works here? sends one Shift press/release
vkbd --render preview.png:1280x800     # draw the layout to a PNG (QT_QPA_PLATFORM=offscreen works)
```

The same keys can be set permanently in `~/.config/vkbd/vkbd.conf`:

```ini
[General]
height=0.42        ; fraction of the screen height (default 0.42 landscape, 0.36 portrait)
layout=auto        ; compact (portrait style), full (PC style) or auto by orientation
fnRow=true         ; Esc/F1–F12/Del row of the full layout
toggleButton=false ; always show the floating show/hide button
tray=true          ; keyboard icon in the system tray (click = show/hide)
keyPreview=true    ; enlarged label above a pressed character key
sound=true         ; key click sound
soundVolume=-8     ; click volume in dB (0 = loudest, -20 = quiet)
backend=auto
shell=auto
```

## Using the modifiers

* **Tap** Shift/Ctrl/Alt/Meta: latched for the next key (outlined). The
  modifier is only pressed around that key, so touching the page in between
  is not a Ctrl+click.
* **Tap twice**: locked until tapped again (filled); again only applied to
  keys you type, not to touches.
* **Hold** with one finger and tap keys with another: behaves like a physical key.
* Held keys auto-repeat (arrows, Backspace) because the app receives a real key
  press and release.
* In terminals (Konsole, QMLKonsole) Ctrl+C interrupts the running program;
  copy and paste there are **Ctrl+Shift+C** and **Ctrl+Shift+V**, exactly as
  with a hardware keyboard.

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

* Start with `vkbd --doctor`. It shows what KWin is configured to launch,
  whether that resolves to a binary, KWin's view of the virtual keyboard, which
  vkbd instance is running and how it was started, and the last log
  (`~/.cache/vkbd/vkbd.log`).
* *Keyboard set in System Settings but never appears*: do not run `vkbd` by hand
  in the same session. KWin starts its own instance; a manually started one is
  now replaced automatically, but older builds made KWin's instance exit. Stop a
  stray one with `vkbd --quit`. KWin's stderr for the input method is in
  `journalctl --user -b | grep vkbd`.
* *Keys do nothing*: run `vkbd --self-test`; it prints which backend it can use.
  "backend = none" means KWin did not activate the IM and `/dev/uinput` is not
  writable. Check `ls -l /dev/uinput` and `id` for the `input` group.
* *Keyboard never appears when tapping a field*: make sure the field is in an app
  that supports Wayland text-input and that KWin's virtual keyboard is enabled in
  the tray icon. `KWIN_IM_SHOW_ALWAYS=1` in KWin's environment forces it to show
  for mouse input too.
* *Wrong characters*: labels come from the keymap KWin sends; in uinput mode they
  come from `~/.config/kxkbrc`. The keys themselves always send keycodes, exactly
  like a physical US-PC keyboard, so the active xkb layout decides the character.
