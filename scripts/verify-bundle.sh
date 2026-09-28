#!/bin/bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
if [ "$#" -lt 1 ]; then
    printf 'Usage: %s APP_PATH [arm64] [x86_64]\n' "$0" >&2
    exit 2
fi
APP="$1"
shift
if [ "$#" -eq 0 ]; then set -- arm64 x86_64; fi
for ARCH in "$@"; do
    case "$ARCH" in arm64|x86_64) ;; *) printf 'Unsupported architecture: %s\n' "$ARCH" >&2; exit 2 ;; esac
done

BINARY="$APP/Contents/MacOS/EndfieldHUD"
RESOURCES="$APP/Contents/Resources"
[ -x "$BINARY" ] || { printf 'Missing executable: %s\n' "$BINARY" >&2; exit 1; }
plutil -lint "$APP/Contents/Info.plist"
xcrun lipo "$BINARY" -verify_arch "$@"
codesign --verify --deep --strict --all-architectures --verbose=2 "$APP"

# Packaging a stale bundle after editing the version or capabilities is an error.
if ! cmp -s "$PROJECT_DIR/Resources/Info.plist" "$APP/Contents/Info.plist"; then
    printf 'Bundle Info.plist differs from the source. Rebuild before packaging.\n' >&2
    exit 1
fi
verify_copy() {
    if [ ! -f "$2" ] || ! cmp -s "$1" "$2"; then
        printf 'Missing or stale bundled resource: %s\n' "$2" >&2
        exit 1
    fi
}
verify_copy "$PROJECT_DIR/LICENSE" "$RESOURCES/LICENSE.txt"
verify_copy "$PROJECT_DIR/CREDITS.md" "$RESOURCES/CREDITS.md"
verify_copy "$PROJECT_DIR/Resources/EndfieldIndustriesSource.png" "$RESOURCES/EndfieldIndustriesSource.png"
[ -s "$RESOURCES/AppIcon.icns" ] || { printf 'Missing generated app icon.\n' >&2; exit 1; }
for NAME in Perlica.png RhodesIsland.png; do
    verify_copy "$PROJECT_DIR/Resources/AppIconSources/$NAME" "$RESOURCES/AppIconSources/$NAME"
done
for DIRECTORY in AppIconSources/Factions AppIconSources/EndfieldWiki WorldMap; do
    for SOURCE in "$PROJECT_DIR/Resources/$DIRECTORY"/*; do
        [ -f "$SOURCE" ] || { printf 'Missing resource directory: %s\n' "$DIRECTORY" >&2; exit 1; }
        verify_copy "$SOURCE" "$RESOURCES/$DIRECTORY/$(basename "$SOURCE")"
    done
done
if [ -e "$RESOURCES/AppIconSources/FactionAtlas.png" ]; then
    printf 'The full faction atlas must not ship in the runtime bundle.\n' >&2
    exit 1
fi
# Source-tree lookup is compiled out of release apps; debug metadata uses a
# neutral prefix. Neither may expose the distributor's account or checkout.
if /usr/bin/strings "$BINARY" | /usr/bin/grep -E '^/(Users|private/var/folders|var/folders)/' > /dev/null; then
    printf 'Executable contains a local account or temporary source path. Rebuild with release path mapping.\n' >&2
    exit 1
fi
printf 'Verified bundle, architectures and current resources: %s\n' "$APP"
