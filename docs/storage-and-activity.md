# Storage and Activity Monitor

These modules share the circular HUD, perspective and navigation. Their compact charcoal rows, fine grid, yellow/cyan graphs and aligned values reference the supplied video's 工业简报 screen around 37 seconds. The first HUD opening in an app session selects Map; later openings restore the last selected section.

## Activity overview

The overview contains CPU, Memory, Network upload/download and Disk read/write. System counters are read on one serial utility queue once per second while Activity Monitor is visible, and once every five seconds in the background. At most 60 snapshots are retained in memory for the app session. Switching sections or closing the HUD preserves that history; the graph covers a different span of time as the sampling cadence changes. Sleep, session suspension and quitting stop sampling. There are no overlapping reads or display-refresh-rate system queries.

Finite Core Animation path transitions interpolate new graph samples. Animation never calls the system readers, and Reduce Motion removes interpolation. Missing readings display `—`, not fabricated zero activity. CPU and transfer rates need two samples. Counter resets, device/interface changes and long sampling gaps establish a fresh rate baseline without deleting the retained chart history.

These readings describe the whole Mac:

- **CPU:** busy host tick delta divided by total tick delta, normalized to 0–100% across all logical CPUs.
- **Memory:** estimated resident anonymous memory excluding purgeable pages, plus wired memory and the physical compressed footprint, bounded by installed RAM. File-backed cache is excluded. This estimate does not reproduce Apple's full memory-pressure analysis.
- **Network:** cumulative 64-bit byte counters from active `en*`/`ppp*` interfaces. Loopback, `utun`, bridge and peer-to-peer interfaces are excluded to avoid obvious duplicate accounting. This measures physical-interface traffic; unusual virtual-interface configurations may not be represented.
- **Disk:** read/write byte deltas from reporting `IOBlockStorageDriver` services. Devices that do not publish the counters remain unavailable. The overview is not a per-volume or per-process disk breakdown.

