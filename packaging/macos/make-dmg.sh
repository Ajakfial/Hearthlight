#!/bin/sh
# Hearthlight macOS .dmg packager. Run after a Release build on macOS:
#
#   cmake --preset macos
#   cmake --build --preset macos --config Release
#   QT_PATH=$(qmake6 -query QT_INSTALL_PREFIX)  # or your Qt install
#   sh packaging/macos/make-dmg.sh 0.1.0 "$QT_PATH"
#
# Produces Hearthlight-0.1.0.dmg containing Hearthlight.app with Qt deployed.
set -eu
VERSION="${1:-0.1.0}"
QT_PATH="${2:-$HOME/Qt/6.7.0/macos}"
APP="build/macos/src/app/Hearthlight.app"
DMG="Hearthlight-${VERSION}.dmg"

if [ ! -d "$APP" ]; then
  echo "Missing $APP — build first (cmake --preset macos)." >&2
  exit 1
fi
"$QT_PATH/bin/macdeployqt" "$APP" -dmg -verbose=1
mv "${APP%.app}.dmg" "$DMG" 2>/dev/null || true
if [ ! -f "$DMG" ]; then
  # Fallback when macdeployqt -dmg is unavailable: plain hdiutil image.
  rm -f "$DMG"
  hdiutil create -volname Hearthlight -srcfolder "$APP" -ov -format UDZO "$DMG"
fi
echo "Wrote $DMG"
