#!/bin/bash
# Prints the macOS SDK the Mac oracles (tools/*_reference.sh/.py) compile the Mac sources against: the same choice
# as the Mac app's build script (scripts/build.sh, kept local since it left the repository). SDKROOT wins when set;
# otherwise xcrun's default SDK, except that a Swift 6.1 compiler cannot use a newer SDK and gets the matched
# Command Line Tools 15.5 SDK (xcrun alone would name the incompatible default, e.g. 26.2).
set -euo pipefail
if [ -n "${SDKROOT:-}" ]; then printf '%s\n' "$SDKROOT"; exit 0; fi
sdk="$(xcrun --sdk macosx --show-sdk-path)"
if [[ "$(xcrun swiftc --version 2>&1)" == *"Swift version 6.1."* ]] &&
   [[ "$(/usr/libexec/PlistBuddy -c 'Print :Version' "$sdk/SDKSettings.plist")" != 15.* ]]; then
    sdk=/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk
    [ -d "$sdk" ] || { echo "Swift 6.1 needs the macOS 15.5 SDK: install it or set SDKROOT" >&2; exit 1; }
fi
[ -d "$sdk/System/Library/Frameworks" ] || { echo "Invalid macOS SDK: $sdk" >&2; exit 1; }
printf '%s\n' "$sdk"
