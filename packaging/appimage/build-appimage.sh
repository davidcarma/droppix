#!/usr/bin/env bash
# Build a thin droppix AppImage. "Thin" = it does NOT bundle Qt6; it relies on the
# host already having the Qt6 runtime (KDE Plasma ships it, which is droppix's target).
# Keeps the image ~4 MB instead of ~50 MB.
#
# Build droppix_gui in the `droppix-dev` distrobox, but run THIS script on the host:
# appimagetool needs the `file` command (absent in the distrobox) and the host's Qt6,
# and everything it touches ($DROPPIX_BUILD_DIR, appimagetool) lives off the CIFS mount.
# The CIFS mount is noexec, so invoke the script via `bash` rather than executing it.
#
#   distrobox enter droppix-dev -- bash -lc 'cmake --build "$HOME/droppix-build" -j'
#   bash packaging/appimage/build-appimage.sh        # on the host
#
# Env overrides: DROPPIX_BUILD_DIR (default ~/droppix-build), APPIMAGETOOL (default
# ~/bin/appimagetool), OUT (default $DROPPIX_BUILD_DIR/droppix-x86_64.AppImage).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${DROPPIX_BUILD_DIR:-$HOME/droppix-build}"
APPIMAGETOOL="${APPIMAGETOOL:-$HOME/bin/appimagetool}"
OUT="${OUT:-$BUILD_DIR/droppix-x86_64.AppImage}"

GUI_BIN="$BUILD_DIR/droppix_gui"
ICON="$REPO/host/icons/droppix-256.png"
APPDIR="$BUILD_DIR/AppDir"

[ -x "$GUI_BIN" ] || { echo "error: $GUI_BIN missing — run 'cmake --build $BUILD_DIR -j' first" >&2; exit 1; }
[ -x "$APPIMAGETOOL" ] || { echo "error: appimagetool not found at $APPIMAGETOOL (set APPIMAGETOOL=)" >&2; exit 1; }
[ -f "$ICON" ] || { echo "error: icon $ICON missing" >&2; exit 1; }

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
cp "$GUI_BIN" "$APPDIR/usr/bin/droppix_gui"

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export PATH="$HERE/usr/bin:$PATH"
exec "$HERE/usr/bin/droppix_gui" "$@"
EOF
chmod +x "$APPDIR/AppRun"

cat > "$APPDIR/droppix.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=droppix
Comment=Use a tablet as a second monitor
Exec=droppix_gui
Icon=droppix
Categories=Utility;
Terminal=false
EOF

cp "$ICON" "$APPDIR/droppix.png"
cp "$ICON" "$APPDIR/.DirIcon"

ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$OUT"
echo "built: $OUT"
