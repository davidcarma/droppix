#!/usr/bin/env bash
# Build the droppix CLIENT AppImage (droppix_client).
#
# What it bundles: droppix_client + the app/codec libraries a host might not have at the
# right version — ffmpeg (avcodec/avutil/swscale + their large external-codec closure) and
# OpenSSL.
#
# What it deliberately does NOT bundle: Qt6 and the low-level display/GPU/glib/xcb system
# stack. A tolerant experiment proved a fully-bundled Qt here SIGSEGVs at startup — the
# bundled Qt xcb platform stack clashes with the host's `libxcb.so.1`/`libGL`, which MUST
# come from the host (they can't be portably bundled). This is the same constraint the HOST
# AppImage documents. So the client AppImage relies on host Qt6 (present on this and any
# KDE/Qt6 machine; verified libQt6Multimedia 6.11 here). For a FULLY self-contained client,
# use the Flatpak (org.droppix.Client) instead — the KDE runtime supplies a consistent Qt6.
#
# Build the binary in the droppix-dev distrobox first; run THIS on the host (appimagetool
# needs `file`, absent in the distrobox). linuxdeploy + patchelf run inside the distrobox.
#
#   distrobox enter droppix-dev -- bash -lc 'cmake --build "$HOME/droppix-client-build" -j'
#   bash packaging/appimage/build-client-appimage.sh
#
# Env overrides: DROPPIX_CLIENT_BUILD_DIR (default ~/droppix-client-build), OUT, APPIMAGETOOL.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${DROPPIX_CLIENT_BUILD_DIR:-$HOME/droppix-client-build}"
APPIMAGETOOL="${APPIMAGETOOL:-$HOME/bin/appimagetool}"
OUT="${OUT:-$BUILD_DIR/droppix-client-x86_64.AppImage}"
CACHE="$HOME/.cache/droppix-appimage"
APPDIR="$BUILD_DIR/AppDir"
DISTROBOX="${DROPPIX_DISTROBOX:-droppix-dev}"

BIN="$BUILD_DIR/droppix_client"
ICON="$REPO/host/icons/droppix-256.png"

[ -x "$BIN" ]          || { echo "error: $BIN missing — build it first" >&2; exit 1; }
[ -x "$APPIMAGETOOL" ] || { echo "error: appimagetool not found at $APPIMAGETOOL" >&2; exit 1; }
mkdir -p "$CACHE"

# --- tooling: linuxdeploy (bundles ldd deps + sets $ORIGIN rpaths) + patchelf ---
LD="$CACHE/linuxdeploy-x86_64.AppImage"
if [ ! -x "$LD" ]; then
  echo "fetching linuxdeploy..."
  curl -fsSL -o "$LD" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
  chmod +x "$LD"
fi
distrobox enter "$DISTROBOX" -- bash -lc 'command -v patchelf >/dev/null || sudo dnf install -y patchelf'

# --- stage icon + desktop ---
cp "$ICON" "$BUILD_DIR/droppix-client.png"
cat > "$BUILD_DIR/droppix-client.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Droppix Client
Comment=Use this PC as a second monitor for a droppix host
Exec=droppix_client
Icon=droppix-client
Categories=Utility;Network;
Terminal=false
EOF

# --- populate the AppDir (inside the distrobox: has patchelf) ---
rm -rf "$APPDIR"; mkdir -p "$APPDIR"
distrobox enter "$DISTROBOX" -- bash -lc "
  export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
  cd '$BUILD_DIR'
  '$LD' --appimage-extract-and-run --appdir AppDir \
    -e droppix_client -i droppix-client.png -d droppix-client.desktop
"

# --- prune the host-provided stack (keep ffmpeg/openssl + their codec deps) ---
# Same rationale as the host AppImage: rely on host Qt + host xcb/GL/glib. Bundling any of
# these on top of the host's copies double-loads glib/xcb and crashes the Qt xcb plugin.
LIBDIR="$APPDIR/usr/lib"
rm -f  "$APPDIR/usr/bin/qt.conf"
rm -rf "$APPDIR/usr/plugins"
for pat in 'libQt6*' \
           'libglib-2*' 'libgobject-2*' 'libgio-2*' 'libgmodule-2*' 'libglycin*' \
           'libcairo*' 'libgdk*' 'libpango*' 'libharfbuzz*' 'libgraphite2*' \
           'libfreetype*' 'libfontconfig*' 'libpng16*' 'libpixman*' 'librsvg*' \
           'libxcb*' 'libX11*' 'libXau*' 'libXdmcp*' 'libxkbcommon*' 'libX*' \
           'libSM*' 'libICE*' 'libuuid*' \
           'libGL*' 'libEGL*' 'libGLX*' 'libGLdispatch*' 'libOpenGL*' 'libglvnd*' \
           'libwayland*' 'libgbm*' 'libdrm*' 'libva*' \
           'libexpat*' 'libbrotli*' 'libbz2*' 'libz.so*' \
           'libmount*' 'libblkid*' 'libselinux*' 'libsystemd*' 'libudev*' 'libcap*' \
           'libffi*' 'libpcre*' 'libicu*' 'libmd4c*' 'libdouble-conversion*'; do
  rm -f "$LIBDIR"/$pat
done
echo "bundled libs kept: $(ls "$LIBDIR" | wc -l)"

# --- package (host: appimagetool needs `file`; use a cached runtime if present) ---
RT="$CACHE/runtime-x86_64"
rm -f "$OUT"
if [ -s "$RT" ]; then
  ARCH=x86_64 "$APPIMAGETOOL" --runtime-file "$RT" "$APPDIR" "$OUT"
else
  ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$OUT"   # appimagetool fetches its runtime
fi
echo "built: $OUT ($(stat -c%s "$OUT") bytes)"

# Deliver a dated + short-commit copy into the repo's git-ignored "complete builds/".
DEST="$REPO/complete builds"
mkdir -p "$DEST"
SHA="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo nogit)"
FINAL="$DEST/droppix-client-$(date +%Y%m%d-%H%M)-$SHA-x86_64.AppImage"
cp "$OUT" "$FINAL"
chmod +x "$FINAL" 2>/dev/null || true
echo "delivered: $FINAL"
