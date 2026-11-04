# Credits

EndfieldHUD is an unofficial fan project by [DDDuoDuo](https://github.com/DDDuoDuo). [Bilibili](https://space.bilibili.com/223936961)

## Visual inspiration

- *Arknights: Endfield* supplies the HUD's visual reference, including the Watch menu, motion, typography and Photo Mode.
- [氰氨锗 / QinAnze](https://github.com/QinAnze/zmd-charge)
- [llynxxx](https://www.bilibili.com/video/BV1DBaP6yEHs/)

EndfieldHUD's Swift app and desktop renderer are independently implemented. No source code or assets from the QinAnze or llynxxx apps are included.

## Game artwork and resources

*Arknights*, *Arknights: Endfield*, character portraits, faction logos, branding and extracted game resources belong to HYPERGRYPH and their respective owners. This project is not affiliated with or endorsed by them.

The app includes selected Watch scene resources and translated shader programs, UI icons, the Endfield cursor, Photo Mode stickers and filters, and the original *Merge! OrbiPom!* rules and artwork. Game audio is not bundled. These materials are excluded from EndfieldHUD's MIT license.

Asset sources and checksums are recorded in the resource manifests:

- [HUD icons](Resources/AppIconSources/EndfieldWiki/SOURCES.json), sourced from the [Endfield Wiki](https://endfield.wiki.gg/wiki/Category:Icons) and the supplied original-game assets.
- [Watch sprites](Resources/Watch/SOURCES.json) and [Watch geometry notice](Resources/WatchSource/NOTICE.txt).
- [Photo Mode assets](Resources/MediaAssembly/provenance.json).
- [OrbiPom rules and artwork](Resources/OrbiPom/provenance.json).
- [Center logo presets](Resources/AppIconSources/CenterLogos/SOURCE.json). The Windows build ships byte-identical copies in `windows/resources/center-logos`.

User-supplied portraits, logos and reference recordings retain their owners' rights. The wiki's page-content license does not relicense the game artwork.

## Libraries and services

| Project | Use and license |
| --- | --- |
| [Sparkle 2.9.6](https://github.com/sparkle-project/Sparkle/releases/tag/2.9.6) | Signed app updates. MIT, Sparkle Project. |
| [MediaRemote Adapter](https://github.com/ungive/mediaremote-adapter) | Now Playing information. BSD 3-Clause, Jonas van den Berg and contributors. [Local changes and license](ThirdParty/MediaRemoteAdapter/NOTICE.md). |
| [Matter.js 0.20.0](https://github.com/liabru/matter-js) | OrbiPom physics. MIT, Liam Brummitt and contributors. [Included license](Resources/OrbiPom/Matter-LICENSE.txt). |
| [Unity uGUI](https://github.com/Unity-Technologies/uGUI) | Image geometry reference. MIT, Unity Technologies. [Included notice](Resources/WatchSource/NOTICE.txt). |
| [LRCLIB](https://lrclib.net/) | Timed lyrics when the player supplies none and a matching track is available. |

[Ronit Singh's FineTune](https://github.com/ronitsingh10/FineTune) informed the Core Audio architecture. FineTune is GPL-3.0; its source is not included. EndfieldHUD's audio engine is independently implemented.

Account integration references include [endfield-wallpaper](https://github.com/Entropy-Increase-Team/endfield-wallpaper), [endfield-gacha](https://github.com/bhaoo/endfield-gacha), [skland-plugin](https://github.com/erzaozi/skland-plugin), and [astrbot_plugin_arknights_sanity](https://github.com/fxquarter/astrbot_plugin_arknights_sanity).

## Fonts

Both builds use one shared font folder, [`Resources/Fonts`](Resources/Fonts). Every font is the unmodified upstream file, listed with its source, size and SHA-256 hash in the [manifest](Resources/Fonts/manifest.json). The macOS app ships the folder, licence texts included, in `EndfieldHUD.app/Contents/Resources/Fonts` and loads the fonts privately for its own process; nothing is installed into macOS. The Windows build ships the same HarmonyOS Sans and Noto Sans files, with their licences, in its `fonts` folder and loads them privately through DirectWrite; nothing is installed into Windows.

| Font | Use and license |
| --- | --- |
| HarmonyOS Sans 1.0 (Light, Regular, Medium, Bold, Black), from the official HarmonyOS Sans package | Every Latin letter and digit in the HUD, on macOS and Windows. Copyright 2021 Huawei Device Co., Ltd., used under the HarmonyOS Sans Fonts License Agreement; the unmodified files ship with the agreement. [License](Resources/Fonts/HarmonyOS-Sans-LICENSE.txt). |
| [Noto Sans SC, TC, JP and KR](https://github.com/google/fonts/tree/a85815a42757630ce188fdad368c2dfc444d4773/ofl), version 2.004 (variable weight), google/fonts commit `a85815a42757630ce188fdad368c2dfc444d4773` | Chinese, Japanese and Korean text, chosen by the text's language: Simplified Chinese (SC), Traditional Chinese (TC), Japanese (JP) and Korean (KR). On Windows they are loaded privately through DirectWrite. SIL Open Font License 1.1, copyright 2014-2021 Adobe, Reserved Font Name "Source". [SC license](Resources/Fonts/NotoSansSC-OFL.txt) · [TC license](Resources/Fonts/NotoSansTC-OFL.txt) · [JP license](Resources/Fonts/NotoSansJP-OFL.txt) · [KR license](Resources/Fonts/NotoSansKR-OFL.txt). |

## Windows build

The native Windows port in `windows/` adds the third-party components below. The vendored libraries and fonts are unmodified and kept with their licenses and a provenance file that pins the upstream source and SHA-256 hashes. Sparkle and MediaRemote Adapter are used only by the macOS app. The Windows build otherwise relies on components that ship with Windows, such as `winsqlite3`, `icu.dll`, Media Foundation, WIC, `Windows.Data.Pdf`, WinHTTP, XmlLite, PDH and Microsoft Active Accessibility, and on the separately installed Evergreen WebView2 Runtime. None of these is redistributed. The Windows SDK's `fxc.exe` compiles the HUD's own `hud.hlsl` at build time; only that shader's bytecode is embedded, and no SDK file ships.

| Project | Use and license |
| --- | --- |
| [QuickJS-NG 0.17.0](https://github.com/quickjs-ng/quickjs/tree/6d46d07d04041b40f4f49eaa7fdebe44c314c699), commit `6d46d07d04041b40f4f49eaa7fdebe44c314c699` | Runs the original OrbiPom scripts and Matter.js in the Windows minigame. Engine sources only. MIT, Fabrice Bellard, Charlie Gordon and Ben Noordhuis. [License](windows/third_party/quickjs-ng/LICENSE) · [provenance](windows/third_party/quickjs-ng/provenance.json). |
| [Microsoft Edge WebView2 SDK 1.0.4191.47](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4191.47) (`Microsoft.Web.WebView2`) | `WebView2.h` and the x64 static loader, linked into `EndfieldHUD.exe` for the short-lived official account sign-in. Microsoft's BSD-style SDK license, with third-party notices. [License](windows/third_party/webview2/LICENSE.txt) · [notices](windows/third_party/webview2/NOTICE.txt) · [provenance](windows/third_party/webview2/provenance.json). |
| [zlib 1.3.2](https://zlib.net/) | Inflate and CRC subset, used to read EPUB files and PDF streams in the Windows Reader and to verify EHUDZ01 payloads when packaging. zlib License, Jean-loup Gailly and Mark Adler. [License](windows/third_party/zlib/LICENSE) · [manifest](windows/third_party/zlib/manifest.json). |
| [Unity uGUI 5.6](https://github.com/Unity-Technologies/uGUI/tree/3394d36ec68d9cba7152c4fce48b6eebaf8a941b), commit `3394d36ec68d9cba7152c4fce48b6eebaf8a941b` | The HUD's image geometry rules in `windows/core/source_image_geometry.cpp`, ported from the macOS app's port of uGUI's `Image.cs`. MIT, Unity Technologies. [Notice](windows/packaging/licenses/unity-ugui-NOTICE.txt). |
| [C++/WinRT](https://github.com/microsoft/cppwinrt) | The Windows SDK's header-only Windows Runtime projection, compiled into `EndfieldHUD.exe`, the setup and the uninstaller. MIT, Microsoft Corporation. [License](windows/packaging/licenses/cppwinrt-LICENSE.txt). |
| [Unicode Character Database](https://www.unicode.org/ucd/) | Unicode data compiled into `EndfieldHUD.exe`: QuickJS-NG's generated Unicode tables (Unicode 17.0) and the account header's generated character tables. Unicode License V3, Unicode, Inc. [License](windows/packaging/licenses/unicode-LICENSE.txt). |
| [EarTrumpet](https://github.com/File-New-Project/EarTrumpet), commit `154980811087fb7c423d806e2d9816733db6a7a5` | Adapted interface declarations for the undocumented default-audio-device switch. They compile only under a research define that no build sets, so they are not part of the app. MIT, with EarTrumpet's excluded-entities clause. [Notice](windows/native/audio_default_device_notice.txt). |

The Windows package ships `THIRD-PARTY-NOTICES.txt` next to `EndfieldHUD.exe`, in the installed folder and in the portable zip. It is generated from [`windows/packaging/third-party-notices.json`](windows/packaging/third-party-notices.json) and reproduces in full the licences of QuickJS-NG, the WebView2 SDK (with its notices), zlib, HarmonyOS Sans, the four Noto Sans fonts, Matter.js, Unity uGUI, C++/WinRT and the Unicode Character Database. It also carries the attributions for the HarmonyOS Sans SC numeral subset in the account header and for the map data. `packaging_third_party_notices_contracts` fails when a vendored folder, bundled font, licence file or licence-marked source file has no entry, or when a licence file staged into `resources/` is missing from it. The font licences, Matter.js's licence in the OrbiPom folder and the WebView2 SDK's `LICENSE.txt` and `NOTICE.txt` at `resources/licenses/webview2` still ship beside the files they cover. Batch 5 (checkpoint 128) and batch 6 (checkpoint 129) added no vendored code, fonts or licence files; batch 6 replaced Now Playing's `Windows.Web.Http` downloads with WinHTTP and reads packaged app names with XmlLite, both part of Windows.

## Map data

Terrain is a simplified derivative of NOAA NCEI's [ETOPO 2022](https://doi.org/10.25921/fd45-gt74), under CC0/public-domain terms. Country outlines use [Natural Earth](https://www.naturalearthdata.com/about/terms-of-use/) public-domain data. The map is a decorative overview, not a navigation or survey map.

[Terrain provenance](Resources/WorldMap/SOURCES.json) · [Country provenance](Resources/WorldMap/Countries-SOURCES.json)

## License

The [MIT license](LICENSE) covers EndfieldHUD's original code and assets. Third-party code, game resources and branding keep their own licenses and ownership. Their notices ship with the app where applicable.
