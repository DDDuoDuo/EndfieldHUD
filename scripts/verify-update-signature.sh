#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
TOOL_DIR="$PROJECT_DIR/build/update-tools"
mkdir -p "$TOOL_DIR/module-cache"
TOOL="$TOOL_DIR/verify-update-signature"
SOURCE="$PROJECT_DIR/scripts/verify-update-signature.swift"
if [ ! -x "$TOOL" ] || [ "$SOURCE" -nt "$TOOL" ]; then
    SDK="$("$PROJECT_DIR/scripts/build.sh" --print-sdk)"
    TEMP_TOOL="$(mktemp "$TOOL_DIR/.verify.XXXXXX")"
    trap 'rm -f "$TEMP_TOOL"' EXIT
    xcrun swiftc -swift-version 5 -O -sdk "$SDK" -module-cache-path "$TOOL_DIR/module-cache" "$SOURCE" -o "$TEMP_TOOL"
    mv "$TEMP_TOOL" "$TOOL"
fi
exec "$TOOL" "$@"
