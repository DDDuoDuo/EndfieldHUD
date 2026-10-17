#!/bin/bash
# Regenerates windows/tests/fixtures/hypergryph-account-source.json and the
# generated Unicode table windows/modules/hypergryph_account_unicode.inc from
# the UNCHANGED macOS sources. Usage (macOS only):
#   windows/tools/hypergryph_account_reference.sh NEW-scratch-directory
# The harness never creates a window, opens a login page, reads Keychain,
# contacts a network host or touches personal data: HTTP is an in-process
# URLProtocol, credentials are synthetic and the profile store is temporary.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin && $# -eq 1 ]] || { echo 'Usage: hypergryph_account_reference.sh NEW-scratch-directory' >&2; exit 1; }
[[ ! -e "$1/reference.json" ]] || { echo 'Refusing to overwrite an existing oracle run' >&2; exit 1; }
mkdir -p "$1/.compiler"
OUT="$(cd "$1" && pwd)"
PIN='ca04f142185c7de40acd8523bdb563195d90a1d1'
(cd "$ROOT" && git diff --quiet "$PIN" -- Sources Resources) || { echo 'Mac Sources/Resources differ from the pinned authority' >&2; exit 1; }
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(cd "$ROOT"; git ls-files 'Sources/*.swift' | sed '/\/main.swift$/d' | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -parse-as-library -module-name EndfieldAccountReference -D HUD_WATCH_MOTION_PREVIEW \
    -sdk "$SDK" -module-cache-path "$OUT/.compiler/module-cache" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz -framework Carbon -framework ServiceManagement \
    -framework Metal -framework MetalKit -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
    "${SOURCES[@]}" "$ROOT/windows/tools/module_reference_layers.swift" \
    "$ROOT/windows/tools/hypergryph_account_reference.swift" "$ROOT/windows/tools/hypergryph_account_reference_ui.swift" \
    -o "$OUT/.compiler/account-reference" >"$OUT/.compiler/compile.log" 2>&1; then
  tail -80 "$OUT/.compiler/compile.log" >&2; exit 1
fi
FIXTURE_HOME="$(mktemp -d "${TMPDIR:-/tmp}/endfield-account-home.XXXXXX")"
trap 'rm -rf "$FIXTURE_HOME"' EXIT
CFFIXED_USER_HOME="$FIXTURE_HOME" TZ=UTC "$OUT/.compiler/account-reference" "$OUT"
python3 -I "$ROOT/windows/tools/hypergryph_account_reference.py" "$ROOT" "$OUT" "$PIN"
