#!/bin/bash

# Configuration
BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${BASE_DIR}/packages"
BUILD_DIR="${BASE_DIR}/pkg_build"

mkdir -p "${PKG_DIR}"
mkdir -p "${BUILD_DIR}"

projects=("auth" "desktop" "AetherDock" "launcher" "osd-notify" "panel" "basilisk" "aetherlock" "aetheridle" "aether-recorder" "vpanel")

for proj in "${projects[@]}"; do
    echo "--- Packaging ${proj} ---"
    PROJ_PATH="${BASE_DIR}/${proj}"
    STAGING="${BUILD_DIR}/${proj}_pkg"
    PKG_NAME="aether-${proj,,}"
    
    # Cleanup staging
    rm -rf "${STAGING}"
    mkdir -p "${STAGING}/DEBIAN"
    mkdir -p "${STAGING}/usr/bin"
    
    # 1. Build
    cd "${PROJ_PATH}"
    if [ -f "Makefile" ]; then
        make clean && make
    elif [ -f "meson.build" ]; then
        rm -rf build
        meson build --prefix=/usr
        ninja -C build
    fi
    
    # 2. Install to staging
    if [ "${proj}" == "auth" ]; then
        cp "${PROJ_PATH}/auth" "${STAGING}/usr/bin/"
        mkdir -p "${STAGING}/usr/lib/systemd/user"
        cp "${PROJ_PATH}/auth.service" "${STAGING}/usr/lib/systemd/user/"
    elif [ "${proj}" == "launcher" ]; then
        cp "${PROJ_PATH}/build/launcher" "${STAGING}/usr/bin/aether-launcher"
        # Install CSS to PKGDATADIR = /usr/share/launcher/style/launcher.css
        mkdir -p "${STAGING}/usr/share/launcher/style"
        cp "${PROJ_PATH}/data/style/launcher.css" "${STAGING}/usr/share/launcher/style/launcher.css"
        mkdir -p "${STAGING}/usr/share/applications"
        cp "${PROJ_PATH}/data/launcher.desktop" "${STAGING}/usr/share/applications/aether-launcher.desktop"
        echo "NoDisplay=true" >> "${STAGING}/usr/share/applications/aether-launcher.desktop"
        # Correct Exec path in desktop file
        sed -i 's/^Exec=.*/Exec=aether-launcher/' "${STAGING}/usr/share/applications/aether-launcher.desktop"
    elif [ "${proj}" == "osd-notify" ]; then
        cp "${PROJ_PATH}/osd-notify" "${STAGING}/usr/bin/"
        mkdir -p "${STAGING}/usr/lib/systemd/user"
        cp "${PROJ_PATH}/osd-notify.service" "${STAGING}/usr/lib/systemd/user/"
    elif [ "${proj}" == "desktop" ]; then
        cp "${PROJ_PATH}/desktop" "${STAGING}/usr/bin/"
        mkdir -p "${STAGING}/usr/share/applications"
        cp "${PROJ_PATH}/desktop.desktop" "${STAGING}/usr/share/applications/aether-desktop.desktop"
        echo "NoDisplay=true" >> "${STAGING}/usr/share/applications/aether-desktop.desktop"
        sed -i 's/^Exec=.*/Exec=desktop/' "${STAGING}/usr/share/applications/aether-desktop.desktop"
    elif [ "${proj}" == "AetherDock" ]; then
        cp "${PROJ_PATH}/AetherDock" "${STAGING}/usr/bin/"
        # Copy theme files next to binary so resolve_resource_path() finds them
        cp "${PROJ_PATH}/style.css"     "${STAGING}/usr/bin/style.css"
        cp "${PROJ_PATH}/launcher.svg"  "${STAGING}/usr/bin/launcher.svg"
    elif [ "${proj}" == "panel" ]; then
        cp "${PROJ_PATH}/panel" "${STAGING}/usr/bin/"
        # Copy resources next to binary so panel_resource_path_in() finds them
        mkdir -p "${STAGING}/usr/bin/resources/images"
        cp "${PROJ_PATH}/style.css"        "${STAGING}/usr/bin/resources/style.css"
        cp "${PROJ_PATH}/images/"*         "${STAGING}/usr/bin/resources/images/"
    elif [ "${proj}" == "basilisk" ]; then
        cp "${PROJ_PATH}/basilisk" "${STAGING}/usr/bin/"
    elif [ "${proj}" == "aetherlock" ]; then
        DESTDIR="${STAGING}" meson install -C "${PROJ_PATH}/build" --no-rebuild
    elif [ "${proj}" == "aetheridle" ]; then
        DESTDIR="${STAGING}" meson install -C "${PROJ_PATH}/build" --no-rebuild
    elif [ "${proj}" == "aether-recorder" ]; then
        DESTDIR="${STAGING}" meson install -C "${PROJ_PATH}/build" --no-rebuild
    elif [ "${proj}" == "vpanel" ]; then
        make DESTDIR="${STAGING}" install

        # Shared assets live under /usr/share to avoid filename conflicts in /usr/bin.
        mkdir -p "${STAGING}/usr/share/vpanel"
        cp "${PROJ_PATH}/style.css"                 "${STAGING}/usr/share/vpanel/style.css"
        cp "${PROJ_PATH}/control-center-icon.svg"   "${STAGING}/usr/share/vpanel/control-center-icon.svg"
        cp "${PROJ_PATH}/launchpad.svg"             "${STAGING}/usr/share/vpanel/launchpad.svg"

        # plugin-sandbox searches beside vpanel or in PATH, while Makefile installs
        # the host under /usr/lib/vpanel. Keep both locations reachable.
        ln -sf "../lib/vpanel/vpanel-plugin-host" "${STAGING}/usr/bin/vpanel-plugin-host"
    fi
    
    # 3. Create control file
    cat <<EOF > "${STAGING}/DEBIAN/control"
Package: ${PKG_NAME}
Version: 0.1.0
Section: utils
Priority: optional
Architecture: amd64
Maintainer: vaxp Developer <dev@vaxp.org>
Description: AetherShell component - ${proj}
EOF

    # 4. Build DEB
    dpkg-deb --build "${STAGING}" "${PKG_DIR}/${PKG_NAME}.deb"
done

echo "--- Finished packaging all projects ---"
ls -lh "${PKG_DIR}"
