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
python3 "$PROJECT_DIR/scripts/normalize-bundle-permissions.py" --check "$APP"
plutil -lint "$APP/Contents/Info.plist"
xcrun lipo "$BINARY" -verify_arch "$@"
codesign --verify --deep --strict --all-architectures --verbose=2 "$APP"

# A macOS 14-only background provider must not prevent a 10.15/11 app from
# launching. Inspect each Mach-O slice; no capture/permission API is invoked.
for ARCH in "$@"; do
    if ! otool -arch "$ARCH" -l "$BINARY" | awk '
        $1 == "cmd" { load_command = $2 }
        $1 == "name" && $2 ~ /ScreenCaptureKit.framework/ {
            if (load_command != "LC_LOAD_WEAK_DYLIB") bad = 1
        }
        END { exit bad ? 1 : 0 }
    '; then
        printf 'ScreenCaptureKit must be weak-linked for %s.\n' "$ARCH" >&2
        exit 1
    fi
done


source "$PROJECT_DIR/scripts/sparkle-config.sh"
FRAMEWORK="$APP/Contents/Frameworks/Sparkle.framework"
[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$FRAMEWORK/Resources/Info.plist")" = "$SPARKLE_VERSION" ]
for COMPONENT in "$FRAMEWORK/Sparkle" "$FRAMEWORK/Versions/B/Autoupdate" "$FRAMEWORK/Versions/B/Updater.app/Contents/MacOS/Updater"; do
    [ -x "$COMPONENT" ] || { printf 'Missing Sparkle helper: %s\n' "$COMPONENT" >&2; exit 1; }
    xcrun lipo "$COMPONENT" -verify_arch "$@"
done
codesign --verify --deep --strict --all-architectures "$FRAMEWORK"
if [ -e "$FRAMEWORK/XPCServices" ] || [ -e "$FRAMEWORK/Versions/B/XPCServices" ]; then
    printf 'Unused Sparkle sandbox XPC services should not be bundled.\n' >&2; exit 1
fi
if ! otool -L "$BINARY" | /usr/bin/grep -F '@rpath/Sparkle.framework/Versions/B/Sparkle' > /dev/null; then
    printf 'App is not linked to the embedded updater.\n' >&2; exit 1
fi
SPARKLE_DIR="$("$PROJECT_DIR/scripts/fetch-sparkle.sh")"

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
verify_copy "$SPARKLE_DIR/LICENSE" "$RESOURCES/Sparkle-LICENSE.txt"
verify_copy "$PROJECT_DIR/ThirdParty/MediaRemoteAdapter/LICENSE" "$RESOURCES/MediaRemoteAdapter-LICENSE.txt"
verify_copy "$PROJECT_DIR/ThirdParty/MediaRemoteAdapter/bin/mediaremote-adapter.pl" "$RESOURCES/NowPlaying/mediaremote-adapter.pl"
MEDIA_FRAMEWORK="$APP/Contents/Frameworks/MediaRemoteAdapter.framework"
xcrun lipo "$MEDIA_FRAMEWORK/MediaRemoteAdapter" -verify_arch "$@"
codesign --verify --strict --all-architectures "$MEDIA_FRAMEWORK"

verify_copy "$PROJECT_DIR/LICENSE" "$RESOURCES/LICENSE.txt"
verify_copy "$PROJECT_DIR/CREDITS.md" "$RESOURCES/CREDITS.md"
verify_copy "$PROJECT_DIR/Resources/EndfieldIndustriesSource.png" "$RESOURCES/EndfieldIndustriesSource.png"
for LOCALIZATION in en zh-Hans zh-Hant ja ko; do
    PURPOSE_STRINGS="$RESOURCES/$LOCALIZATION.lproj/InfoPlist.strings"
    verify_copy "$PROJECT_DIR/Resources/$LOCALIZATION.lproj/InfoPlist.strings" "$PURPOSE_STRINGS"
    plutil -lint "$PURPOSE_STRINGS"
    [ -n "$(plutil -extract NSAudioCaptureUsageDescription raw -o - "$PURPOSE_STRINGS")" ] || {
        printf 'Missing localized audio permission description: %s\n' "$PURPOSE_STRINGS" >&2
        exit 1
    }
done
[ -s "$RESOURCES/AppIcon.icns" ] || { printf 'Missing generated app icon.\n' >&2; exit 1; }
for NAME in Perlica.png RhodesIsland.png; do
    verify_copy "$PROJECT_DIR/Resources/AppIconSources/$NAME" "$RESOURCES/AppIconSources/$NAME"
done
python3 - "$PROJECT_DIR/Resources" "$RESOURCES" <<'PY'
from pathlib import Path
import sys

source_root, bundle_root = map(Path, sys.argv[1:])
for directory in ('AppIconSources/Factions', 'AppIconSources/EndfieldWiki', 'WorldMap', 'MediaAssembly', 'OrbiPom'):
    source, bundle = source_root / directory, bundle_root / directory
    def files(root):
        if not root.is_dir():
            sys.exit(f'Missing resource directory: {root}')
        result = {}
        for path in root.rglob('*'):
            if path.is_symlink():
                sys.exit(f'Unexpected resource symlink: {path}')
            if path.is_file():
                result[path.relative_to(root)] = path
        if not result:
            sys.exit(f'Empty resource directory: {root}')
        return result
    expected, actual = files(source), files(bundle)
    if expected.keys() != actual.keys():
        sys.exit(f'Bundled resource file set differs: {directory}; '
                 f'missing={sorted(map(str, expected.keys() - actual.keys()))}; '
                 f'extra={sorted(map(str, actual.keys() - expected.keys()))}')
    for relative, path in expected.items():
        if path.read_bytes() != actual[relative].read_bytes():
            sys.exit(f'Stale bundled resource: {directory}/{relative}')
print('Verified exact recursive resource trees, including Media Assembly subdirectories.')
PY
python3 "$PROJECT_DIR/scripts/package-watch-resources.py" verify \
    "$PROJECT_DIR/Resources/WatchSource" "$RESOURCES/WatchSource"
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
