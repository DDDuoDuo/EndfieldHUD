#!/bin/bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
# No Keychain, GitHub credentials, uploads, or installation in this test.
"$PROJECT_DIR/scripts/verify-update-signature.sh" --self-test
"$PROJECT_DIR/scripts/verify-update-signature.sh" "$PROJECT_DIR/Resources/Info.plist" --feed "$PROJECT_DIR/updates/appcast.xml"
python3 "$PROJECT_DIR/scripts/validate-appcast.py" feed "$PROJECT_DIR/updates/appcast.xml" "$PROJECT_DIR/Resources/Info.plist" --allow-empty
python3 -B "$PROJECT_DIR/scripts/test-update-tools.py"
SPARKLE="$("$PROJECT_DIR/scripts/fetch-sparkle.sh")"
SDK="$("$PROJECT_DIR/scripts/build.sh" --print-sdk)"
STAGE="$(mktemp -d "$PROJECT_DIR/build/.sparkle-probe.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
APP="$STAGE/Probe.app"
mkdir -p "$APP/Contents/MacOS"
cat > "$STAGE/probe.swift" <<'SWIFT'
import Cocoa
import Sparkle
let version = Bundle(for: SPUUpdater.self).object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String
precondition(version == "2.9.6")
print("PASS: embedded Sparkle framework loaded without starting an updater")
SWIFT
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>io.github.endfieldhud.sparkle-probe</string>
<key>CFBundleExecutable</key><string>Probe</string>
<key>CFBundlePackageType</key><string>APPL</string>
</dict></plist>
PLIST
xcrun swiftc -swift-version 5 -sdk "$SDK" -module-cache-path "$PROJECT_DIR/build/update-tools/module-cache" \
    -F "$SPARKLE" -framework Sparkle -Xlinker -rpath -Xlinker @executable_path/../Frameworks \
    "$STAGE/probe.swift" -o "$APP/Contents/MacOS/Probe"
CODE_SIGN_IDENTITY=- "$PROJECT_DIR/scripts/embed-sparkle.sh" "$APP" "$SPARKLE"
codesign --force --sign - "$APP"
codesign --verify --deep --strict --all-architectures "$APP"
"$APP/Contents/MacOS/Probe"
