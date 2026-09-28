# EndfieldHUD 1.0

The first stable release of EndfieldHUD, an unofficial Endfield-inspired macOS HUD.

- Notes, temporary file shelf, clipboard, app shortcuts, map and personal card.
- Work Mode, volume controls, battery/device information, Storage and Activity Monitor.
- Configurable appearance, display selection and summon hotkey.
- Update checks in the menu bar and About, new-release notifications, and signed automatic updates for installed release copies.
- Final navigation polish: “便笺” and “剪贴板” labels, restored Activity Monitor chart icon, and balanced bottom-button icons.

## Download and install

Download **EndfieldHUD-1.0.0-macOS.dmg**, drag **EndfieldHUD.app** to **Applications**, eject the disk image, and open the installed app. The default summon shortcut is **Ctrl + backtick**. The ZIP contains the same universal application; the source ZIP and SHA-256 checksums are also included.

Apple silicon requires macOS 11 or later; Intel requires macOS 10.15.4 or later. Live testing has been performed on an M2 MacBook Air running macOS 15.7.4. Other hardware and older supported systems have not received equivalent testing.

The application is **ad hoc signed and not notarized**; macOS may require its per-app “Open Anyway” approval. See [installation and compatibility](https://github.com/DDDuoDuo/EndfieldHUD/blob/v1.0.0/docs/testing-build.md).

Per-app volume requires macOS 14.2+, System Audio Recording permission, and compatible audio hardware. Automatic Focus requires macOS 13+ and the user's two configured Shortcuts; installing the app does not install those shortcuts. See [Focus setup](https://github.com/DDDuoDuo/EndfieldHUD/blob/v1.0.0/docs/audio-and-work-mode.md#automatic-focus-setup).

Update signatures, packaging and update coordination are verified. A live upgrade between published updater-enabled versions has not yet been exercised; this is the first release that includes that updater.

Original code: MIT. Game artwork belongs to its respective owners; see [credits](https://github.com/DDDuoDuo/EndfieldHUD/blob/v1.0.0/CREDITS.md).

---

首个正式版本。下载 DMG 后，将应用拖入“应用程序”，推出磁盘映像，再打开应用。默认快捷键为 **Control + `**。

此版本包含便笺、文件暂存架、剪贴板、应用快捷方式、地图、个人名片、工作模式、音量、电量、存储及活动监视器，并提供检查更新、更新通知和签名自动更新。

应用尚未经过 Apple 公证。各功能的系统要求、专注模式快捷指令配置及已验证范围见上方说明。非官方同人项目。
