#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PREVIEW_DIR="$PROJECT_DIR/build/readme-previews"
SDK="$("$PROJECT_DIR/scripts/build.sh" --print-sdk)"
PREVIEW_APP="$PREVIEW_DIR/EndfieldHUDPreview.app"
mkdir -p "$PREVIEW_DIR/module-cache" "$PREVIEW_DIR/frames" "$PROJECT_DIR/docs/media" \
    "$PREVIEW_APP/Contents/MacOS" "$PREVIEW_APP/Contents/Resources"
SOURCES=()
for source in "$PROJECT_DIR"/Sources/*.swift; do
    if [[ "$source" != */main.swift ]]; then SOURCES+=("$source"); fi
done
xcrun swiftc -swift-version 5 -O -whole-module-optimization -parse-as-library \
    -sdk "$SDK" -module-cache-path "$PREVIEW_DIR/module-cache" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework ServiceManagement \
    -framework Carbon -framework Quartz -lsqlite3 \
    "${SOURCES[@]}" "$PROJECT_DIR/Tests/HUDReadmePreview.swift" -o "$PREVIEW_DIR/render"
cp "$PREVIEW_DIR/render" "$PREVIEW_APP/Contents/MacOS/EndfieldHUDPreview"
cp "$PROJECT_DIR/Resources/Info.plist" "$PREVIEW_APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleExecutable EndfieldHUDPreview' "$PREVIEW_APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier io.github.endfieldhud.ReadmePreview' "$PREVIEW_APP/Contents/Info.plist"
ditto "$PROJECT_DIR/Resources" "$PREVIEW_APP/Contents/Resources"
"$PREVIEW_APP/Contents/MacOS/EndfieldHUDPreview" "$PREVIEW_DIR/frames"
cp "$PREVIEW_DIR/frames/"*.png "$PROJECT_DIR/docs/media/"
for sequence in motion modules; do
    ffmpeg -hide_banner -loglevel error -y -framerate 12 -i "$PREVIEW_DIR/frames/$sequence/%03d.png" \
        -filter_complex '[0:v]split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=3' \
        -loop 0 "$PROJECT_DIR/docs/media/$sequence.gif"
done
