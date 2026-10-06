#!/bin/bash
set -euo pipefail

if [ "$#" -ne 0 ]; then
    printf 'Usage: %s\nEnvironment: DEV_BUILD_DIR, SDKROOT, DEV_OPTIMIZATION (-Onone, -O, -Osize)\n' "$0" >&2
    exit 2
fi

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
DEV_BUILD_DIR="${DEV_BUILD_DIR:-$PROJECT_DIR/build/dev}"
APP_NAME="EndfieldHUD"
DEV_OPTIMIZATION="${DEV_OPTIMIZATION:--O}"
case "$DEV_OPTIMIZATION" in
    -Onone|-O|-Osize) ;;
    *) printf 'DEV_OPTIMIZATION must be -Onone, -O, or -Osize.\n' >&2; exit 2 ;;
esac
SELECTED_SDK="$("$PROJECT_DIR/scripts/build.sh" --print-sdk)"
SWIFTC="$(xcrun --find swiftc)"
SPARKLE_DIR="$("$PROJECT_DIR/scripts/fetch-sparkle.sh")"
HOST_ARCH="$(uname -m)"
# Prefer the native architecture even if this shell was opened under Rosetta.
if [ "$HOST_ARCH" = x86_64 ] && [ "$(/usr/sbin/sysctl -in sysctl.proc_translated 2>/dev/null || true)" = 1 ]; then
    HOST_ARCH=arm64
fi
case "$HOST_ARCH" in
    arm64) TARGET="arm64-apple-macosx11.0" ;;
    x86_64) TARGET="x86_64-apple-macosx10.15.4" ;;
    *) printf 'Unsupported host architecture: %s\n' "$HOST_ARCH" >&2; exit 2 ;;
esac

mkdir -p "$DEV_BUILD_DIR"
DEV_BUILD_DIR="$(cd "$DEV_BUILD_DIR" && pwd -P)"
if [ -d "$PROJECT_DIR/build" ] && [ "$DEV_BUILD_DIR" = "$(cd "$PROJECT_DIR/build" && pwd -P)" ]; then
    printf 'DEV_BUILD_DIR must be separate from the universal build directory.\n' >&2
    exit 2
fi
mkdir -p "$DEV_BUILD_DIR/module-cache"
DEV_APP="$DEV_BUILD_DIR/$APP_NAME.app"
BASE_APP="$PROJECT_DIR/build/$APP_NAME.app"
# Reuse the existing icon/resources on the first build under the new app name.
if [ ! -d "$BASE_APP" ] && [ -d "$DEV_BUILD_DIR/EndfieldCharge.app" ]; then
    BASE_APP="$DEV_BUILD_DIR/EndfieldCharge.app"
fi
DEV_STAGE="$(mktemp -d "$DEV_BUILD_DIR/.stage.XXXXXX")"
trap 'rm -rf "$DEV_STAGE"' EXIT

# Preserve the old executable outside the renamed bundle. Its embedded Info.plist
# belongs to the old app name and would fail nested signature verification.
preserve_legacy_executable() {
    if [ -f "$1/Contents/MacOS/EndfieldCharge" ]; then
        LEGACY_EXECUTABLE_DIR="$(mktemp -d "$DEV_BUILD_DIR/.legacy-executable.XXXXXX")"
        mv "$1/Contents/MacOS/EndfieldCharge" "$LEGACY_EXECUTABLE_DIR/EndfieldCharge"
    fi
}

