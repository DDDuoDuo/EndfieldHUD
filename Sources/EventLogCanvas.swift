import AppKit
import QuartzCore

/// The Event Log's filter tabs (spec §3.19). Records keep their stored kind;
/// the tabs group them: work timers, the file shelf, devices (power, audio
/// devices, displays) and everything else.
enum EventLogFilter: Int, CaseIterable {
    case all, timer, shelf, devices, other

    static func of(_ kind: SystemEventKind) -> EventLogFilter {
        switch kind {
        case .workStarted, .workPaused, .workResumed, .workReset, .workCompleted: return .timer
        case .shelfAdded, .shelfRemoved, .shelfCleared: return .shelf
        case .powerConnected, .powerDisconnected, .batteryStateChanged, .audioDeviceConnected, .audioDeviceDisconnected,
             .displayConnected, .displayDisconnected: return .devices
        default: return .other
        }
    }

    func includes(_ kind: SystemEventKind) -> Bool { self == .all || Self.of(kind) == self }

    var title: String {
        switch self {
        case .all: return L10n.text("All", "全部")
        case .timer: return L10n.text("Timer", "计时")
        case .shelf: return L10n.text("Shelf", "暂存架")
        case .devices: return L10n.text("Devices", "设备")
        case .other: return L10n.text("Other", "其他")
        }
    }

    /// Timer = yellow, Devices = cyan, otherwise dark.
    var chip: HUDChip.Variant { self == .timer ? .yellow : self == .devices ? .cyan : .dark }
}

