#!/bin/bash
set -euo pipefail
if [ "$#" -lt 3 ]; then printf 'Usage: %s APP_PATH SDK_PATH ARCH...\n' "$0" >&2; exit 2; fi
APP="$1"; SELECTED_SDK="$2"; shift 2
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
VENDOR="$PROJECT_DIR/ThirdParty/MediaRemoteAdapter"
FRAMEWORK="$APP/Contents/Frameworks/MediaRemoteAdapter.framework"
STAGE="$(mktemp -d "$(dirname "$APP")/.mediaremote.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/MediaRemoteAdapter.framework/Versions/A/Resources" "$APP/Contents/Resources/NowPlaying" "$APP/Contents/Frameworks"
SOURCES=("$VENDOR"/src/adapter/*.m "$VENDOR"/src/private/*.m "$VENDOR"/src/utility/*.m)
BINARIES=()
for ARCH in "$@"; do
    case "$ARCH" in arm64) TARGET=arm64-apple-macosx11.0 ;; x86_64) TARGET=x86_64-apple-macosx10.15.4 ;; *) exit 2 ;; esac
    BINARY="$STAGE/MediaRemoteAdapter-$ARCH"
    xcrun clang -O2 -dynamiclib -fobjc-arc -fvisibility=default -target "$TARGET" -isysroot "$SELECTED_SDK" \
        -ffile-prefix-map="$PROJECT_DIR"=/EndfieldHUD -I "$VENDOR/include" -I "$VENDOR/src" \
        -framework Foundation -framework AppKit -framework ImageIO -framework CoreServices -weak_framework UniformTypeIdentifiers \
        -install_name @rpath/MediaRemoteAdapter.framework/Versions/A/MediaRemoteAdapter \
        "${SOURCES[@]}" -o "$BINARY"
    BINARIES+=("$BINARY")
done
DEST="$STAGE/MediaRemoteAdapter.framework"
if [ "${#BINARIES[@]}" -gt 1 ]; then xcrun lipo -create "${BINARIES[@]}" -output "$DEST/Versions/A/MediaRemoteAdapter"; else cp "${BINARIES[0]}" "$DEST/Versions/A/MediaRemoteAdapter"; fi
cat > "$DEST/Versions/A/Resources/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>com.vandenbe.MediaRemoteAdapter</string>
<key>CFBundleName</key><string>MediaRemoteAdapter</string>
<key>CFBundleExecutable</key><string>MediaRemoteAdapter</string>
<key>CFBundlePackageType</key><string>FMWK</string>
<key>CFBundleVersion</key><string>0.1.0</string>
<key>CFBundleShortVersionString</key><string>0.1</string>
</dict></plist>
PLIST
ln -s A "$DEST/Versions/Current"
ln -s Versions/Current/MediaRemoteAdapter "$DEST/MediaRemoteAdapter"
ln -s Versions/Current/Resources "$DEST/Resources"
SIGN_ARGS=(--force --sign "${CODE_SIGN_IDENTITY:--}")
if [ "${CODE_SIGN_IDENTITY:--}" != '-' ]; then SIGN_ARGS+=(--timestamp --options runtime); fi
codesign "${SIGN_ARGS[@]}" "$DEST"
codesign --verify --strict --all-architectures "$DEST"
if [ -d "$FRAMEWORK" ]; then rm -rf "$FRAMEWORK"; fi
ditto "$DEST" "$FRAMEWORK"
cp "$VENDOR/bin/mediaremote-adapter.pl" "$APP/Contents/Resources/NowPlaying/mediaremote-adapter.pl"
cp "$VENDOR/LICENSE" "$APP/Contents/Resources/MediaRemoteAdapter-LICENSE.txt"
