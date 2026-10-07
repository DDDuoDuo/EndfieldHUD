# Native desktop shell presentation

`DesktopShell` binds the fresh branch's original desktop source to native Windows
text and icons. This is first-milestone shell presentation. Every module binding
has `implemented=false`; selecting a label does not claim a module implementation.
The model creates no timers, providers, network clients, persistence or account reads.

Source authorities are `HUDNavigationTarget.swift` (`HUDDesktopWatchNavigation`),
`HUDModule.swift`, `HUDNavigation.swift` (`gameIcon` and `iconPath`),
`EndfieldGameIcon.swift`, `Localization.swift`, `LocalizationCatalog.swift`,
`HUDSourceDesktopNavigationLayout.swift`, `HUDSourceWatchView.swift`, and
`SystemHUDView.swift`. `Document::loadDesktop` owns profile mounting, source hiding,
authored animation/layout and bounded physical navigation row recycling.

The first four authored buttons bind System, Display, Hotkeys and About.
TechtreeBtn binds Storage; ReportBtn binds Activity Monitor. The right pool remains
row-major: Notes / Temporary File Shelf, Clipboard Cache / Archive,
Media Assembly / Closure's Minigame, Now Playing / Volume, Projection / E-Reader,
Work Mode / Calendar, Map / Event Log, Personal Profile / Account Linking,
Power / Add App. The logical assignment comes from the submitted Frame, so source
geometry, icons, labels and hit regions use one current scroll sample.

Approved PNG names follow the original `gameIcon` mapping under
`AppIconSources/EndfieldWiki`. Action glyphs retain the exact Swift silhouette
geometry and even-odd holes. Curves are flattened to bounded native quads with
less than 0.003 icon-unit error. The Report glyph retains its original texture,
mesh and UV coordinates, rendered as a white alpha template. There is no invented
generic icon or unrelated game category substitution.

Integrate in this order:

1. Construct `DesktopShell` from `Document::loadDesktop`.
2. Supply `DesktopShellFixture`, including the same raw PlaybackSample phase and
   phaseElapsed, and closingCanvasOpacity captured when close interrupts a fade.
   The caller provides its clock strings and registered shortcut label.
3. Pass `sourcePresentation(fixture)` to `FrameInput::desktopPresentation` before
   building the source frame. Then call `decorate(frame, fixture)` after every
   rebuilt frame. Pointer-only updates use `Document::reproject` in place;
   native overlays retain their node-local geometry and the footer's fixedWorld.
   For current hit planes, read submitted Graphics' updated world matrices;
   DesktopShellPresentation metadata describes the last full rebuild.
4. Render appended `DesktopText`, `DesktopIcon`, `DesktopVector` and
   `DesktopSourceIcon` using normal alpha. Retain source quads, current world and
   sceneWorld matrices, masks, sorting order and inherited alpha. Decoration is
   idempotent and preserves Document's source nodes/hit regions.

Native text properties in sampledProperties are `desktop.textAlignment`
(0 left, 1 center, 2 right), `desktop.textVerticalAlignment` (0 top),
`desktop.textWrap`, `desktop.textTruncate` (0 none, 1 end), `desktop.fontWeight`,
`desktop.textFit`, `desktop.minimumFontSize`, and `desktop.fontSizeStep`.
`desktop.fontFamily` is0 for the native UI fallback and1 for the native monospace
fallback. Header/subtitle/footer/date retain the source monospace contract;
the renderer currently chooses Consolas on Windows. Digital time additionally
sets `desktop.monospacedDigits=1` for DirectWrite tabular figures. Both time/date
are right-aligned as in `HUDClockStyleArtwork.update(.digital)`. These Windows
font choices do not establish matching Mac font metrics.
The source fitter measures width-2 and height-2, reducing font size by0.5 to10.
English shelf/clipboard/profile/activity captions keep the source line breaks;
Japanese shelf uses two lines, Japanese/Korean shelf planes expand to124x56,
and Chinese shelf uses the source unwrapped20-point maximum.

PNG templates use `desktop.iconAlphaCrop=8` and `desktop.iconTint=1`. The renderer
normalizes to96 once, crops alpha>8 once, then aspect-fits the visible26/32 box.
The original file is immutable. Work Mode and Storage retain artwork scale0.9.
PNG UVs explicitly use top-left row origin. `DesktopSourceIcon` uses original
Unity texture UVs and does not apply the PNG aspect-fit/crop pipeline.

The header shares the calibrated MiddleDecoNode plane: 1000x640 design points,
ENDFIELDHUD at270,2 / 300x27 /20pt and SYSTEM INTERFACE at270,33 /300x18 /9pt.
The source BannerNode remains hidden; only its528.28x122 transform anchors the
native clock plate, inset86.28 and8 points. Its12-point rounded inner plate has
the original gray1-point stroke,17-point rounded outer edge has the accent1.5-point
stroke, and all five49x3 indicator lines plus the selected digital accent remain
at the source's131-point panel baseline. The supplied digital clock occupies
296x39 at108.28,28 /32pt and date296x20 at108.28,69 /13pt. The close hint lives in
the original untransformed screen/design footer at275,622 /450x18 /9pt.
The original Endfield source wordmark remains untouched. Native canvas alpha
uses the source finite(.20,.72,.22,1) Bezier: opening delay0.20 /duration0.24;
closing delay0.35 /duration0.06. It adds no second fold or repeated easing.

The default name/authority retain fresh `UserProfile` defaults Endministrator/60;
the isolated empty UID displays a clear dash and never impersonates account data.
Clock/date inputs are caller-owned and source-formatted HH:mm:ss / EEE MMM d.
Default accent is source FAD41F and default highlighted Power is presentation only. Profile fields
bind the original compact card nodes; no account or profile storage is consulted.
The original Seraph game character UIImage is explicitly replaced on its authored
PlayerHead plane by `HUDPortraitArtwork.makeLayer(image:nil)`'s gray plate and
exact ellipse/cubic silhouette. Source hit regions and avatar frame remain intact.
All four source profile accent decorations (levelSlider, headFrameImg, IconRight,
ArrowImage) receive the source accent before the frame is sampled. Their IDs are
resolved once and do not introduce pointer-time scene scans. Immutable portrait
and clock vector triangulation is cached by a fixed set of canonical skin keys.
The clock is a digital first-gate presentation; alternative clock styles, clock
controls, profile editing/imported photos, saved-app navigation, hover texture synthesis,
live status providers and module surfaces remain unfinished.

`desktop_shell_tests` covers canonical module order, localized identities,
approved paths, source node/irregular-hit preservation, row assignment, finite
native fade, fixture profile fields, idempotence, plane projection and the source
footer's stationary behavior under gyro. These contracts are not evidence of
live Windows recording, native CJK IME, Narrator, Mac font metrics, complete
source visual parity, mixed-DPI hardware or animation/performance gates.
