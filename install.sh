#!/usr/bin/env bash
# Build and install vkbd on the tablet, then wire it into Plasma.
#
#   ./install.sh            install build deps, build, install to /usr/local, set up uinput, register with KWin
#   ./install.sh --no-kwin  skip the KWin/kwinrc step (X11 sessions)
#   ./install.sh --no-deps  skip the package installation step
#
set -euo pipefail
cd "$(dirname "$0")"

PREFIX="${PREFIX:-/usr/local}"
DO_KWIN=1
DO_DEPS=1
for arg in "$@"; do
    case "$arg" in
        --no-kwin) DO_KWIN=0 ;;
        --no-deps) DO_DEPS=0 ;;
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
        sudo apt-get update
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
        # A package name may differ on this release; install what exists one by one.
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
    install_deps
fi

for tool in cmake g++; do
    if ! command -v "$tool" >/dev/null; then
        echo "error: $tool is still missing; see README.md for the dependency list" >&2
        exit 1
    fi
done

echo "==> Building"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build build -j"$(nproc)"

echo "==> Installing to $PREFIX (sudo)"
sudo cmake --install build
# KWin only looks in the XDG applications dirs; /usr/local/share is one of them
# on most distros, but make sure the entry is visible everywhere.
sudo install -Dm644 data/vkbd.desktop /usr/share/applications/vkbd.desktop
sudo install -Dm644 data/vkbd-toggle.desktop /usr/share/applications/vkbd-toggle.desktop

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
        kwriteconfig6 --file kwinrc --group Wayland --key InputMethod /usr/share/applications/vkbd.desktop
        kwriteconfig6 --file kwinrc --group Wayland --key VirtualKeyboardEnabled true
        if command -v qdbus6 >/dev/null; then
            qdbus6 org.kde.KWin /KWin reconfigure || true
        elif command -v qdbus >/dev/null; then
            qdbus org.kde.KWin /KWin reconfigure || true
        fi
        echo "    done. You can also pick it in System Settings > Keyboard > Virtual Keyboard."
    else
        echo "    kwriteconfig6 not found; choose 'vkbd Virtual Keyboard' in System Settings > Keyboard > Virtual Keyboard."
    fi
fi

echo
echo "All set. Tap a text field to bring the keyboard up."
echo "Standalone/X11: run 'vkbd' (or pin 'Toggle vkbd Keyboard' to the panel)."
