#!/bin/bash
WIDGETS_DIR="${HOME}/.config/vaxp/desktop/widgets"
mkdir -p "$WIDGETS_DIR"
cp "${MESON_BUILD_ROOT}"/*.so "$WIDGETS_DIR"/
echo "Widgets installed to $WIDGETS_DIR"