/// One localised line per record ("Countdown finished, 30 min"). Only the
/// allowlisted metadata the log stores (names, kinds, durations) appears.
enum EventLogText {
    static func line(_ event: SystemEvent) -> String {
        let m = event.metadata
        func joined(_ phrase: String, _ value: String?) -> String {
            guard let value, !value.isEmpty else { return phrase }
            return L10n.text("\(phrase), \(value)", "\(phrase)，\(value)")
        }
        let stopwatch = m["kind"] == "stopwatch"
        let duration = stopwatch ? nil : m["seconds"].flatMap(Int.init).map { WorkModeCanvas.durationTitle(TimeInterval($0)) }
        let percentage = m["percentage"].map { $0 + "%" }
        switch event.kind {
        case .overlayOpened: return L10n.text("HUD opened", "界面已打开")
        case .moduleOpened: return L10n.text("Module opened", "模块已打开")
        case .appShortcutOpened:
            if let app = m["app"] { return L10n.text("Opened \(app)", "已打开 \(app)") }
            return L10n.text("App shortcut opened", "已打开应用快捷方式")
        case .clipboardCopied:
            switch m["kind"] {
            case "text": return L10n.text("Copied text", "已复制文本")
            case "url": return L10n.text("Copied a link", "已复制链接")
            case "image": return L10n.text("Copied an image", "已复制图像")
            case "files": return L10n.text("Copied files", "已复制文件")
            default: return L10n.text("Copied from the clipboard", "已从剪贴板复制")
            }
        case .shelfAdded:
            if let name = m["filename"] { return L10n.text("Added \(name)", "已添加 \(name)") }
            return L10n.text("Added to the shelf", "已添加到暂存架")
        case .shelfRemoved:
            if let name = m["filename"] { return L10n.text("Removed \(name)", "已移除 \(name)") }
            return L10n.text("Removed from the shelf", "已从暂存架移除")
        case .shelfCleared:
            if let count = m["count"] { return L10n.text("Shelf cleared, \(count) items", "暂存架已清空，共 \(count) 项") }
            return L10n.text("Shelf cleared", "暂存架已清空")
        case .workStarted:
            return stopwatch ? L10n.text("Stopwatch started", "秒表开始") : joined(L10n.text("Countdown started", "倒计时开始"), duration)
        case .workPaused:
            return stopwatch ? L10n.text("Stopwatch paused", "秒表暂停") : joined(L10n.text("Countdown paused", "倒计时暂停"), duration)
        case .workResumed:
            return stopwatch ? L10n.text("Stopwatch resumed", "秒表继续") : joined(L10n.text("Countdown resumed", "倒计时继续"), duration)
        case .workReset:
            return stopwatch ? L10n.text("Stopwatch reset", "秒表重置") : joined(L10n.text("Countdown reset", "倒计时重置"), duration)
        case .workCompleted: return joined(L10n.text("Countdown finished", "倒计时结束"), duration)
        case .powerConnected: return joined(L10n.text("Power connected", "电源已连接"), percentage)
        case .powerDisconnected: return joined(L10n.text("Power disconnected", "电源已断开"), percentage)
        case .batteryStateChanged:
            let phrase: String
            switch m["state"] {
            case "charging": phrase = L10n.text("Charging started", "开始充电")
            case "full": phrase = L10n.text("Battery full", "电池已充满")
            case "connected": phrase = L10n.text("Plugged in, not charging", "已接通电源，未在充电")
            case "battery": phrase = L10n.text("On battery power", "正在使用电池")
            case "noBattery", "unavailable": phrase = L10n.text("Battery unavailable", "电池不可用")
            default: phrase = L10n.text("Battery state changed", "电池状态已变化")
            }
            return joined(phrase, percentage)
        case .audioDeviceConnected, .displayConnected:
            if let device = m["device"] { return L10n.text("\(device) connected", "\(device) 已连接") }
            return event.kind == .audioDeviceConnected ? L10n.text("Audio device connected", "音频设备已连接")
                : L10n.text("Display connected", "显示器已连接")
        case .audioDeviceDisconnected, .displayDisconnected:
            if let device = m["device"] { return L10n.text("\(device) disconnected", "\(device) 已断开") }
            return event.kind == .audioDeviceDisconnected ? L10n.text("Audio device disconnected", "音频设备已断开")
                : L10n.text("Display disconnected", "显示器已断开")
        case .displaySettingsChanged:
            switch (m["field"], m["value"]) {
            case ("clockStyle", let value?):
                let number = HUDFormat.pad2((HUDClockStyle(rawValue: value)?.index ?? 0) + 1)
                return L10n.text("Clock style \(number)", "时钟样式 \(number)")
            case ("centerLogo", "customImported"): return L10n.text("Center logo imported", "已导入中心标志")
            case ("centerLogo", let value?):
                let title = HUDCenterLogo(rawValue: value)?.title ?? value
                return L10n.text("Center logo: \(title)", "中心标志：\(title)")
            case ("alertMetric", let value?):
                let title = HUDChargeMetric(rawValue: value)?.title ?? value
                return L10n.text("Battery alert reading: \(title)", "电池提醒数据：\(title)")
            default: return L10n.text("Display setting changed", "显示设置已更改")
            }
        case .profileCropChanged:
            switch m["target"] {
            case "background": return L10n.text("Profile background cropped", "已裁剪名片背景")
            case "thumbnail": return L10n.text("Profile picture cropped", "已裁剪头像")
            case "both": return L10n.text("Profile background and picture cropped", "已裁剪名片背景和头像")
            default: return L10n.text("Profile crop changed", "名片裁剪已更改")
            }
        case .mapPinStyleChanged: return L10n.text("Map pin style changed", "地图标记样式已更改")
        case .mapRecentered: return L10n.text("Map recentered", "地图已居中")
        case .noteAction:
            switch m["action"] {
            case "createdText": return L10n.text("Text note added", "已添加文字便笺")
            case "createdTODO": return L10n.text("Checklist added", "已添加待办清单")
            case "createdMedia": return L10n.text("Media note added", "已添加媒体便笺")
            case "createdDrawing": return L10n.text("Drawing added", "已添加画画便笺")
            case "editedText": return L10n.text("Note edited", "便笺已编辑")
            case "formattedText": return L10n.text("Note formatted", "便笺格式已更改")
            case "editedTODO": return L10n.text("Checklist edited", "待办清单已编辑")
            case "drawingEdited": return L10n.text("Drawing edited", "绘图已编辑")
            case "deletedNote": return L10n.text("Note deleted", "便笺已删除")
            case "mediaPlayback": return L10n.text("Note media played", "已播放便笺媒体")
            default: return L10n.text("Note changed", "便笺已更改")
            }
        case .playbackAction:
            let phrase: String
            switch m["action"] {
            case "playPause": phrase = L10n.text("Play / pause", "播放 / 暂停")
            case "previous": phrase = L10n.text("Previous track", "上一首")
            case "next": phrase = L10n.text("Next track", "下一首")
            case "seek": phrase = L10n.text("Playback position changed", "播放进度已调整")
            default: phrase = L10n.text("Playback changed", "播放已更改")
            }
            return joined(phrase, m["source"].flatMap(NowPlayingSource.init(rawValue:))?.title)
        case .projectionAction:
            switch m["action"] {
            case "drawingEdited": return L10n.text("Projection drawing edited", "投影绘图已编辑")
            case "erased": return L10n.text("Projection drawing erased", "投影绘图已擦除")
            case "brushChanged": return L10n.text("Projection brush changed", "投影画笔已更改")
            case "backgroundChanged": return L10n.text("Projection background changed", "投影背景已更改")
            case "mediaAdded": return L10n.text("Media added to the projection", "已向投影添加媒体")
            case "mediaRemoved": return L10n.text("Media removed from the projection", "已从投影移除媒体")
            case "cleared": return L10n.text("Projection cleared", "投影已清空")
            default: return L10n.text("Projection changed", "投影已更改")
            }
        case .archiveAction:
            switch m["action"] {
            case "created": return L10n.text("Archive document created", "已创建档案文档")
            case "edited": return L10n.text("Archive document edited", "档案文档已编辑")
            case "deleted": return L10n.text("Archive document deleted", "档案文档已删除")
            case "mediaAdded": return L10n.text("Media added to a document", "已向文档添加媒体")
            case "mediaRemoved": return L10n.text("Media removed from a document", "已从文档移除媒体")
            case "categoryCreated": return L10n.text("Archive category created", "已创建档案分类")
            case "categoryChanged": return L10n.text("Archive category changed", "档案分类已更改")
            case "categoryDeleted": return L10n.text("Archive category deleted", "档案分类已删除")
            default: return L10n.text("Archive changed", "档案已更改")
            }
        case .readerAction:
            switch m["action"] {
            case "imported": return L10n.text("Book added to the reader", "已向阅读器添加书籍")
            case "deleted": return L10n.text("Book removed from the reader", "已从阅读器移除书籍")
            case "bookmarked": return L10n.text("Bookmark changed", "书签已更改")
            case "progress": return L10n.text("Reading progress saved", "阅读进度已保存")
            case "settings": return L10n.text("Reading settings changed", "阅读设置已更改")
            default: return L10n.text("Reader changed", "阅读器已更改")
            }
        case .mediaAssemblyAction:
            switch m["action"] {
            case "imported": return L10n.text("Media imported", "已导入媒体")
            case "edited": return L10n.text("Media edited", "媒体已编辑")
            case "exported": return L10n.text("Media exported", "媒体已导出")
            default: return L10n.text("Media changed", "媒体已更改")
            }
        case .calendarAction:
            switch m["action"] {
            case "created": return L10n.text("Calendar event added", "已添加日历事项")
            case "edited": return L10n.text("Calendar event edited", "日历事项已编辑")
            case "deleted": return L10n.text("Calendar event deleted", "日历事项已删除")
            default: return L10n.text("Calendar changed", "日历已更改")
            }
        case .minigameAction:
            switch m["action"] {
            case "started": return L10n.text("Minigame started", "小游戏已开始")
            case "restarted": return L10n.text("Minigame restarted", "小游戏已重新开始")
            case "finished": return L10n.text("Minigame finished", "小游戏已结束")
            default: return L10n.text("Minigame played", "已进行小游戏")
            }
        case .accountAction:
            switch m["action"] {
            case "linked": return L10n.text("Account linked", "账户已绑定")
            case "unlinked": return L10n.text("Account unlinked", "账户已解绑")
            case "synced": return L10n.text("Profile refreshed", "名片已刷新")
            case "settings": return L10n.text("Account settings changed", "账户设置已更改")
            default: return L10n.text("Account changed", "账户已更改")
            }
        }
    }
}

