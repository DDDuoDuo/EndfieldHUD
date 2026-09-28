# Shared System HUD / 共享系统浮层

## Scope and use

The summonable **System HUD** preserves the existing compact charging HUD and adds a shared mechanical shell, persistent navigation, and one center content host. It uses native AppKit/Core Animation and the existing battery source.

The navigation contains **16 built-in modules**, plus saved app launch tiles, in these groups:

| Group | Modules |
| --- | --- |
| Left | System, Display, Hotkeys, About |
| Right | Notes, Temporary File Shelf, Clipboard Cache, Volume, Work Mode, Event Log, Map, saved apps, + Add App |
| Bottom | Storage, Activity Monitor |
| Center, above sectors | Power / Device Battery |
| Lower left | Quit confirmation, Personal Profile |

**All 16 modules are functional.** [Map](map.md) provides an offline terrain map with raised country plates and saved pins. [Notes](notes-canvas.md) provides a persistent spatial canvas for text, checklists and images. [Temporary File Shelf](file-shelf.md) retains Finder references with native dragging and Quick Look. [Clipboard Cache](clipboard-cache.md) keeps the latest 10 copied items as compact rows with pinning and copy-back. [Volume and Work Mode](audio-and-work-mode.md) provide supported hardware controls, audio activity, a countdown dial and a stopwatch. Volume also includes inline [per-process audio routing](per-app-audio.md) on supported macOS 14.2+ devices; see that guide for the confirmed listening test and remaining device limits. [Event Log](event-log.md) keeps up to 500 local action and selected system-event records, with newest-first scrolling, category filters and Clear. [Storage and Activity Monitor](storage-and-activity.md) use report rows with yellow/cyan area charts. Storage shows startup-filesystem capacity and hands detailed inspection to macOS Storage Settings. Activity samples CPU/memory/network/disk once per second while visible, retains its recent history and reduces system sampling to once every five seconds while hidden. Its Apps view provides a scrollable, sortable list of available app resource readings. The first opening defaults to Map; later openings retain the last section. [App shortcuts](app-shortcuts.md) save chosen applications with a custom name and original or preset icon, then hand off only after the HUD has closed. [Personal Profile](personal-profile.md) stores local identity, images, levels, counters and cumulative Work Mode hours. [Settings](settings.md) implements the four left entries inside the same retained center architecture. Navigation and center-content switching work across all 16 built-in entries; saved app tiles launch applications without becoming new center-content modules.

Open it from the menu bar or the configured summon shortcut. **Open overlay** displays that shortcut beside its title; **Settings…** opens System in the same HUD. The central battery badge opens Battery. The lower-left power button asks for confirmation, then completes the closing animation before quitting the app.

Press the configured summon shortcut again (Ctrl + backtick by default), click the empty dimmed background, or switch focus to another application with Close when focus lost enabled to close. Normal closing requests play the retraction animation before the panel disappears. The lifecycle is `closed → opening → open → closing → closed`; repeated presses and held-key repeats must not create duplicate windows or overlapping transitions. Sleep, session suspension and process termination use cleanup paths so hidden animations cannot remain running.

## Motion reference

The supplied reference video is the motion reference. These are approximate observed time anchors, not a claim of frame-perfect reproduction:

| Reference point | Observation |
| --- | --- |
| About 3.70 seconds | Opening becomes visible. The elliptical frame grows and tilts upward. |
| Following deployment | Circular segments and rings appear, followed by structural panels from top to bottom. |
| About 4.03 seconds | The central core becomes visible, roughly 0.33 seconds after opening starts. |
| About 4.23 seconds | Panels settle and status elements appear last; the opening sequence spans roughly 0.53 seconds. |
| Closing, roughly 0.30 seconds | Content disappears, panels retract, then the elliptical structure shrinks away. |

The current implementation deploys in approximately 0.55 seconds and retracts in approximately 0.40 seconds. These finite transitions remain separate from the slow mechanical rotations, scan, glow, and other ambient tracks that run while the HUD is open. Content refreshes independently of animation frames. The app does not drive a continuous frame callback or poll battery data to animate the scene.

## Rendering and motion ownership

`SystemHUDView` owns the shared 2.5D shell. `HUDMechanicalArtwork` supplies distant, rear, secondary, frame, inner, marker, glass and foreground rim groups; `HUDNavigation` supplies the persistent navigation. They remain attached while `HUDModuleContent` changes the center display. `HUDDepthPlane` wrappers separate their motion:

| Layer | Owner and responsibility |
| --- | --- |
| `deployment` | Finite opening and closing transforms, owned by the overlay view. |
| `spatial` | Bounded perspective and pointer response, owned by `HUDMotionController`. |
| `content` | Artwork and independent ambient animation targets. |

Pointer response is measured within a 320 × 240 design-point radius around the HUD, rather than across the entire display. The camera uses a 950-point perspective distance. The instrument has a mild resting attitude of **5° pitch, −4° yaw and −1° roll**. Bounded pointer pitch/yaw is added relative to that pose; signed foreground/rear travel and pointer-relative depth translation remain separate. Pointer easing starts with positive velocity; repeated movement therefore continues to respond instead of repeatedly restarting a slow ease-in. Foreground travel is stronger than rear counter-motion, and the projected attitude stays bounded.

