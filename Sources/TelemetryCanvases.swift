import AppKit
import QuartzCore

/// Number formats shared by Storage, Activity Monitor and Battery (spec §1.8):
/// decimal units as Finder and Activity Monitor show them, "—" for a missing
/// or invalid reading (never a fabricated zero).
enum TelemetryFormat {
    static let missing = "—"

    /// 82.4 GB, 480 KB, 1.2 MB: one decimal below 100, none from 100.
    static func bytes(_ bytes: Double?) -> String {
        guard let bytes, bytes.isFinite, bytes >= 0 else { return missing }
        let units = ["B", "KB", "MB", "GB", "TB", "PB"]
        var value = bytes, index = 0
        while value >= 1000 && index + 1 < units.count { value /= 1000; index += 1 }
        return String(format: value >= 100 || index == 0 ? "%.0f %@" : "%.1f %@", locale: posix, value, units[index])
    }

    static func rate(_ value: Double?) -> String {
        let text = bytes(value)
        return text == missing ? missing : text + "/s"
    }

    /// 30.4% (one decimal, no space); `limit` clamps a total load to 100.
    static func percent(_ value: Double?, limit: Double? = 100) -> String {
        guard let value, value.isFinite, value >= 0 else { return missing }
        return String(format: "%.1f%%", locale: posix, limit.map { min($0, value) } ?? value)
    }

    /// Disk sizes (spec §3.24): `x.x GB`, from 1000 GB `x.xx TB`, split into
    /// the numeral and its unit.
    static func storage(_ bytes: Int64) -> (number: String, unit: String) {
        let text = HUDFormat.storage(gigabytes: Double(max(0, bytes)) / 1_000_000_000)
        let parts = text.split(separator: " ", maxSplits: 1).map(String.init)
        return (parts.first ?? text, parts.count > 1 ? parts[1] : "")
    }

    /// An app's memory in the Apps table: 720 MB, 1.2 GB.
    static func memory(_ bytes: UInt64?) -> String {
        guard let bytes else { return missing }
        let value = Double(bytes)
        return value >= 1_000_000_000 ? String(format: "%.1f GB", locale: posix, value / 1_000_000_000)
            : String(format: "%.0f MB", locale: posix, value / 1_000_000)
    }

    /// Battery times with an unpadded hour: 0:11, 6:17.
    static func duration(minutes: Int?) -> String {
        guard let minutes, minutes >= 0 else { return missing }
        return "\(minutes / 60):" + HUDFormat.pad2(minutes % 60)
    }

    private static let posix = Locale(identifier: "en_US_POSIX")
}

/// The game's caption under an English chip: other UIs show it in their own
/// language; an English UI shows nothing (the chip already says it, and no
/// other language appears in English).
enum TelemetryCaption {
    static func text(_ english: L10n.Text, _ chinese: L10n.Text) -> (text: String, language: String?) {
        (L10n.resolvedLanguage == .english ? "" : L10n.text(english, chinese), nil)
    }
}

/// Storage (spec §3.24): the chosen disk's total, used and available space.
/// StorageController reads, remembers the disk and refreshes; this canvas
/// presents it and updates the readings in place. No per-category breakdown.
final class StorageCanvas: HUDDesignCanvas {
    /// Closes the HUD and opens macOS's storage settings (OverlayController).
    var onOpenSystemStorage: (() -> Void)?
    /// The redesigned canvas has no telemetry-owned loops; the host's
    /// diagnostics still ask (SystemHUDView.telemetryAnimationCount).
    var animationCount: Int { 0 }
    private let controller: StorageController
    private var observer: UUID?
    /// Every reading, the disk's name and the counter update in place; the
    /// volume menu lists the disks when it opens.
    private var live: Live?
    private var pendingRefreshToast = false
    private var switchingVolume = false
    private var lastError: String?
    /// Filled cells and the low-space (red) state, as drawn.
    private(set) var cellFill: (filled: Int, low: Bool) = (0, false)

    private struct Live {
        let volume: HUDDropdown
        let available: HUDBigNumeral, used: HUDBigNumeral
        let cells: HUDCells
        let plates: [HUDStatPlate]
        let readout: HUDReadout
    }

    init(context: HUDDesignContext, controller: StorageController) {
        self.controller = controller
        super.init(module: .storage, context: context)
    }

