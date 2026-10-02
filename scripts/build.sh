#!/bin/bash
set -euo pipefail
umask 022

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build}"
APP_NAME="EndfieldHUD"
SWIFTC="$(xcrun --find swiftc)"

# An SDK installed by a newer CLT package can be incompatible with an older
# compiler that remains selected. This machine's Swift 6.1 uses the 15.5 SDK.
# SDKROOT is an explicit override for other correctly matched installations.
if [ -n "${SDKROOT:-}" ]; then
    SELECTED_SDK="$SDKROOT"
else
    SELECTED_SDK="$(xcrun --sdk macosx --show-sdk-path)"
    COMPILER_VERSION="$("$SWIFTC" --version 2>&1)"
    SDK_VERSION="$(/usr/libexec/PlistBuddy -c 'Print :Version' "$SELECTED_SDK/SDKSettings.plist")"
    if [[ "$COMPILER_VERSION" == *"Swift version 6.1."* ]] && [[ "$SDK_VERSION" != 15.* ]]; then
        MATCHED_SDK="/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk"
        if [ -d "$MATCHED_SDK" ]; then
            SELECTED_SDK="$MATCHED_SDK"
            printf 'Using macOS 15.5 SDK for Swift 6.1 (default SDK is %s).\n' "$SDK_VERSION" >&2
        else
            printf 'Swift 6.1 and SDK %s may be incompatible. Set SDKROOT to a matching SDK, or update Command Line Tools.\n' "$SDK_VERSION" >&2
            exit 1
        fi
    fi
fi

if [ ! -d "$SELECTED_SDK/System/Library/Frameworks" ]; then
    printf 'Invalid macOS SDK: %s\n' "$SELECTED_SDK" >&2
    exit 1
fi

if [ "${1:-}" = '--print-sdk' ]; then
    printf '%s\n' "$SELECTED_SDK"
    exit 0
fi
if [ "$#" -ne 0 ]; then
    printf 'Usage: %s [--print-sdk]\nEnvironment: ARCHS, SDKROOT, BUILD_DIR, CODE_SIGN_IDENTITY\n' "$0" >&2
    exit 2
fi

read -r -a ARCHITECTURES <<< "${ARCHS:-arm64 x86_64}"
if [ "${#ARCHITECTURES[@]}" -eq 0 ]; then
    printf 'ARCHS must contain arm64, x86_64, or both.\n' >&2
    exit 2
fi
for ARCH in "${ARCHITECTURES[@]}"; do
    case "$ARCH" in arm64|x86_64) ;; *) printf 'Unsupported architecture: %s\n' "$ARCH" >&2; exit 2 ;; esac
done

SPARKLE_DIR="$("$PROJECT_DIR/scripts/fetch-sparkle.sh")"

mkdir -p "$BUILD_DIR/module-cache"
STAGE="$(mktemp -d "$BUILD_DIR/.stage.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
STAGED_APP="$STAGE/$APP_NAME.app"
mkdir -p "$STAGED_APP/Contents/MacOS" "$STAGED_APP/Contents/Resources"