Before opening, the attached view converts the current screen-space pointer into its local design coordinates and seeds every spatial plane. Pointer response continues during opening and closing, and promotion to the fully open state preserves the current pose. An unmoved cursor already tilts the HUD, and reopening samples its current position. Detached previews remain centered; Reduce Motion stays flat.

The opening holds a broad inclined ellipse before lifting upright, with the central readout appearing later. Closing folds the layers into an ellipse before shrinking. The larger rim uses a translucent gray chassis and **five stationary white arcs wrapping the rim: two on the right and three on the left**, with flat ends and gaps between the spans. They draw in a foreground rim group above the chassis and connector strands, with 78% stroke opacity. That group shares the central content’s full foreground perspective and pointer response (depth 54, travel 24), while retaining its ring deployment timing. A 208-point radius preserves its resting footprint at the nearer depth. The left spans have 3° gaps; the right gap remains 7°. Ambient rotation preserves their grouping. A thin accent arc and three floating triangular markers retain independent motion. Each opening randomizes the markers' starting positions and independent 28–64-second orbit periods, with both directions represented. Static child rotations preserve those positions during opening and Reduce Motion. Equal-sized square dots form a regular Cartesian grid inside the well; crosses and connecting traces sit behind the content. The rear sidewall, recessed well, selective bevels, softened outer grid and curved navigation backing panels retain depth without dense ticks, bolts, or tiny labels.

Separating those layers prevents pointer movement or a rotating ring from replacing the deployment transform. Pointer events retarget 60–120 ms interpolations from each plane's current presentation pose, retaining different travel and response by depth. The pointer-side edge rises toward the viewer, with restrained angles to keep controls legible. All thirteen spatial-plane updates share one Core Animation transaction. Native button bounds reserve the full motion envelope during layout; exact hit testing and accessibility frames use the current projection without resizing buttons on every pointer event. The left, right and lower-left navigation participates in the panel depth plane; the two bottom sectors instead share the central core depth plane. Each accessible button and exact pointer hit follows its own plane and face expansion. The English title, subtitle, Work Mode badge and lower wordmark follow the core depth plane.

Ambient tracks start only after the opening transaction finishes and the attached view becomes interactive. Updating battery data, layout, or module selection does not rebuild the shell or add another set of ambient tracks. Closing freezes ambient rotations while event-driven pointer response continues through the deployment wrappers. Normal close, forced shutdown, and view detachment remove the remaining animations; the controller releases the System HUD when closed. The System HUD and compact charging HUD share one panel and one battery stream. Work Mode owns one separate timer-progress track only while its page is active and running; it stops on pause, completion, module changes and HUD closure. The session itself survives closure. Its continuous track is counted separately from finite deployment, bounded action feedback, and the ten shell ambient tracks.

## Navigation appearance and text

The video reference uses pale silver tiles with centered dark icons and labels. Each card has one thin translucent underplate offset eight points sideways and ten points downward. Side tiles use a detached neutral outer outline with an approximately three-point gap from the filled face, instead of an inset frame. Navigation labels are bold. Four larger left tiles follow the ring. Each left card owns five longer gray connector strands extending from beneath its right edge toward the center. They share the stable card base and common panel perspective, with a short fade at their inner ends so they blend into the rim during tilt. The old artwork-owned connector bundles are removed. Restrained local slant and rotation avoid compounding the shared perspective.

Right tiles follow the opposite curve in two columns within a taller 370-point viewport. A retained vertical alpha-gradient mask feathers the upper and lower scroll edges without a whole-canvas blur. Right cards measure 78 × 74 points, with a 90-point row pitch and 84-point column pitch; left tiles use a 95-point pitch to leave clearance between the detached outlines. Trackpad and mouse-wheel input move the right strip continuously, with elastic overscroll and a finite rebound instead of page snapping. Finger release starts one rebound; the same gesture's remaining momentum cannot interrupt or restart it. Inertial scrolling that reaches a boundary settles immediately. A new finger gesture can interrupt the rebound.

Up/down controls provide small nudges. Their mirrored double chevrons have a faint offset echo and a long thin baseline while retaining the existing hit rectangles. Selection reveals an offscreen entry before expanding it. Native buttons reserve the full curved scroll envelope, including expansion and elastic travel. Per-sample motion does not rebuild those bounds; only visibility/arrow-state changes and finite-animation completion notify the host. Exact hit testing and accessibility use each card's current visible portion; module identities and groups stay unchanged.

