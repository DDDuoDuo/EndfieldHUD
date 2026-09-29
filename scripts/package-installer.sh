#!/bin/bash
set -euo pipefail

# Build a standard, script-free Installer product from an existing app. No
# installation, application signing, permissions repair, or quarantine changes
# take place here. INSTALLER_SIGN_IDENTITY optionally signs the outer product.
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
if [ "$(uname -s)" != "Darwin" ] || [ "$#" -gt 2 ]; then
    printf 'Usage on macOS: %s [APP_PATH] [OUTPUT_DIRECTORY]\n' "$0" >&2
    exit 2
fi
APP="$(cd "${1:-$PROJECT_DIR/build/EndfieldHUD.app}" && pwd -P)"
OUTPUT_DIRECTORY="${2:-${DIST_DIR:-$PROJECT_DIR/dist}}"
INFO="$APP/Contents/Info.plist"
BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$INFO")"
VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$INFO")"
APP_BUILD="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$INFO")"
if [ "$BUNDLE_ID" != 'io.github.endfieldcharge.EndfieldCharge' ] || \
   [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || [[ ! "$APP_BUILD" =~ ^[1-9][0-9]*$ ]]; then
    printf 'Expected EndfieldHUD with a numeric release version and positive integer build.\n' >&2
    exit 1
fi
if [ -n "${INSTALLER_SIGN_IDENTITY:-}" ] && [[ "$INSTALLER_SIGN_IDENTITY" != 'Developer ID Installer: '* ]]; then
    printf 'INSTALLER_SIGN_IDENTITY must name a Developer ID Installer certificate.\n' >&2
    exit 2
fi
codesign --verify --deep --strict --all-architectures --verbose=2 "$APP"
xcrun lipo "$APP/Contents/MacOS/EndfieldHUD" -verify_arch arm64 x86_64
mkdir -p "$OUTPUT_DIRECTORY"
OUTPUT_DIRECTORY="$(cd "$OUTPUT_DIRECTORY" && pwd -P)"
case "$OUTPUT_DIRECTORY/" in "$APP/"*) printf 'Output must be outside the app bundle.\n' >&2; exit 2 ;; esac
ARTIFACT="EndfieldHUD-$VERSION-build$APP_BUILD-Installer.pkg"
if [ -e "$OUTPUT_DIRECTORY/$ARTIFACT" ]; then
    printf 'Refusing to replace an existing installer: %s\n' "$OUTPUT_DIRECTORY/$ARTIFACT" >&2
    exit 1
fi
PACKAGE_STAGE="$(mktemp -d "${TMPDIR:-/tmp}/EndfieldHUD-installer.XXXXXX")"
trap 'rm -rf "$PACKAGE_STAGE"' EXIT
mkdir -p "$PACKAGE_STAGE/root/Applications" "$PACKAGE_STAGE/packages"
ditto --norsrc --noextattr --noqtn "$APP" "$PACKAGE_STAGE/root/Applications/EndfieldHUD.app"

python3 - "$PACKAGE_STAGE" "$BUNDLE_ID" "$APP_BUILD" <<'PY'
import plistlib
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

stage, identifier, build = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
components = [{"RootRelativeBundlePath": "Applications/EndfieldHUD.app",
               "BundleIsRelocatable": False, "BundleHasStrictIdentifier": True,
               "BundleIsVersionChecked": True, "BundleOverwriteAction": "upgrade"}]
(stage / "components.plist").write_bytes(plistlib.dumps(components))
distribution = ET.Element("installer-gui-script", {"minSpecVersion": "1"})
ET.SubElement(distribution, "title").text = "EndfieldHUD"
ET.SubElement(distribution, "options", {"customize": "never", "require-scripts": "false",
                                      "allow-external-scripts": "false", "rootVolumeOnly": "true",
                                      "hostArchitectures": "arm64,x86_64"})
ET.SubElement(distribution, "domains", {"enable_localSystem": "true", "enable_currentUserHome": "false",
                                      "enable_anywhere": "false"})
ET.SubElement(distribution, "product", {"id": identifier, "version": build})
outline = ET.SubElement(distribution, "choices-outline")
ET.SubElement(outline, "line", {"choice": "endfieldhud"})
choice = ET.SubElement(distribution, "choice", {"id": "endfieldhud", "title": "EndfieldHUD",
                                              "visible": "false", "selected": "true"})
ET.SubElement(choice, "pkg-ref", {"id": identifier})
reference = ET.SubElement(distribution, "pkg-ref", {"id": identifier, "version": build, "onConclusion": "None"})
reference.text = "EndfieldHUD-component.pkg"
# Apple's Distribution schema requires must-close in a separate reference.
close_reference = ET.SubElement(distribution, "pkg-ref", {"id": identifier})
ET.SubElement(ET.SubElement(close_reference, "must-close"), "app", {"id": identifier})
ET.indent(distribution)
ET.ElementTree(distribution).write(stage / "Distribution.xml", encoding="utf-8", xml_declaration=True)
PY

pkgbuild --root "$PACKAGE_STAGE/root" --component-plist "$PACKAGE_STAGE/components.plist" \
    --identifier "$BUNDLE_ID" --version "$APP_BUILD" --install-location / \
    --ownership recommended "$PACKAGE_STAGE/packages/EndfieldHUD-component.pkg"
if [ -n "${INSTALLER_SIGN_IDENTITY:-}" ]; then
    productbuild --distribution "$PACKAGE_STAGE/Distribution.xml" --package-path "$PACKAGE_STAGE/packages" \
        --sign "$INSTALLER_SIGN_IDENTITY" --timestamp "$PACKAGE_STAGE/$ARTIFACT"
else
    productbuild --distribution "$PACKAGE_STAGE/Distribution.xml" --package-path "$PACKAGE_STAGE/packages" \
        "$PACKAGE_STAGE/$ARTIFACT"
fi
if [ -n "${INSTALLER_SIGN_IDENTITY:-}" ]; then
    pkgutil --check-signature "$PACKAGE_STAGE/$ARTIFACT"
fi
"$PROJECT_DIR/scripts/test-installer-package.sh" "$PACKAGE_STAGE/$ARTIFACT" "$APP" "$PACKAGE_STAGE/verification"
mv "$PACKAGE_STAGE/$ARTIFACT" "$OUTPUT_DIRECTORY/$ARTIFACT"
(cd "$OUTPUT_DIRECTORY" && shasum -a 256 "$ARTIFACT" > "$ARTIFACT.sha256")
printf 'Created: %s\n' "$OUTPUT_DIRECTORY/$ARTIFACT"
printf 'Packaging does not install, notarize, or publish this file.\n'
