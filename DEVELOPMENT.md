# Development workflow

See [architecture](docs/architecture.md) for module ownership and lifecycle boundaries,
[testing](TESTING.md) for checks, and [test distribution](docs/testing-build.md) for DMGs.

For ongoing iterations, use a native development build and checks relevant to
the change. Skip release packaging and CPU/RSS measurement rounds unless the
user explicitly requests them.

1. Make the focused change, then run `./scripts/dev.sh`.
2. Run `./scripts/test.sh` when core logic changes; use the relevant graphical
   smoke check or a brief manual check when behavior or appearance changes.
3. Quit the running copy before opening `build/dev/EndfieldHUD.app` to inspect
   the result. The build script never quits or launches an app automatically.

The development script compiles only the host's native architecture, using the
same SDK selection as `build.sh`. It targets macOS 11 on Apple silicon and
macOS 10.15.4 on Intel. These builds omit release optimization by default. Set `DEV_OPTIMIZATION=-O` for a native optimized performance run without release packaging.

The first build reuses `build/EndfieldHUD.app` resources when available, or
creates a minimal bundle with the existing Info.plist, license, and credits.
Every successful build refreshes the bundle's Info.plist from
`Resources/Info.plist`, so capability purpose strings stay in step with the
executable. Supplied icon presets are copied into the development bundle. The
default Endfield icon is cached and regenerated only when its source or renderer
changes. Later builds replace
the executable only after compilation succeeds, then apply a local ad hoc
signature. A minimal bundle may display the default app icon. Bundle identity
is unchanged, so development and release copies share user preferences.

Use `DEV_BUILD_DIR` for a different development output directory and `SDKROOT`
for an explicit SDK. The universal app in `build/EndfieldHUD.app` is preserved.
No ZIP, DMG, source archive or version bump is part of this
workflow. Use release and performance workflows only when requested.

Per-process audio routing has synthetic-buffer and injected-hardware checks;
these must not enable capture or alter real output devices. Real capture/volume behavior has also been manually verified in earlier integration work; synthetic checks do not repeat or replace that hardware check. See [per-app audio](docs/per-app-audio.md).

Faction presets ship as individual 512px crops under `Resources/AppIconSources/Factions`.
The original atlas stays in the repository; the picker does not decode it at runtime.
After changing atlas selections, rebuild these assets with
`scripts/prepare-faction-icons.swift` (arguments: atlas PNG, `Sources/HUDApplicationIcon.swift`, output directory).

Release builds define `HUD_RELEASE`. The shared `HUDResources` locator then reads
only the signed app bundle; checkout resource fallbacks are compiled out.
Development and standalone test tools retain those fallbacks. Verify both modes
with `./scripts/test-release-resources.sh` before changing resource loading.
