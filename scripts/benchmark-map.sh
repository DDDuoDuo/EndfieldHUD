#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
OUTPUT_DIR="${MAP_PERFORMANCE_DIR:-/tmp/EndfieldMapPerformance}"
SOURCE_DIR="${MAP_PERFORMANCE_SOURCE_DIR:-$PROJECT_DIR/Sources}"
SELECTED_SDK="$("$PROJECT_DIR/scripts/build.sh" --print-sdk)"
mkdir -p "$OUTPUT_DIR" "$OUTPUT_DIR/module-cache"
MAP_DEFINES=(-D WORLD_MAP_SYNCHRONOUS_RENDERER)
if [[ -f "$SOURCE_DIR/WorldMapRasterController.swift" ]]; then
    MAP_DEFINES=(-D WORLD_MAP_ASYNC_RENDERER)
fi
RESOURCE_SOURCES=()
if [ -f "$SOURCE_DIR/HUDResources.swift" ]; then RESOURCE_SOURCES+=("$SOURCE_DIR/HUDResources.swift"); fi
xcrun swiftc -swift-version 5 -O -whole-module-optimization -parse-as-library \
    -sdk "$SELECTED_SDK" -module-cache-path "$OUTPUT_DIR/module-cache" "${MAP_DEFINES[@]}" \
    -framework AppKit -framework QuartzCore \
    ${RESOURCE_SOURCES[@]+"${RESOURCE_SOURCES[@]}"} \
    "$SOURCE_DIR/Localization.swift" \
    "$SOURCE_DIR/HUDModule.swift" \
    "$SOURCE_DIR/HUDControlHighlightLayer.swift" \
    "$SOURCE_DIR/HUDRenderScale.swift" \
    "$SOURCE_DIR"/WorldMap*.swift \
    "$PROJECT_DIR/Tests/WorldMapPerformance.swift" \
    -o "$OUTPUT_DIR/WorldMapPerformance"
"$OUTPUT_DIR/WorldMapPerformance" "$@"
