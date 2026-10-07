#!/bin/bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build}"
DIST_DIR="${DIST_DIR:-$PROJECT_DIR/dist}"
BUILD=1
MAKE_DMG=0
BUILD_SUFFIX=0
for ARG in "$@"; do
    case "$ARG" in
        --skip-build) BUILD=0 ;;
        --dmg) MAKE_DMG=1 ;;
        --build-suffix) BUILD_SUFFIX=1 ;;
        *) printf 'Usage: %s [--skip-build] [--dmg] [--build-suffix]\n' "$0" >&2; exit 2 ;;
    esac
done
if [ "$BUILD" -eq 1 ]; then
    "$PROJECT_DIR/scripts/build.sh"
fi
APP="$BUILD_DIR/EndfieldHUD.app"
if [ ! -d "$APP" ]; then
    printf 'Missing app: %s. Run scripts/build.sh first.\n' "$APP" >&2
    exit 1
fi
read -r -a EXPECTED_ARCHITECTURES <<< "${ARCHS:-arm64 x86_64}"
"$PROJECT_DIR/scripts/verify-bundle.sh" "$APP" "${EXPECTED_ARCHITECTURES[@]}"
VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")"
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'Expected a numeric three-component app version, received: %s\n' "$VERSION" >&2
    exit 1
fi
ARTIFACT_STEM="EndfieldHUD-$VERSION"
if [ "$BUILD_SUFFIX" -eq 1 ]; then
    APP_BUILD="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$APP/Contents/Info.plist")"
    if [[ ! "$APP_BUILD" =~ ^[1-9][0-9]*$ ]]; then
        printf 'Expected a positive integer app build, received: %s\n' "$APP_BUILD" >&2
        exit 1
    fi
    ARTIFACT_STEM="$ARTIFACT_STEM-build$APP_BUILD"
fi
mkdir -p "$DIST_DIR"
PACKAGE_STAGE="$(mktemp -d "$BUILD_DIR/.package.XXXXXX")"
trap 'rm -rf "$PACKAGE_STAGE"' EXIT
ARCHIVE="$PACKAGE_STAGE/$ARTIFACT_STEM-macOS.zip"
# Bundle resources are ordinary files. Omit machine-local extended attributes
# and resource-fork sidecars from the public archive.
ditto -c -k --norsrc --noextattr --noqtn --keepParent "$APP" "$ARCHIVE"
unzip -tq "$ARCHIVE"
GENERATED_FILES=("$(basename "$ARCHIVE")")

# Use a fixed source manifest so caches, local Git data, and signing material
# cannot accidentally enter the source download.
SOURCE_ROOT="$PACKAGE_STAGE/EndfieldHUD"
mkdir -p "$SOURCE_ROOT"
SOURCE_PATHS=(Sources Resources ThirdParty scripts README.md README.zh-CN.md README.zh-TW.md README.ja.md CREDITS.md LICENSE .gitignore)
if [ -d "$PROJECT_DIR/docs" ]; then SOURCE_PATHS+=(docs); fi
if [ -d "$PROJECT_DIR/updates" ]; then SOURCE_PATHS+=(updates); fi
# Local-only research and tests remain in the checkout. Include only public,
# tracked files so they cannot leak back into a release source archive.
git -C "$PROJECT_DIR" ls-files -z -- "${SOURCE_PATHS[@]}" > "$PACKAGE_STAGE/source-manifest"
if [ ! -s "$PACKAGE_STAGE/source-manifest" ]; then
    printf 'A Git checkout with tracked source files is required for packaging.\n' >&2
    exit 1
fi
COPYFILE_DISABLE=1 tar -C "$PROJECT_DIR" \
    --exclude='.DS_Store' --exclude='._*' --exclude='.git' \
    --exclude='build' --exclude='dist' \
    --exclude='*.p12' --exclude='*.p8' --exclude='*.mobileprovision' \
    --exclude='*.provisionprofile' --exclude='.env' --exclude='.env.*' \
    --null -T "$PACKAGE_STAGE/source-manifest" \
    -cf - | COPYFILE_DISABLE=1 tar -C "$SOURCE_ROOT" -xf -
SOURCE_ARCHIVE="$PACKAGE_STAGE/$ARTIFACT_STEM-source.zip"
ditto -c -k --norsrc --noextattr --keepParent "$SOURCE_ROOT" "$SOURCE_ARCHIVE"
unzip -tq "$SOURCE_ARCHIVE"
unzip -Z -1 "$SOURCE_ARCHIVE" > "$PACKAGE_STAGE/source-files.txt"
if ! awk '
    $0 !~ /^EndfieldHUD\// ||
    $0 ~ /(^|\/)(build|dist|\.git|__MACOSX|\.DS_Store)(\/|$)/ ||
    $0 ~ /(^|\/)\._/ { invalid = 1 }
    END { exit invalid }
' "$PACKAGE_STAGE/source-files.txt"; then
    printf 'Source archive contains an unexpected path.\n' >&2
    exit 1
fi
GENERATED_FILES+=("$(basename "$SOURCE_ARCHIVE")")

if [ "$MAKE_DMG" -eq 1 ]; then
    DMG_ROOT="$PACKAGE_STAGE/dmg"
    mkdir -p "$DMG_ROOT"
    ditto --norsrc --noextattr --noqtn "$APP" "$DMG_ROOT/EndfieldHUD.app"
    ln -s /Applications "$DMG_ROOT/Applications"
    cp "$PROJECT_DIR/LICENSE" "$DMG_ROOT/LICENSE.txt"
    cp "$PROJECT_DIR/README.md" "$DMG_ROOT/README.md"
    DMG="$PACKAGE_STAGE/$ARTIFACT_STEM-macOS.dmg"
    hdiutil create -volname EndfieldHUD -srcfolder "$DMG_ROOT" -format UDZO -ov "$DMG"
    if [ -n "${CODE_SIGN_IDENTITY:-}" ] && [ "$CODE_SIGN_IDENTITY" != '-' ]; then
        codesign --sign "$CODE_SIGN_IDENTITY" --timestamp "$DMG"
    fi
    hdiutil verify "$DMG"
    if [ -n "${CODE_SIGN_IDENTITY:-}" ] && [ "$CODE_SIGN_IDENTITY" != '-' ]; then
        codesign --verify --strict --verbose=2 "$DMG"
    fi
    GENERATED_FILES+=("$(basename "$DMG")")
fi
# The checksum file names only artifacts produced by this invocation. Historical
# dist files cannot be mixed into this release's download manifest.
CHECKSUMS="$ARTIFACT_STEM-SHA256SUMS.txt"
(cd "$PACKAGE_STAGE" && shasum -a 256 "${GENERATED_FILES[@]}" > "$CHECKSUMS" && shasum -a 256 -c "$CHECKSUMS")
GENERATED_FILES+=("$CHECKSUMS")
for FILE in "${GENERATED_FILES[@]}"; do
    mv -f "$PACKAGE_STAGE/$FILE" "$DIST_DIR/$FILE"
    printf 'Created: %s\n' "$DIST_DIR/$FILE"
done
printf 'Packaging does not notarize or publish these files.\n'
