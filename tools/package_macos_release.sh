#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DIST_DIR="$REPO_ROOT/dist/macos-arm64"
APP_NAME="PlotJugglerPro"
APP_PATH="$DIST_DIR/$APP_NAME.app"
DMG_STAGING="$DIST_DIR/.dmg-staging"
QT_BIN="/opt/homebrew/opt/qt@5/bin"
MACDEPLOYQT="$QT_BIN/macdeployqt"
INSTALL_PREFIX="$REPO_ROOT/install/macos-arm64"
PLUGIN_SOURCE="$INSTALL_PREFIX/lib/plotjuggler/plugins"

die() {
  echo "error: $*" >&2
  exit 1
}

require_file() {
  [[ -f "$1" ]] || die "missing required file: $1"
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

cd "$REPO_ROOT"
[[ "$(uname -s)" == "Darwin" ]] || die "macOS packaging must run on macOS"
[[ "$(uname -m)" == "arm64" ]] || die "build this release on an Apple Silicon Mac"
require_file "$MACDEPLOYQT"
require_command uv
require_command brew
require_command codesign
require_command hdiutil
require_command iconutil
require_command sips

raw_version="$(git describe --tags --always --dirty)"
release_id="$(
  printf '%s' "$raw_version" |
    sed -E 's#^v##; s#/#-#g; s#\.#-#g; s#[^A-Za-z0-9_-]#-#g; s#-+#-#g; s#^-##; s#-$##'
)"
bundle_version="$(
  printf '%s' "$raw_version" |
    sed -E 's#^v##' |
    grep -Eo '^[0-9]+([.][0-9]+)*' || true
)"
[[ -n "$release_id" ]] || die "git describe produced an empty release id"
[[ -n "$bundle_version" ]] || bundle_version="0.0.0"

DMG_PATH="$DIST_DIR/${APP_NAME}-${release_id}-macOS-arm64.dmg"

echo "Building macOS arm64 Release and installing plugins..."
cmake --preset macos-arm64-pro
cmake --build --preset macos-arm64-pro-release-install --parallel "${PJ_BUILD_JOBS:-4}"

require_file "$INSTALL_PREFIX/bin/plotjuggler"
[[ -d "$PLUGIN_SOURCE" ]] || die "missing installed plugin directory: $PLUGIN_SOURCE"
plugin_count="$(find "$PLUGIN_SOURCE" -maxdepth 1 -type f -name '*.dylib' | wc -l | tr -d ' ')"
((plugin_count > 0)) || die "no plugins found in $PLUGIN_SOURCE"

PYTHON_BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/plotjuggler-mf4-python.XXXXXX")"
trap 'rm -rf "$PYTHON_BUILD_DIR" "$DMG_STAGING"' EXIT
PYTHON_INSTALL_DIR="$PYTHON_BUILD_DIR/python"
PYTHON_VERSION="${PJ_MF4_PYTHON_VERSION:-3.13}"
ASAMMDF_VERSION="${PJ_ASAMMDF_VERSION:-8.8.28}"

echo "Provisioning relocatable Python $PYTHON_VERSION with asammdf $ASAMMDF_VERSION..."
uv python install --install-dir "$PYTHON_INSTALL_DIR" "$PYTHON_VERSION"
PYTHON_RUNTIME="$(find "$PYTHON_INSTALL_DIR" -mindepth 1 -maxdepth 1 -type d \
  -name 'cpython-*-macos-aarch64-none' -print -quit)"
[[ -n "$PYTHON_RUNTIME" ]] || die "uv did not install an Apple Silicon standalone Python runtime"
PYTHON_EXECUTABLE="$PYTHON_RUNTIME/bin/python3"
require_file "$PYTHON_EXECUTABLE"
PYTHON_SITE_PACKAGES="$("$PYTHON_EXECUTABLE" -c 'import sysconfig; print(sysconfig.get_path("purelib"))')"
uv pip install --python "$PYTHON_EXECUTABLE" --target "$PYTHON_SITE_PACKAGES" \
  "asammdf==$ASAMMDF_VERSION"
"$PYTHON_EXECUTABLE" -c 'import asammdf, numpy; print("Bundled asammdf", asammdf.__version__)'

echo "Creating macOS application icon..."
ICONSET_DIR="$PYTHON_BUILD_DIR/PlotJugglerPro.iconset"
ICON_SOURCE_PNG="$PYTHON_BUILD_DIR/PlotJugglerPro.png"
mkdir -p "$ICONSET_DIR"
sips -s format png "$REPO_ROOT/plotjuggler.svg" --out "$ICON_SOURCE_PNG" >/dev/null
for size in 16 32 128 256 512; do
  sips -z "$size" "$size" "$ICON_SOURCE_PNG" \
    --out "$ICONSET_DIR/icon_${size}x${size}.png" >/dev/null
  double_size=$((size * 2))
  sips -z "$double_size" "$double_size" "$ICON_SOURCE_PNG" \
    --out "$ICONSET_DIR/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET_DIR" -o "$PYTHON_BUILD_DIR/PlotJugglerPro.icns"