    deinit {
        if let observer { controller.removeObserver(observer) }
        if isActive { controller.deactivate() }
    }

    // MARK: Lifecycle

    override func didActivate() {
        if observer == nil { observer = controller.observe { [weak self] in self?.controllerChanged() } }
        lastError = controller.snapshot.error
        controller.activate()
        controllerChanged()
    }

    override func didDeactivate() { stopReading() }
    override func willLeave() { stopReading() }

    private func stopReading() {
        if let observer { controller.removeObserver(observer) }
        observer = nil; pendingRefreshToast = false
        controller.deactivate()
    }

    private func controllerChanged() {
        guard renderedLanguage != nil, !switchingVolume else { return }
        applyReadings()
        guard isActive else { return }
        let snapshot = controller.snapshot
        if pendingRefreshToast, !snapshot.isLoading {
            pendingRefreshToast = false
            if snapshot.error == nil {
                toast(icon: .refresh, "Readings updated", "读数已更新", detail: volumeTitle)
            }
        }
        if !snapshot.isLoading, let error = snapshot.error, error != lastError {
            toast(icon: .warn, "Storage unavailable", "存储不可用", detail: Self.localizedError(error))
        }
        if !snapshot.isLoading { lastError = snapshot.error }
    }

    // MARK: Actions

    /// `storage:settings` and `storage:refresh`, as the visible controls do.
    func perform(actionID: String) {
        switch actionID {
        case "storage:settings": openSettings()
        case "storage:refresh": refresh()
        default: break
        }
    }

    private func openSettings() {
        toast(icon: .ext, "Storage settings", "系统设置")
        onOpenSystemStorage?()
    }

    private func refresh() {
        guard isActive else { return }
        pendingRefreshToast = true
        controller.refresh()
        controllerChanged()
    }

    private func openVolumeMenu(_ anchor: HUDDropdown) {
        if isMenuOpen { closeMenu(); return }
        let current = controller.currentVolume?.id
        let items = controller.volumes.map { HUDMenu.Item(id: $0.id, title: $0.name, selected: $0.id == current) }
        guard !items.isEmpty else { return }
        openMenu(id: "volume", items: items, anchor: anchor) { [weak self] item in
            guard let self, item.id != self.controller.currentVolume?.id else { return }
            // The chosen disk re-enters from below with its listed numbers;
            // its fresh reading then updates that page in place.
            self.switchingVolume = true
            let chosen = self.controller.selectVolume(identifier: item.id)
            self.switchingVolume = false
            if chosen { self.navigate(dx: 0, dy: 1) }
        }
    }

    // MARK: Page

    private var volumeTitle: String {
        controller.currentVolume?.name ?? controller.snapshot.capacity?.volumeName ?? L10n.text("Startup disk", "启动磁盘")
    }