SOURCES=("$PROJECT_DIR"/Sources/*.swift)
BACKDROP_LINK_FLAGS=()
if [ -d "$SELECTED_SDK/System/Library/Frameworks/ScreenCaptureKit.framework" ]; then
    # Match the release link policy: the guarded macOS 14 background path
    # must not prevent the native 10.15/11 development app from launching.
    BACKDROP_LINK_FLAGS=(-Xlinker -weak_framework -Xlinker ScreenCaptureKit)
fi
if [ ! -f "${SOURCES[0]}" ]; then
    printf 'No Swift source files found in %s/Sources.\n' "$PROJECT_DIR" >&2
    exit 1
fi

printf 'Building native development app for %s (%s)…\n' "$TARGET" "$DEV_OPTIMIZATION"
# Review builds use the release optimizer. Opt into -Onone only for debugging;
# unoptimized scene evaluation is not representative of shipping performance.
"$SWIFTC" -swift-version 5 "$DEV_OPTIMIZATION" -whole-module-optimization \
    -sdk "$SELECTED_SDK" -target "$TARGET" \
    -F "$SPARKLE_DIR" -framework Sparkle -Xlinker -rpath -Xlinker @executable_path/../Frameworks \
    -module-cache-path "$DEV_BUILD_DIR/module-cache" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework ServiceManagement -framework Carbon -framework Quartz -framework Metal -framework MetalKit \
    "${BACKDROP_LINK_FLAGS[@]}" -lsqlite3 -lz -framework WebKit -framework Security -framework PDFKit \
    "${SOURCES[@]}" -o "$DEV_STAGE/$APP_NAME"

# Refresh the generated default icon only when its script or source changes.
ICON_CACHE="$DEV_BUILD_DIR/AppIcon.icns"
if [ ! -f "$ICON_CACHE" ] || [ "$PROJECT_DIR/scripts/generate-icon.swift" -nt "$ICON_CACHE" ] || [ "$PROJECT_DIR/Resources/EndfieldIndustriesSource.png" -nt "$ICON_CACHE" ]; then
    "$SWIFTC" -swift-version 5 -sdk "$SELECTED_SDK" -module-cache-path "$DEV_BUILD_DIR/module-cache" -framework Cocoa \
        "$PROJECT_DIR/scripts/generate-icon.swift" -o "$DEV_STAGE/generate-icon"
    "$DEV_STAGE/generate-icon" "$ICON_CACHE" "$PROJECT_DIR/Resources/EndfieldIndustriesSource.png"
fi

if [ ! -d "$DEV_APP" ]; then
    STAGED_APP="$DEV_STAGE/$APP_NAME.app"
    if [ -d "$BASE_APP" ]; then
        # Preserve the bundle layout, then install the current icon/resources below.
        ditto "$BASE_APP" "$STAGED_APP"
    else
        mkdir -p "$STAGED_APP/Contents/MacOS" "$STAGED_APP/Contents/Resources"
        cp "$PROJECT_DIR/Resources/Info.plist" "$STAGED_APP/Contents/Info.plist"
        cp "$PROJECT_DIR/LICENSE" "$STAGED_APP/Contents/Resources/LICENSE.txt"
        cp "$PROJECT_DIR/CREDITS.md" "$STAGED_APP/Contents/Resources/CREDITS.md"
    fi
    mv "$DEV_STAGE/$APP_NAME" "$STAGED_APP/Contents/MacOS/$APP_NAME"
    cp "$PROJECT_DIR/Resources/Info.plist" "$STAGED_APP/Contents/Info.plist"
    for LOCALIZATION in en zh-Hans zh-Hant ja ko; do
        ditto "$PROJECT_DIR/Resources/$LOCALIZATION.lproj" "$STAGED_APP/Contents/Resources/$LOCALIZATION.lproj"
    done
    cp "$PROJECT_DIR/Resources/EndfieldIndustriesSource.png" "$STAGED_APP/Contents/Resources/"
    cp "$ICON_CACHE" "$STAGED_APP/Contents/Resources/AppIcon.icns"
    ditto "$PROJECT_DIR/Resources/AppIconSources" "$STAGED_APP/Contents/Resources/AppIconSources"
    # Prepared cells replace the full atlas in the running app. Keep the original in source.
    if [ -d "$STAGED_APP/Contents/Resources/AppIconSources/Factions" ]; then rm -f "$STAGED_APP/Contents/Resources/AppIconSources/FactionAtlas.png"; fi
    ditto "$PROJECT_DIR/Resources/WorldMap" "$STAGED_APP/Contents/Resources/WorldMap"
    rm -rf "$STAGED_APP/Contents/Resources/MediaAssembly"
    ditto "$PROJECT_DIR/Resources/MediaAssembly" "$STAGED_APP/Contents/Resources/MediaAssembly"
    ditto "$PROJECT_DIR/Resources/OrbiPom" "$STAGED_APP/Contents/Resources/OrbiPom"
    ditto "$PROJECT_DIR/Resources/Watch" "$STAGED_APP/Contents/Resources/Watch"
    python3 "$PROJECT_DIR/scripts/package-watch-resources.py" stage \
        "$PROJECT_DIR/Resources/WatchSource" "$STAGED_APP/Contents/Resources/WatchSource"
    cp "$PROJECT_DIR/CREDITS.md" "$STAGED_APP/Contents/Resources/CREDITS.md"
    preserve_legacy_executable "$STAGED_APP"
    CODE_SIGN_IDENTITY=- "$PROJECT_DIR/scripts/embed-sparkle.sh" "$STAGED_APP" "$SPARKLE_DIR"
    CODE_SIGN_IDENTITY=- "$PROJECT_DIR/scripts/embed-now-playing.sh" "$STAGED_APP" "$SELECTED_SDK" "$HOST_ARCH"
    codesign --force --sign - --entitlements "$PROJECT_DIR/Resources/EndfieldHUD.entitlements" "$STAGED_APP"
    mv "$STAGED_APP" "$DEV_APP"
else
    # The temporary binary is on the same volume, so rename replaces the
    # executable atomically. A failed compile leaves the existing app intact.
    mv -f "$DEV_STAGE/$APP_NAME" "$DEV_APP/Contents/MacOS/$APP_NAME"
    # Keep new capability purpose strings in step with the development binary.
    cp "$PROJECT_DIR/Resources/Info.plist" "$DEV_APP/Contents/Info.plist"
    for LOCALIZATION in en zh-Hans zh-Hant ja ko; do
        ditto "$PROJECT_DIR/Resources/$LOCALIZATION.lproj" "$DEV_APP/Contents/Resources/$LOCALIZATION.lproj"
    done
    cp "$PROJECT_DIR/Resources/EndfieldIndustriesSource.png" "$DEV_APP/Contents/Resources/"
    cp "$ICON_CACHE" "$DEV_APP/Contents/Resources/AppIcon.icns"
    ditto "$PROJECT_DIR/Resources/AppIconSources" "$DEV_APP/Contents/Resources/AppIconSources"
    # Prepared cells replace the full atlas in the running app. Keep the original in source.
    if [ -d "$DEV_APP/Contents/Resources/AppIconSources/Factions" ]; then rm -f "$DEV_APP/Contents/Resources/AppIconSources/FactionAtlas.png"; fi
    ditto "$PROJECT_DIR/Resources/WorldMap" "$DEV_APP/Contents/Resources/WorldMap"
    rm -rf "$DEV_APP/Contents/Resources/MediaAssembly"
    ditto "$PROJECT_DIR/Resources/MediaAssembly" "$DEV_APP/Contents/Resources/MediaAssembly"
    ditto "$PROJECT_DIR/Resources/OrbiPom" "$DEV_APP/Contents/Resources/OrbiPom"
    ditto "$PROJECT_DIR/Resources/Watch" "$DEV_APP/Contents/Resources/Watch"
    python3 "$PROJECT_DIR/scripts/package-watch-resources.py" stage \
        "$PROJECT_DIR/Resources/WatchSource" "$DEV_APP/Contents/Resources/WatchSource"
    cp "$PROJECT_DIR/CREDITS.md" "$DEV_APP/Contents/Resources/CREDITS.md"
    preserve_legacy_executable "$DEV_APP"
    CODE_SIGN_IDENTITY=- "$PROJECT_DIR/scripts/embed-sparkle.sh" "$DEV_APP" "$SPARKLE_DIR"
    CODE_SIGN_IDENTITY=- "$PROJECT_DIR/scripts/embed-now-playing.sh" "$DEV_APP" "$SELECTED_SDK" "$HOST_ARCH"
    codesign --force --sign - --entitlements "$PROJECT_DIR/Resources/EndfieldHUD.entitlements" "$DEV_APP"
fi

# Use the bundle verifier's native architecture, signature, weak-link and
# current WatchSource checks without its release-only source-path restriction.
# Development intentionally retains its checkout paths for source fallback.
# Only runtime Resources/WatchSource is copied; extraction/recording files and
# the installed game are outside this bundle's resource source directory.
DEV_BINARY="$DEV_APP/Contents/MacOS/$APP_NAME"
xcrun lipo "$DEV_BINARY" -verify_arch "$HOST_ARCH"
codesign --verify --deep --strict --all-architectures "$DEV_APP"
if ! otool -arch "$HOST_ARCH" -l "$DEV_BINARY" | awk '
    $1 == "cmd" { load_command = $2 }
    $1 == "name" && $2 ~ /ScreenCaptureKit.framework/ {
        if (load_command != "LC_LOAD_WEAK_DYLIB") bad = 1
    }
    END { exit bad ? 1 : 0 }
'; then
    printf 'ScreenCaptureKit must be weak-linked for %s.\n' "$HOST_ARCH" >&2
    exit 1
fi
python3 "$PROJECT_DIR/scripts/package-watch-resources.py" verify \
    "$PROJECT_DIR/Resources/WatchSource" "$DEV_APP/Contents/Resources/WatchSource"

python3 - "$DEV_BINARY" "$DEV_OPTIMIZATION" "$TARGET" "$DEV_BUILD_DIR/build-info.json" <<'PY'
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import sys

binary, optimization, target, output = sys.argv[1:]
Path(output).write_text(json.dumps({
    "builtAt": datetime.now(timezone.utc).isoformat(),
    "optimization": optimization,
    "target": target,
    "binarySHA256": hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
}, indent=2) + "\n")
PY

printf '\nBuilt: %s\n' "$DEV_APP"
printf 'Launch after quitting any running copy:\n  open %q\n' "$DEV_APP"
