#!/bin/bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PREVIEW_DIR="$PROJECT_DIR/build/watch-previews"
RUNTIME_DIR="$PROJECT_DIR/build/watch-preview-runtime"
SDK="$("$PROJECT_DIR/scripts/build.sh" --print-sdk)"
PREVIEW_APP="$RUNTIME_DIR/EndfieldHUDWatchPreview.app"
mkdir -p "$PREVIEW_DIR" "$RUNTIME_DIR/module-cache" \
    "$PREVIEW_APP/Contents/MacOS" "$PREVIEW_APP/Contents/Resources"

# Compile the existing fixture renderer with the app's real drawing code.
# Keep its isolated app bundle outside the uploaded PNG/metadata directory.
# Includes HUDSourceWatchDomain with the other app rendering sources.
SOURCES=()
for source in "$PROJECT_DIR"/Sources/*.swift; do
    if [[ "$source" != */main.swift ]]; then SOURCES+=("$source"); fi
done
xcrun swiftc -swift-version 5 -D HUD_WATCH_MOTION_PREVIEW -O -whole-module-optimization -parse-as-library \
    -sdk "$SDK" -module-cache-path "$RUNTIME_DIR/module-cache" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework ServiceManagement \
    -framework Carbon -framework Quartz -lsqlite3 \
    "${SOURCES[@]}" "$PROJECT_DIR/Tests/HUDReadmePreview.swift" \
    -o "$PREVIEW_APP/Contents/MacOS/EndfieldHUDWatchPreview"
cp "$PROJECT_DIR/Resources/Info.plist" "$PREVIEW_APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleExecutable EndfieldHUDWatchPreview' "$PREVIEW_APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier io.github.endfieldhud.WatchPreview' "$PREVIEW_APP/Contents/Info.plist"
ditto "$PROJECT_DIR/Resources" "$PREVIEW_APP/Contents/Resources"

# The renderer uses temporary fixture stores and draws only its own layers.
# It writes no README media, reads no desktop framebuffer, and needs no FFmpeg.
"$PREVIEW_APP/Contents/MacOS/EndfieldHUDWatchPreview" "$PREVIEW_DIR" --watch-motion
