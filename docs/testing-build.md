# EndfieldHUD 1.0.1 — installation and compatibility

The download is a universal macOS application. It is
**ad hoc signed and not notarized**. That signature checks bundle integrity; it
is not an Apple Developer ID identity or Apple approval. No Developer ID
certificate is included in this repository.

## Current integration test build

`codex/endfield-hud-integration` combines the Watch presentation with the stable
desktop functions and saved-data identity. It remains version 1.0.1/internal
build 11 and is a manual test build, not a replacement for the published release.
See the [design record](design.md) and [integration checks and measurements](integration-preservation.md).
Local packages include `integration-build.json` beside the DMG with the exact
commit, architecture checks and artifact hashes; branch CI artifacts have their
own provenance file. Install only one running copy, following the steps below.

In this integration branch, Work Mode first tries public Control Center controls
on macOS 11+ when you grant Accessibility access and the controls are recognized.
Existing configured Focus shortcuts are a fallback on macOS 13+. It preserves
an already-active Focus and verifies changes; an uncertain toggle never triggers
a second attempt through Shortcuts. UI differences between macOS versions can
prevent automation. The stable v1.0.1 release and original Watch branch use only
the two shortcuts described in the [Focus setup guide](audio-and-work-mode.md#automatic-focus-setup).

## Current Watch branch build

For `codex/endfield-watch-motion`, download **EndfieldHUD-macOS-<source commit>**
from the successful branch run's **Artifacts** section in
[GitHub Actions](https://github.com/DDDuoDuo/EndfieldHUD/actions/workflows/build.yml?query=branch%3Acodex%2Fendfield-watch-motion).
Unzip that download first to obtain the DMG and the accompanying packages.
`branch-build.json` identifies the source commit, tested commit/tree, workflow run,
architectures and SHA-256 hashes. Pull-request runs test a merge commit; branch
push runs test the branch commit directly. Downloads are retained for 30 days.

The current branch compiles and verifies Apple silicon and Intel binaries and
runs its interaction and Metal fixtures on the macOS 15 CI runner. It also mounts
the DMG read-only and verifies that its app matches the built bundle. These checks
do not establish live coverage on both architectures or exact reproduction of
the game's recording. The source ZIP includes the complete setup and testing docs.

## Install the DMG

1. Quit any running EndfieldHUD copy from its menu bar menu.
2. Open `EndfieldHUD-1.0.1-macOS.dmg` and drag **EndfieldHUD.app** to **Applications**.
3. Eject the disk image, then open the installed app from Applications. The app
   lives in the menu bar. Its default summon shortcut is **Ctrl + backtick**.
4. If macOS blocks this trusted download because the developer cannot be
   verified, first attempt to open it, then use **System Settings → Privacy &
   Security → Open Anyway** for EndfieldHUD, if macOS offers that choice. Older
   macOS versions use **System Preferences → Security & Privacy**. Review the
   app identity before approving it. Follow [Apple's documented opening
   procedure](https://support.apple.com/102445); do not disable Gatekeeper or
   remove quarantine globally. Stop if macOS identifies the app as malicious or
   damaged instead of offering its normal per-app exception.

The accompanying `EndfieldHUD-1.0.1-SHA256SUMS.txt` lists only this package's
artifacts. It detects accidental changes when checked against a checksum
obtained from the trusted distributor; it does not replace publisher identity.

## Compatibility and permissions

- The universal binary targets **Apple silicon/macOS 11+** and
  **Intel/macOS 10.15.4+**. Those are build targets, not completed hardware coverage.
- Earlier development and interactive checks were performed on an **Apple M2 MacBook
  Air with macOS 15.7.4**. Intel, older OS releases, physical multi-monitor
  handoff and other audio devices still need testing. This is historical coverage,
  not live hardware validation of the current Watch branch. The repository's
  `TESTING.md` records the scope of each check.
- The Watch branch's original background filters use desktop pixels on macOS 14+
  when Screen Recording access is granted. System blur is the fallback without
  that access. The background capture is not saved or uploaded.
- Per-app audio adjustment requires **macOS 14.2+**, System Audio Recording
  permission, and a compatible output device. Grant it only when using that
  feature. Audio is processed in memory and is not saved or uploaded. Some
  applications and devices cannot be independently adjusted.
- Automatic Focus in the integration branch uses **Accessibility** access and
  recognizable Control Center controls on **macOS 11+**. If that path is
  unavailable before a toggle, it can use the current user's uniquely named
  `EndfieldCharge Focus Start` and `EndfieldCharge Focus End` shortcuts on
  **macOS 13+**. The stable release and original Watch branch require this
  shortcut setup. The app installs no shortcuts and grants no permissions;
  follow the [Focus setup guide](audio-and-work-mode.md#automatic-focus-setup).
  Native controls were inspected read-only on macOS 15.7.4; actual Control Center
  toggling is covered by synthetic tests, not live end-to-end validation. Intel,
  older macOS versions and Tahoe remain unverified. Work Mode's timer works
  without automation. Existing Focus is preserved; previous schedules and
  same-mode user changes cannot be fully restored. Normal quit allows up to
  eight seconds for cleanup, so forced exit or a stalled operation can leave
  Focus active.
- Launch at login works after installation in Applications and may require
  approval in macOS Login Items. Opening the DMG alone does not install the app.
- File and image access follows the user's chosen items. The app keeps its
  notes, bookmarks, map pins, profile and settings in the user's own Library;
  those files are not part of the download. Existing installations keep their
  data because the bundle identifier is unchanged.

## Build and verify locally

With Apple's Command Line Tools installed, run:

```sh
./scripts/test.sh
./scripts/build.sh
./scripts/package.sh --skip-build --dmg
```

Release builds verify both architectures, signatures and bundled resources,
then packaging verifies the ZIPs, disk image and SHA-256 manifest. These scripts
create local files only. They do not upload, publish, notarize, install, launch
or change macOS security settings.
