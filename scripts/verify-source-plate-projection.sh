#!/bin/bash
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT="${1:-$TASK_ROOT/build/watch-source-plate-projection}"
RUNTIME="$TASK_ROOT/build/source-plate-runtime"
SDK="$("$TASK_ROOT/scripts/build.sh" --print-sdk)"
mkdir -p "$OUTPUT" "$RUNTIME/module-cache"
SOURCE_NAMES=(HUDResources HUDSourceScene HUDSourceWatchAnimation HUDSourceWatchDocument HUDSourceWatchWidgets HUDSourceBannerScroll HUDSourceSelectableColor
    HUDSourceImageGeometry HUDSourceTextGeometry HUDSourceWatchLayout HUDSourceWatchButtonAnimation HUDSourceWatchDomain HUDSourceRectClipping HUDSourceCanvasSorting
    HUDSourceWatchCamera HUDSourceDomainAnimation HUDSourceDrawableReadback HUDSourceUIComposite HUDSourceMetalRenderer HUDSourceWatchFrameBuilder)
SOURCE_FILES=()
for name in "${SOURCE_NAMES[@]}"; do SOURCE_FILES+=("$TASK_ROOT/Sources/$name.swift"); done
xcrun swiftc -g -swift-version 5 -parse-as-library -D HUD_SOURCE_RENDER_PREVIEW -sdk "$SDK" \
    -module-cache-path "$RUNTIME/module-cache" -framework Cocoa -framework Metal -framework MetalKit \
    "${SOURCE_FILES[@]}" "$TASK_ROOT/scripts/VerifySourcePlateProjection.swift" -o "$RUNTIME/VerifySourcePlateProjection"
"$RUNTIME/VerifySourcePlateProjection" "$OUTPUT"
