#!/bin/sh
# Hearthlight Linux AppImage packager. Run after a Release build on Linux:
#
#   cmake --preset linux
#   cmake --build --preset linux --config Release
#   sh packaging/linux/make-appimage.sh 0.1.0
#
# Uses linuxdeploy + the Qt plugin (downloaded once into packaging/linux/tools).
# Produces Hearthlight-<version>-x86_64.AppImage. Portable mode inside the
# AppImage is off; user data lives in ~/.local/share/Hearthlight (or
# $XDG_DATA_HOME), game downloads need a normal internet connection once.
set -eu
VERSION="${1:-0.1.0}"
ROOT="$(dirname "$0")"
TOOLS="$ROOT/tools"
BIN="build/linux/src/app/Hearthlight"
APPDIR="Hearthlight.AppDir"

if [ ! -x "$BIN" ]; then
  echo "Missing $BIN — build first (cmake --preset linux)." >&2
  exit 1
fi
mkdir -p "$TOOLS"
if [ ! -x "$TOOLS/linuxdeploy-x86_64.AppImage" ]; then
  wget -O "$TOOLS/linuxdeploy-x86_64.AppImage" \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
  chmod +x "$TOOLS/linuxdeploy-x86_64.AppImage"
fi
if [ ! -x "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage" ]; then
  wget -O "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage" \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
  chmod +x "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage"
fi
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
cp "$BIN" "$APPDIR/usr/bin/Hearthlight"
cp "$ROOT/hearthlight.desktop" "$APPDIR/usr/share/applications/hearthlight.desktop" 2>/dev/null || {
  mkdir -p "$APPDIR/usr/share/applications"
  cp "$ROOT/hearthlight.desktop" "$APPDIR/usr/share/applications/hearthlight.desktop"
}
# Icon: rasterize the SVG logo (rsvg-convert preferred, ImageMagick fallback).
ICON_PNG="$APPDIR/usr/share/icons/hicolor/256x256/apps/hearthlight.png"
if command -v rsvg-convert >/dev/null 2>&1; then
  rsvg-convert -w 256 -h 256 assets/icons/logo.svg -o "$ICON_PNG"
elif command -v convert >/dev/null 2>&1; then
  convert -background none -resize 256x256 assets/icons/logo.svg "$ICON_PNG"
fi
ICON_ARG=""
if [ -f "$ICON_PNG" ]; then
  ICON_ARG="--icon-file $ICON_PNG"
fi
# shellcheck disable=SC2086
LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}" \
"$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" \
  --plugin qt \
  --desktop-file "$APPDIR/usr/share/applications/hearthlight.desktop" \
  $ICON_ARG \
  --output appimage
mv Hearthlight-*.AppImage "Hearthlight-${VERSION}-x86_64.AppImage" 2>/dev/null || true
echo "Wrote Hearthlight-${VERSION}-x86_64.AppImage"
