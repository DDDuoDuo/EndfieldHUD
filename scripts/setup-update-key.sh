#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
source "$PROJECT_DIR/scripts/sparkle-config.sh"
SPARKLE="$("$PROJECT_DIR/scripts/fetch-sparkle.sh")"
# Sparkle stores the private Ed25519 seed directly in login Keychain. Never
# export it, put it in an environment variable, or pass it on a command line.
"$SPARKLE/bin/generate_keys" --account "$SPARKLE_KEYCHAIN_ACCOUNT" > /dev/null
PUBLIC_KEY="$("$SPARKLE/bin/generate_keys" --account "$SPARKLE_KEYCHAIN_ACCOUNT" -p)"
INFO="$PROJECT_DIR/Resources/Info.plist"
EXISTING="$(/usr/libexec/PlistBuddy -c 'Print :SUPublicEDKey' "$INFO" 2>/dev/null || true)"
if [ -n "$EXISTING" ] && [ "$EXISTING" != "$PUBLIC_KEY" ]; then
    printf 'The Keychain key does not match the published public key. Restore the original signing Keychain; do not replace the public key.\n' >&2
    exit 1
fi
if [ -z "$EXISTING" ]; then
    /usr/libexec/PlistBuddy -c "Add :SUPublicEDKey string $PUBLIC_KEY" "$INFO"
fi
printf 'Update key is available in login Keychain; Info.plist contains its matching public key.\n'
