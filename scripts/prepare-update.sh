#!/bin/bash
# Generate a signed feed locally. This script never uploads or publishes.
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
source "$PROJECT_DIR/scripts/sparkle-config.sh"
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    printf 'Usage: %s RELEASE_ZIP [RELEASE_NOTES.txt]\n' "$0" >&2
    exit 2
fi
ARCHIVE="$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")"
INFO="$PROJECT_DIR/Resources/Info.plist"
FEED="$PROJECT_DIR/updates/appcast.xml"
SPARKLE="$("$PROJECT_DIR/scripts/fetch-sparkle.sh")"
python3 "$PROJECT_DIR/scripts/validate-appcast.py" archive "$ARCHIVE" "$INFO"
PUBLIC_KEY="$("$SPARKLE/bin/generate_keys" --account "$SPARKLE_KEYCHAIN_ACCOUNT" -p)"
[ "$PUBLIC_KEY" = "$(/usr/libexec/PlistBuddy -c 'Print :SUPublicEDKey' "$INFO")" ] || {
    printf 'Keychain signing key does not match the release public key.\n' >&2; exit 1;
}
TAG="$(/usr/libexec/PlistBuddy -c 'Print :HUDReleaseTag' "$INFO")"
CHANNEL_ARGS=()
if [[ "$TAG" == *-preview.* ]]; then CHANNEL_ARGS=(--channel preview); fi
BUILD="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$INFO")"
mkdir -p "$PROJECT_DIR/build" "$(dirname "$FEED")"
STAGE="$(mktemp -d "$PROJECT_DIR/build/.update.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
mkdir "$STAGE/archives" "$STAGE/extracted"
if [ -f "$FEED" ]; then
    "$PROJECT_DIR/scripts/verify-update-signature.sh" "$INFO" --feed "$FEED"
    python3 "$PROJECT_DIR/scripts/validate-appcast.py" feed "$FEED" "$INFO" --allow-empty
    python3 - "$FEED" "$BUILD" <<'PY'
import sys, xml.etree.ElementTree as ET
versions = [int(x.text) for x in ET.parse(sys.argv[1]).findall('.//{http://www.andymatuschak.org/xml-namespaces/sparkle}version')]
if versions and int(sys.argv[2]) <= max(versions):
    sys.exit('Increment CFBundleVersion before generating a new update; published builds are immutable.')
PY
    cp "$FEED" "$STAGE/appcast.xml"
fi
ditto -x -k "$ARCHIVE" "$STAGE/extracted"
"$PROJECT_DIR/scripts/verify-bundle.sh" "$STAGE/extracted/EndfieldHUD.app" arm64 x86_64
cp "$ARCHIVE" "$STAGE/archives/$(basename "$ARCHIVE")"
if [ "$#" -eq 2 ]; then cp "$2" "$STAGE/archives/$(basename "$ARCHIVE" .zip).txt"; fi
"$SPARKLE/bin/generate_appcast" --account "$SPARKLE_KEYCHAIN_ACCOUNT" ${CHANNEL_ARGS[@]+"${CHANNEL_ARGS[@]}"} \
    --download-url-prefix "https://github.com/$SPARKLE_REPOSITORY/releases/download/$TAG/" \
    --link "https://github.com/$SPARKLE_REPOSITORY" --embed-release-notes \
    --versions "$BUILD" --maximum-versions 0 --maximum-deltas 0 \
    -o "$STAGE/appcast.xml" "$STAGE/archives"
# Sparkle uses the bundle's numeric short version by default. Present the full
# GitHub semantic version (including preview.N), then re-sign the changed feed.
python3 - "$STAGE/appcast.xml" "$BUILD" "$TAG" <<'PYFEED'
import sys, xml.etree.ElementTree as ET
namespace = "http://www.andymatuschak.org/xml-namespaces/sparkle"
ET.register_namespace("sparkle", namespace)
feed = ET.parse(sys.argv[1])
for item in feed.findall("channel/item"):
    if item.findtext("{" + namespace + "}version") == sys.argv[2]:
        node = item.find("{" + namespace + "}shortVersionString")
        if node is None:
            node = ET.SubElement(item, "{" + namespace + "}shortVersionString")
        node.text = sys.argv[3][1:]
        break
else:
    sys.exit("Generated feed does not contain the requested build")
feed.write(sys.argv[1], encoding="utf-8", xml_declaration=True)
PYFEED
"$SPARKLE/bin/sign_update" --account "$SPARKLE_KEYCHAIN_ACCOUNT" -p "$STAGE/appcast.xml"
SIGNATURE="$(python3 "$PROJECT_DIR/scripts/validate-appcast.py" feed "$STAGE/appcast.xml" "$INFO" --archive "$ARCHIVE" --print-signature)"
"$PROJECT_DIR/scripts/verify-update-signature.sh" "$INFO" --archive "$ARCHIVE" "$SIGNATURE"
"$PROJECT_DIR/scripts/verify-update-signature.sh" "$INFO" --feed "$STAGE/appcast.xml"
mv "$STAGE/appcast.xml" "$FEED"
printf 'Prepared signed feed: %s\nNothing was published. Review it, then use scripts/publish-update.sh with the same ZIP.\n' "$FEED"
