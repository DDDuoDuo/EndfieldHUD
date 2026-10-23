import AppKit
import QuartzCore

/// The daily boot log (spec §3.28): a terminal panel on the plane that types
/// out a short status of the HUD's own data, then wipes up. It is a terminal,
/// so its text stays English in every language; only the data varies. The
/// host plays it on the first open of the day (never under Reduce Motion),
/// hides the centre meanwhile and calls `skip()` on any click or key.
/// Coordinates are content px; the host places `layer` at stage (690, 300).
final class HUDBootLog {
    static let size = CGSize(width: 570, height: 440)
    static let firstLineTop: CGFloat = 61
    static let lineHeight: CGFloat = 22
    /// Lines that fit above the cursor's own line.
    static let capacity = Int((size.height - 20 - firstLineTop) / lineHeight) - 1

    /// One terminal line. Its parts are flex items 8 px apart, as in the demo.
    struct Line: Equatable {
        enum Kind: Equatable {
            /// `> BOOT SEQUENCE  2026.10.19 06:45`
            case header
            /// `> KEY ...... VALUE  TRAILING`
            case entry
            /// An indented grey detail line (one calendar event).
            case sub
            /// `ALL SYSTEMS ONLINE. WELCOME BACK, …` with the status in green.
            case footer
        }

        /// The value's colour class: yellow value, green good, orange warning,
        /// or a data colour (the battery's tone).
        enum Tone: Equatable {
            case value, good, warning
            case color(CGColor)

            var color: CGColor {
                switch self {
                case .value: return HUDColor.yellow
                case .good: return HUDColor.green
                case .warning: return HUDColor.orange
                case .color(let color): return color
                }
            }
        }

        struct Segment: Equatable {
            let text: String
            let color: CGColor
        }

        var kind: Kind
        /// header: the title; entry: the key before the dot leader; sub: the
        /// text; footer: the green status.
        var key: String
        /// header: the timestamp; entry: the coloured value; footer: the greeting.
        var value = ""
        var tone = Tone.value
        /// Plain text two spaces after the value: `CHARGE MODE`, `NEXT +1 IN 04:31`.
        var trailing: String? = nil

        static func header(_ title: String, _ stamp: String) -> Line { Line(kind: .header, key: title, value: stamp) }
        static func entry(_ key: String, _ value: String, tone: Tone = .value, trailing: String? = nil) -> Line {
            Line(kind: .entry, key: key, value: value, tone: tone, trailing: trailing)
        }
        static func sub(_ text: String) -> Line { Line(kind: .sub, key: text) }
        static func footer(_ status: String, _ message: String) -> Line { Line(kind: .footer, key: status, value: message, tone: .good) }

        /// `KEY + ' ' + '.' × max(2, 22 − len(KEY)) + ' '`.
        static func dotLeader(_ key: String) -> String {
            key + " " + String(repeating: ".", count: max(2, 22 - key.count)) + " "
        }

        static let text = HUDColor.rgb(0xCFCFCF)
        static let subText = HUDColor.rgb(0x9A9A9A)
        /// The log floats over the shell, dark in both themes: fixed greys.
        static let steel = HUDColor.rgb(0x8A8A8A)

        /// The flex items in order (spaces preserved).
        var segments: [Segment] {
            let prompt = Segment(text: ">", color: Line.steel)
            var parts: [Segment]
            switch kind {
            case .header: parts = [prompt, Segment(text: " " + key + "  ", color: Self.text), Segment(text: value, color: tone.color)]
            case .entry:
                parts = [prompt, Segment(text: " " + Self.dotLeader(key), color: Self.text), Segment(text: value, color: tone.color)]
                if let trailing { parts.append(Segment(text: "  " + trailing, color: Self.text)) }
            case .sub: parts = [Segment(text: key, color: Self.subText)]
            case .footer: parts = [Segment(text: key, color: tone.color), Segment(text: " " + value, color: Self.text)]
            }
            return parts.filter { !$0.text.isEmpty }
        }
    }

