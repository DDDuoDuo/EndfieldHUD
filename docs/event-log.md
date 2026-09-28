# Event Log / 事件日志

Event Log is a functional module in the shared HUD. It keeps the latest **500 events** on this Mac, newest first, with timestamps, titles and short details. Older entries are dropped when the limit is reached. Opening the page uses the existing central-content transition and preserves the outer shell.

Log categories, event titles, generated details, confirmation controls and status messages stay in English. Only the module heading and **Clear log** button follow the app's English/Chinese language setting. File, device and shortcut display names remain the original data, including Chinese names. Timestamps use the Mac's local time in `MM-dd HH:mm:ss` format. The count appears above the list without a “Newest first” label.

## Controls

- Scroll with the wheel or trackpad. Up/Down selects rows; Page Up/Page Down scrolls; Home/End moves to the ends.
- Filter by **All, Navigation, Clipboard, Files, Work, Power, Audio or Display**. Filtering changes the view, not the saved history. Two bounded row planes exchange through matching masks with one moving edge; outgoing rows remain until the incoming rows replace them, while category controls remain still. New events and scrolling update only the incoming plane. Rapid filter changes, hiding and Reduce Motion settle the latest selection safely.
- **Clear log** opens an inline confirmation. **Clear** removes the entire log, including categories hidden by the filter; **Cancel** or Escape keeps it. Clearing does not create a replacement “log cleared” entry.

Rows expose their timestamp, category, title and details to accessibility. When new events arrive while scrolled down, the visible row stays anchored where possible. Only the handful of visible rows are drawn; old artwork is released when a filter transition ends, including after Clear. The page has no idle animation or polling timer.

## Recorded events

| Category | Events | Saved details |
| --- | --- | --- |
| Navigation | HUD opened; selected module opened; saved app shortcut successfully opened or activated | Module identifier for module selections; display name only for app shortcuts |
| Clipboard | An item successfully copied back from Clipboard Cache | Item type only: text, link, image or files |
| Files | Added to or removed from Temporary File Shelf; shelf cleared | File basename, or count for Clear |
| Work | Timer started, paused, resumed, reset or completed | Countdown/stopwatch kind; configured countdown seconds |
| Power | Power connected/disconnected; battery state changed | Reported state and available percentage |
| Audio | Observed physical audio device connected/disconnected | Short device name |
| Display | Observed display connected/disconnected | Short display name |

Initial power, timer, audio-device and display snapshots establish baselines. They do not invent startup connection or timer events. Ordinary battery percentage updates and timer ticks do not create rows. Opening the HUD or a module remains a real action and can create an entry, including opening Event Log itself.

[Add App](app-shortcuts.md) records **App shortcut opened** only after a successful application handoff. The detail is the saved display name; no application path, bookmark or bundle identifier is logged. Choosing, saving, editing or removing a shortcut does not create a launch event, and failed launch requests do not claim success.

This is the app's own event history. It does not import macOS Unified Logging or monitor general application usage. Clipboard capture alone does not create a log event. Note bodies, clipboard text/URLs/images, full file paths, audio samples, hardware identifiers and arbitrary metadata fields are excluded. File basenames, device labels and shortcut display names can still contain personal information; they remain in the local file as short text.

The recorder uses existing battery/work updates and display-change notifications. A separate, event-driven audio device-list listener runs off the main thread; it excludes EndfieldCharge's private devices and known aggregate/virtual devices. It does not capture audio or inspect process streams. The recorder continues while the HUD is hidden; the Event Log page detaches its drawing observer when hidden.

## Storage and lifetime

Normal app runs save a versioned JSON document at:

```text
~/Library/Application Support/EndfieldCharge/EventLog/events.json
```

History survives normal app launches. Changes are coalesced briefly and written atomically on a serial utility queue; normal termination flushes pending changes. Abrupt termination can lose the most recent unflushed events. The file is local plain JSON, with no upload, account or sync service.

On load, the app limits the file size to 2 MiB, sanitizes allowed metadata again, removes duplicate event IDs, sorts by time and keeps at most 500 entries. An unreadable, oversized or newer-format file is preserved rather than overwritten. The page shows a storage message and new events remain in memory for that session; Clear does not overwrite that protected original. A later save failure also leaves the current session's events available and reports the problem. Normal successful Clear persists an empty log.

Diagnostic fixtures use an in-memory log and do not seed synthetic events into the user's saved history.

## 中文

事件日志已接入共享 HUD，按最新在前显示最多 **500 条**本机记录，超出时移除最早记录。只有模块标题和“清空日志”按钮随应用语言切换；分类、事件名称、生成的详情、确认控件及状态提示保留英文。文件名、设备名和快捷方式显示名属于原始数据，不会翻译或改写，中文名称仍照常显示。时间使用本机时区，格式为 `MM-dd HH:mm:ss`；列表上方只显示条数，不再显示“最新在前”提示。

支持鼠标滚轮／触控板连续滚动。方向键选择行，Page Up／Page Down 翻动视口，Home／End 移动到两端。新事件到达时，已向下滚动的内容尽量保持原有位置。

分类筛选按钮为 **All、Navigation、Clipboard、Files、Work、Power、Audio、Display**。筛选不删除记录；列表内容通过有限的方向遮罩和深度过渡切换，分类控件保持原位。“清空日志”会先显示内联确认；点击 **Clear** 清空所有分类，包括当前筛选隐藏的记录；**Cancel** 或 Esc 保留记录，清空操作不会再生成一条新日志。

记录范围包括打开 HUD／模块、通过已保存快捷方式成功打开或激活应用、成功复制剪贴板项目、添加／移除／清空文件暂存架、计时开始／暂停／继续／重置／完成，以及观察到的电源、电池状态、音频设备和显示器变化。初始快照仅建立基线，不虚构启动时的连接或计时事件；每秒计时更新和普通电量百分比变化不会逐条记录。

应用快捷方式成功交接后才记录 **App shortcut opened**，详情只保存用户设置的显示名。选择、保存、编辑、移除快捷方式和失败的打开请求不会生成这条成功记录；应用路径、书签和 Bundle ID 不写入事件日志。

日志只保存允许的短元数据，例如模块名称、快捷方式显示名、项目类型、文件基本名、项目数量、计时类型与配置时长、设备名称和电源状态。**不保存剪贴板正文或图片、便笺内容、完整文件路径、音频样本、硬件标识符或任意字段。** 文件名、设备名称和快捷方式显示名仍可能带有个人信息。此日志不读取 macOS 系统日志，也不记录其他应用的日常使用活动。

数据保存在上述 `events.json`，正常退出后仍可在下次启动读取。后台队列合并写入并原子替换文件，退出时保存待写入内容；强制终止可能丢失尚未写入的最新事件。文件是本机普通 JSON，不上传或同步。读取失败、超过 2 MiB 或格式较新时，应用保留原文件并提示问题；本次新事件暂存内存，清空也不会覆盖受保护的原文件。正常情况下清空会保存空日志。

隐藏日志页面后停止绘制观察；真实事件记录继续运行。音频设备变化由独立设备列表监听获取，排除本应用的私有路由及已知虚拟／聚合设备，不捕获音频。诊断使用内存日志，不向用户历史写入虚构事件。
