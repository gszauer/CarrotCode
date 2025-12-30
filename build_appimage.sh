#!/usr/bin/env bash
# CarrotCode → AppImage using linuxdeploy + appimage plugin (no direct appimagetool download)
set -euo pipefail

OUTPUT="carrotcode"
rm -f "./${OUTPUT}"

g++ -std=c++17 -O3 -DNDEBUG -s \
    -o "${OUTPUT}" \
    code/linux.cpp \
    code/document.cpp \
    code/syntax.cpp \
    code/strings.cpp \
    code/software_renderer.cpp \
    code/imgui.cpp \
    code/view.cpp \
    code/application.cpp \
    -lX11

printf 'Built %s\n' "${OUTPUT}"

APP=CarrotCode
BIN=carrotcode
ICON=carrotcode.png
ARCH=$(uname -m)

# verify inputs
test -x "./$BIN"   || { echo "ERROR: ./$BIN not found or not executable"; exit 1; }
test -f "./$ICON"  || { echo "ERROR: ./$ICON not found"; exit 1; }

echo "[1/5] Clean up"
rm -rf AppDir linuxdeploy linuxdeploy-plugin-appimage*.AppImage *.AppImage

echo "[2/5] Create AppDir layout"
mkdir -p AppDir/usr/bin
cp "./$BIN" "AppDir/usr/bin/$BIN"
chmod +x "AppDir/usr/bin/$BIN"

# .desktop (Terminal=true for CLI; set to false if GUI)
cat > AppDir/${APP}.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=CarrotCode
Comment=CarrotCode application
Exec=carrotcode
Icon=carrotcode
Terminal=true
Categories=Utility;
EOF

# icon
mkdir -p AppDir/usr/share/icons/hicolor/256x256/apps
cp "./$ICON" AppDir/usr/share/icons/hicolor/256x256/apps/carrotcode.png

echo "[3/5] Download linuxdeploy and its AppImage plugin"
dl() {  # wget with curl -L fallback, ensure non-zero file
  local url="$1" out="$2"
  rm -f "$out"
  wget -qO "$out" "$url" || curl -fsSL "$url" -o "$out"
  test -s "$out" || { echo "Download failed for $url"; rm -f "$out"; return 1; }
}

# linuxdeploy (arch-specific, with fallback to latest URL)
LD="linuxdeploy"
dl "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" "$LD" \
  || dl "https://github.com/linuxdeploy/linuxdeploy/releases/latest/download/linuxdeploy-${ARCH}.AppImage" "$LD"

# plugin (one binary works for all, we’ll grab x86_64 as that’s what upstream publishes most consistently)
PLUGIN="linuxdeploy-plugin-appimage-x86_64.AppImage"
dl "https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/$PLUGIN" "$PLUGIN" \
  || dl "https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/latest/download/$PLUGIN" "$PLUGIN"

chmod +x "$LD" "$PLUGIN"

echo "[4/5] Bundle deps and output AppImage"
# Make the plugin discoverable. Two options:
#  - Put plugin next to linuxdeploy and call with -o appimage
#  - Or use --plugin appimage (requires plugin in PATH)
export PATH="$PWD:$PATH"

# Avoid FUSE by extracting and running AppImages on the fly
export APPIMAGE_EXTRACT_AND_RUN=1

# linuxdeploy will:
#   • stage deps into AppDir
#   • use the plugin to produce an AppImage (-o appimage)
"./$LD" --appdir AppDir \
  --executable AppDir/usr/bin/$BIN \
  --desktop-file AppDir/${APP}.desktop \
  --icon-file AppDir/usr/share/icons/hicolor/256x256/apps/carrotcode.png \
  -o appimage

echo "[5/5] Done. Produced AppImage(s):"
ls -1 *.AppImage || true
echo
echo "If you don't see ${APP}-*.AppImage, check the logs above."
echo "Tip: set Terminal=false in AppDir/${APP}.desktop if CarrotCode is GUI."
