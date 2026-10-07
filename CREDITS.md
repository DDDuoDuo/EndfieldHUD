# Credits

EndfieldHUD is an unofficial fan project by [DDDuoDuo](https://github.com/DDDuoDuo). [Bilibili](https://space.bilibili.com/223936961)

## Visual inspiration

- *Arknights: Endfield* supplies the HUD's visual reference, including the Watch menu, motion, typography and Photo Mode.
- [氰氨锗 / QinAnze](https://github.com/QinAnze/zmd-charge) supplied the Windows charging concept and circle → banner → battery sequence.
- [llynxxx](https://www.bilibili.com/video/BV1DBaP6yEHs/) created the earlier macOS demonstration used as a visual reference.

EndfieldHUD's Swift app and desktop renderer are independently implemented. No source code or assets from the QinAnze or llynxxx apps are included.

## Game artwork and resources

*Arknights*, *Arknights: Endfield*, character portraits, faction logos, branding and extracted game resources belong to HYPERGRYPH and their respective owners. This project is not affiliated with or endorsed by them.

The app includes selected Watch scene resources and translated shader programs, UI icons, the Endfield cursor, Photo Mode stickers and filters, and the original *Merge! OrbiPom!* rules and artwork. Game audio is not bundled. These materials are excluded from EndfieldHUD's MIT license.

Asset sources and checksums are recorded in the resource manifests:

- [HUD icons](Resources/AppIconSources/EndfieldWiki/SOURCES.json), sourced from the [Endfield Wiki](https://endfield.wiki.gg/wiki/Category:Icons) and the supplied original-game assets.
- [Watch sprites](Resources/Watch/SOURCES.json) and [Watch geometry notice](Resources/WatchSource/NOTICE.txt).
- [Photo Mode assets](Resources/MediaAssembly/provenance.json).
- [OrbiPom rules and artwork](Resources/OrbiPom/provenance.json).

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

## Map data

Terrain is a simplified derivative of NOAA NCEI's [ETOPO 2022](https://doi.org/10.25921/fd45-gt74), under CC0/public-domain terms. Country outlines use [Natural Earth](https://www.naturalearthdata.com/about/terms-of-use/) public-domain data. The map is a decorative overview, not a navigation or survey map.

[Terrain provenance](Resources/WorldMap/SOURCES.json) · [Country provenance](Resources/WorldMap/Countries-SOURCES.json)

## License

The [MIT license](LICENSE) covers EndfieldHUD's original code and assets. Third-party code, game resources and branding keep their own licenses and ownership. Their notices ship with the app where applicable.
