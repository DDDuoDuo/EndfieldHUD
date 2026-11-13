# Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/)

## What is signed

The Windows files attached to the [releases](https://github.com/DDDuoDuo/EndfieldHUD/releases) of this repository, from v1.3.2 on: `EndfieldHUD.exe` and `uninstall.exe` inside the package, and the setup (`EndfieldHUD-<version>-build<N>-windows-x64-setup.exe`). The portable zip holds the same signed `EndfieldHUD.exe`

GitHub Actions builds these files from the release's tagged commit of this repository ([windows-release.yml](.github/workflows/windows-release.yml)) and sends them to SignPath. Nothing built elsewhere is signed. Each signing request is approved by hand. The Mac files are not part of this

## Team roles

- Committers and reviewers: [DDDuoDuo](https://github.com/DDDuoDuo)
- Approvers: [DDDuoDuo](https://github.com/DDDuoDuo)

## Privacy policy

EndfieldHUD has no telemetry and sends no data about you or your computer to the project. It talks to other systems only for these features:

- **Updates:** once a day by default, and when you check by hand, the Windows version asks GitHub (`api.github.com`) for the latest release. Installing an update downloads it from GitHub
- **Now Playing:** while a song is playing, its title and artist are sent to [LRCLIB](https://lrclib.net) and NetEase Cloud Music (`music.163.com`) to find its lyrics and cover art
- **Account:** only when you sign in, your HYPERGRYPH/SKLAND account details go to HYPERGRYPH's services (`hypergryph.com`, `skland.com`, `skport.com`) to show your game data, and the sign-in's human check loads from Geetest

Your notes, settings and other data stay on your computer
