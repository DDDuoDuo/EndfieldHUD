# Launch and installer verification

The published 1.0.1 build 10 DMG was downloaded unchanged and tested on a
disposable Apple silicon runner with macOS 26.6.2 (25G83). Its SHA-256 was
`29d7adbd96cf9264858d64481e797beac9a329ebda8d297beff7805955aaf40a`.

Both LaunchServices and direct execution passed 38 HUD lifecycle assertions.
The runner had Reduce Motion enabled, so animation midpoint assertions were
skipped. Code-signature integrity checks passed for both architectures.

This verifies the distributed executable, embedded framework and isolated HUD
lifecycle on that runner. It does **not** reproduce browser quarantine,
Gatekeeper's first-launch approval, user permissions, or a process held before
the app's entry point. An app that passes this test can still encounter a
machine-specific launch-policy problem. The app remains ad hoc signed and is
not Apple-notarized.

## Standard installer

`scripts/package-installer.sh [APP_PATH] [OUTPUT_DIRECTORY]` creates an
additional macOS Installer package from the existing verified app. It does not
rebuild or re-sign the app. The package version uses the internal build number;
the app's displayed version and bundle identifier remain unchanged.

The only payload is `/Applications/EndfieldHUD.app`. Installer atomically
replaces the bundle, disables relocation, and checks its identifier and version.
The standard close-app requirement is declared in the package. There are no
installation scripts, privileged helpers, security-setting changes, automatic
restarts, or changes to preferences and saved data in Library.

An unsigned installer may require macOS's normal approval. It cannot supply a
Developer ID certificate or notarization ticket. The close-app requirement also
does not guarantee that Installer can terminate an already hung process. This
package is an alternate clean installation method, not a confirmed remedy for
every launch-policy failure.

The Tahoe workflow checks the package contents, installs and reinstalls it on a
disposable runner, checks that stale bundle files are removed, and reruns the
distributed launch checks. It does not install onto a developer's Mac.

## Release assets

Keep every filename referenced by the signed update feed available. Renaming a
GitHub release asset does not update the signed feed and can break updates.
Build-specific ZIP assets must retain their exact bytes and filenames even if
a separate, shorter download name is provided for manual installation.

Apple references: [Updating Mac software](https://developer.apple.com/documentation/security/updating-mac-software)
and [Developer ID distribution](https://developer.apple.com/developer-id/).
