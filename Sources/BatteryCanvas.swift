import AppKit
import QuartzCore

/// The Battery screen of the redesign (spec §3.22), reached from the ⚡ chip.
/// Every value is a real reading from BatteryMonitor (macOS's percentage,
/// power state and time estimates, the gauge capacity, cycles, condition,
/// temperature and the adapter's rating) plus the charge alert's reading.
/// The host forwards every reading; values and colours update in place.
final class BatteryCanvas: HUDDesignCanvas {
    /// The BAR READING plate opens Display → Battery alert (the v1.2.0 Power
    /// screen's battery-alert link). The host wires it.
    var onOpenBatteryAlert: (() -> Void)?
    private(set) var snapshot: BatterySnapshot = .unavailable
    private(set) var configuration: AppConfiguration = .defaults
    private var live: Live?
    private var cellState: CellState?

    /// The level tone (spec §3.22, `BatteryLevelTone`'s thresholds). It drives
    /// the numerals, the cells and the status chip.
    enum Tone: Equatable {
        case good, moderate, low
        init?(level: Int?) {
            switch BatteryLevelTone.forPercentage(level) {
            case .green?: self = .good
            case .yellow?: self = .moderate
            case .red?: self = .low
            case nil: return nil
            }
        }
        var tag: String { self == .good ? "GOOD" : self == .moderate ? "MODERATE" : "LOW" }
        var color: CGColor { HUDColor.rgb(self == .good ? 0x8CE03C : self == .moderate ? 0xFFFA00 : 0xE5383B) }
        var high: CGColor { HUDColor.rgb(self == .good ? 0xB6F27A : self == .moderate ? 0xFFFC6B : 0xFF6B6D) }
        var low: CGColor { HUDColor.rgb(self == .good ? 0x5FB51E : self == .moderate ? 0xE9CF12 : 0xB8262A) }
        /// `cellPulseB`: the tone mixed 35 % into #2A2A2A.
        var pulse: CGColor {
            let c = color.components ?? [0, 0, 0], base: CGFloat = 42 / 255
            return CGColor(srgbRed: c[0] * 0.35 + base * 0.65, green: c[1] * 0.35 + base * 0.65, blue: c[2] * 0.35 + base * 0.65, alpha: 1)
        }
        var caption: (text: String, language: String?) {
            switch self {
            case .good: return TelemetryCaption.text("Charge good", "电量充足")
            case .moderate: return TelemetryCaption.text("Charge moderate", "电量中等")
            case .low: return TelemetryCaption.text("Charge low", "电量不足")
            }
        }
    }

    private struct CellState: Equatable { let filled: Int; let tone: Tone?; let pulsing: Bool }

    private struct Live {
        let numeral: HUDBigNumeral
        let status: CALayer
        let caption: HUDTextLayer
        let timeLabel: HUDTextLayer
        let time: HUDTextLayer
        let cells: HUDCells
        let plates: [HUDStatPlate]
        let hero: HUDReadout
        let readouts: [HUDReadout]
        let reading: BatteryReadingPlate
    }

    init(context: HUDDesignContext) { super.init(module: .power, context: context) }

    /// Every battery reading and configuration change (main thread).
    func update(snapshot: BatterySnapshot, configuration: AppConfiguration) {
        guard snapshot != self.snapshot || configuration != self.configuration else { return }
        self.snapshot = snapshot; self.configuration = configuration
        guard renderedLanguage != nil else { return }
        if renderedLanguage != L10n.resolvedLanguage { rerender() } else { apply(animated: true) }
    }

