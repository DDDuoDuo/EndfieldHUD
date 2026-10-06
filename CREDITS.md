# Credits

EndfieldHUD is an independently implemented, Endfield-inspired macOS HUD and unofficial fan project by DDDuoDuo.

- **llynxxx** created the macOS demonstration used as the primary visual reference: [终末地充电弹窗 for mac](https://www.bilibili.com/video/BV1DBaP6yEHs/). [Creator profile](https://space.bilibili.com/628334492).
- **氰氨锗 / QinAnze** is credited for the Windows concept and its circle → supercharge banner → battery capsule sequence. The separate Windows project, [QinAnze/zmd-charge](https://github.com/QinAnze/zmd-charge), was consulted as a conceptual reference. [Creator profile](https://space.bilibili.com/413124333).
- **Arknights: Endfield / 明日方舟：终末地** and its branding belong to their respective owners. This is an unofficial fan project, not affiliated with or endorsed by the game creators or the referenced authors.
- **Ronit Singh's [FineTune](https://github.com/ronitsingh10/FineTune)** was consulted as an architectural reference for public Core Audio process taps and per-app playback routing. FineTune is GPL-3.0 licensed; its source is not incorporated into this repository. EndfieldHUD's audio engine is independently implemented.

The system overlay’s motion and visual structure use the **Arknights: Endfield** menu shown in a user-supplied reference recording dated **2026-09-26** and the extracted Watch UI resources described below. This integration branch renders the source Watch scene, materials and animation with Metal around native desktop features. The footer displays the user-supplied ENDFIELD INDUSTRIES wordmark, clipped to exclude its triangle.

The Swift application and renderer implementation, original battery icon, native vector interface and settings were newly written. No source code or assets from the referenced llynxxx or QinAnze applications were copied. Extracted game resources, including translated shader programs, are included as third-party material; no game audio or downloaded application binary is bundled. The localized charge/battery wording and staged presentation acknowledge the visual inspiration; their presence does not describe actual charging speed.

The [MIT license](LICENSE) covers the original code and assets in this repository. References, names, and third-party material retain their respective ownership; credit does not relicense another creator’s work under MIT.

## 中文说明

本项目是独立编写的 macOS 充电浮窗，主要视觉参考来自 **llynxxx** 的 [Mac 演示视频](https://www.bilibili.com/video/BV1DBaP6yEHs/)，三阶段动效概念参考 **氰氨锗 / QinAnze** 的 [Windows 项目](https://github.com/QinAnze/zmd-charge)。

系统浮层的动效与视觉结构使用用户提供的 **2026-09-26** 录屏中《明日方舟：终末地》的菜单及下述提取的 Watch 界面资源。本整合分支通过 Metal 渲染原始 Watch 场景、材质与动画，并结合原生桌面功能；页脚使用用户提供的 ENDFIELD INDUSTRIES 标识，通过原生裁剪隐藏三角背景。

应用独立音量的 Core Audio 架构参考了 **Ronit Singh** 的 [FineTune](https://github.com/ronitsingh10/FineTune)。FineTune 使用 GPL-3.0 许可证，本仓库没有引入其源码；音频引擎为独立实现。

本仓库的 Swift 应用与渲染器为独立编写，没有引入 llynxxx 或 QinAnze 参考应用的源码与素材。包含的游戏资源（包括翻译后的着色器程序）另列于下方，未包含游戏音频或其他应用的二进制文件。MIT 许可证仅适用于本仓库原创的代码与素材，不改变参考作品及相关名称的权利归属。本项目为非官方同人项目。

## Supplied wordmark

`Resources/EndfieldIndustriesSource.png` is the original user-provided branding image. Its lettering is displayed through a native clipped mask; the source is unchanged. It remains third-party branding and is excluded from the MIT grant for original assets. Rendering uses the supplied pixels directly.

## Supplied app icon presets

`Resources/AppIconSources/Perlica.png`, `RhodesIsland.png`, and `FactionAtlas.png` preserve the user-supplied artwork. The icon picker retains Endfield, Perlica, Rhodes Island, and selected faction/event emblems such as Babel, Rhine Lab, Blacksteel, Penguin Logistics, D.D.D., Kjerag and Coral Coast. All original Arknights presets are available alongside the Endfield menu artwork below and the original battery icon. Faction emblems are prepared as individual 512-pixel assets before packaging; the original atlas remains in the source repository and is excluded from the running app. Arknights / Endfield character, faction, and brand artwork remains third-party material owned by its respective rights holders and is excluded from the MIT grant. The Battery preset and the native Originium/Orundum resource-symbol drawings were newly drawn.


## Endfield UI artwork

The matching HUD navigation, app shortcut presets, and icon chooser use original UI artwork obtained from the [Arknights: Endfield Wiki Icons category](https://endfield.wiki.gg/wiki/Category:Icons) and its Menu icons and Template icons subcategories on 2026-09-27. Exact original-file URLs and SHA-256 hashes are recorded in [SOURCES.json](Resources/AppIconSources/EndfieldWiki/SOURCES.json). Files are bundled unchanged; the application sizes and tints their alpha silhouettes in memory. The game graphics are copyright **Hypergryph Network Technology Co., Ltd.** and their respective rights holders; these third-party images are **not included in this project's MIT license**. This is an unofficial fan project. The wiki's page-content license does not relicense the underlying game artwork. Controls with no matching asset in the source category retain their independently drawn action glyphs.

## Extracted Watch resources and motion

`Resources/Watch/ui_mid_ring.png` and `Resources/Watch/ui_triangle_fx.png` are the game's `ui_mid_ring` and `ui_triangle_fx` Unity Sprite images, exported from the user-supplied local **Arknights: Endfield** installation on **2026-09-30**. The exported Sprite PNGs are bundled unchanged at 1004×1004 and 143×123 pixels. [SOURCES.json](Resources/Watch/SOURCES.json) records their exact CAB files, Sprite path IDs, dimensions, and SHA-256 hashes. The app replaces their RGB colors with the selected theme color in memory while retaining source transparency.

The original native motion implementation adapts timing, easing, scale, opacity and rotation values from the game's serialized Watch UI animation data into Core Animation. The integrated Metal shell additionally loads the source scene hierarchy, component and camera data, animation clips, sprites, materials, textures and translated shader programs from `Resources/WatchSource`. The repository also retains font/glyph data, Domain and widget resources, HDR/blur programs and reference evidence. The desktop package selects its required resources rather than copying the complete reference tree. Resource identities, manifests and the rendering contract are documented in [Watch source](docs/watch-source.md) and [integration preservation](docs/integration-preservation.md).

These game resources and translated shader programs remain third-party material owned by **Hypergryph** and their respective rights holders, and are excluded from the project's MIT grant. Image geometry also uses the pinned MIT-licensed Unity uGUI reference credited in [WatchSource/NOTICE.txt](Resources/WatchSource/NOTICE.txt), which includes its license.

`Resources/Watch` 中的中环与三角形是从用户提供的《明日方舟：终末地》本地安装文件导出的原始 Unity Sprite，PNG 保持导出后的像素、方向和透明度。来源记录包含 CAB、Sprite path ID、尺寸与 SHA-256。原生动效将检查到的时间、缓动、缩放、透明度和旋转数值适配为 Core Animation；整合后的 Metal 外层还从 `Resources/WatchSource` 读取原始场景、组件、相机、动画、Sprite、材质、纹理及翻译后的着色器程序。仓库保留字体／字形、Domain、组件、HDR／模糊程序和参考证据，桌面安装包只选取需要的资源。这些游戏资源和着色器程序仍归 **Hypergryph** 及相应权利人所有，不适用本仓库的 MIT 授权。图像几何所参考的 Unity uGUI 版本及其 MIT 许可见 [WatchSource/NOTICE.txt](Resources/WatchSource/NOTICE.txt)。

## Earth terrain

The Map module bundles a generalized, offline vector derivative of **NOAA ETOPO 2022** real Earth land elevation and ocean bathymetry. Citation: NOAA National Centers for Environmental Information. 2022: *ETOPO 2022 15 Arc-Second Global Relief Model*. [doi:10.25921/fd45-gt74](https://doi.org/10.25921/fd45-gt74), accessed 2026-09-27. The source data are [CC0-1.0 / public domain](https://www.ncei.noaa.gov/access/metadata/landing-page/bin/iso?id=gov.noaa.ngdc.mgg.dem%3Aetopo_2022). The app's smaller 15-arcminute derivative is simplified for a decorative overview; it is not a navigation or survey map. The preprocessing, exact source request and asset hashes are recorded in [map-data.md](docs/map-data.md) and [SOURCES.json](Resources/WorldMap/SOURCES.json). No elevation download occurs at runtime.

Country outlines are made with [Natural Earth](https://www.naturalearthdata.com/downloads/10m-cultural-vectors/10m-admin-0-countries/), **1:10m Admin 0 Countries v5.1.1**, accessed 2026-09-27. Natural Earth's raster and vector data are [public domain](https://www.naturalearthdata.com/about/terms-of-use/). The application's user-requested map presentation groups the source China, Taiwan, Hong Kong and Macao geometries into one China plate. This derivative grouping and the original source IDs are recorded in [Countries-SOURCES.json](Resources/WorldMap/Countries-SOURCES.json), separately from the underlying Natural Earth classification. The bundle contains simplified offline vectors only.

## Update framework

Signed self-updates use [Sparkle 2.9.6](https://github.com/sparkle-project/Sparkle/releases/tag/2.9.6), from the Sparkle Project, under its MIT license and included third-party notices. The complete license is included as `Sparkle-LICENSE.txt` in the application bundle. The official binary archive is pinned by SHA-256 in `scripts/sparkle-config.sh`; the dependency is fetched at build time and is not vendored into this source repository.

## Now Playing adapter

The optional, module-scoped playback stream uses [MediaRemote Adapter](https://github.com/ungive/mediaremote-adapter), copyright Jonas van den Berg and contributors, under the BSD 3-Clause License. Source is pinned at commit `29718252613a5b0e210bdc64de0bd944ab379706`; local compatibility changes are documented in `ThirdParty/MediaRemoteAdapter/NOTICE.md`. The complete license ships as `MediaRemoteAdapter-LICENSE.txt`. Timed lyrics use [LRCLIB](https://lrclib.net/) when the player supplies no timed lyrics.

Batch6 uses the original Database (`wiki_icon`) and Adventure Book artwork from
`codex/endfield-watch-motion` for Clipboard and E-Reader. Archive retains the
previous Archive icon. Exact source paths, commit and SHA256 are recorded in
`Resources/AppIconSources/EndfieldWiki/SOURCES.json`. The two user-provided
October4 videos guide the Archive gallery/reading layout; they are not bundled.

Batches 7/8 reuse the equipment-processing (`icon_equipmake`) and calendar
(`ui_main_menu_date_btn`) glyphs from the same original-game branch at commit
`90e3a09bdd3103caebe6acb060398d394555ea07`. Only the two small PNGs are added;
the source paths and hashes are recorded in the icon provenance file above.
Media Assembly uses 24 original sticker PNGs, 14 original filter icons and 14
losslessly compacted LUT components from the user-supplied
`Endfield-PhotoMode-Assets-20261004` package. The Photo Mode screenshot references
guide its editing layout. These are unofficial fan-use game resources owned by
their respective rights holders; the project's MIT license does not relicense
them. Relative source paths, package version and checksums are in
`Resources/MediaAssembly/provenance.json`. Source scripts, scenes, videos and
reference screenshots are excluded from the app.

Closure's Minigame uses the original **Merge! OrbiPom! / 融合！山团团！**
public frontend rules and artwork from the user-supplied
`Endfield-OrbiPom-Merge-20261005` package. HYPERGRYPH and the respective rights
holders retain ownership of the game code and artwork; the application's MIT
license does not relicense them. Selected source hashes and exact declaration
ranges are in `Resources/OrbiPom/provenance.json`. Its original Matter.js 0.20.0
physics engine is by Liam Brummitt and contributors under the included MIT
license, `Resources/OrbiPom/Matter-LICENSE.txt`. Audio, account APIs, archived
web pages, Unity bundles and reference galleries are not included.
The minigame navigation icon is the unchanged `game_tool_icon` from
`codex/endfield-watch-motion`, recorded in the icon provenance file.