Storage and Activity Monitor are mirrored theme-colored sectors **inside the lower central circle**. Their 144 × 65-point design rectangles begin at `(350, 463)` and `(506, 463)`, separated by a 12-point seam. The lower edges follow the 208-point central radius. Each sector has a muted translucent theme-colored face, a brighter lower band with a straight upper edge that tapers toward its outer tip, two fine borders and sparse technical marks. Both sectors use the bundled factory icon: left of the Storage label and right of the Activity label. Only the icons cast a subtle shadow. Both labels use 11.5-point text; English Activity Monitor wraps to two lines. Selection and hover animate their theme-colored highlight without changing position or size. The `bottomLayer` belongs to the central core plane so its depth and pointer travel match the instrument. Hit testing uses the curved outline; transparent corners are not buttons.

The battery control sits immediately above the sectors at `(500, 448)`. Its expanded capsule is 168.96 × 24.32 points; the main HUD and module sizes remain unchanged. It reuses the notification renderer and real capacity/percentage readings. Its original circle → supercharge → compact entrance begins 0.58 seconds after the shell starts opening. The compact state stays for a fixed three seconds after that sequence finishes, then collapses to a circle. Hover expands it with a theme-colored edge and unchanged background; pointer exit returns it to the circle once the initial hold has elapsed. Hit testing follows the visible morph, with a small hover allowance to prevent edge jitter. Clicking opens Battery. There is no hover popup. Closing carries the badge inward with the dial's folding transform while completing its circle → nothing sequence. It shares the core’s perspective and pointer range on a retained independent plane.

Opening and closing add brief irregular local signal flicker across mechanical elements, navigation cards, readouts, notes, the name card and the badge. A vertical sweep orders the interruptions from top to bottom when opening and bottom to top when closing. Lower elements remain invisible until the opening sweep reaches them. Closing holds each element invisible after its turn, with parent layers preserved until the sweep completes. Pinned notes use their individual screen positions. Individual pulse timing remains irregular; the dimmed background and blur remain smooth. Core Animation runs these finite tracks without timers or frame callbacks. Closing/cancellation removes them, and Reduce Motion skips them.

Module actions use retained hover/press layers with theme-colored tint and an outline. Cut-corner controls have a detached surrounding frame; disabled and clipped controls cannot highlight. The same system covers toolbar actions, object controls, settings, tabs and sortable table headings. Work Mode's countdown and module controls follow the theme; Activity graphs retain yellow and cyan, and warning colors retain their meaning.

The lower-left red striped power control opens an in-HUD quit confirmation. Cancel retains the current section and window; confirming retracts and cleans up the HUD before requesting application termination. Beside it, the compact card opens [Personal Profile](personal-profile.md), sharing the saved name, portrait crop, frame, card accent and authority level. Local profile defaults include a generated UID, editable levels and installation date.

The lower **ENDFIELD INDUSTRIES** wordmark displays the supplied reference lettering through a native image viewport, keeping the source image intact and excluding the triangular artwork. It shares the core plane with the sectors and adapts its lettering color to the theme.

The panel plane retains its shared 3D perspective. Each side card separates its scrolling base, translucent backing, face and lettering. Selection uses one **0.22-second eased expansion** to 1.045× scale, with a smaller 1.012× backing expansion. Hover adds no scale: it gently lifts the backing, moves the face farther forward and gives the icon/text a 6.8-point total lift compared with the face’s 3 points, while keeping them within the face. Right backings retain their leftward offset. The side glow and detached outline use neutral white; the icon ring follows the theme on hover or selection. The highlight stays steady without blink or opacity tracks. The bottom sectors stay fixed and use a stronger theme-colored highlight. Native click testing follows the face, and reserved interaction bounds accommodate its movement. Interrupted poses begin from their current presentation; generation-checked cleanup removes finite tracks. Closing clears hover ownership and animations. Reduce Motion applies the final pose immediately.