SOURCES=("$PROJECT_DIR"/Sources/*.swift)
BACKDROP_LINK_FLAGS=()
if [ -d "$SELECTED_SDK/System/Library/Frameworks/ScreenCaptureKit.framework" ]; then
    # Screen pixels use a guarded macOS 14 path; older deployment targets
    # must still launch when that system framework is absent.
    BACKDROP_LINK_FLAGS=(-Xlinker -weak_framework -Xlinker ScreenCaptureKit)
fi
if [ ! -f "${SOURCES[0]}" ]; then
    printf 'No Swift source files found in %s/Sources.\n' "$PROJECT_DIR" >&2
    exit 1
fi

BINARIES=()
for ARCH in "${ARCHITECTURES[@]}"; do
    case "$ARCH" in
        arm64) TARGET="arm64-apple-macosx11.0" ;;
        x86_64) TARGET="x86_64-apple-macosx10.15.4" ;;
    esac
    printf 'Building %s for %s…\n' "$APP_NAME" "$TARGET"
    BINARY="$STAGE/$APP_NAME-$ARCH"
    "$SWIFTC" -swift-version 5 -O -whole-module-optimization -D HUD_RELEASE \
        -sdk "$SELECTED_SDK" -target "$TARGET" \
        -file-prefix-map "$PROJECT_DIR=/EndfieldHUD" \
        -F "$SPARKLE_DIR" -framework Sparkle -Xlinker -rpath -Xlinker @executable_path/../Frameworks \
        -module-cache-path "$BUILD_DIR/module-cache" \
        -framework Cocoa -framework IOKit -framework CoreAudio -framework ServiceManagement -framework Carbon -framework Quartz -framework Metal -framework MetalKit \
        "${BACKDROP_LINK_FLAGS[@]}" -lsqlite3 \
        "${SOURCES[@]}" -o "$BINARY"
    BINARIES+=("$BINARY")
done

if [ "${#BINARIES[@]}" -gt 1 ]; then
    xcrun lipo -create "${BINARIES[@]}" -output "$STAGED_APP/Contents/MacOS/$APP_NAME"
else
    cp "${BINARIES[0]}" "$STAGED_APP/Contents/MacOS/$APP_NAME"
fi

cp "$PROJECT_DIR/Resources/Info.plist" "$STAGED_APP/Contents/Info.plist"
cp "$PROJECT_DIR/LICENSE" "$STAGED_APP/Contents/Resources/LICENSE.txt"
cp "$PROJECT_DIR/CREDITS.md" "$STAGED_APP/Contents/Resources/CREDITS.md"
for LOCALIZATION in en zh-Hans zh-Hant ja ko; do
    ditto "$PROJECT_DIR/Resources/$LOCALIZATION.lproj" "$STAGED_APP/Contents/Resources/$LOCALIZATION.lproj"
done

if [ -f "$PROJECT_DIR/scripts/generate-icon.swift" ]; then
    "$SWIFTC" -swift-version 5 -sdk "$SELECTED_SDK" \
        -module-cache-path "$BUILD_DIR/module-cache" -framework Cocoa \
        "$PROJECT_DIR/scripts/generate-icon.swift" -o "$STAGE/generate-icon"
    "$STAGE/generate-icon" "$STAGED_APP/Contents/Resources/AppIcon.icns" "$PROJECT_DIR/Resources/EndfieldIndustriesSource.png"
fi

plutil -lint "$STAGED_APP/Contents/Info.plist"
cp "$PROJECT_DIR/Resources/EndfieldIndustriesSource.png" "$STAGED_APP/Contents/Resources/"
ditto "$PROJECT_DIR/Resources/AppIconSources" "$STAGED_APP/Contents/Resources/AppIconSources"
# Prepared cells replace the full atlas in the running app. Keep the original in source.
if [ -d "$STAGED_APP/Contents/Resources/AppIconSources/Factions" ]; then rm -f "$STAGED_APP/Contents/Resources/AppIconSources/FactionAtlas.png"; fi
ditto "$PROJECT_DIR/Resources/WorldMap" "$STAGED_APP/Contents/Resources/WorldMap"
  ditto "$PROJECT_DIR/Resources/Watch" "$STAGED_APP/Contents/Resources/Watch"
  python3 "$PROJECT_DIR/scripts/package-watch-resources.py" stage \
        "$PROJECT_DIR/Resources/WatchSource" "$STAGED_APP/Contents/Resources/WatchSource"
"$PROJECT_DIR/scripts/embed-sparkle.sh" "$STAGED_APP" "$SPARKLE_DIR"
# Imported images can carry owner-only permissions. A release must remain
# readable when Installer makes the bundle root-owned or another user opens it.
python3 "$PROJECT_DIR/scripts/normalize-bundle-permissions.py" "$STAGED_APP"
SIGNING_IDENTITY="${CODE_SIGN_IDENTITY:--}"
if [ "$SIGNING_IDENTITY" = '-' ]; then
    codesign --force --sign - "$STAGED_APP"
    printf 'Signed locally (ad hoc); this build is not notarized.\n'
else
    codesign --force --sign "$SIGNING_IDENTITY" --timestamp --options runtime "$STAGED_APP"
    printf 'Signed with supplied identity; notarization is still a separate step.\n'
fi
"$PROJECT_DIR/scripts/verify-bundle.sh" "$STAGED_APP" "${ARCHITECTURES[@]}"

OUTPUT="$BUILD_DIR/$APP_NAME.app"
if [ -e "$OUTPUT" ]; then
    rm -rf "$OUTPUT"
fi
mv "$STAGED_APP" "$OUTPUT"
printf '\nBuilt: %s\n' "$OUTPUT"
xcrun lipo -info "$OUTPUT/Contents/MacOS/$APP_NAME"
