#!/bin/bash
# Build-only Power/charge oracle. Writes charge-indicator-source.json into a NEW
# output directory; copy it to windows/tests/fixtures after review.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: charge_indicator_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1"; CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT"; OUTPUT="$(cd "$OUTPUT" && pwd)"; CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk | tail -1)"
python3 -I "$ROOT/windows/tools/charge_indicator_reference.py" "$ROOT" "$OUTPUT"
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
 -framework AppKit -framework QuartzCore -framework IOKit -framework CryptoKit \
 "$ROOT/Sources/BatteryMonitor.swift" "$ROOT/Sources/BatteryCapacity.swift" "$ROOT/Sources/HUDChargeMetric.swift" \
 "$ROOT/Sources/ChargeIndicatorView.swift" "$ROOT/Sources/HUDChargeBadge.swift" "$ROOT/Sources/HUDDeploymentFlicker.swift" \
 "$ROOT/Sources/DisplayPolicy.swift" "$ROOT/Sources/OverlayGeometry.swift" "$ROOT/Sources/DeviceBatteryProvider.swift" \
 "$OUTPUT/ChargeReferenceFixture.swift" "$ROOT/windows/tools/module_reference_layers.swift" \
 "$ROOT/windows/tools/charge_indicator_reference.swift" -o "$OUTPUT/charge-reference" >"$OUTPUT/compile.log" 2>&1 \
 || { cat "$OUTPUT/compile.log" >&2; exit 1; }
python3 -I - "$OUTPUT" <<'PY'
import os,pathlib,subprocess,sys,tempfile
out=pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='ehud-charge-oracle-') as home:
    subprocess.run([str(out/'charge-reference'),str(out)],env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
PY
python3 -I "$ROOT/windows/tools/charge_indicator_reference.py" --compact "$OUTPUT"
