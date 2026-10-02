#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
SOURCE_PROJECT="${HUD_BENCHMARK_SOURCE_PROJECT:-$PROJECT_DIR}"
OUTPUT_DIR="${HUD_BENCHMARK_DIR:-$PROJECT_DIR/build/hud-benchmark}"
SDK="$("$SOURCE_PROJECT/scripts/build.sh" --print-sdk)"
SPARKLE="$("$SOURCE_PROJECT/scripts/fetch-sparkle.sh")"
mkdir -p "$OUTPUT_DIR/module-cache"
SOURCES=()
for SOURCE in "$SOURCE_PROJECT"/Sources/*.swift; do
    [ "$(basename "$SOURCE")" = main.swift ] || SOURCES+=("$SOURCE")
done
FLAGS=()
if [ -f "$SOURCE_PROJECT/Sources/HUDSourceWatchView.swift" ]; then FLAGS+=(-D HUD_SOURCE_INTEGRATION); fi
xcrun swiftc -swift-version 5 -O -whole-module-optimization -parse-as-library \
    -sdk "$SDK" -module-cache-path "$OUTPUT_DIR/module-cache" \
    -F "$SPARKLE" -framework Sparkle -Xlinker -rpath -Xlinker "$SPARKLE" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework ServiceManagement \
    -framework Carbon -framework Quartz -framework Metal -framework MetalKit \
    -Xlinker -weak_framework -Xlinker ScreenCaptureKit -lsqlite3 \
    ${FLAGS[@]+"${FLAGS[@]}"} "${SOURCES[@]}" "$PROJECT_DIR/Tests/HUDIntegrationPerformance.swift" \
    -o "$OUTPUT_DIR/HUDIntegrationPerformance"
# Use the same staged resources as the app. A standalone executable would
# fall back to the complete authoring tree and measure unused game assets.
APP="$OUTPUT_DIR/EndfieldHUD-Benchmark.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$SOURCE_PROJECT/Resources/Info.plist" "$APP/Contents/Info.plist"
cp "$OUTPUT_DIR/HUDIntegrationPerformance" "$APP/Contents/MacOS/EndfieldHUD"
RESOURCE_APP="${HUD_BENCHMARK_RESOURCE_APP:-$SOURCE_PROJECT/build/dev/EndfieldHUD.app}"
if [ -d "$RESOURCE_APP/Contents/Resources" ]; then
    rsync -a --delete "$RESOURCE_APP/Contents/Resources/" "$APP/Contents/Resources/"
else
    ditto "$SOURCE_PROJECT/Resources" "$APP/Contents/Resources"
    if [ -f "$SOURCE_PROJECT/scripts/package-watch-resources.py" ]; then
        python3 "$SOURCE_PROJECT/scripts/package-watch-resources.py" --help >/dev/null
        printf 'Build the development app first so benchmark resources match the packaged app.\n' >&2
        exit 1
    fi
fi
if [ "${1:-}" = --build-only ]; then exit 0; fi
"$APP/Contents/MacOS/EndfieldHUD" --ui-test --output "$OUTPUT_DIR/report.json" "$@"
