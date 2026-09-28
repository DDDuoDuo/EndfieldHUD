# EndfieldHUD settings

Settings live in the HUD's four left navigation modules. Open them from the menu bar's **Settings…** item or click **System**, **Display**, **Hotkeys** or **About**. Controls and subsection changes use finite mechanical animations. Scroll within the center to reach additional rows.

## System

Language applies immediately (System, English, 简体中文). Launch at login defaults on for new preferences; installation in `/Applications` or `~/Applications` is required before registration. The status shows pending approval or installation errors rather than claiming registration succeeded. Existing explicit preferences are retained.

Close on focus loss, opening on the display containing the pointer, and ambient animation default on. With active-display selection off, the HUD opens on the primary display. Battery alerts can be disabled independently. The central battery control opens the Battery module and no longer displays a hover popup.

Restore defaults requires an inline confirmation and resets preferences, preserving notes, shelf contents, clipboard items and saved application shortcuts.

## Display

UI scale ranges from **0.2× to 2.0×**, initially **1.0×**. Dragging stages the value until release. A screen-centered recovery panel stays readable at either extreme. Choose Keep/Return within 12 seconds or the previous scale returns. Revert/Escape and closing the HUD also restore it. X and Y position controls use offsets from the default HUD placement, expressed as percentages of the current display; positive X moves right and positive Y moves down. Position and scale share the same confirmation and rollback. Unconfirmed layout values are never written to disk.

Parallax and perspective are independent intensity controls. Background brightness defaults to **37%** (63% dimming), and blur defaults to **75%** for new or restored preferences. Existing saved values are preserved. Blur amount blends the native AppKit behind-window material; macOS determines its actual blur radius. No screenshot capture or Screen Recording access is needed. See [Apple's visual-effect documentation](https://developer.apple.com/documentation/appkit/nsvisualeffectview).

Reduce Motion applies to the HUD and module actions, and also honors the system accessibility preference. Theme selects Dark, Light or System. Five accent presets and the native color wheel are available. Low Power visual mode disables decorative ambient tracks and background blur; it does not change macOS Low Power Mode.

**Time format** switches the header clock between 24-hour `22:05:54` (default) and 12-hour `10:05:54 PM`. The smaller English date below it reads `SUN Sep 27`. Both follow the existing header perspective and opening/closing animations. A retained one-second timer runs only while the HUD is visible; it stops during closing and on detachment. The next opening refreshes wall time immediately, including time-zone or date changes. Existing installations without this setting receive 24-hour time without changing other preferences.

Theme color applies to module controls, timer rings, Storage charts, navigation accents and the battery badge's hover outline. Activity Monitor graph series retain their fixed yellow/blue palette. Display also contains an **App / menu bar icon** chooser with **30 image-only choices**, including 27 original Endfield wiki assets plus Endfield, the original Battery and Perlica. Saved legacy selections remain readable; the picker offers the new game artwork. A four-column grid omits visible names while retaining accessibility labels. Choices persist and update the running application and menu bar immediately. The signed bundle's default icon is Endfield; changing the running icon does not modify its signature. Monochrome menu symbols adapt to macOS's menu-bar appearance; Perlica remains a color portrait.

Battery alert settings use complementary page masks with matched timing, keeping the outgoing and incoming text separated throughout the transition. Returning restores the main list scroll position. They have their own subsection: charging changes or always visible, duration (new default **3 seconds**), top-center/custom placement, and size. Position editing closes the HUD with animation and uses the existing draggable battery preview with confirm/discard controls.

## Hotkeys

The default summon shortcut is **Ctrl + physical backtick**. Record a replacement inside the HUD: one regular key with one or two modifier keys (at most three keys total). Escape cancels recording. Ordinary typing is never globally monitored.

The menu's **Open overlay / 打开浮层** item shows the current shortcut in the native grey, right-aligned shortcut column, using macOS modifier glyphs. Preview charging effect has no keyboard shortcut.

Validation rejects common reserved app commands, checks enabled macOS symbolic hotkeys, and tests exclusive Carbon registration. Failure retains the previous binding. Public APIs cannot enumerate every private/event-tap binding in other apps, so this is not a guarantee against every third-party shortcut conflict. No Accessibility permission is required.

## About and rename

The app is named **EndfieldHUD**. About shows version, author DDDuoDuo, repository status, MIT license and credits. Arknights: Endfield appears first, followed by the visual inspirations and audio reference. `HUDAuthor` and `HUDRepositoryURL` in `Resources/Info.plist` supply release metadata; the repository link remains unconfigured until a real URL is provided.

The bundle identifier and existing `Application Support/EndfieldCharge` storage paths are retained intentionally, preserving preferences, saved content and previously granted permissions. New builds are `EndfieldHUD.app`.

## 中文

设置位于浮层左侧的**系统、显示、快捷键、关于**四个模块。中央列表可滚动，控件与子页面切换带过渡动画。

界面缩放范围为 **0.2×–2.0×**。更改后，屏幕中央的确认栏不随界面缩放或移动；12 秒内未确认会自动恢复，Esc 或关闭浮层也会恢复。X、Y 位置按当前屏幕的百分比调整，正值分别向右、向下。未确认的缩放或位置不会保存。

默认召唤快捷键为 **Ctrl + 反引号**。可录入一个普通键和一至两个修饰键，最多同时三个键；遇到系统或已注册的冲突会保留原快捷键。新设置默认启用登录启动、失焦关闭、活动显示器和环境动画；登录启动仍需将应用安装到“应用程序”文件夹，系统可能要求批准。电池提醒默认显示3秒。

主题提供五个颜色预设和原生色轮。低功耗视觉模式仅关闭装饰动画和背景模糊，不修改系统低电量模式。恢复默认设置保留便签、文件架、剪贴板与应用快捷方式。