    let layer = HUDLayer.plain("boot")
    let env: HUDDesignEnvironment
    /// After the wipe-up: the host shows the centre (entering from below) and
    /// runs the facility checks.
    var onFinish: (() -> Void)?
    /// From `play` until `onFinish`.
    private(set) var isPlaying = false
    private(set) var revealedCount = 0

    private let timers: HUDTimers
    private let term = HUDLayer.plain("boot.term")
    private let title = HUDTextLayer(style: Style.header)
    private let terminal = HUDTextLayer(style: Style.header.colored(Line.steel), alignment: .right)
    private let cursor = HUDLayer.plain("boot.cursor")
    private let bar = HUDLayer.plain("boot.bar")
    private let tri = HUDLayer.plain("boot.tri")
    private var rows: [HUDTextLayer] = []
    private var pending: DispatchWorkItem?
    private var finishing = false

    private enum Style {
        static let header = HUDTextStyle(size: 11, weight: .bold, color: HUDColor.rgb(0xE8E8E8), tracking: 0.16)
        static let line = HUDTextStyle(size: 12.5, weight: .medium, color: Line.text)
    }

    init(env: HUDDesignEnvironment, timers: HUDTimers = .main) {
        self.env = env; self.timers = timers
        let bounds = CGRect(origin: .zero, size: Self.size), ring = HUDColor.rgb(0x2E2E2E)
        HUDMotion.perform {
            layer.bounds = bounds; layer.zPosition = HUDLayerOrder.bootLog; layer.isHidden = true
            term.frame = bounds
            let plate = HUDLayer.shape("boot.plate")
            plate.frame = bounds; plate.path = HUDShape.chamfer(bounds, cut: 16); plate.fillColor = HUDColor.rgb(0x0A0A0A, 0.94)
            // inset 4px 0 0 yellow under inset 0 0 0 1px #2e2e2e (the ring stops at the chamfer).
            bar.frame = CGRect(x: 0, y: 0, width: 4, height: bounds.height)
            let edges = HUDLayer.shape("boot.ring"), w = bounds.width, h = bounds.height, path = CGMutablePath()
            path.addRect(CGRect(x: 0, y: 0, width: w - 16, height: 1)); path.addRect(CGRect(x: w - 1, y: 16, width: 1, height: h - 16))
            path.addRect(CGRect(x: 0, y: h - 1, width: w, height: 1)); path.addRect(CGRect(x: 0, y: 0, width: 1, height: h))
            edges.frame = bounds; edges.path = path; edges.fillColor = ring
            tri.frame = CGRect(x: 24, y: 20, width: 4, height: 16)
            let rule = HUDLayer.plain(); rule.frame = CGRect(x: 24, y: 48, width: w - 48, height: 1); rule.backgroundColor = ring
            title.text = "BOOT LOG"
            title.lineFrame = CGRect(x: 38, y: 22.5, width: ceil(title.preferredWidth), height: 11)
            cursor.frame = CGRect(x: 24, y: Self.firstLineTop + 3.5, width: 9, height: 15)
            [plate, bar, edges, tri, title, terminal, rule, cursor].forEach(term.addSublayer)
            layer.addSublayer(term)
        }
    }

    /// Opens the panel (bandIn .3 s SNAP) and reveals `lines`: the first at
    /// 280 ms, the second 260 ms later, then every 190 ms; 900 ms after the
    /// last one it finishes.
    func play(_ lines: [Line], version: String) {
        reset()
        isPlaying = true
        let shown = Array(lines.prefix(Self.capacity))
        HUDMotion.perform {
            // The accent follows the theme colour as of this boot.
            bar.backgroundColor = HUDColor.yellow; cursor.backgroundColor = HUDColor.yellow
            tri.showArtwork(HUDArtwork.triStrip(size: tri.bounds.size, scale: env.renderScale), scale: env.renderScale)
            terminal.text = "ENDFIELD INDUSTRIES TERMINAL v\(version)"
            let x = title.lineFrame.maxX + 10
            terminal.lineFrame = CGRect(x: x, y: 22.5, width: Self.size.width - 24 - x, height: 11)
            rows = shown.enumerated().map { index, line in
                let row = HUDTextLayer(style: Style.line)
                row.runs = Self.runs(line)
                let x: CGFloat = line.kind == .sub ? 42 : 24
                row.lineFrame = CGRect(x: x, y: Self.slotTop(index) + (Self.lineHeight - 12.5) / 2, width: Self.size.width - 24 - x, height: 12.5)
                row.opacity = 0
                row.setValue(-6, forKeyPath: "transform.translation.x")
                term.insertSublayer(row, below: cursor)
                return row
            }
            moveCursor(to: 0)
            layer.isHidden = false
        }
        env.applyScale(to: layer)
        HUDMotion.wipe(term, from: .centerX, duration: HUDDuration.bandIn, timing: HUDEasing.snap, env: env)
        startBlink()
        pending = timers.after(rows.isEmpty ? 0.9 : 0.28) { [weak self] in self?.step() }
    }

