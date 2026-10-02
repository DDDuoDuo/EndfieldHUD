#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
APP="${1:-$PROJECT_DIR/build/EndfieldHUD.app}"
if [ "$#" -gt 1 ] || [ ! -d "$APP/Contents/Resources/WatchSource" ]; then
    printf 'Usage: %s [built EndfieldHUD.app]\n' "$0" >&2
    exit 1
fi
RESOURCE_ROOT="$(cd "$APP/Contents/Resources/WatchSource" && pwd -P)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build}"
mkdir -p "$BUILD_DIR/module-cache"
PROBE_STAGE="$(mktemp -d "$BUILD_DIR/.source-metadata-probe.XXXXXX")"
trap 'rm -rf "$PROBE_STAGE"' EXIT
SDK="$("$PROJECT_DIR/scripts/build.sh" --print-sdk)"
# Use the same established source closure as the isolated renderer fixtures.
# Linking Metal is necessary for its type definitions; this probe calls only
# static CPU metadata methods and never creates a Metal device or NSView.
SOURCE_NAMES=(HUDResources HUDSourceScene HUDSourceWatchAnimation HUDSourceWatchDocument HUDSourceWatchWidgets HUDSourceBannerScroll HUDSourceSelectableColor
    HUDSourceImageGeometry HUDSourceTextGeometry HUDSourceDesktopNavigationLayout HUDSourceWatchLayout HUDSourceWatchButtonAnimation HUDSourceWatchDomain HUDSourceRectClipping HUDSourceCanvasSorting
    HUDSourceWatchCamera HUDSourceDomainAnimation HUDSourceDrawableReadback HUDSourceUIComposite HUDSourceMetalRenderer HUDSourceWatchFrameBuilder)
SOURCE_FILES=()
for name in "${SOURCE_NAMES[@]}"; do SOURCE_FILES+=("$PROJECT_DIR/Sources/$name.swift"); done
xcrun swiftc -O -swift-version 5 -parse-as-library -D HUD_SOURCE_RENDER_PREVIEW \
    -sdk "$SDK" -module-cache-path "$BUILD_DIR/module-cache" \
    -framework Cocoa -framework Metal -framework MetalKit \
    "${SOURCE_FILES[@]}" "$PROJECT_DIR/Tests/HUDSourceMetadataCacheProbe.swift" \
    -o "$PROBE_STAGE/HUDSourceMetadataCacheProbe"
"$PROBE_STAGE/HUDSourceMetadataCacheProbe" "$RESOURCE_ROOT" "$PROBE_STAGE/WatchSource"
