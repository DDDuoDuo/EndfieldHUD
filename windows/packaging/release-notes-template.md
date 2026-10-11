<!--
EndfieldHUD Windows release notes template.

stage_release.cmake renders this file into <dist>/<artifact stem>-release-notes.md
with the staged artifact names, sizes and SHA-256 values filled in. It is a
draft for DDDuoDuo: nothing is published by the build or by CI.

Publishing checklist (maintainer, after choosing a Windows release):
  1. Stage from a clean Release build with the Mac-exported inputs configured,
     so no artifact name contains "-incomplete".
  2. Write the "What's new" section below and remove this comment.
  3. Create the release under the Windows tag namespace only, never a Mac tag:
       gh release create windows-v@EHUD_NOTES_VERSION@ --target <commit> --latest=false \
         --title "EndfieldHUD @EHUD_NOTES_VERSION@ for Windows" --notes-file <this file> \
         "@EHUD_NOTES_SETUP@" "@EHUD_NOTES_PORTABLE@" "@EHUD_NOTES_SUMS@"
     Add --prerelease for a SemVer prerelease tag (windows-v1.3.0-beta.1).
     The macOS app's release checker rejects every windows-v tag, and
     updates/appcast.xml (the Mac Sparkle feed) is never edited for Windows.
  4. Upload exactly these three artifacts: the Windows updater installs a
     release only when its setup and SHA256SUMS assets carry these names.
  5. Do not upload a symbols zip, a size report or an "-incomplete" artifact.
-->
@EHUD_NOTES_DEVELOPER@# EndfieldHUD @EHUD_NOTES_VERSION@ for Windows (build @EHUD_NOTES_BUILD@)

This is a Windows release (tag `windows-v@EHUD_NOTES_VERSION@`). It does not
change the macOS app, its downloads or its update feed.

## What's new

- _Describe the changes of this Windows build._

## Install

1. Download `@EHUD_NOTES_SETUP@` (@EHUD_NOTES_SETUP_SIZE@).
2. Run it. EndfieldHUD installs for your Windows account only, into
   `%LOCALAPPDATA%\Programs\EndfieldHUD`, without administrator rights. Your
   data stays in `%LOCALAPPDATA%\EndfieldHUD`.

@EHUD_NOTES_SIGNING@

The portable zip (`@EHUD_NOTES_PORTABLE@`, @EHUD_NOTES_PORTABLE_SIZE@) contains
the same program without the installer. A portable copy does not update itself.

## Update

- **From EndfieldHUD for Windows:** open About and choose *Check for updates*.
  When this version is listed, click the row to download and verify it, then
  click again to install. EndfieldHUD closes, saves your data, updates in place
  and reopens.
- **Or** run the new setup over the installed version. Settings and data are
  kept.

## Requirements

- 64-bit Windows 11, or Windows 10 version 2004 or later. Tested on Windows 11
  25H2 (build 26200).
- Account sign-in uses the Microsoft Edge WebView2 Runtime, which Windows 11
  includes.

## Verify the download

The checksums are listed in `@EHUD_NOTES_SUMS@`. In PowerShell:

```powershell
Get-FileHash .\@EHUD_NOTES_SETUP@ -Algorithm SHA256
```

| File | Size | SHA-256 |
| --- | ---: | --- |
| `@EHUD_NOTES_SETUP@` | @EHUD_NOTES_SETUP_SIZE@ | `@EHUD_NOTES_SETUP_SHA256@` |
| `@EHUD_NOTES_PORTABLE@` | @EHUD_NOTES_PORTABLE_SIZE@ | `@EHUD_NOTES_PORTABLE_SHA256@` |

Ported from the macOS source at commit `@EHUD_NOTES_COMMIT@`.
