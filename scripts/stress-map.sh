#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
OUTPUT_DIR="${MAP_PERFORMANCE_DIR:-/tmp/EndfieldMapPerformance/live-current}"
SOURCE_DIR="${MAP_PERFORMANCE_SOURCE_DIR:-$PROJECT_DIR/Sources}"
SELECTED_SDK="$("$PROJECT_DIR/scripts/build.sh" --print-sdk)"
mkdir -p "$OUTPUT_DIR" "$PROJECT_DIR/build/dev/module-cache"
SWIFT_DEFINES=()
if [ -f "$SOURCE_DIR/WorldMapRasterController.swift" ]; then SWIFT_DEFINES+=("-D" "MAP_RASTER_PIPELINE"); fi
RESOURCE_SOURCES=()
if [ -f "$SOURCE_DIR/HUDResources.swift" ]; then RESOURCE_SOURCES+=("$SOURCE_DIR/HUDResources.swift"); fi
xcrun swiftc -swift-version 5 -O -whole-module-optimization -parse-as-library \
    ${SWIFT_DEFINES[@]+"${SWIFT_DEFINES[@]}"} \
    -sdk "$SELECTED_SDK" -module-cache-path "$PROJECT_DIR/build/dev/module-cache" \
    -framework AppKit -framework QuartzCore \
    ${RESOURCE_SOURCES[@]+"${RESOURCE_SOURCES[@]}"} \
    "$SOURCE_DIR/Localization.swift" \
    "$SOURCE_DIR/HUDModule.swift" \
    "$SOURCE_DIR/HUDControlHighlightLayer.swift" \
    "$SOURCE_DIR/HUDRenderScale.swift" \
    "$SOURCE_DIR/HUDMotionController.swift" \
    "$SOURCE_DIR"/WorldMap*.swift \
    "$PROJECT_DIR/Tests/WorldMapLivePerformance.swift" \
    -o "$OUTPUT_DIR/WorldMapLivePerformance"
if [ "${1:-}" = '--build-only' ]; then
    printf 'Built: %s\n' "$OUTPUT_DIR/WorldMapLivePerformance"
    exit 0
fi
"$OUTPUT_DIR/WorldMapLivePerformance" "$@"