/// Event Log (spec §3.19): filter tabs over a scrolling list of log rows
/// (time, category chip, text) with the micro tag; Clear asks first. Only the
/// rows in view (and one either side) exist, so a 500-record log keeps the
/// layer tree small; the store is observed only while the module is active.
final class EventLogCanvas: HUDDesignCanvas {
    private let store: SystemEventLog
    private var observer: UUID?
    private(set) var filter: EventLogFilter = .all
    private var renderedEvents: [SystemEvent]?
    /// The listed records (filtered) and the virtualised rows by index.
    private(set) var shown: [SystemEvent] = []
    private var rows: [Int: HUDRowPlate] = [:]
    private var list: HUDScrollArea?
    private var listOffset: CGFloat = 0
    private var rowWidth = HUDMetric.box.width
    private var lastStatus: String?
    static let listTop: CGFloat = 125, listHeight: CGFloat = 350, rowHeight: CGFloat = 40, pitch: CGFloat = 46

    init(context: HUDDesignContext, store: SystemEventLog) {
        self.store = store
        super.init(module: .eventLog, context: context)
    }

    deinit { if let observer { store.removeObserver(observer) } }

    /// Records the log lists: routine navigation stays saved but hidden.
    var events: [SystemEvent] { store.events.filter { $0.kind != .overlayOpened && $0.kind != .moduleOpened } }

    // MARK: Host interface

