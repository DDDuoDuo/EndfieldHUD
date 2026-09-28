# App shortcuts

Open **+ Add App** in the right navigation. Choose an installed `.app` bundle, drag one from Finder into the central canvas, or paste a copied application there. Choosing an application opens a draft with its localized name and original icon; it does not launch the app.

Rename the shortcut, keep the original application icon or choose one of thirteen bundled presets, then select **Save shortcut**. The presets are Bolt, Star, Terminal, Globe, Folder, Music, Play, Brush, Code, Game, Camera, Grid and Text bubble. Press Return to apply an edited name to the draft, then **Save shortcut** to persist it. **Cancel** discards the draft; Escape first cancels a name edit, or closes the draft when the name field is not active. A saved card opens from its main body; **✎** edits its name or icon, and **×** removes the shortcut while leaving the installed application untouched. The list scrolls within the shared HUD; the editor keeps the existing perspective and shell. The icon grid shows symbols without preset captions; icon names and selection remain available to accessibility. Extra drop/help captions are omitted.

Saving also adds a tile to the right navigation, after Map and the previously saved apps. **+ Add App** always stays last. The tile uses the saved name and icon; edits update it in place, and removing the shortcut removes its tile. These tiles share the existing layered hover, shadow and continuous scrolling behavior. Edit and Remove remain in the Add App management list. The HUD still has 16 built-in modules; saved app tiles are additional launch actions.

Opening or leaving an app draft uses a finite directional shutter and depth transition inside the same center canvas. Event Log category filters and Clipboard/Shelf pages use a similar content-only reveal; their surrounding controls stay still. Activity tabs retain both reports through a complementary moving-mask handoff. Reduce Motion settles these transitions immediately, and closing the HUD cancels them.

Clicking a saved shortcut requests the HUD's normal closing animation. Only after the panel has closed does the launcher use public `NSWorkspace.openApplication` to open or reopen the chosen application. An existing process at the saved location is reused and receives the normal reopen request, allowing it to restore a closed window. Running applications at another location are not silently substituted, even if they share the same bundle identifier. On macOS 14 and later, cooperative activation yields the foreground to the selected app; LaunchServices handles opening and activation on all supported systems.

The controller rejects repeated launch clicks, shares one animation/deadline completion gate, and cancels a queued handoff on forced close. A missing or replaced application reports an error before closing. If macOS rejects a launch after the panel closes, the app reopens Add App and displays the error. Successful launch/activation requests produce an Event Log entry containing the shortcut's display name, without the full file path.

## Persistence

The store writes versioned JSON atomically to:

```text
~/Library/Application Support/EndfieldCharge/AppShortcuts/shortcuts.json
```

Records contain a UUID, custom and original name, bundle identifier when present, selected icon preset, creation time, bookmark and last known path. Application files and icon images are never copied into this directory. Native icons are cached only in memory. The store has no timer, application-directory scan or network request.

Bookmarks can follow applications moved or renamed on an available volume when macOS resolves their new location; the store then renews the bookmark. Security-scoped bookmarks are used where available, with balanced access during inspection and launch. Non-sandboxed builds can use ordinary persistent bookmarks when scoped bookmark creation is unavailable. Resolution does not display permission UI or mount missing volumes. A missing app stays on the list so its shortcut can be removed and the application chosen again after it becomes available. Different installations can have separate shortcuts; adding the same resolved app twice is rejected.

Only existing `.app` directories with an `APPL` Info.plist and an available executable are accepted. A saved bundle identifier must continue to match before launch. Normal macOS LaunchServices and Gatekeeper checks still apply. Corrupt, unsupported-version and externally modified archives are preserved; failed writes do not replace the previous in-memory state. Diagnostic launches use an isolated temporary directory rather than the user's saved shortcuts.

## 简体中文

在右侧打开 **+ 添加应用**，选择已安装的 `.app`，或从 Finder 拖入／粘贴一个应用。选择后会自动读取应用名称和原始图标，进入草稿编辑；此时不会启动应用。可修改名称，选择“原始图标”或十三种内置预设，再点击“保存快捷方式”。回车只将名称应用到草稿；点击“取消”放弃草稿。点击卡片主体打开应用，**✎** 编辑名称／图标，**×** 移除快捷方式；移除不会删除应用。

保存后，右侧导航会在“地图”和已有应用之后加入对应图块，**+ 添加应用始终排在最后**。图块显示自定义名称和所选图标；编辑后原位更新，移除后同步消失。悬停层次、阴影与连续滚动沿用原有右侧按钮。编辑和移除仍在“添加应用”的管理列表中进行。HUD 保留 16 个内置模块，已保存应用是额外的启动入口。

图标选择区只显示图形，不显示预设名称或额外操作提示；辅助功能仍可读出名称及选择状态。进入／退出草稿编辑时使用有限的方向遮罩和深度切换，外壳不变；活动监视器子页、日志分类和剪贴板／暂存架翻页也使用内容区域过渡。“减弱动态效果”会直接呈现结果，关闭浮层时清理过渡。

点击快捷方式时，浮层先完成关闭动画，再启动应用或激活已运行的对应实例。不会在动画结束前切换应用，也不会用不同位置的同名应用替代用户选择的版本。若应用丢失或已被其他应用替换，会显示错误；可移除失效快捷方式，在应用可用后重新添加。启动失败后会重新打开添加应用页面。成功的启动／激活请求会在事件日志记录快捷方式名称，不记录完整路径。

应用引用、名称与预设选择保存在上述本机 JSON 文件中；不会复制应用或保存图标文件，也不会持续扫描应用目录。macOS 能解析新位置时，书签可跟随应用移动并更新；同一应用的重复引用会被拒绝。损坏、不支持的版本或外部更改的数据不会被静默覆盖。此功能沿用项目的 macOS 10.15.4+（Intel）及 macOS 11+（Apple Silicon）支持范围。
