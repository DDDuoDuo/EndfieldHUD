#!/bin/bash
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT="$TASK_ROOT/build/watch-source-backdrop"
RUNTIME="$TASK_ROOT/build/watch-source-runtime"
SDK="$("$TASK_ROOT/scripts/build.sh" --print-sdk)"
mkdir -p "$OUTPUT" "$RUNTIME/module-cache"
SOURCE_NAMES=(HUDResources HUDSourceScene HUDSourceWatchAnimation HUDSourceWatchDocument HUDSourceWatchWidgets
    HUDSourceImageGeometry HUDSourceTextGeometry HUDSourceWatchLayout HUDSourceWatchButtonAnimation HUDSourceWatchDomain HUDSourceRectClipping HUDSourceCanvasSorting
    HUDSourceWatchCamera HUDSourceDomainAnimation HUDSourceDrawableReadback HUDSourceUIComposite HUDSourceMetalRenderer HUDSourceWatchFrameBuilder
    HUDSourceDesktopBackdrop HUDSourceFrostedGlass HUDSourceWatchBackdrop)
SOURCE_FILES=()
for name in "${SOURCE_NAMES[@]}"; do SOURCE_FILES+=("$TASK_ROOT/Sources/$name.swift"); done
BACKDROP_LINK_FLAGS=()
if [ -d "$SDK/System/Library/Frameworks/ScreenCaptureKit.framework" ]; then
    BACKDROP_LINK_FLAGS=(-Xlinker -weak_framework -Xlinker ScreenCaptureKit)
fi
xcrun swiftc -g -swift-version 5 -parse-as-library -sdk "$SDK" -module-cache-path "$RUNTIME/module-cache" \
    -framework Cocoa -framework Metal -framework MetalKit "${BACKDROP_LINK_FLAGS[@]}" \
    "${SOURCE_FILES[@]}" "$TASK_ROOT/Tests/HUDSourceDesktopBackdropPureChecks.swift" \
    "$TASK_ROOT/scripts/VerifySourceBackdropGPU.swift" -o "$RUNTIME/VerifySourceBackdropGPU"
"$RUNTIME/VerifySourceBackdropGPU" "$OUTPUT"
