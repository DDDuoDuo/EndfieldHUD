#!/usr/bin/env bash
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RESOURCE_ROOT="${1:-$TASK_ROOT/Resources/WatchSource}"
PROBE_OUTPUT="${2:-$TASK_ROOT/build/metal-source-probe}"
mkdir -p "$PROBE_OUTPUT"
TOOLCHAIN_LOG="$PROBE_OUTPUT/metal-toolchain.log"
: > "$TOOLCHAIN_LOG"
PROBE_DEVELOPER_DIR=""
METAL_COMPILER=""
if METAL_COMPILER="$(/usr/bin/xcrun --find metal 2>> "$TOOLCHAIN_LOG")"; then
    :
else
    # The main build deliberately uses CommandLineTools. Probe installed Xcode
    # toolchains for this subprocess without changing the host xcode-select.
    for developer_path in /Applications/Xcode*.app/Contents/Developer; do
        [[ -d "$developer_path" ]] || continue
        if METAL_COMPILER="$(env DEVELOPER_DIR="$developer_path" /usr/bin/xcrun --find metal 2>> "$TOOLCHAIN_LOG")"; then
            PROBE_DEVELOPER_DIR="$developer_path"
            break
        fi
    done
fi
if [[ -z "$METAL_COMPILER" || ! -x "$METAL_COMPILER" ]]; then
    cat "$TOOLCHAIN_LOG" >&2
    printf '%s\n' 'Metal source probe failed: no installed Metal compiler was found in the active developer directory or /Applications/Xcode*.app. Install the required toolchain on the host; this probe does not download one.' >&2
    exit 1
fi
printf 'Metal compiler: %s\nProbe developer directory: %s\n' "$METAL_COMPILER" "${PROBE_DEVELOPER_DIR:-${DEVELOPER_DIR:-active xcode-select}}" >> "$TOOLCHAIN_LOG"
swiftc "$TASK_ROOT/scripts/MetalSourceProbe.swift" -o "$PROBE_OUTPUT/MetalSourceProbe"
if [[ -n "$PROBE_DEVELOPER_DIR" ]]; then
    env DEVELOPER_DIR="$PROBE_DEVELOPER_DIR" "$PROBE_OUTPUT/MetalSourceProbe" "$RESOURCE_ROOT" "$PROBE_OUTPUT"
else
    "$PROBE_OUTPUT/MetalSourceProbe" "$RESOURCE_ROOT" "$PROBE_OUTPUT"
fi