    override func buildPage(_ page: HUDDesignPage) {
        let volume = HUDDropdown(id: "volume", label: L10n.text("Disk", "磁盘"), value: volumeTitle, env: env)
        volume.onActivate = { [weak self, weak volume] in if let volume { self?.openVolumeMenu(volume) } }
        let refresh = HUDIconButton(id: "refresh", label: L10n.text("Refresh storage", "刷新存储"), icon: .refresh, env: env)
        refresh.onActivate = { [weak self] in self?.refresh() }
        header.configure(crumbs: ["STORAGE"], code: "SYS-10", title: HUDModuleTitle("Storage", "存储空间"),
                         counter: TelemetryFormat.missing, showsBack: false, actions: [volume, refresh])

        let width = HUDMetric.box.width
        let readoutRow = HUDLayer.plain("storage.readout")
        let availableLabel = HUDTextLayer(L10n.text("Available", "可用"), style: HUDType.label)
        let usedLabel = HUDTextLayer(L10n.text("Used", "已用"), style: HUDType.label, alignment: .right)
        let cells = HUDCells(count: 40, width: width, capacity: true, height: 30, env: env)
        let frames = HUDStatPlate.gridFrames(count: 3, width: width)
        let plates = frames.map { _ in HUDStatPlate(width: frames[0].width, env: env) }
        let readout = HUDReadout(id: "storage.readings", label: volumeTitle,
                                       frame: CGRect(x: 0, y: 81, width: width, height: 92), env: env)
        let live = Live(volume: volume, available: HUDBigNumeral(size: 84, env: env), used: HUDBigNumeral(size: 30, env: env),
                        cells: cells, plates: plates, readout: readout)
        HUDMotion.perform {
            readoutRow.frame = CGRect(x: 0, y: 81, width: width, height: 92)
            availableLabel.lineFrame = CGRect(x: 0, y: 0, width: 300, height: 10)
            usedLabel.lineFrame = CGRect(x: width - 300, y: 49, width: 300, height: 10)
            // Pinned at their outer corners: the readings change their widths.
            live.available.layer.anchorPoint = .zero; live.available.layer.position = CGPoint(x: 0, y: 16)
            live.used.layer.anchorPoint = CGPoint(x: 1, y: 0); live.used.layer.position = CGPoint(x: width, y: 65)
            [availableLabel, usedLabel, live.available.layer, live.used.layer].forEach(readoutRow.addSublayer)
            cells.layer.frame.origin = CGPoint(x: 0, y: 189)
            for (plate, frame) in zip(plates, frames) { plate.layer.frame = frame.offsetBy(dx: 0, dy: 233) }
            [readoutRow, cells.layer].forEach(page.layer.addSublayer)
            plates.forEach { page.layer.addSublayer($0.layer) }
            page.layer.addSublayer(readout.layer)
        }
        let settings = HUDButton(id: "settings", title: L10n.text("Storage Settings", "存储设置"), icon: .ext,
                                 kind: .primary, size: .large, env: env)
        settings.place(at: CGPoint(x: ((width - settings.preferredWidth) / 2).rounded(), y: 304))
        settings.onActivate = { [weak self] in self?.openSettings() }
        HUDMotion.perform { page.layer.addSublayer(settings.layer) }
        page.add(readout); page.add(settings)
        page.stagger(readoutRow); page.stagger(cells.layer)
        plates.forEach { page.stagger($0.layer) }
        page.stagger(settings.layer)
        self.live = live
        applyReadings(animated: false)
        env.applyScale(to: page.layer)
    }

    /// Readings, counter and disk name, in place.
    private func applyReadings(animated: Bool = true) {
        guard let live else { return }
        let capacity = controller.snapshot.capacity
        header.setCounter(capacity.map { TelemetryFormat.percent($0.usedFraction * 100) } ?? TelemetryFormat.missing)
        live.volume.setValue(volumeTitle)
        let available = capacity.map { TelemetryFormat.storage($0.availableBytes) }
        live.available.set(available?.number ?? TelemetryFormat.missing, unit: available?.unit ?? "")
        let usedPercent = capacity.map { TelemetryFormat.percent($0.usedFraction * 100) }
        live.used.set(usedPercent.map { String($0.dropLast()) } ?? TelemetryFormat.missing, unit: usedPercent == nil ? "" : "%")
        let filled = capacity.map { Int(($0.usedFraction * 40).rounded()) } ?? 0
        let low = capacity?.isLowOnSpace == true
        live.cells.set(filled: filled, tone: low ? (HUDColor.rgb(0xFF6B6D), HUDColor.rgb(0xB8262A)) : nil, animated: animated)
        cellFill = (filled, low)
        let labels = [L10n.text("Total", "总计"), L10n.text("Used", "已用"), L10n.text("Available", "可用")]
        let values = [capacity?.totalBytes, capacity?.usedBytes, capacity?.availableBytes].map { bytes in
            bytes.map { let value = TelemetryFormat.storage($0); return value.number + " " + value.unit } ?? TelemetryFormat.missing
        }
        for (index, plate) in live.plates.enumerated() {
            plate.set(label: labels[index], value: values[index], highlighted: index == 1)
        }
        live.readout.label = volumeTitle
        if let usedPercent {
            live.readout.value = zip(labels, values).map { $0 + " " + $1 }.joined(separator: ", ") + ", " + usedPercent
        } else {
            live.readout.value = controller.snapshot.isLoading ? L10n.text("Refreshing", "正在刷新") : Self.localizedError(controller.snapshot.error)
        }
        env.applyScale(to: live.available.layer); env.applyScale(to: live.used.layer)
    }

    static func localizedError(_ value: String?) -> String {
        switch value {
        case "The startup filesystem is unavailable.": return L10n.text("The startup filesystem is unavailable.", "启动文件系统不可用。")
        case "Storage capacity is unavailable.", nil: return L10n.text("Storage capacity is unavailable.", "无法读取存储容量。")
        case let value?: return value
        }
    }
}
