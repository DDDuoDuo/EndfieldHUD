#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build}"
SELECTED_SDK="$("$PROJECT_DIR/scripts/build.sh" --print-sdk)"
mkdir -p "$BUILD_DIR/module-cache"
PROBE_STAGE="$(mktemp -d "$BUILD_DIR/.resource-probe.XXXXXX")"
trap 'rm -rf "$PROBE_STAGE"' EXIT
for MODE in development release; do
    DEFINES=()
    if [ "$MODE" = release ]; then DEFINES=(-D HUD_RELEASE); fi
    xcrun swiftc -swift-version 5 -O -parse-as-library ${DEFINES[@]+"${DEFINES[@]}"} \
        -sdk "$SELECTED_SDK" -module-cache-path "$BUILD_DIR/module-cache" \
        -file-prefix-map "$PROJECT_DIR=/EndfieldHUD" \
        "$PROJECT_DIR/Sources/HUDResources.swift" "$PROJECT_DIR/Tests/HUDResourcesReleaseProbe.swift" \
        -o "$PROBE_STAGE/$MODE"
    "$PROBE_STAGE/$MODE"
done
if /usr/bin/strings "$PROBE_STAGE/release" | /usr/bin/grep -F "$PROJECT_DIR" > /dev/null; then
    printf 'Release resource probe embedded the checkout path.\n' >&2
    exit 1
fi
printf 'PASS: release resource probe contains no checkout path\n'
