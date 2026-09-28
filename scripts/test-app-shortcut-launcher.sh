#!/bin/bash
set -euo pipefail

# A separate integration check requiring a logged-in graphical macOS session.
# It creates and controls only one disposable fixture app, never user apps.
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
TEST_BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build}/tests"
LAUNCHER_TEST_SDK="$("$PROJECT_DIR/scripts/build.sh" --print-sdk)"
SWIFTC="$(env -u SDKROOT xcrun --find swiftc)"
mkdir -p "$TEST_BUILD_DIR" "$TEST_BUILD_DIR/launcher-module-cache"
RUN_ROOT="$(mktemp -d "$TEST_BUILD_DIR/launcher-reopen.XXXXXX")"
trap 'rm -rf "$RUN_ROOT"' EXIT

FIXTURE_APP="$RUN_ROOT/AppShortcutReopenFixture.app"
FIXTURE_ID="org.endfieldcharge.tests.reopen.$(uuidgen | tr '[:upper:]' '[:lower:]')"
mkdir -p "$FIXTURE_APP/Contents/MacOS"
cat > "$FIXTURE_APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>$FIXTURE_ID</string>
<key>CFBundleExecutable</key><string>AppShortcutReopenFixture</string>
<key>CFBundleName</key><string>EndfieldCharge Reopen Fixture</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST

# Supply the selected SDK explicitly; do not repurpose/export SDKROOT, which
# would change xcrun's SDK discovery during the second compiler invocation.
env -u SDKROOT "$SWIFTC" -swift-version 5 -parse-as-library -sdk "$LAUNCHER_TEST_SDK" \
    -module-cache-path "$TEST_BUILD_DIR/launcher-module-cache" -framework Cocoa \
    "$PROJECT_DIR/Tests/AppShortcutReopenFixture.swift" \
    -o "$FIXTURE_APP/Contents/MacOS/AppShortcutReopenFixture"
codesign --force --sign - "$FIXTURE_APP"

env -u SDKROOT "$SWIFTC" -swift-version 5 -parse-as-library -sdk "$LAUNCHER_TEST_SDK" \
    -module-cache-path "$TEST_BUILD_DIR/launcher-module-cache" -framework Cocoa -framework IOKit \
    "$PROJECT_DIR/Sources/Models.swift" \
    "$PROJECT_DIR/Sources/DisplayPolicy.swift" \
    "$PROJECT_DIR/Sources/BatteryMonitor.swift" \
    "$PROJECT_DIR/Sources/BatteryCapacity.swift" \
    "$PROJECT_DIR/Sources/Localization.swift" \
    "$PROJECT_DIR/Sources/AppShortcutStore.swift" \
    "$PROJECT_DIR/Sources/AppShortcutLauncher.swift" \
    "$PROJECT_DIR/Tests/AppShortcutLauncherIntegration.swift" \
    -o "$RUN_ROOT/AppShortcutLauncherIntegration"

"$RUN_ROOT/AppShortcutLauncherIntegration" "$FIXTURE_APP" "$RUN_ROOT/state.json"
