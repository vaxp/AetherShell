#!/bin/bash

# AetherShell - Master Uninstaller
# This script removes all AetherShell components installed from .deb packages.

set -e

if [[ $EUID -ne 0 ]]; then
   echo "Error: This script must be run as root (use sudo)."
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
    aether-vpanel
)

echo "--- Uninstalling AetherShell Components ---"
apt remove --purge -y "${packages[@]}" || true

echo ""
echo "--- Removing unused dependencies ---"
apt autoremove --purge -y

echo ""
echo "--- Uninstallation Complete ---"