    /// Finishes at once: the wipe-up, then `onFinish`.
    func skip() { finish() }

    private func step() {
        pending = nil
        guard isPlaying, !finishing else { return }
        guard revealedCount < rows.count else { finish(); return }
        let row = rows[revealedCount]
        HUDMotion.animate(row, "opacity", to: Float(1), duration: 0.16, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(row, "transform.translation.x", to: 0, duration: 0.2, env: env)
        revealedCount += 1
        HUDMotion.perform { moveCursor(to: revealedCount) }
        let last = revealedCount == rows.count
        pending = timers.after(last ? 0.9 : (revealedCount == 1 ? 0.26 : 0.19)) { [weak self] in self?.step() }
    }

    /// bootOut .38 s E: the clip rises from the bottom while fading to .2.
    private func finish() {
        guard isPlaying, !finishing else { return }
        finishing = true
        pending?.cancel(); pending = nil
        HUDMotion.removeWipe(term)
        guard !env.reducesMotion else { complete(); return }
        let clip = HUDLayer.plain("boot.out"), bounds = term.bounds
        clip.backgroundColor = HUDColor.black(1)
        clip.anchorPoint = CGPoint(x: 0.5, y: 0)
        clip.bounds = bounds; clip.position = CGPoint(x: bounds.midX, y: bounds.minY)
        HUDMotion.perform { term.mask = clip }
        let wipe = CABasicAnimation(keyPath: "transform.scale.y"); wipe.fromValue = 1; wipe.toValue = 0
        wipe.duration = 0.38; wipe.timingFunction = HUDEasing.standard
        wipe.fillMode = .forwards; wipe.isRemovedOnCompletion = false
        clip.add(wipe, forKey: "hud.bootOut")
        let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 1; fade.toValue = 0.2
        fade.duration = 0.38; fade.timingFunction = HUDEasing.standard
        fade.fillMode = .forwards; fade.isRemovedOnCompletion = false
        term.add(fade, forKey: "hud.bootOut")
        pending = timers.after(0.38) { [weak self] in self?.complete() }
    }

    private func complete() {
        reset()
        onFinish?()
    }

    /// Back to the idle, hidden panel with no loops.
    private func reset() {
        pending?.cancel(); pending = nil
        HUDMotion.removeWipe(term)
        term.removeAllAnimations(); cursor.removeAllAnimations()
        HUDMotion.perform {
            term.mask = nil
            rows.forEach { $0.removeFromSuperlayer() }
            layer.isHidden = true
        }
        rows = []; revealedCount = 0
        isPlaying = false; finishing = false
    }

    /// blink 1 s steps(2): 1, .6, .2, .6.
    private func startBlink() {
        guard !env.reducesMotion else { return }
        let blink = CAKeyframeAnimation(keyPath: "opacity")
        blink.values = [1, 0.6, 0.2, 0.6]; blink.keyTimes = [0, 0.25, 0.5, 0.75, 1]
        blink.calculationMode = .discrete; blink.duration = 1; blink.repeatCount = .infinity
        cursor.add(blink, forKey: "hud.blink")
    }

    /// The cursor sits on its own line below the last revealed one.
    private func moveCursor(to slot: Int) {
        cursor.frame.origin = CGPoint(x: 24, y: Self.slotTop(slot) + (Self.lineHeight - 15) / 2)
    }

    static func slotTop(_ index: Int) -> CGFloat { firstLineTop + CGFloat(index) * lineHeight }

    /// The segments as runs; the last character of every segment but the last
    /// carries the 8 px flex gap.
    static func runs(_ line: Line) -> [HUDText.Run] {
        let segments = line.segments
        let gap = Style.line.tracking + 8 / Style.line.size
        return segments.enumerated().flatMap { index, segment -> [HUDText.Run] in
            let style = Style.line.colored(segment.color)
            guard index < segments.count - 1, let last = segment.text.last else { return [HUDText.Run(segment.text, style)] }
            var spaced = style; spaced.tracking = gap
            return [HUDText.Run(String(segment.text.dropLast()), style), HUDText.Run(String(last), spaced)]
        }.filter { !$0.text.isEmpty }
    }

    // MARK: Verification

    var cursorFrameForTesting: CGRect { cursor.frame }
    var rowTextsForTesting: [String] { rows.map(\.text) }
    var isBlinkingForTesting: Bool { cursor.animation(forKey: "hud.blink") != nil }
    var isFinishingForTesting: Bool { finishing }
    var terminalTextForTesting: String { terminal.text }
}

/// The boot log's lines from data the HUD already has (spec §3.28). A line
/// whose data is unavailable (nil) is left out.
enum HUDBootLogContent {
    static func lines(date: Date, uptime: TimeInterval, battery: (percent: Int, charging: Bool, tone: CGColor)?,
                      eventsToday: [String], sanity: (current: Int, maximum: Int, nextIn: TimeInterval?)?,
                      workStreak: (days: Int, totalHours: Double)?, disk: (freePercent: Int, low: Bool)?,
                      operatorName: String, calendar: Calendar = .current) -> [HUDBootLog.Line] {
        typealias Line = HUDBootLog.Line
        var lines: [Line] = [.header("BOOT SEQUENCE", HUDFormat.dotted(date, time: true, calendar: calendar))]
        let seconds = uptime.isFinite ? max(0, Int(uptime)) : 0
        lines.append(.entry("UPTIME", "\(seconds / 86_400)D \(HUDFormat.pad2(seconds % 86_400 / 3600))H \(HUDFormat.pad2(seconds % 3600 / 60))M"))
        if let battery {
            lines.append(.entry("POWER", "\(battery.percent)%", tone: .color(battery.tone),
                                trailing: battery.charging ? "CHARGE MODE" : "POWER MODE"))
        }
        let count = eventsToday.count
        lines.append(.entry("CALENDAR", "\(count) EVENT\(count == 1 ? "" : "S") TODAY"))
        for title in eventsToday.prefix(2) {
            let text = title.components(separatedBy: .newlines).joined(separator: " ").trimmingCharacters(in: .whitespaces)
            if !text.isEmpty { lines.append(.sub(text)) }
        }
        if let sanity {
            let next = sanity.nextIn.flatMap { $0.isFinite ? "NEXT +1 IN " + HUDFormat.clock(seconds: Int(ceil(max(0, $0)))) : nil }
            lines.append(.entry("SANITY", "\(sanity.current) / \(sanity.maximum)", trailing: next))
        }
        if let workStreak {
            lines.append(.entry("WORK MODE STREAK", "\(workStreak.days) DAY\(workStreak.days == 1 ? "" : "S")",
                                trailing: String(format: "%.1f H TOTAL", workStreak.totalHours)))
        }
        if let disk {
            lines.append(disk.low ? .entry("FACILITY", "DISK \(disk.freePercent)% FREE", tone: .warning) : .entry("FACILITY", "NOMINAL", tone: .good))
        }
        let name = operatorName.trimmingCharacters(in: .whitespacesAndNewlines)
        let greeting = (name.isEmpty ? "ENDMINISTRATOR" : name).uppercased(with: Locale(identifier: "en_US_POSIX"))
        lines.append(.footer("ALL SYSTEMS ONLINE.", "WELCOME BACK, \(greeting)."))
        return lines
    }
}
