#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
source "$PROJECT_DIR/scripts/sparkle-config.sh"
CACHE="${SPARKLE_CACHE_DIR:-$PROJECT_DIR/build/dependencies}"
mkdir -p "$CACHE"
CACHE="$(cd "$CACHE" && pwd -P)"
ARCHIVE="$CACHE/Sparkle-$SPARKLE_VERSION.tar.xz"
DEST="$CACHE/Sparkle-$SPARKLE_VERSION"
STAGE="$(mktemp -d "$CACHE/.sparkle.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
if [ ! -f "$ARCHIVE" ]; then
    printf 'Fetching Sparkle %s…\n' "$SPARKLE_VERSION" >&2
    curl --fail --location --silent --show-error --retry 3 --connect-timeout 15 --max-time 180 \
        --proto '=https' --proto-redir '=https' --tlsv1.2 \
        "https://github.com/sparkle-project/Sparkle/releases/download/$SPARKLE_VERSION/Sparkle-$SPARKLE_VERSION.tar.xz" \
        -o "$STAGE/download.tar.xz"
    mv "$STAGE/download.tar.xz" "$ARCHIVE"
fi
ACTUAL="$(shasum -a 256 "$ARCHIVE" | awk '{print $1}')"
if [ "$ACTUAL" != "$SPARKLE_ARCHIVE_SHA256" ]; then
    printf 'Sparkle archive checksum failed. Remove %s and retry.\n' "$ARCHIVE" >&2
    exit 1
fi
if [ ! -f "$DEST/.verified-$SPARKLE_ARCHIVE_SHA256" ]; then
    mkdir "$STAGE/distribution"
    tar -xJf "$ARCHIVE" -C "$STAGE/distribution" Sparkle.framework bin LICENSE
    codesign --verify --deep --strict "$STAGE/distribution/Sparkle.framework"
    touch "$STAGE/distribution/.verified-$SPARKLE_ARCHIVE_SHA256"
    # This versioned directory contains only disposable downloaded dependencies.
    rm -rf "$DEST"
    mv "$STAGE/distribution" "$DEST"
fi
[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$DEST/Sparkle.framework/Resources/Info.plist")" = "$SPARKLE_VERSION" ]
[ -x "$DEST/bin/generate_appcast" ] && [ -x "$DEST/bin/sign_update" ]
printf '%s\n' "$DEST"