The overview uses Mach host statistics, BSD routing-interface records and public IOKit registry statistics. Reference definitions: [VM statistics](https://developer.apple.com/documentation/kernel/vm_statistics64_data_t), [XNU network interface structures](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/net/if.h), [IOBlockStorageDriver statistics](https://github.com/apple-oss-distributions/IOStorageFamily/blob/main/IOBlockStorageDriver.h).

## Apps

The **Apps** tab lists running applications with continuous scrolling. Click the table headings—App, CPU, Memory, Network or Disk—to sort; the active heading shows an arrow, and clicking it again reverses the order. CPU descending is the initial order. Network sorts combined upload/download rates and Disk sorts combined read/write rates, while each row retains its separate directional readings. There is no separate sorting toolbar. App readings update once per second only while this tab is visible. Returning to Overview, switching modules or closing the HUD stops that separate sampler and its network-counter process. Reopening Apps establishes new rate baselines.

AppKit identifies running app bundles. Identifiable helper processes whose executables are inside those bundles are grouped with their parent app. The process inventory is bounded and refreshed periodically; standalone services, helpers outside the bundle and protected processes cannot always be attributed. This is an app view rather than an unrestricted list of every system process.

- **CPU:** the app group's user-plus-system CPU time divided by elapsed wall time. Here **100% means one logical CPU**, so a multithreaded app can exceed 100%. This differs from the overview's whole-machine percentage.
- **Memory:** the sum of available process physical-footprint readings (`ri_phys_footprint`). It is not the overview's VM estimate or a sum of all virtual address space.
- **Disk:** deltas of `proc_pid_rusage` read/write byte counters. A protected process, changed membership or missing baseline leaves the group unavailable rather than reporting a misleading partial total.
- **Network:** one local `/usr/bin/nettop` summary stream requests external-interface, per-process byte counters at one-second intervals. It does not collect packet contents, socket endpoints or DNS names. The data can differ from system-wide interface totals, and newly appearing/disappearing socket owners require another baseline.

CPU, memory and disk use public `proc_pid_rusage` data. macOS can deny access to individual processes; unknown data remains `—`. The app does not request administrator access, obtain task ports or use the private NetworkStatistics framework. `nettop` launch failure, denied access, stale output or an unsupported output format leaves network rates unavailable. Both system and app telemetry are session-memory data, not a saved activity history or an upload.

## Storage

Storage shows the startup filesystem's total, used and available capacity with a segmented meter. The arrow refreshes capacity with one click and completes a smooth clockwise turn, continuing one turn at a time while a query remains pending. The button face stays steady; fast reads finish the current turn instead of snapping backward. Reduce Motion leaves the arrow still. Normal readouts have no update-time footer.

**Storage Settings** closes the HUD with its existing animation and opens macOS's own storage interface. On macOS 13+, the destination is **System Settings → General → Storage**. Older supported systems use Storage Management when present. A rejected modern pane request falls back to System Settings; an unavailable legacy utility falls back to System Information. A successful launch request cannot guarantee a particular pane on every future OS version. Apple's [Storage settings guide](https://support.apple.com/guide/mac-help/mchl3d437fbc/mac) and [version-specific storage instructions](https://support.apple.com/102624) describe the manual destinations.

The modern URL is `x-apple.systempreferences:com.apple.settings.Storage`. On the tested macOS 15.7.4 installation, `Storage.appex` declares that bundle identifier and explicitly permits the System Preferences URL scheme; System Settings registers the scheme. The handoff uses `NSWorkspace.open`, without AppleScript or private preference APIs.

Capacity is one `statfs` query against `/System/Volumes/Data`, falling back to `/`. Available means presently unallocated filesystem capacity (`f_bavail`), not all purgeable space macOS might reclaim. Used is total minus available. APFS volumes share a container, so their capacities are not summed. Capacity is cached for 60 seconds and automatically refreshed once per minute only while Storage is visible. The refresh arrow forces a new capacity read.

The HUD no longer includes a folder-details panel or invokes local folder scans. Category analysis and cleanup remain in macOS's own storage interface.

## 中文

首次打开默认进入地图，之后恢复上次模块。概览显示 CPU、内存、网络上传／下载及磁盘读／写。活动监视器可见时每秒采样，后台每五秒采样，内存中保留最近 60 个采样点；关闭浮层或切换模块不会清空图表。不同采样间隔会改变图表覆盖的时间范围，睡眠、会话暂停或退出时停止采样。图表动画独立插值；无数据时显示 `—`，不伪造为零。

“应用”页按应用分组显示 CPU、物理内存占用、网络与磁盘速率，可连续滚动。直接点击“应用、CPU、内存、网络、磁盘”表头排序，当前列显示方向箭头，再次点击反向；初始按 CPU 从高到低排列，不再提供单独的排序按钮。网络按上传与下载合计排序，磁盘按读写合计排序，行内仍分别显示两个方向。仅该页可见时，每秒读取公开的进程资源计数，并运行一个本机 `nettop` 汇总进程。离开该页就停止，重新打开后重新建立速率基线。能够识别的应用包内辅助进程会合并；受保护进程、包外服务或缺少基线的数据可能不可用。应用 CPU 的 100% 表示占满一个逻辑核心，可超过 100%；概览 CPU 则以整台机器为 100%。应用内存使用进程物理占用计数，与概览的系统 VM 估算方式不同。

存储概览显示启动文件系统总量、已用量和当前可用空间，缓存一分钟。单击箭头重新读取容量并顺时针完整旋转一圈；查询未结束时逐圈继续，快速读取完成后也会平滑结束当前圈，按钮底色保持不变。开启减少动态效果时箭头保持静止。页面不显示更新时间说明，也不再提供文件夹详情或扫描文件夹。“存储设置”先播放浮层关闭动画，再打开 macOS 自带的存储界面。macOS 13 及以上使用“系统设置 → 通用 → 存储空间”；旧系统优先打开“存储管理”，无法打开时使用系统工具作为回退。可用空间不包含所有可能被系统清理回收的空间。