    // The charging pulse is a live readout: it runs only while Battery is the
    // active module.
    override func didActivate() { applyCells(animated: false) }
    override func didDeactivate() { applyCells(animated: false) }
    override func willLeave() { applyCells(animated: false, pulseAllowed: false) }
    /// Tracks of that live loop on the current page (host diagnostics).
    var liveAnimationCount: Int { live?.cells.pulseAnimationCount ?? 0 }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        header.configure(crumbs: ["BATTERY"], code: "PWR-00", title: HUDModuleTitle("Battery", "电池状态"),
                         counter: "LIVE", showsBack: false)
        let width = HUDMetric.box.width
        let top = HUDLayer.plain("battery.top")
        let source = HUDTextLayer("// " + L10n.text("Power source", "电源"), style: HUDType.label)
        let numeral = HUDBigNumeral(size: 96, env: env)
        let status = HUDLayer.plain("battery.status")
        let caption = HUDTextLayer(style: HUDType.labelCJK)
        let timeLabel = HUDTextLayer(style: HUDType.label, alignment: .right)
        let time = HUDTextLayer(style: HUDType.numeral.sized(30), alignment: .right)
        let cells = HUDCells(count: 20, width: width, capacity: true, height: 22, env: env)
        let frames = HUDStatPlate.gridFrames(count: 6, width: width).map { $0.offsetBy(dx: 0, dy: 231) }
        let plates = frames.map { HUDStatPlate(width: $0.width, env: env) }
        plates.forEach { $0.valueMinimumScale = 0.7 }
        let reading = BatteryReadingPlate(plate: plates[5], env: env)
        reading.onActivate = { [weak self] in self?.onOpenBatteryAlert?() }
        let hero = HUDReadout(id: "battery.level", label: L10n.text("Battery", "电池"),
                                    frame: CGRect(x: 0, y: 81, width: width, height: 138), env: env)
        let readouts = frames.prefix(5).enumerated().map { index, frame in
            HUDReadout(id: "battery.plate.\(index)", label: "", frame: frame, env: env)
        }
        HUDMotion.perform {
            top.frame = CGRect(x: 0, y: 81, width: width, height: 138)
            source.lineFrame = CGRect(x: 0, y: 0, width: 300, height: 10)
            // Pinned at its top-left: the digits change the numeral's width.
            numeral.layer.anchorPoint = .zero; numeral.layer.position = CGPoint(x: 0, y: 16)
            timeLabel.lineFrame = CGRect(x: width - 200, y: 18, width: 200, height: 10)
            time.lineFrame = CGRect(x: width - 200, y: 34, width: 200, height: 27)
            cells.layer.frame.origin = CGPoint(x: 0, y: 116)
            [source, numeral.layer, status, caption, timeLabel, time, cells.layer].forEach(top.addSublayer)
            page.layer.addSublayer(top)
            for (plate, frame) in zip(plates, frames) { plate.layer.frame = frame; page.layer.addSublayer(plate.layer) }
            ([hero] + readouts).forEach { page.layer.addSublayer($0.layer) }
        }
        page.add(hero); page.add(readouts); page.add(reading)
        page.stagger(top)
        plates.forEach { page.stagger($0.layer) }
        live = Live(numeral: numeral, status: status, caption: caption, timeLabel: timeLabel, time: time, cells: cells,
                    plates: plates, hero: hero, readouts: readouts, reading: reading)
        cellState = nil
        apply(animated: false)
        env.applyScale(to: page.layer)
    }

    private var currentTone: Tone? { snapshot.hasBattery ? Tone(level: snapshot.percentage) : nil }

    /// The hero, status chips, time, cells and plates from the latest reading.
    private func apply(animated: Bool) {
        guard let live else { return }
        let s = snapshot, tone = currentTone, level = s.hasBattery ? s.percentage.flatMap { (0...100).contains($0) ? $0 : nil } : nil
        live.numeral.set(level.map { String($0) } ?? TelemetryFormat.missing, unit: level == nil ? "" : "%",
                         tone: tone?.color ?? HUDColor.yellow, numberTone: tone?.color ?? HUDColor.steel)

        // Status column: the tone chip, the mode chip, the caption line.
        let charge = s.isChargeMode
        var chips: [HUDChip] = []
        if let tone { chips.append(HUDChip(tone.tag, variant: .custom(fill: tone.color, text: HUDColor.nearBlack), env: env)) }
        else if !s.hasBattery { chips.append(HUDChip("NO BATTERY", variant: .outline, env: env)) }
        chips.append(charge ? HUDChip("CHARGE MODE", variant: .yellow, icon: .bolt, env: env) : HUDChip("POWER MODE", variant: .outline, env: env))
        let mode = charge ? TelemetryCaption.text("CHARGE MODE", "超充模式") : TelemetryCaption.text("POWER MODE", "电源模式")
        let lead = tone?.caption ?? (s.hasBattery ? nil : TelemetryCaption.text("No battery", "未检测到电池"))
        let x = ceil(live.numeral.layer.bounds.width) + 24
        HUDMotion.perform {
            live.status.sublayers?.forEach { $0.removeFromSuperlayer() }
            var chipX: CGFloat = 0
            for chip in chips {
                chip.layer.frame.origin = CGPoint(x: chipX, y: 0)
                live.status.addSublayer(chip.layer)
                chipX += chip.layer.bounds.width + 4
            }
            live.status.frame = CGRect(x: x, y: 53, width: chipX, height: 18)
            live.caption.text = ([lead?.text, mode.text].compactMap { $0 }).filter { !$0.isEmpty }.joined(separator: "\u{3000}")
            live.caption.language = mode.language
            live.caption.lineFrame = CGRect(x: x, y: 79, width: HUDMetric.box.width - x - 160, height: 11.5)
            live.timeLabel.text = charge ? L10n.text("Until full", "充满还需") : L10n.text("Remaining", "剩余时间")
            live.time.text = timeText
        }

        applyCells(animated: animated)
        applyPlates()
        live.hero.value = [level.map { "\($0)%" } ?? TelemetryFormat.missing, tone?.tag, charge ? "CHARGE MODE" : "POWER MODE",
                           live.timeLabel.text + " " + timeText].compactMap { $0 }.joined(separator: ", ")
    }

    /// UNTIL FULL on external power (0:00 once full), REMAINING on battery;
    /// "—" while macOS is still estimating.
    private var timeText: String {
        let s = snapshot
        guard s.hasBattery else { return TelemetryFormat.missing }
        if s.isChargeMode {
            if s.isFullyCharged { return TelemetryFormat.duration(minutes: 0) }
            return s.isCharging ? TelemetryFormat.duration(minutes: s.minutesToFull) : TelemetryFormat.missing
        }
        return TelemetryFormat.duration(minutes: s.minutesToEmpty)
    }

    private func applyCells(animated: Bool, pulseAllowed: Bool = true) {
        guard let live else { return }
        let tone = currentTone, level = snapshot.hasBattery ? snapshot.percentage ?? 0 : 0
        let state = CellState(filled: tone == nil ? 0 : Int((Double(level) / 5).rounded()), tone: tone,
                              pulsing: pulseAllowed && isActive && snapshot.hasBattery && snapshot.isCharging)
        guard state != cellState else { return }
        cellState = state
        live.cells.set(filled: state.filled, tone: tone.map { ($0.high, $0.low) }, pulsing: state.pulsing, animated: animated,
                       pulse: tone?.pulse, pulseCount: 3)
    }

    private func applyPlates() {
        guard let live else { return }
        let s = snapshot
        let labels = [L10n.text("Source", "电源"), L10n.text("Capacity", "容量"), L10n.text("Cycles", "循环次数"),
                      L10n.text("Condition", "状态"), L10n.text("Temperature", "温度"), L10n.text("Bar reading", "电池条读数")]
        var values: [(String, String?)] = []
        let external = !s.hasBattery || s.isPluggedIn
        values.append((external ? L10n.text("External", "外接电源") : L10n.text("Battery", "电池"), s.adapterWatts.map { "\($0) W" }))
        if let capacity = s.capacity {
            values.append(capacity.unit == .milliampHours
                ? (HUDFormat.grouped(capacity.current), "/ " + HUDFormat.grouped(capacity.maximum) + " mAh")
                : ("\(capacity.current)%", nil))
        } else { values.append((TelemetryFormat.missing, nil)) }
        values.append((s.cycleCount.map(HUDFormat.grouped) ?? TelemetryFormat.missing, nil))
        values.append((Self.condition(s.healthCategory), nil))
        values.append((s.temperatureCelsius.map { String(format: "%.1f", locale: Locale(identifier: "en_US_POSIX"), $0) } ?? TelemetryFormat.missing,
                       s.temperatureCelsius == nil ? nil : "°C"))
        values.append((configuration.alertMetric.title, nil))
        let hot = (s.temperatureCelsius ?? 0) > 80
        for (index, plate) in live.plates.enumerated() {
            plate.set(label: labels[index], value: values[index].0, unit: values[index].1, highlighted: index == 5,
                      tone: index == 4 && hot ? HUDColor.orange : nil)
            if index < live.readouts.count {
                live.readouts[index].label = labels[index]
                live.readouts[index].value = [values[index].0, values[index].1].compactMap { $0 }.joined(separator: " ")
            }
        }
        live.reading.label = L10n.text("Battery alert", "电池提醒") + ", " + labels[5] + " " + configuration.alertMetric.title
    }

    /// macOS's battery health (IOPS BatteryHealth / BatteryHealthCondition)
    /// in the words System Settings uses; other reports appear as written.
    static func condition(_ value: String?) -> String {
        switch value {
        case nil: return TelemetryFormat.missing
        case "Good"?: return L10n.text("Normal", "正常")
        case "Fair"?: return L10n.text("Fair", "一般")
        case "Poor"?: return L10n.text("Poor", "较差")
        case "Check Battery"?, "Service Battery"?, "Service Recommended"?, "Permanent Battery Failure"?:
            return L10n.text("Service Recommended", "建议维修")
        case let value?: return value
        }
    }
}

/// The BAR READING stat plate as the link to the Battery alert settings:
/// it brightens on hover, eased in and out.
final class BatteryReadingPlate: HUDControl {
    private let brighten = HUDLayer.plain("battery.reading.brighten")

    init(plate: HUDStatPlate, env: HUDDesignEnvironment) {
        super.init(id: "battery.reading", env: env, layer: plate.layer)
        HUDMotion.perform {
            brighten.frame = plate.layer.bounds; brighten.backgroundColor = HUDColor.foreground(0.06); brighten.opacity = 0
            plate.layer.addSublayer(brighten)
        }
        setTarget(plate.layer.bounds)
    }

    override func hoverChanged() {
        HUDMotion.animate(brighten, "opacity", to: Float(isHovered && isEnabled ? 1 : 0), duration: HUDDuration.hover,
                          timing: HUDEasing.ease, env: env)
    }
}