`HUDRenderScale` applies one resolution policy to every glyph layer. It compensates for perspective enlargement with `ceil(baseScale × 1.35)`, bounded to 4 pixels per design point, while artwork retains its normal backing scale. This follows [Core Animation's contentsScale mapping](https://developer.apple.com/documentation/quartzcore/calayer/contentsscale). Navigation and center wrappers avoid group-opacity surfaces; pointer input does not rerasterize text or resize native controls. Section selection refreshes only section presentation, not unrelated battery readouts.

## Module content and switching

`HUDModuleContent` retains one **440 × 440-point** center host with its original local design frame `(280, 100, 440, 440)`. Ordinary modules preserve their **400 × 334-point** canvas coordinates, but the host is displayed at 0.86× scale around `(500, 294)`, leaving space for the battery capsule and inner-circle sectors. Work Mode uses a full-size 1× presentation around `(500, 320)`, preserving the actual 430-point timer diameter. Pointer input inversely applies the same translation and scale; projected editors and accessibility controls use its forward mapping. The existing live Power layer is injected into the host; the other modules are registered through `HUDModuleContentFactory`. The four settings modules each retain their own content factory, so transitions between settings pages preserve both outgoing and incoming artwork without rebuilding navigation or the mechanical shell.

Notes retains its heading and direct-add buttons in that center host, while cards use a separate screen-sized workspace above the shell. A retained Notes depth plane shares the side buttons' depth, pointer travel and response timing; an inverse scale/origin wrapper preserves existing screen-point geometry. Input uses the displayed inverse perspective, and accessibility/editor rectangles use forward projection. Direct manipulation freezes the pose, with native text input kept upright while editing. Text/TODO buttons add immediately; Image opens a chooser. Cards can move beyond the circle, and pinned cards remain visible and editable on other sections without locking their geometry. Deletion exposes a compact ×/✓ confirmation below the note. Smaller-screen display clamping preserves saved geometry. Note actions have finite depth/reveal animations cleaned up with the overlay.

File Shelf accepts drops onto its right navigation tile from any section, then switches to the shelf after adding the references. Shift-click selects a card range for one native multi-item drag. Outgoing dragging plays the HUD closing animation; a cancelled drag restores the same HUD with its opening animation. Source files and shelf references are retained.

A center-content selection takes approximately **0.30 seconds**: six staggered, beveled shutter masks reveal the actual incoming content as the outgoing screen retracts. The incoming surface makes two small alignment corrections in position and shear before settling. Fine neutral seams follow the reveal frontier; there are no filled interference bars or whole-screen opacity flashes. Left, right, and bottom groups enter from their corresponding sides; changes within a group follow its vertical order. Side navigation selections expand smoothly, bottom sectors change their highlight, and the frame makes a small indexing movement. The HUD entrance does not replay.

Only the current and incoming screens coexist during a swap; one remains afterward. A newer selection replaces the pending destination, and generation guards reject stale completions. Closing cancels the swap and removes its incoming layer and temporary registration seams. Completion and immediate settling also remove those seams and their animation keys. These are lifecycle rules, not new test or performance claims.

Work Mode keeps its clock and compact controls inside a 430-point-diameter local dial, displayed above the inner-circle navigation. It offers 5/30/60-minute presets and Custom, initially 30 minutes. Running sessions provide Pause + Reset; paused sessions provide Resume + Reset. Reset ends the session and clears progress, restoring the configured countdown duration or a zero stopwatch. The theme-colored remaining arc starts full and its endpoint moves counter-clockwise as time expires. Work Mode buttons use a single eased expansion, while timer changes retain their clipped transition; they do not recreate the window or use a generic crossfade. Volume's inline choices likewise use bounded mechanical transitions.

Volume's detailed hardware/process metadata listeners stop when its page is hidden. Event Log retains a separate device-list listener for connection changes. An explicitly enabled per-process mixer has a separate app-session lifetime: it continues while the HUD is hidden and at 100% unity gain, then stops on sleep/session suspension, quit, or relevant route/process changes. An idle 100% slider does not start a route. It captures only the selected process through public APIs without saving audio. See [per-app audio](per-app-audio.md) for its strict device constraints, the user-confirmed Bilibili attenuation test and remaining hardware checks.

Event Log uses the ordinary retained center canvas and existing mechanical section transition. It records real app actions plus selected battery, audio-device and display changes. Initial snapshots establish baselines without inventing startup connection events. Its bounded local file excludes clipboard/note contents and full paths; [the Event Log guide](event-log.md) describes storage and Clear behavior. Hiding the page removes its drawing observer while the app-session recorder continues.

Add App accepts one `.app` from its chooser, a Finder drop or a copied application pasted into the module. Selection creates an editable draft with the application’s name and icon; Save persists a bookmark, display name and original-icon or vector-preset choice. Cards offer launch, edit and remove. Saving adds a tile after Event Log and existing saved apps, with **+ Add App** always last; its chosen icon and name update in place when edited. Preset choices are icon-only, with names retained for accessibility and extra helper captions omitted. The native chooser and temporary name field use the existing projection and input locking; the center shell remains attached. A launch request resolves the saved target before retracting the HUD, then calls public AppKit application APIs after teardown. Running targets are activated at their selected installation; launch failures return to Add App with an error. Successful handoff records only the shortcut’s display name in the Navigation log. See [App shortcuts](app-shortcuts.md) for storage and controls.

Storage reads capacity for one startup filesystem rather than adding shared APFS volumes together. Total, used and presently unallocated capacity appear in three compact report rows. Capacity results are cached for a minute; the refresh icon requests only a new capacity read. **Storage Settings** first retracts the HUD, then asks LaunchServices to open the macOS storage panel. Older supported systems use Storage Management, with System Information as a fallback. macOS owns the detailed category analysis; opening this module does not enumerate user folders or scan the entire disk.

Activity's overview retains up to 60 system snapshots across HUD closure and recreation. A utility queue reads statistics at 1 Hz while the module is visible and 0.2 Hz while hidden; sleep, session suspension and quit stop sampling. CPU, memory (including a separate compressed-memory readout), upload/download and disk read/write use explicit units; unavailable readings remain gaps. The graph's 60 vertices keep a fixed interpolation topology, while horizontal positions use actual elapsed sample time. Core Animation interpolates paths independently of the statistics cadence. No frame callback acquires system statistics.

The Apps subview adds app icons, names, CPU, memory, network and disk rates, with continuous vertical scrolling. Its five column headings are the sorting controls, with an arrow on the active column and no separate sort toolbar. CPU descending is the default; Network and Disk sort combined directional rates while the rows retain both readings. Rows are reused within a bounded viewport. App CPU follows Activity Monitor's per-logical-CPU convention and can exceed 100%. App statistics and the bounded public `nettop` stream run only while Apps is visible; hiding that subview, switching modules or closing the HUD stops them. Inaccessible process counters and unavailable intervals show `—`, not invented zeroes. System history remains separate from these app-detail lifetimes.

Activity Overview/Apps and Event Log categories use `HUDSubsectionHandoff`: complementary masks share one continuous moving boundary while the retained pages move beneath it. The outgoing page remains until the incoming page covers its pixels, avoiding gaps and overlapping text. Four finite tracks are canceled on interruption, hiding or Reduce Motion. Graph history and sampling cadence remain unchanged. `HUDSubsectionTransition` continues to give Clipboard/Shelf pages a 0.26-second directional mask reveal. Add App list/draft editing has its own finite shutter/depth transition that delays native name-field placement until the reveal completes.

**Reduce motion** disables ambient movement and pointer depth, flattens the common spatial transform, and omits selection expansion and section registration effects. The view observes changes to that macOS preference and settles an active deployment or retraction before completing its lifecycle callback. Detached previews render a static pose without starting ambient tracks.

The graphical lifecycle checks distinguish finite deployment keys from ambient and pointer keys. An open view may have ambient animations; a closed view must have none. These checks establish lifecycle behavior, not a CPU, GPU, or memory measurement.

## Ctrl + backtick shortcut

The default binding is **Ctrl plus the physical ANSI backtick key**, key code 50. The app registers the configured chord with the standard Carbon global hotkey API, `RegisterEventHotKey`. The HUD’s Hotkeys module can record one ordinary key plus one or two modifiers, up to three keys total. Escape cancels recording. Validation checks common reserved commands, enabled macOS symbolic hotkeys and exclusive Carbon registration; a rejected binding preserves the previous setting. Other applications’ private event-tap bindings cannot all be enumerated.

The shortcut works while text fields and editors have focus. While EndfieldHUD is running and registration succeeds, the registered chord opens or closes the overlay. The unmodified backtick key remains ordinary input. No Accessibility permission or setup in macOS privacy settings is required.

Secure Input and system-reserved contexts may prevent delivery of the global shortcut. Registration can also fail when another application already owns the same chord. Use the menu bar whenever the shortcut is unavailable.

The app receives its registered hotkey events; **it does not log keystrokes, read text contents, or inspect focused controls through Accessibility APIs**. It does not inject or replay characters. Repeated hotkey events are handled by the overlay lifecycle so a held chord cannot create duplicate windows or overlapping transitions.

## Battery and connected-device limits

The Power view shares the existing host battery snapshot: charge percentage, charging state, external-power connection and matching remaining/full-charge capacity when available. Unknown values stay unavailable; battery health is not inferred from a remaining/full-charge ratio.

`DeviceBatteryProvider` currently adapts **only this Mac's battery**. It does not enumerate or connect Bluetooth peripherals and does not request Bluetooth permissions. AirPods, headphones, iPhones and other accessory percentages are not fabricated or obtained through private APIs. Such readings require a separately implemented provider with reliable public-API data; the row model supports that future extension, but this release does not provide those readings. A desktop Mac is reported as having no internal battery rather than being given a sample percentage.

## Compatibility and validation

The build targets remain **macOS 10.15.4 for Intel** and **macOS 11 for Apple silicon**. These are deployment targets, not proof of testing on every supported OS/device or a promise of compatibility with every historical Mac. Test reports, including focus changes, repeated transition cycles and historical resource measurements, belong in [TESTING.md](../TESTING.md). Previous compact-HUD and static Power measurements do not measure the current 2.5D renderer.

Shortcut-policy tests alone do not verify hotkey registration, text-field behavior, or an end-to-end global shortcut cycle. Consult the recorded test coverage before treating these as verified.

For iteration, use `./scripts/dev.sh` and checks relevant to the changed behavior. It builds the native app at `build/dev/EndfieldHUD.app` without launching it or replacing the universal app. Release packaging and CPU/RSS measurement rounds are separate work performed only when explicitly requested; see [DEVELOPMENT.md](../DEVELOPMENT.md).

## 中文使用与限制

系统浮层保留原有充电浮窗，并使用共享机械外壳、常驻导航和中央内容容器。共有 **16 个内置模块**，另有已保存应用的启动图块：左侧为 **系统、显示、快捷键、关于**；右侧为 **便笺、文件暂存架、剪贴板、音量、工作模式、事件日志、地图、已保存应用、+ 添加应用**；底部为 **存储、活动监视器**；中央底部胶囊为 **电源 / 设备电量**；左下角为 **红色退出确认按钮和个人名片入口**。

**电源、便笺、文件暂存架、剪贴板、音量、工作模式、事件日志、存储、活动监视器及添加应用已提供实际功能**。便笺支持文字、待办和图片的空间画布及本机持久存储。文件暂存架保留文件引用，剪贴板保留最近 10 个复制项目。音量支持公开 API 可控的硬件操作，以及 macOS 14.2+ 上需手动启用的[实验性独立进程混音](per-app-audio.md)；应用音量可通过行内滑块直接调整；用户已确认本机哔哩哔哩的实际音量衰减；其他设备和异常恢复仍待验证。工作模式提供中央倒计时圆盘与秒表。事件日志保留最多 500 条本机操作与选定系统事件，支持最新在前、分类筛选和清空。存储显示启动文件系统容量，并通过系统存储设置查看详细分类；活动监视器可见时每秒读取一次 CPU、内存、网络和磁盘统计，隐藏时降为每五秒一次，保留最近历史。应用视图提供带图标的可滚动、可排序资源列表，图表独立平滑插值。每次启动应用后首次打开浮层默认进入地图，之后重新打开恢复上次的模块。[添加应用](app-shortcuts.md)支持拖入／选择应用、自定义名称和十三种内置图标预设；点击快捷方式先完成关闭动画，再启动或激活应用。左侧的[系统、显示、快捷键、关于](settings.md)均已实现，保留在同一浮层中；16 个内置模块、所有导航入口及中央内容切换均已接入。

通过菜单栏或可配置的召唤快捷键打开界面；菜单栏“设置…”直接打开浮层内的系统模块。入场结束后保持打开；中央电量胶囊延后入场，完整显示三秒后收为圆形；悬停展开并显示主题色边缘，背景保持不变，移开后再次收起。点击打开电池菜单，原悬停弹层已移除。左下角红色电源图标打开退出确认，确认后先完成收回动画并清理浮层，再退出应用。个人名片支持本机身份、头像与背景、等级、计数和工作模式时长，并与左下角卡片同步。再次按召唤快捷键（默认 Ctrl + 反引号）、点击空白背景或在启用“失去焦点时关闭”后切换到其他应用时，先播放收回动画，再移除浮层。连续按键和长按重复不能创建重复窗口或重叠动画。

参考视频中的展开约在 **3.70–4.23 秒**，总计约 **0.53 秒**：椭圆框架放大并向上倾转 → 分段圆环展开 → 结构面板由上至下出现 → 中央核心约在 **4.03 秒** 出现 → 状态信息最后显示。收回约 **0.30 秒**：内容先隐藏，面板收回，椭圆结构缩小消失。时间为视频观察的近似值，不表示逐帧完全复刻。

展开前读取当前指针的屏幕位置，转换到浮层设计坐标并应用到各透视层。环境动画开始时再次读取，不必先移动鼠标即可倾斜，重新打开也会更新位置。离屏预览保持居中，减弱动态效果时保持平面。

当前实现的展开约 **0.55 秒**，收回约 **0.40 秒**。展开、指针视差和环境运动分别由独立图层管理。整个仪表静止时保持较轻的 5° 俯仰、−4° 偏航和 −1° 侧倾，指针变化叠加在该姿态上。灰色框体的重叠图层降低不透明度，五段固定白色弧线沿圆环两侧分布（右侧两段、左侧三段），端部平直，段间留空。弧线置于最前方的圆环图层，位于框体和连接线束之上，不透明度为 78%；该图层跟随中央内容层完整的前景透视和指针运动（深度 54、位移 24），保留圆环的展开时序；208 点半径补偿较近的深度，使静止大小保持一致。左侧段间距缩至 3°，右侧仍为 7°。它们独立于旋转层，不会随环境旋转改变分组。细黄色弧线和三个悬浮三角标记仍可独立运动；背后保留圆点、十字、侧壁、凹陷内层与柔和外网格。

卡片只保留一层薄而半透明的底板，向侧面偏移 8 点、向下偏移 10 点。侧面按钮的边框位于填充面外，与按钮约留 3 点空隙，不再使用内部边框；导航按钮文字加粗。左侧四块卡片沿曲线排列，每块右缘连出五条加长的灰色线束；线束属于按钮稳定的底层，与按钮共享视差，朝圆环的一端渐隐，以减少倾斜时的断开感。旧的独立线束已移除。

右侧沿圆环排列为双列，保留 370 点高的视口，按钮缩为 78 × 74 点，行距 90 点、列距 84 点；左侧行距为 95 点，为外置边框留出间隔。右侧连续滚动，手指释放或惯性抵达边界时开始一次回弹，剩余惯性不会重启弹簧；新手势可立即接管。上下边缘使用固定渐变遮罩柔和淡出，不对整个画布模糊处理。上下双箭头互为镜像，带微弱偏移影和长细基线，原有点击区域不变，仍按像素微调。

存储和活动监视器为**中央圆盘内下方**的镜像主题色扇形按钮，设计矩形分别为 `(350, 463, 144, 65)` 与 `(506, 463, 144, 65)`，中央留 12 点间隙，下缘贴合半径 208 点的圆弧。柔和的半透明主题色卡片面叠加较亮的底条，底条上缘为直线，在外侧尖端收窄；另有细双边框与少量技术标记。两者使用内置工厂图标，分别位于存储标题左侧和活动监视器标题右侧，仅图标带轻微阴影。两个标题字号统一为 11.5 点，英文 Activity Monitor 分为两行。选中与悬停只改变主题色高亮，不改变大小或位置。独立的底部导航层共享中央核心的视差与深度；弧线外透明角落不接收点击。其他导航仍属于侧面板层。

下方 **ENDFIELD INDUSTRIES** 字标通过原生图层视口显示用户提供的参考字样，源图保持完整，三角形图案位于视口之外。字标随主题换色，与中央核心共同倾斜。

侧面导航将稳定底座、半透明底板、卡片面和文字图标分层。选中时以 0.22 秒平滑放大到 1.045 倍，底板仅放大到 1.012 倍。悬停不再放大，而是轻微向前突出：底板移动最少，卡片面更多，文字图标总计上移 6.8 点，比卡片面的 3 点更明显，但始终留在卡片内；右侧底板仍向左延伸。侧面高亮和外置边框使用中性白色，选中或悬停时图标光环使用主题色。高亮保持稳定，不再闪烁，也没有透明度动画。底部主题色扇形保持位置和大小，仅加强高亮。点击区域跟随实际卡片面并预留移动范围；连续操作从当前显示姿态衔接，带代次检查的有限任务清理动画。关闭清除悬停状态与所有动画，减弱动态效果时直接应用最终姿态且不闪烁。

`SystemHUDView` 保留共享外壳，`HUDNavigation` 保留导航，`HUDModuleContent` 工厂容器只替换中央内容。切换约 **0.30 秒**：六段带切角的遮罩按导航方向错开揭示实际内容，新内容短暂进行两次小幅位置与剪切校准后归位，旧内容沿深度方向收回。细淡的中性边线跟随遮罩前沿，不再覆盖实心干扰条；选中侧面导航时平滑放大，底部扇形改变高亮，框架做小幅索引运动，不重播整个浮层的入场。临时边线在完成、取消或立即结束切换时清除。电源使用已有实时数据层，其余模块可通过替换内容工厂扩展。连续选择只保留最新待切换目标，关闭会清理未完成的内容过渡。

中央常驻容器保持 **440 × 440 点**局部坐标，普通模块保留原有 **400 × 334 点**画布，以 0.86 倍围绕 `(500, 294)` 显示，为圆盘内下方按钮留空；工作模式以完整 1 倍围绕 `(500, 320)` 显示，计时圆环实际直径为 430 点。鼠标坐标逆向应用同一缩放和平移，原生编辑器与辅助功能位置使用对应正向投影。工作模式保留局部直径 430 点的圆环，将时钟和紧凑控制包在内部，提供 5／30／60 分钟及自定义，默认 30 分钟；主题色剩余弧线从完整开始，端点逆时针收缩。运行时提供“暂停＋重置”，暂停时提供“继续＋重置”；重置会结束会话、清零进度并恢复配置的倒计时时长或秒表零值。工作模式按钮采用单次平滑放大，计时变化保留遮罩过渡，音量内联页面也使用机械过渡，不重建窗口。

便笺标题和直接添加按钮保留在中央容器，卡片使用外壳上方独立的全屏工作区，可移出中央圆形区域。点击文字／待办即添加，图片按钮打开选择面板；固定便笺在切换模块时仍显示且可编辑、拖动和缩放。删除时在便笺下方显示 ×／✓ 确认。输入和原生编辑框使用屏幕坐标，较小屏幕上的临时位置限制不覆写存储数据；便笺动作使用有限的深度与揭示动画，关闭时清理。

在任何模块中，都可把文件拖到右侧文件暂存架按钮，添加引用后切换到暂存架。Shift 点击选择一段卡片，再作为同一个原生拖放会话拖出。拖出时播放浮层关闭动画，取消时播放打开动画恢复；原文件与暂存架引用均保留。

隐藏音量页面时移除详细硬件与进程信息监听；事件日志另有设备列表监听。手动启用的独立进程混音在隐藏浮层及滑块回到 100% 后继续运行，100% 仅表示单位增益。尚未启用的 100% 滑块不会启动捕获。睡眠／会话暂停、退出或相关进程、路由与格式变化会结束路由。混音使用公开接口且不保存音频、不自动重启；本机哔哩哔哩衰减已有用户确认，其他设备、音质及异常恢复仍待验证。

[事件日志](event-log.md)沿用普通中央画布与现有切换动效，记录真实操作及选定电源、音频设备和显示器变化。初始快照仅建立基线，不虚构启动连接事件。日志仅本机保存，不包含剪贴板正文、便笺内容或完整路径；隐藏页面时停止绘制观察，记录器继续接收真实事件。

“+ 添加应用”中点击“+ 选择应用”，或拖入／粘贴一个 `.app`，即可建立自动填入名称与图标的草稿。可改名、保留原图标或选择 13 种内置图标，再点击“保存快捷方式”。已保存卡片支持打开、编辑和移除；移除不会删除原应用。保存后在“地图”及已有应用之后加入右侧导航图块，显示自定义名称和所选图标，编辑后原位更新；“+ 添加应用”始终最后。图标选择区省略预设名称和多余提示，辅助功能仍可读出图标名称。列表和编辑器共用中央内容区域，原生名称输入框与选择面板使用现有投影和输入锁定。点击打开时先解析书签，再播放完整收回动画，最后通过公开 AppKit 接口打开或激活所选安装位置的应用；失败时显示错误。成功打开只在 Navigation 分类记录快捷方式显示名，不记录完整路径。详细操作与持久存储见[应用快捷方式](app-shortcuts.md)。

存储只读取一个启动文件系统的容量，不重复累加共享 APFS 容量。总量、已用及当前空闲空间显示在三行简报中，结果缓存一分钟；刷新图标只重新读取容量。点击“存储设置”先收回浮层，再通过 LaunchServices 打开 macOS 存储面板；旧系统使用存储管理，并以系统信息作为后备。详细分类交给系统完成，打开本模块不会枚举用户文件夹或扫描整盘。

活动监视器的系统历史最多保留 60 个快照，关闭／重建浮层不会清空；后台队列在页面可见时每秒采样，隐藏时每五秒采样，睡眠、会话暂停及退出时停止。内存另外显示压缩占用；CPU、内存、上下行及磁盘读写使用真实单位，不可用数据留空。图表保留固定 60 个顶点用于插值，但横轴按实际采样时间间隔排列；渲染帧不会触发统计采样。

应用子页显示图标、名称及 CPU、内存、网络、磁盘读写，支持排序和连续滚动，复用有限数量的行。应用 CPU 以单个逻辑处理器的 100% 为基准，因此可超过 100%。应用统计和有界的公开 `nettop` 输出仅在应用子页可见时运行，返回概览、切换模块或关闭浮层便停止；受保护、不可读或尚未建立基线的读数显示 `—`，不虚构零值。系统历史与应用详情分别管理生命周期。

活动监视器“概览／应用”、事件日志分类及剪贴板／暂存架翻页使用 `HUDSubsectionTransition`：0.26 秒方向深度移动与四段遮罩揭示，只移动内容集合，不移动周围标题和控件。每次最多两条有限动画，连续切换替换旧动画；隐藏或减弱动态效果时清理，统计采样频率不变。应用草稿编辑另有有限的方向遮罩与深度过渡，原生名称输入框待揭示完成再就位；音量和工作模式保留已有过渡。

系统的“减弱动态效果”会停用环境运动、指针视差、选中放大动效与内容切换校准效果，并将共同空间变换恢复平面；设置在浮层打开期间发生变化时也会立即响应，并结束正在进行的展开或收回过渡。日常迭代使用 `./scripts/dev.sh` 进行本机架构构建，只运行与改动相关的检查；发布打包和 CPU/RSS 测量仅在明确要求时进行。此前静态 Power 界面的性能记录不代表当前 2.5D 界面。

默认快捷键为 **Ctrl 加 ANSI 键盘上反引号所在的物理键（键码 50）**，通过标准 Carbon 全局快捷键 API `RegisterEventHotKey` 注册。在浮层内“快捷键”页可改为一个普通键加一至两个修饰键，最多同时三个键；Esc 取消录入。应用检查常见保留组合、已启用的系统快捷键及 Carbon 注册冲突，失败时保留原组合；无法枚举其他应用所有私有事件拦截绑定。**文本框和编辑器获得焦点时也可使用**，不带修饰键的反引号保持普通输入。

无需辅助功能权限，也无需在系统隐私设置中授权。安全输入或系统保留场景可能阻止快捷键事件；其他应用已注册同一组合键时，也可能注册失败。此时使用菜单栏打开即可。应用只接收已注册的快捷键事件，不记录按键、不读取文字内容、不通过辅助功能 API 检查焦点控件，也不模拟或补发字符。

设备弹层当前仅显示 **本机 Mac** 的可靠电池信息。不会扫描或连接蓝牙设备，也不请求蓝牙权限；AirPods、耳机、iPhone 等附件电量尚未实现，不会使用私有接口或虚构电量。没有内置电池的 Mac 显示相应不可用状态。

编译目标仍为 **Intel macOS 10.15.4+**、**Apple silicon macOS 11+**，不等于所有系统和设备均已完成实机验证。具体验证结果见 [TESTING.md](../TESTING.md)。

快捷键策略测试不等于已验证系统注册、文本框内使用或真实全局按键的完整交互；请以记录的实际测试范围为准。

Click feedback uses one finite, screen-aligned bracket animation above the HUD and inline editors. A window-scoped AppKit local mouse monitor covers navigation, notes, controls, and empty overlay space without consuming the click. It is removed when the HUD detaches; it does not monitor other applications. Reduce Motion disables the effect.

Pinned note appearance is refreshed independently of the selected center module, so reopening on another section uses the current theme immediately.
