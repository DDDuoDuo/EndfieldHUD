#!/bin/bash
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT="$TASK_ROOT/build/watch-source-previews"
RUNTIME="$TASK_ROOT/build/watch-source-runtime"
SDK="$("$TASK_ROOT/scripts/build.sh" --print-sdk)"
mkdir -p "$OUTPUT" "$RUNTIME/module-cache"
SOURCE_NAMES=(HUDResources HUDSourceScene HUDSourceWatchAnimation HUDSourceWatchDocument
    HUDSourceImageGeometry HUDSourceTextGeometry HUDSourceWatchLayout HUDSourceWatchButtonAnimation HUDSourceWatchDomain HUDSourceRectClipping HUDSourceCanvasSorting
    HUDSourceWatchCamera HUDSourceMetalRenderer HUDSourceWatchFrameBuilder)
SOURCE_FILES=()
for name in "${SOURCE_NAMES[@]}"; do SOURCE_FILES+=("$TASK_ROOT/Sources/$name.swift"); done
xcrun swiftc -g -swift-version 5 -parse-as-library -sdk "$SDK" -module-cache-path "$RUNTIME/module-cache" \
    -framework Cocoa -framework Metal -framework MetalKit \
    "${SOURCE_FILES[@]}" "$TASK_ROOT/scripts/RenderSourceWatchPreviews.swift" -o "$RUNTIME/RenderSourceWatchPreviews"
"$RUNTIME/RenderSourceWatchPreviews" "$OUTPUT"