echo "Assembling application bundle..."
mkdir -p "$DIST_DIR"
rm -rf "$APP_PATH" "$DMG_STAGING"
mkdir -p "$APP_PATH/Contents/MacOS" \
         "$APP_PATH/Contents/PlugIns/plotjuggler" \
         "$APP_PATH/Contents/Resources/MF4Python"
cp "$INSTALL_PREFIX/bin/plotjuggler" "$APP_PATH/Contents/MacOS/plotjuggler"
cp "$PLUGIN_SOURCE"/*.dylib "$APP_PATH/Contents/PlugIns/plotjuggler/"
cp -R "$PYTHON_RUNTIME"/. "$APP_PATH/Contents/Resources/MF4Python/"
cp "$PYTHON_BUILD_DIR/PlotJugglerPro.icns" \
  "$APP_PATH/Contents/Resources/PlotJugglerPro.icns"
cp "$REPO_ROOT/LICENSE.md" "$APP_PATH/Contents/Resources/LICENSE.md"
mkdir -p "$APP_PATH/Contents/Resources/licenses"
cp "$REPO_ROOT"/installer/io.plotjuggler.application/meta/license_*.txt \
  "$APP_PATH/Contents/Resources/licenses/"

cat > "$APP_PATH/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>English</string>
  <key>CFBundleExecutable</key>
  <string>plotjuggler</string>
  <key>CFBundleIconFile</key>
  <string>PlotJugglerPro.icns</string>
  <key>CFBundleIdentifier</key>
  <string>org.plotjuggler.PlotJugglerPro</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>PlotJugglerPro</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>${bundle_version}</string>
  <key>CFBundleVersion</key>
  <string>${bundle_version}</string>
  <key>LSMinimumSystemVersion</key>
  <string>13.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>
</dict>
</plist>
PLIST

echo "Embedding Qt frameworks and runtime plugins..."
"$MACDEPLOYQT" "$APP_PATH"

FRAMEWORKS_DIR="$APP_PATH/Contents/Frameworks"
echo "Adding Homebrew transitive runtime libraries..."
cp -L "$(brew --prefix brotli)/lib/libbrotlicommon.1.dylib" "$FRAMEWORKS_DIR/"
cp -L "$(brew --prefix webp)/lib/libsharpyuv.0.dylib" "$FRAMEWORKS_DIR/"

[[ -d "$APP_PATH/Contents/Frameworks/QtCore.framework" ]] ||
  die "Qt frameworks were not embedded by macdeployqt"
[[ -d "$APP_PATH/Contents/PlugIns/platforms" ]] ||
  die "Qt platform plugins were not embedded by macdeployqt"
[[ -d "$APP_PATH/Contents/PlugIns/plotjuggler" ]] ||
  die "PlotJuggler plugins are missing from the app bundle"
require_file "$APP_PATH/Contents/Resources/MF4Python/bin/python3"

echo "Validating the bundled MF4 runtime..."
"$APP_PATH/Contents/Resources/MF4Python/bin/python3" -c \
  'import asammdf, numpy; print("Bundled asammdf", asammdf.__version__)'

echo "Checking bundled Mach-O files for Homebrew build paths..."
while IFS= read -r -d '' binary; do
  binary_type="$(file -b "$binary")"
  case "$binary_type" in
    *Mach-O*)
      if [[ "$binary_type" == *executable* ]]; then
        dependency_lines="$(otool -L "$binary" | tail -n +2)"
      else
        dependency_lines="$(otool -L "$binary" | tail -n +3)"
      fi
      if grep -E '/opt/homebrew|/usr/local/opt/' <<<"$dependency_lines"; then
        die "unbundled Homebrew dependency found in $binary"
      fi
      while IFS= read -r dependency; do
        dependency_name="$(basename "$dependency")"
        find "$APP_PATH/Contents" -name "$dependency_name" -print -quit | grep -q . ||
          die "missing bundled dependency $dependency in $binary"
      done < <(awk 'index($1, "@rpath/") == 1 { print $1 }' <<<"$dependency_lines")
      ;;
  esac
done < <(find "$APP_PATH/Contents" -type f -print0)

echo "Applying a free ad-hoc signature (no Apple Developer account required)..."
codesign --force --deep --sign - "$APP_PATH"
codesign --verify --deep --strict "$APP_PATH"

mkdir -p "$DMG_STAGING"
cp -R "$APP_PATH" "$DMG_STAGING/"
ln -s /Applications "$DMG_STAGING/Applications"
rm -f "$DMG_PATH"
hdiutil create -volname "$APP_NAME" -srcfolder "$DMG_STAGING" \
  -ov -format UDZO "$DMG_PATH"
rm -rf "$DMG_STAGING"

echo "Release ready (ad-hoc signed, not notarized):"
echo "  App: $APP_PATH"
echo "  DMG: $DMG_PATH"
echo "  Plugins bundled: $plugin_count"
echo "  Python: $(basename "$PYTHON_RUNTIME") with asammdf $ASAMMDF_VERSION"
