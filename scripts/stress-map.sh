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
LOCALIZATION_SOURCES=("$SOURCE_DIR/Localization.swift")
if [ -f "$SOURCE_DIR/LocalizationCatalog.swift" ]; then LOCALIZATION_SOURCES+=("$SOURCE_DIR/LocalizationCatalog.swift"); fi
if [ -f "$SOURCE_DIR/LocalizationCatalogRedesign.swift" ]; then LOCALIZATION_SOURCES+=("$SOURCE_DIR/LocalizationCatalogRedesign.swift"); fi
# HUDControlHighlightLayer and HUDRenderScale use the centre's drawing and text
# library. The harness drives WorldMapSurface, not the Map module's canvas.
DESIGN_SOURCES=()
for name in HUDDesignDrawing HUDDesignText HUDDesignTokens HUDDesignFonts HUDStartupTrace; do
    if [ -f "$SOURCE_DIR/$name.swift" ]; then DESIGN_SOURCES+=("$SOURCE_DIR/$name.swift"); fi
done
MAP_SOURCES=()
for file in "$SOURCE_DIR"/WorldMap*.swift; do
    if [ "$(basename "$file")" != WorldMapCanvas.swift ] || [ ! -f "$SOURCE_DIR/WorldMapSurface.swift" ]; then MAP_SOURCES+=("$file"); fi
done
xcrun swiftc -swift-version 5 -O -whole-module-optimization -parse-as-library \
    ${SWIFT_DEFINES[@]+"${SWIFT_DEFINES[@]}"} \
    -sdk "$SELECTED_SDK" -module-cache-path "$PROJECT_DIR/build/dev/module-cache" \
    -framework AppKit -framework QuartzCore -framework CoreText \
    ${RESOURCE_SOURCES[@]+"${RESOURCE_SOURCES[@]}"} \
    "${LOCALIZATION_SOURCES[@]}" \
    "$SOURCE_DIR/HUDModule.swift" \
    "$SOURCE_DIR/HUDControlHighlightLayer.swift" \
    "$SOURCE_DIR/HUDRenderScale.swift" \
    "$SOURCE_DIR/HUDMotionController.swift" \
    ${DESIGN_SOURCES[@]+"${DESIGN_SOURCES[@]}"} \
    "${MAP_SOURCES[@]}" \
    "$PROJECT_DIR/Tests/WorldMapLivePerformance.swift" \
    -o "$OUTPUT_DIR/WorldMapLivePerformance"
if [ "${1:-}" = '--build-only' ]; then
    printf 'Built: %s\n' "$OUTPUT_DIR/WorldMapLivePerformance"
    exit 0
fi
"$OUTPUT_DIR/WorldMapLivePerformance" "$@"
