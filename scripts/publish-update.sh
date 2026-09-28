#!/bin/bash
# Explicit publishing action; never called by build, package, tests, or CI.
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
if [ "$#" -ne 1 ]; then printf 'Usage: %s RELEASE_ZIP\n' "$0" >&2; exit 2; fi
exec python3 "$PROJECT_DIR/scripts/publish-update.py" "$1"