    override func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        let content = super.makeContent(for: module, style: style)
        refresh()
        return content
    }

    override func didActivate() {
        lastStatus = store.statusMessage
        if observer == nil { observer = store.observe { [weak self] in self?.storeChanged() } }
        refresh()
    }

    override func didDeactivate() {
        if let observer { store.removeObserver(observer) }
        observer = nil
    }

    override func recordingModeChanged() {}

    private func storeChanged() {
        let status = store.statusMessage
        if let status, status != lastStatus, isActive { toast(icon: .warn, "Event log", "事件日志", detail: status) }
        lastStatus = status
        refresh()
    }

    /// New records re-render in place; the row at the top stays in view.
    private func refresh() {
        guard renderedLanguage != nil, !isTransitioning, events != renderedEvents else { return }
        rerender()
    }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        let all = events
        let anchor = topVisibleAnchor()
        renderedEvents = all
        shown = all.filter { filter.includes($0.kind) }
        rows = [:]; list = nil
        let clear = HUDButton(id: "eventLog.clear", title: L10n.text("Clear", "清空"), icon: .trash, kind: .secondary, size: .small, env: env)
        clear.place(at: .zero)
        clear.isEnabled = !all.isEmpty
        clear.onActivate = { [weak self] in self?.confirmClear() }
        header.configure(crumbs: ["EVENT LOG"], code: "MOD-14", title: HUDModuleTitle("Event Log", "事件日志"),
                         counter: HUDFormat.pad2(all.count), showsBack: false, actions: [clear])
        let tabs = HUDTabs(id: "eventLog.filter", titles: EventLogFilter.allCases.map { ($0.title, nil) },
                           selectedIndex: filter.rawValue, width: HUDMetric.box.width, env: env)
        tabs.onSelect = { [weak self] index, direction in self?.select(EventLogFilter(rawValue: index) ?? .all, direction: direction) }
        HUDMotion.perform { tabs.layer.frame.origin = CGPoint(x: 0, y: HUDMetric.headerHeight); page.layer.addSublayer(tabs.layer) }
        page.add(tabs.controls); page.tabs = tabs; page.stagger(tabs.layer)
        guard !shown.isEmpty else {
            let empty = HUDEmptyState.bilingual("NO EVENTS", "日志为空", env: env)
            HUDMotion.perform { empty.frame.origin = CGPoint(x: 0, y: Self.listTop); page.layer.addSublayer(empty) }
            page.stagger(empty)
            listOffset = 0
            return
        }
        let contentHeight = CGFloat(shown.count) * Self.pitch - 6
        let area = HUDScrollArea(frame: CGRect(x: 0, y: Self.listTop, width: HUDMetric.box.width, height: min(Self.listHeight, contentHeight)))
        HUDMotion.perform { page.layer.addSublayer(area.layer) }
        area.setContentHeight(contentHeight)
        rowWidth = area.contentWidth
        var offset = listOffset
        if let anchor, let index = shown.firstIndex(where: { $0.id == anchor.id }) { offset = CGFloat(index) * Self.pitch + anchor.remainder }
        area.setOffset(offset)
        listOffset = area.offset
        list = area
        updateRows(page, initial: true)
    }

    private func topVisibleAnchor() -> (id: UUID, remainder: CGFloat)? {
        guard let list, list.offset > 0 else { return nil }
        let index = Int(list.offset / Self.pitch)
        guard shown.indices.contains(index) else { return nil }
        return (shown[index].id, list.offset - CGFloat(index) * Self.pitch)
    }

    /// Builds the rows in view (plus one either side) and drops the rest.
    private func updateRows(_ page: HUDDesignPage, initial: Bool) {
        guard let list else { return }
        let first = max(0, Int(floor(list.offset / Self.pitch)) - 1)
        let last = min(shown.count - 1, Int(ceil((list.offset + list.layer.bounds.height) / Self.pitch)))
        let wanted = first <= last ? Set(first...last) : []
        for (index, row) in rows where !wanted.contains(index) {
            row.clearInteractionState(); page.remove(row)
            HUDMotion.perform { row.layer.removeFromSuperlayer() }
            rows[index] = nil
        }
        for index in wanted.sorted() where rows[index] == nil {
            let row = makeRow(shown[index])
            HUDMotion.perform { row.layer.frame.origin = CGPoint(x: 0, y: CGFloat(index) * Self.pitch); list.content.addSublayer(row.layer) }
            if !initial { env.applyScale(to: row.layer) } else { page.stagger(row.layer) }
            page.add(row); rows[index] = row
        }
        if !initial { refreshRouter() }
    }

    private func makeRow(_ event: SystemEvent) -> HUDRowPlate {
        let category = EventLogFilter.of(event.kind)
        let text = EventLogText.line(event)
        let row = HUDRowPlate(id: "eventLog.row:" + event.id.uuidString, title: "", width: rowWidth, height: Self.rowHeight, env: env)
        let time = Self.time(event.createdAt)
        row.label = ([time.clock] + (time.day.map { [$0] } ?? []) + [category.title]).joined(separator: ", ")
        row.readout = text
        let chip = HUDChip(category.title, variant: category.chip, env: env)
        let clock = HUDTextLayer(time.clock, style: HUDTextStyle(size: 11, weight: .bold, color: HUDColor.rgb(0x444444), tabular: true))
        let line = HUDTextLayer(text, style: HUDTextStyle(size: 13, weight: .regular, color: HUDColor.nearBlack, lineHeight: 1.3))
        HUDMotion.perform {
            // An earlier day adds its date under the time.
            clock.lineFrame = CGRect(x: 18, y: time.day == nil ? 15.5 : 9, width: 52, height: 11)
            if let day = time.day {
                let date = HUDTextLayer(day, style: HUDTextStyle(size: 9.5, weight: .regular, color: HUDColor.steel2, tabular: true))
                date.lineFrame = CGRect(x: 18, y: 23, width: 52, height: 9.5)
                row.layer.addSublayer(date)
            }
            chip.layer.frame.origin = CGPoint(x: 80, y: 12)
            line.lineFrame = CGRect(x: 166, y: 21 - 8.45, width: rowWidth - 166 - 12, height: 16.9)
            [clock, chip.layer, line].forEach(row.layer.addSublayer)
        }
        return row
    }

    /// `06:45` today; earlier records also carry `10.19`.
    static func time(_ date: Date, now: Date = Date(), calendar: Calendar = .current) -> (clock: String, day: String?) {
        let c = calendar.dateComponents([.month, .day, .hour, .minute], from: date)
        let clock = HUDFormat.pad2(c.hour ?? 0) + ":" + HUDFormat.pad2(c.minute ?? 0)
        guard !calendar.isDate(date, inSameDayAs: now) else { return (clock, nil) }
        return (clock, HUDFormat.pad2(c.month ?? 0) + "." + HUDFormat.pad2(c.day ?? 0))
    }

    // MARK: Actions

    private func select(_ next: EventLogFilter, direction: CGFloat) {
        guard next != filter else { return }
        filter = next; listOffset = 0; list = nil
        navigate(dx: direction)
    }

    private func confirmClear() {
        let count = events.count
        guard count > 0 else { return }
        confirm(HUDConfirmRequest(id: "eventLog.clear", title: L10n.text("Clear the event log?", "清空事件日志？"),
                                  message: L10n.text("\(count) entries", "\(count) 条记录"), confirmTitle: L10n.text("Clear", "清空"),
                                  danger: true, icon: .trash)) { [weak self] in
            guard let self else { return }
            self.listOffset = 0; self.list = nil
            self.store.clear()
            self.refresh()
            self.toast(icon: .trash, "Log cleared", "日志已清空")
        }
    }

    /// The wheel over the list scrolls it and brings the next rows in.
    override func handleScroll(at point: CGPoint, delta: CGPoint, event: NSEvent?) -> Bool {
        guard let list, list.layer.bounds.contains(list.layer.convert(point, from: root)) else { return false }
        scrollList(by: delta.y)
        return true
    }

    private func scrollList(by delta: CGFloat) {
        guard let list, !delta.isNaN else { return }
        list.setOffset(delta.isInfinite ? (delta > 0 ? list.maximumOffset : 0) : list.offset + delta)
        listOffset = list.offset
        updateRows(page, initial: false)
    }

    /// ↑ / ↓ one row, Page Up / Page Down one view, Home / End.
    override func handleKey(_ event: NSEvent) -> Bool {
        guard list != nil, event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        let view = Self.listHeight - 30
        switch event.keyCode {
        case 125: scrollList(by: Self.pitch)
        case 126: scrollList(by: -Self.pitch)
        case 121: scrollList(by: view)
        case 116: scrollList(by: -view)
        case 119: scrollList(by: .infinity)
        case 115: scrollList(by: -.infinity)
        default: return false
        }
        return true
    }

    // MARK: Testing

    var rowCountForTesting: Int { rows.count }
    var listOffsetForTesting: CGFloat { list?.offset ?? 0 }
    func scrollListForTesting(_ delta: CGFloat) { scrollList(by: delta) }
}
