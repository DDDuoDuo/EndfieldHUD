#!/bin/bash
set -euo pipefail
if [ "$#" -ne 2 ]; then printf 'Usage: %s APP_PATH SPARKLE_DISTRIBUTION\n' "$0" >&2; exit 2; fi
APP="$1"
SPARKLE="$2"
IDENTITY="${CODE_SIGN_IDENTITY:--}"
FRAMEWORK="$APP/Contents/Frameworks/Sparkle.framework"
mkdir -p "$APP/Contents/Frameworks" "$APP/Contents/Resources"
rm -rf "$FRAMEWORK"
ditto "$SPARKLE/Sparkle.framework" "$FRAMEWORK"
cp "$SPARKLE/LICENSE" "$APP/Contents/Resources/Sparkle-LICENSE.txt"
# This app is not sandboxed. The optional XPC services are unused; retain the
# Autoupdate and Updater.app helpers required for ordinary Sparkle app updates.
rm -rf "$FRAMEWORK/Versions/B/XPCServices" "$FRAMEWORK/XPCServices"
SIGN_ARGS=(--force --sign "$IDENTITY")
if [ "$IDENTITY" != '-' ]; then SIGN_ARGS+=(--timestamp --options runtime); fi
# Sign inside out, without --deep; do not propagate app entitlements to helpers.
codesign "${SIGN_ARGS[@]}" "$FRAMEWORK/Versions/B/Autoupdate"
codesign "${SIGN_ARGS[@]}" "$FRAMEWORK/Versions/B/Updater.app"
codesign "${SIGN_ARGS[@]}" "$FRAMEWORK"
codesign --verify --deep --strict "$FRAMEWORK"
