#!/bin/bash

# AetherShell - Master Installer
# This script installs all components of AetherShell from .deb packages.

set -e

# check for sudo
if [[ $EUID -ne 0 ]]; then
   echo "Error: This script must be run as root (use sudo)." 
   exit 1
fi

PKG_DIR="./packages"

if [[ ! -d "$PKG_DIR" ]]; then
    echo "Error: 'packages' directory not found. Please run ./package_all.sh first."
    exit 1
fi

packages=(
    aether-auth
    aether-desktop
    aether-aetherdock
    aether-launcher
    aether-osd-notify
    aether-panel
    aether-basilisk
    aether-aetherlock
    aether-aetheridle
    aether-aether-recorder
)

deb_files=()
for pkg in "${packages[@]}"; do
    deb_file="${PKG_DIR}/${pkg}.deb"
    if [[ ! -f "$deb_file" ]]; then
        echo "Error: missing package '${deb_file}'. Please run ./package_all.sh first."
        exit 1
    fi
    deb_files+=("$deb_file")
done

echo "--- Installing AetherShell Components ---"
apt update || true
apt install -y "${deb_files[@]}"

echo ""
echo "--- Installation Complete ---"
echo "Note: All applications have been hidden from launchers (NoDisplay=true)."
echo "You can start them via aether.ini [autostart] or keybindings."
