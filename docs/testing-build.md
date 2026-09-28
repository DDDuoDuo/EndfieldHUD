# EndfieldHUD 0.4.0 — testing preview

This is a testing build, not a broadly validated release. The default build is
**ad hoc signed and not notarized**. That signature checks bundle integrity; it
is not an Apple Developer ID identity or Apple approval. No Developer ID
certificate is included in this repository.

## Install the DMG

1. Quit any running EndfieldHUD copy from its menu bar menu.
2. Open `EndfieldHUD-0.4.0-macOS.dmg` and drag **EndfieldHUD.app** to **Applications**.
3. Eject the disk image, then open the installed app from Applications. The app
   lives in the menu bar. Its default summon shortcut is **Ctrl + backtick**.
4. If macOS blocks this trusted test download because the developer cannot be
   verified, first attempt to open it, then use **System Settings → Privacy &
   Security → Open Anyway** for EndfieldHUD, if macOS offers that choice. Older
   macOS versions use **System Preferences → Security & Privacy**. Review the
   app identity before approving it. Follow [Apple's documented opening
   procedure](https://support.apple.com/102445); do not disable Gatekeeper or
   remove quarantine globally. Stop if macOS identifies the app as malicious or
   damaged instead of offering its normal per-app exception.

The accompanying `EndfieldHUD-0.4.0-SHA256SUMS.txt` lists only this package's
artifacts. It detects accidental changes when checked against a checksum
obtained from the trusted distributor; it does not replace publisher identity.

## Compatibility and permissions

- The universal binary targets **Apple silicon/macOS 11+** and
  **Intel/macOS 10.15.4+**. Those are build targets, not completed hardware coverage.
- Development and interactive checks were performed on an **Apple M2 MacBook
  Air with macOS 15.7.4**. Intel, older OS releases, physical multi-monitor
  handoff and other audio devices still need testing. The repository's
  `TESTING.md` records the scope of each check.
- Per-app audio adjustment requires **macOS 14.2+**, System Audio Recording
  permission, and a compatible output device. Grant it only when using that
  feature. Audio is processed in memory and is not saved or uploaded. Some
  applications and devices cannot be independently adjusted.
- Automatic Focus integration requires **macOS 13+** and the user's configured
  Start/End shortcuts. macOS does not provide the app with unrestricted direct
  Focus control. Setup is described in the repository's audio and Work Mode guide.
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
