# Testing EndfieldHUD

## Core checks

Run on macOS with Xcode Command Line Tools installed:

```sh
./scripts/test.sh
./scripts/test-release-resources.sh
```

The suite covers models and persistence, bounded caches, map geometry and raster
scheduling, clipboard change detection, audio routing with fixtures, telemetry,
shortcuts, display selection, settings, HUD motion, input, and teardown. Hardware
fixtures do not grant permissions or enable real audio capture.

The scripts select a compatible SDK; set `SDKROOT` only when overriding it.
Generated binaries and compiler caches stay in ignored `build/` directories.
The resource probe compiles both development and release modes to check that a
release never falls back to checkout assets or embeds the checkout's path.

## Native HUD checks

Build first with `./scripts/dev.sh`. Run one graphical check at a time from a
logged-in macOS desktop session. These commands briefly open the HUD and then exit:

```sh
APP=build/dev/EndfieldHUD.app/Contents/MacOS/EndfieldHUD
"$APP" --ui-test --lifecycle-smoke-test
"$APP" --ui-test --navigation-smoke-test
"$APP" --ui-test --notes-shelf-smoke-test
"$APP" --ui-test --telemetry-smoke-test
"$APP" --ui-test --app-shortcut-smoke-test
"$APP" --ui-test --work-smoke-test
"$APP" --ui-test --event-log-smoke-test
```

`--ui-test` isolates preferences, stores, and the clipboard from normal use.
Lifecycle checks cover pointer response during deployment/retraction, modal quit
cancellation, close-before-quit ordering, and zero retained animations after close.
Navigation checks cover every module, retained window/shell identity, and interrupted
transitions. Notes/Shelf checks cover pinned notes and native drag-session teardown.

Fixture tests do not replace testing real app activation, audio hardware, physical
hotkeys, Finder drag/drop, multiple monitors, or Accessibility behavior. Respect
system Reduce Motion when interpreting animation checks.

## Release checks

```sh
./scripts/build.sh
xcrun lipo build/EndfieldHUD.app/Contents/MacOS/EndfieldHUD -verify_arch arm64 x86_64
codesign --verify --deep --strict build/EndfieldHUD.app
./scripts/package.sh --skip-build --dmg
```

Verify the DMG checksum, mount it read-only, and confirm that it contains the signed
app and an Applications shortcut. Test the mounted app's resource loading and run
its lifecycle check. Build targets are not a claim that every supported OS or Intel
Mac has been tested. See [test distribution](docs/testing-build.md).

## Manual regression checklist

- Summon with the configured physical hotkey on the chosen display; close by hotkey,
  Escape, and focus loss. Pointer tilt must work throughout both transitions.
- Confirm and cancel the red power button. Confirmed quit closes the HUD before the
  app exits; ordinary HUD dismissal keeps the app available.
- Edit/reopen Notes; verify pinned placement and persistence. Drag multiple Shelf
  items to Finder and cancel a drag. Copy representative Clipboard items back out.
- Change audio devices and app volume on permitted hardware, including returning
  an app to 100%. Run/pause/reset Work Mode and verify Focus automation if configured.
- Check settings, event filtering, profile/image cropping, app shortcuts, Storage,
  Activity history, and Map pan/zoom/reset/pins after switching tabs and reopening.
- Check light/dark themes, custom accent, Reduce Motion, display disconnects, and
  extreme UI scale/position rollback. Preserve user data during testing.

## Performance work

Use `scripts/benchmark-map.sh` and `scripts/stress-map.sh` for focused map measurements.
Profile a release-optimized build when performance changes warrant it. Record the
hardware, OS, sample duration, visible module, and settings alongside measurements.
Old measurements of earlier static HUD versions are not current performance claims.
