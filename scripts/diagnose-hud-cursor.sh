#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
SOURCE_DIR="${HUD_CURSOR_SOURCE_DIR:-$PROJECT_DIR/Sources}"
OUTPUT_DIR="${HUD_CURSOR_DIAGNOSTIC_DIR:-$PROJECT_DIR/build/cursor-diagnostic}"
MODULE_CACHE="${HUD_CURSOR_MODULE_CACHE:-$OUTPUT_DIR/module-cache}"
RUN_DIR="${HUD_CURSOR_RUN_DIR:-$OUTPUT_DIR/run}"
RESOURCE_APP="${HUD_CURSOR_RESOURCE_APP:-$PROJECT_DIR/build/dev/EndfieldHUD.app}"
SDK="$("$PROJECT_DIR/scripts/build.sh" --print-sdk)"
SPARKLE="$("$PROJECT_DIR/scripts/fetch-sparkle.sh")"
mkdir -p "$MODULE_CACHE" "$OUTPUT_DIR"
SOURCES=()
for SOURCE in "$SOURCE_DIR"/*.swift; do
    [ "$(basename "$SOURCE")" = main.swift ] || SOURCES+=("$SOURCE")
done
if [ "${1:-}" != --run-only ]; then
    xcrun swiftc -swift-version 5 -O -whole-module-optimization -parse-as-library \
        -sdk "$SDK" -module-cache-path "$MODULE_CACHE" \
        -F "$SPARKLE" -framework Sparkle -Xlinker -rpath -Xlinker "$SPARKLE" \
        -framework Cocoa -framework IOKit -framework CoreAudio -framework ServiceManagement \
        -framework Carbon -framework Quartz -framework Metal -framework MetalKit \
        -framework WebKit -framework Security -framework PDFKit -framework ScreenCaptureKit -lsqlite3 -lz \
        "${SOURCES[@]}" "$PROJECT_DIR/Tests/HUDCursorDiagnostics.swift" -o "$OUTPUT_DIR/HUDCursorDiagnostics"
    APP="$OUTPUT_DIR/EndfieldHUD-CursorDiagnostic.app"
    mkdir -p "$APP/Contents/MacOS"
    cp "$PROJECT_DIR/Resources/Info.plist" "$APP/Contents/Info.plist"
    /usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier local.EndfieldHUD.CursorDiagnostic' "$APP/Contents/Info.plist"
    cp "$OUTPUT_DIR/HUDCursorDiagnostics" "$APP/Contents/MacOS/EndfieldHUD"
    test -d "$RESOURCE_APP/Contents/Resources"
    if [ ! -e "$APP/Contents/Resources" ]; then ln -s "$RESOURCE_APP/Contents/Resources" "$APP/Contents/Resources"; fi
fi
if [ "${1:-}" = --build-only ]; then exit 0; fi
if [ "${1:-}" = --run-only ]; then shift; fi
"$OUTPUT_DIR/EndfieldHUD-CursorDiagnostic.app/Contents/MacOS/EndfieldHUD" --ui-test --output "$RUN_DIR" "$@"
