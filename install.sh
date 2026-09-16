#!/usr/bin/env bash
# Build and install vkbd on the tablet, then wire it into Plasma.
#
#   ./install.sh            install build deps, build, install to /usr/local, set up uinput, register with KWin
#   ./install.sh --no-kwin  skip the KWin/kwinrc step (X11 sessions)
#   ./install.sh --no-deps  skip the package installation step
#   ./install.sh --no-sddm  do not touch the login screen (SDDM) configuration
#
set -euo pipefail
cd "$(dirname "$0")"

PREFIX="${PREFIX:-/usr/local}"
DO_KWIN=1
DO_DEPS=1
DO_SDDM=1
for arg in "$@"; do
    case "$arg" in
        --no-kwin) DO_KWIN=0 ;;
        --no-deps) DO_DEPS=0 ;;
        --no-sddm) DO_SDDM=0 ;;
        *) echo "unknown option: $arg" >&2; exit 1 ;;
    esac
done

install_deps() {
    local pkgs=()
    local cmd=()
    if command -v apt-get >/dev/null; then
        cmd=(sudo apt-get install -y)
        pkgs=(cmake ninja-build g++ pkg-config
              qt6-base-dev qt6-base-private-dev qt6-wayland-dev qt6-wayland-private-dev
              libwayland-dev libxkbcommon-dev liblayershellqtinterface-dev)
        if ! sudo apt-get update; then
            echo "    apt-get update failed (another package manager such as Discover/packagekitd may hold the lock); continuing with what is installed"
        fi
    elif command -v dnf >/dev/null; then
        cmd=(sudo dnf install -y)
        pkgs=(cmake ninja-build gcc-c++ pkgconf-pkg-config
              qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtwayland-devel
              wayland-devel libxkbcommon-devel layer-shell-qt-devel)
    elif command -v pacman >/dev/null; then
        cmd=(sudo pacman -S --needed --noconfirm)
        pkgs=(cmake ninja gcc pkgconf qt6-base qt6-wayland wayland libxkbcommon layer-shell-qt)
    elif command -v zypper >/dev/null; then
        cmd=(sudo zypper install -y)
        pkgs=(cmake ninja gcc-c++ pkg-config
              qt6-base-devel qt6-base-private-devel qt6-wayland-devel
              wayland-devel libxkbcommon-devel layer-shell-qt-devel)
    else
        echo "    no known package manager found; install the dependencies listed in README.md by hand"
        return
    fi

    if ! "${cmd[@]}" "${pkgs[@]}"; then
        # A package name may differ on this release, or the package manager is
        # busy; install what can be installed one by one and carry on.
        echo "    bulk install failed, retrying package by package"
        local missing=()
        for p in "${pkgs[@]}"; do
            "${cmd[@]}" "$p" >/dev/null 2>&1 || missing+=("$p")
        done
        if [[ ${#missing[@]} -gt 0 ]]; then
            echo "    could not install: ${missing[*]}"
            echo "    (private Qt headers and layer-shell-qt are optional; cmake/g++/qt6 base+wayland dev are required)"
        fi
    fi
}

if [[ $DO_DEPS -eq 1 ]]; then
    echo "==> Installing build dependencies (sudo)"
    install_deps || echo "    dependency step had errors; continuing (use --no-deps to skip it)"
fi

for tool in cmake g++; do
    if ! command -v "$tool" >/dev/null; then
        echo "error: $tool is still missing; see README.md for the dependency list" >&2
        exit 1
    fi
done

echo "==> Building"
# A build directory that was created for another source path (checkout moved
# or copied) makes CMake refuse to configure; start it afresh in that case.
if [[ -f build/CMakeCache.txt ]]; then
    cached_src=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' build/CMakeCache.txt)
    if [[ -n "$cached_src" && "$cached_src" != "$PWD" ]]; then
        echo "    build/ was configured for $cached_src; recreating it"
        rm -rf build
    fi
fi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build build -j"$(nproc)"

echo "==> Installing to $PREFIX (sudo)"
# Stop any instance started by hand; it would otherwise block KWin's own one.
if command -v vkbd >/dev/null; then vkbd --quit 2>/dev/null || true; fi
sudo cmake --install build
# KWin only looks in the XDG applications dirs and runs Exec with its own PATH,
# so install the entries under /usr/share with an absolute Exec path.
sed "s|^Exec=vkbd|Exec=$PREFIX/bin/vkbd|" data/vkbd.desktop | sudo tee /usr/share/applications/vkbd.desktop >/dev/null
for entry in vkbd-toggle vkbd-copy vkbd-paste; do
    sed "s|^Exec=vkbd|Exec=$PREFIX/bin/vkbd|" "data/$entry.desktop" | sudo tee "/usr/share/applications/$entry.desktop" >/dev/null
done
sudo chmod 644 /usr/share/applications/vkbd.desktop /usr/share/applications/vkbd-*.desktop

echo "==> uinput access (fallback backend for X11 / apps without text-input support)"
sudo install -Dm644 data/60-vkbd-uinput.rules /etc/udev/rules.d/60-vkbd-uinput.rules
echo uinput | sudo tee /etc/modules-load.d/vkbd-uinput.conf >/dev/null
sudo modprobe uinput || true
sudo udevadm control --reload-rules && sudo udevadm trigger --name-match=uinput || true
if ! id -nG "$USER" | tr ' ' '\n' | grep -qx input; then
    sudo usermod -aG input "$USER"
    echo "    added $USER to the 'input' group (takes effect after you log out and back in)"
fi

if [[ $DO_KWIN -eq 1 ]]; then
    echo "==> Registering vkbd as KWin's virtual keyboard"
    if command -v kwriteconfig6 >/dev/null; then
        # KWin only (re)starts the input method when the command changes, so
        # clear it first to make sure the freshly installed binary is launched.
        kwriteconfig6 --notify --file kwinrc --group Wayland --key InputMethod ""
        sleep 1
        kwriteconfig6 --notify --file kwinrc --group Wayland --key InputMethod /usr/share/applications/vkbd.desktop
        kwriteconfig6 --notify --file kwinrc --group Wayland --key VirtualKeyboardEnabled true
        if command -v qdbus6 >/dev/null; then
            qdbus6 org.kde.KWin /KWin reconfigure || true
        elif command -v qdbus >/dev/null; then
            qdbus org.kde.KWin /KWin reconfigure || true
        fi
        sleep 2
        echo "    done. You can also pick it in System Settings > Keyboard > Virtual Keyboard."
        echo "    If it does not appear when you tap a text field, run: vkbd --doctor"
    else
        echo "    kwriteconfig6 not found; choose 'vkbd Virtual Keyboard' in System Settings > Keyboard > Virtual Keyboard."
    fi
fi

if [[ $DO_SDDM -eq 1 ]] && command -v sddm >/dev/null; then
    echo "==> Login screen (SDDM on Wayland) on-screen keyboard"
    # Plasma starts the login screen's own KWin with "--inputmethod
    # maliit-keyboard" (or plasma-keyboard) from an SDDM config file; the
    # keyboard chosen in System Settings is not consulted there. Override the
    # compositor command in a drop-in that sorts last, keeping every other flag.
    cmd=$(grep -hs "^CompositorCommand=" /usr/lib/sddm/sddm.conf.d/*.conf /etc/sddm.conf /etc/sddm.conf.d/*.conf 2>/dev/null | grep -v vkbd | tail -1 | cut -d= -f2-)
    [[ -z "$cmd" ]] && cmd="kwin_wayland --no-global-shortcuts --no-lockscreen --locale1"
    if [[ "$cmd" == *--inputmethod* ]]; then
        cmd=$(printf '%s' "$cmd" | sed -E "s#--inputmethod[= ][^ ]+#--inputmethod $PREFIX/bin/vkbd#")
    else
        cmd="$cmd --inputmethod $PREFIX/bin/vkbd"
    fi
    printf '# Installed by vkbd/install.sh: use vkbd as the login screen keyboard.
[Wayland]
CompositorCommand=%s
' "$cmd" | sudo tee /etc/sddm.conf.d/zz-vkbd.conf >/dev/null
    # The sddm user needs /dev/uinput too, otherwise only the input-method path is available there.
    sudo usermod -aG input sddm 2>/dev/null || true
    echo "    /etc/sddm.conf.d/zz-vkbd.conf -> $cmd"
    echo "    (takes effect at the next login screen; remove the file to go back to the default keyboard)"
fi

echo
echo "All set. Tap a text field to bring the keyboard up."
echo "Standalone/X11: run 'vkbd' (or pin 'Toggle vkbd Keyboard' to the panel)."
