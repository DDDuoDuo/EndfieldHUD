#!/usr/bin/env bash
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RESOURCE_ROOT="${1:-$TASK_ROOT/Resources/WatchSource}"
PROBE_OUTPUT="${2:-$TASK_ROOT/build/metal-source-probe}"
mkdir -p "$PROBE_OUTPUT"
xcrun --find metal >/dev/null
swiftc "$TASK_ROOT/scripts/MetalSourceProbe.swift" -o "$PROBE_OUTPUT/MetalSourceProbe"
"$PROBE_OUTPUT/MetalSourceProbe" "$RESOURCE_ROOT" "$PROBE_OUTPUT"
