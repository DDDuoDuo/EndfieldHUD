import AppKit
import QuartzCore

/// Merge! OrbiPom! (spec §3.11): the original game session and physics
/// (OrbiPomSession / OrbiPomRuntime) drawn on the redesigned board, with the
/// SCORE / NEXT plates, the SP bar, the four skills and Pause / Restart in a
/// narrow column on its right. The "?" opens the instructions. The frame clock
/// runs only while the module is the visible, active screen.
final class OrbiPomCanvas: HUDDesignCanvas {
    /// The original container (230 × 280) plus its drop zone, scaled to the
    /// demo's 375 px board height: one game unit is 1.2 px.
    static let unit: CGFloat = 1.2
    static let dropZone: CGFloat = 32.5
    static let world = CGSize(width: 230, height: 280)
    /// Centred in the box; the space on its left is kept free.
    static let board = CGRect(x: ((HUDMetric.box.width - world.width * unit) / 2).rounded(), y: HUDMetric.headerHeight,
                              width: world.width * unit, height: 375)
    /// The HUD column right of the board.
    static let column = CGRect(x: board.maxX + 12, y: board.minY, width: HUDMetric.box.width - board.maxX - 12, height: board.height)
    /// Box px → game units.
    static func world(_ point: CGPoint) -> CGPoint {
        CGPoint(x: (point.x - board.minX) / unit, y: (point.y - board.minY) / unit - dropZone)
    }
    /// Game units → board px.
    static func boardPoint(x: Double, y: Double) -> CGPoint {
        CGPoint(x: CGFloat(x) * unit, y: (CGFloat(y) + dropZone) * unit)
    }

    let session: OrbiPomSession
    private let foregroundProvider: () -> Bool
    private var notifications: [NSObjectProtocol] = []
    private var timer: Timer?, lastTime: TimeInterval = 0
    private var foreground = true
    private(set) var restartConfirmation = false
    /// The instructions pop-over is open (play holds meanwhile).
    private(set) var rulesPresented = false
    private weak var rulesButton: HUDIconButton?
    var automaticallySchedulesFrames = true
    var hasFrameTimer: Bool { timer != nil }
    var bodyLayerCount: Int { bodies.count }
    private var pointerPressed = false
    private var keyboardPoint = CGPoint(x: 115, y: 120)
    private var lastSkill: OrbiPomSkill?
    private var lastSkillUses = 0
    private var lastState = "idle"

    // Board layers of the current page (rebuilt with the page, updated per frame).
    private weak var pieces: CALayer?
    private var ghost: OrbiPomPiece?
    private var dangerShown = false
    private weak var guide: CALayer?
    private weak var danger: CAShapeLayer?
    private weak var countdown: HUDTextLayer?
    private weak var wind: CAShapeLayer?
    private weak var overlay: CALayer?
    private weak var overlayText: HUDTextLayer?
    private weak var nextWell: CALayer?
    private var nextPiece: OrbiPomPiece?
    private var scorePlate: HUDStatPlate?
    private weak var bestText: HUDTextLayer?
    private weak var energyText: HUDTextLayer?
    private var cells: HUDCells?
    private var skillButtons: [OrbiPomSkill: OrbiPomSkillButton] = [:]
    private weak var pauseButton: HUDButton?
    private weak var resumeButton: HUDButton?
    private var bodies: [Int: OrbiPomPiece] = [:]
    private var renderedBodies: [Int: OrbiPomBody] = [:]
    private var rendered: (score: Int, best: Int, energy: Int, progress: Int, next: Int, ghost: Int, danger: String?,
                           overlay: String?, controls: String)?

    init(context: HUDDesignContext, session: OrbiPomSession, foregroundProvider: @escaping () -> Bool = { NSApp?.isActive ?? true }) {
        self.session = session
        self.foregroundProvider = foregroundProvider
        super.init(module: .minigame, context: context)
        for name in [NSApplication.didResignActiveNotification, NSApplication.didBecomeActiveNotification] {
            notifications.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self] note in
                guard let self else { return }
                self.foreground = note.name == NSApplication.didBecomeActiveNotification; self.reconcileClock()
            })
        }
        notifications.append(NSWorkspace.shared.notificationCenter.addObserver(forName: NSWorkspace.willSleepNotification, object: nil, queue: .main) { [weak self] _ in
            self?.foreground = false; self?.reconcileClock()
        })
        notifications.append(NSWorkspace.shared.notificationCenter.addObserver(forName: NSWorkspace.didWakeNotification, object: nil, queue: .main) { [weak self] _ in
            guard let self else { return }; self.foreground = self.foregroundProvider(); self.reconcileClock()
        })
    }

    deinit {
        timer?.invalidate(); session.pause(true)
        notifications.forEach { NotificationCenter.default.removeObserver($0); NSWorkspace.shared.notificationCenter.removeObserver($0) }
    }


    // MARK: Lifecycle and clock

    override func didActivate() {
        foreground = foregroundProvider()
        if session.runtime == nil || session.snapshot.state == "idle" { startGame(announce: false) }
        lastSkillUses = session.snapshot.skillUseCount; lastState = session.snapshot.state
        if session.error != nil { showError() }
        reconcileClock()
    }

    override func didDeactivate() {
        pointerPressed = false
        OrbiPomArtwork.releaseDecodedImages()
        if restartConfirmation { restartConfirmation = false; context.overlay.dismissConfirm() }
        reconcileClock()
    }

    override func willLeave() { pointerPressed = false; stopClock(); session.pause(true) }

    private var shouldRun: Bool {
        isActive && foreground && !session.manuallyPaused && !restartConfirmation && !rulesPresented && session.error == nil
    }

    private func reconcileClock() {
        let run = shouldRun
        if session.runtime != nil && session.snapshot.paused == run { session.pause(!run) }
        let interval = HUDRuntimeAppearance.configuration.lowPowerVisualMode ? 1.0 / 30 : 1.0 / 60
        let wantsTimer = run && session.snapshot.isPlaying && automaticallySchedulesFrames
        if !wantsTimer || timer.map({ abs($0.timeInterval - interval) > 0.001 }) == true { stopClock() }
        if wantsTimer && timer == nil {
            lastTime = ProcessInfo.processInfo.systemUptime
            let clock = Timer(timeInterval: interval, repeats: true) { [weak self] _ in
                guard let self else { return }
                let now = ProcessInfo.processInfo.systemUptime
                let elapsed = now - self.lastTime; self.lastTime = now
                self.advance(seconds: elapsed)
            }
            clock.tolerance = 0.001; timer = clock
            RunLoop.main.add(clock, forMode: .common)
        }
        render()
    }

    private func stopClock() { timer?.invalidate(); timer = nil; lastTime = 0 }

    func advance(seconds: Double) {
        guard shouldRun else { return }
        session.advance(seconds: seconds)
        render()
        if !session.snapshot.canAdvance || session.error != nil { reconcileClock() }
    }

    private func startGame(announce: Bool) {
        _ = session.start()
        lastSkillUses = 0; lastState = session.snapshot.state; lastSkill = nil
        if session.error != nil { showError() }
        if announce { toast(icon: .reset, "New game", "新的一局") }
        reconcileClock()
    }

    private func showError() { toast(icon: .warn, "Notice", "提示", detail: session.error ?? "") }

    // MARK: Actions

    /// Accessibility, keyboard and harness entry (the original action ids).
    func perform(_ id: String) {
        guard isActive else { return }
        switch id {
        case "start": startGame(announce: false)
        case "pause": togglePause()
        case "rules": if rulesPresented { closePopover() } else { openRules() }
        case "restart": requestRestart()
        case "cancelRestart":
            guard restartConfirmation else { return }
            restartConfirmation = false; context.overlay.dismissConfirm(); reconcileClock()
        case "confirmRestart":
            if restartConfirmation { restartConfirmation = false; context.overlay.dismissConfirm() }
            startGame(announce: true)
        case "cancelSkill": session.runtime?.cancelSkill(); render()
        default:
            guard let skill = OrbiPomSkill(rawValue: id) else { return }
            if session.snapshot.skill == skill { session.runtime?.cancelSkill(); render(); return }
            guard session.snapshot.canUse(skill) else { return }
            if session.runtime?.activate(skill) == true { lastSkill = skill }
            render()
        }
    }

    private func togglePause() {
        guard session.snapshot.isPlaying else { return }
        session.setManuallyPaused(!session.manuallyPaused)
        reconcileClock()
    }

    /// The "?" toggles the instructions (Esc, its close button or a press
    /// outside close them too); play holds while they are open.
    private func openRules() {
        let guide = OrbiPomGuide.popover(env: env)
        openPopover(guide, anchor: rulesButton, controls: [])
        rulesPresented = true; pointerPressed = false
        rulesButton?.isOn = true
        reconcileClock()
    }

    override func closePopover() {
        super.closePopover()
        guard rulesPresented else { return }
        rulesPresented = false
        rulesButton?.isOn = false
        reconcileClock()
    }

    private func requestRestart() {
        guard session.snapshot.isPlaying else { startGame(announce: true); return }
        restartConfirmation = true
        reconcileClock()
        confirm(HUDConfirmRequest(id: "minigameRestart", title: L10n.text("Restart the game?", "重新开始游戏？"),
                                  message: L10n.text("The current score is lost.", "当前得分将会丢失。"),
                                  confirmTitle: L10n.text("Restart", "重新开始"), icon: .reset),
                onConfirm: { [weak self] in
                    guard let self else { return }
                    self.restartConfirmation = false
                    self.startGame(announce: true)
                },
                onCancel: { [weak self] in
                    self?.restartConfirmation = false
                    self?.reconcileClock()
                })
    }

    // MARK: Pointer and keys

    /// The original only aims and drops inside its container; the drop zone
    /// above it counts as its top edge unless a skill is choosing a target.
    private func gamePoint(_ point: CGPoint) -> CGPoint {
        let p = Self.world(point)
        guard session.snapshot.skill == nil else { return p }
        return CGPoint(x: p.x, y: min(Self.world.height, max(0, p.y)))
    }

    private func aim(at point: CGPoint) {
        guard !restartConfirmation else { return }
        let p = gamePoint(point)
        keyboardPoint = p
        session.runtime?.movePointer(x: Double(p.x), y: Double(p.y))
        render()
    }

    override func handlePointerMoved(at point: CGPoint?) {
        guard let point, Self.board.contains(point) else { return }
        aim(at: point)
    }

    override func handlePointerDown(at point: CGPoint, event: NSEvent?) -> Bool {
        guard Self.board.contains(point) else { return false }
        pointerPressed = !restartConfirmation
        aim(at: point)
        return true
    }

    override func handlePointerDragged(to point: CGPoint) { if pointerPressed, Self.board.contains(point) { aim(at: point) } }

    override func handlePointerUp(at point: CGPoint) {
        defer { pointerPressed = false }
        guard pointerPressed, Self.board.contains(point), !restartConfirmation else { return }
        let p = gamePoint(point)
        session.runtime?.pointerUp(x: Double(p.x), y: Double(p.y))
        render()
    }

    /// Space pauses (spec §1.12); Return drops at the keyboard aim, the arrows
    /// aim, 1–4 pick a skill, P pauses, Esc cancels a selected skill.
    override func handleKey(_ event: NSEvent) -> Bool {
        guard event.modifierFlags.intersection([.command, .control, .option]).isEmpty else { return false }
        let s = session.snapshot
        switch event.keyCode {
        case 49: if !event.isARepeat { togglePause() }; return true
        case 53:
            guard s.skill != nil else { return false }
            perform("cancelSkill"); return true
        case 36, 76:
            guard s.canAdvance else { return true }
            if s.skill != nil { session.runtime?.pointerUp(x: Double(keyboardPoint.x), y: Double(keyboardPoint.y)) }
            else { _ = session.runtime?.drop() }
            render(); return true
        case 123...126:
            if s.skill == nil { keyboardPoint.x = CGFloat(s.previewX) }
            switch event.keyCode {
            case 123: keyboardPoint.x -= 6
            case 124: keyboardPoint.x += 6
            case 125: keyboardPoint.y += 6
            default: keyboardPoint.y -= 6
            }
            keyboardPoint.x = min(Self.world.width, max(0, keyboardPoint.x))
            keyboardPoint.y = min(Self.world.height, max(-Self.dropZone, keyboardPoint.y))
            session.runtime?.movePointer(x: Double(keyboardPoint.x), y: Double(keyboardPoint.y)); render()
            return true
        default: break
        }
        guard let c = event.charactersIgnoringModifiers?.lowercased() else { return false }
        if c == "p" { if !event.isARepeat { togglePause() }; return true }
        if let n = Int(c), (1...4).contains(n) {
            if !event.isARepeat { perform(OrbiPomSkill.allCases[n - 1].rawValue) }
            return true
        }
        return false
    }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        bodies = [:]; renderedBodies = [:]; rendered = nil; skillButtons = [:]; dangerShown = false
        let rules = HUDIconButton(id: "rules", label: L10n.text("How to play", "玩法说明"), icon: .help, round: true, env: env)
        rules.onActivate = { [weak self] in self?.perform("rules") }
        rules.isOn = rulesPresented
        rulesButton = rules
        header.configure(crumbs: ["MINIGAME"], code: "MOD-06", title: HUDModuleTitle("Minigame", "小游戏"),
                         counter: "SCORE", showsBack: false, actions: [rules])
        let board = makeBoard()
        HUDMotion.perform { page.layer.addSublayer(board) }
        page.stagger(board)
        buildColumn(page)
        render()
    }

    private func makeBoard() -> CALayer {
        let frame = Self.board
        let board = HUDLayer.plain("orbipom.board")
        let grid = HUDLayer.shape("orbipom.grid"), line = HUDLayer.shape("orbipom.danger")
        let wind = HUDLayer.shape("orbipom.wind"), pieces = HUDLayer.plain("orbipom.pieces")
        let guide = HUDLayer.plain("orbipom.guide"), ghost = OrbiPomPiece(env: env)
        let countdown = HUDTextLayer(style: HUDTextStyle(size: 15, weight: .bold, color: HUDColor.red, tabular: true), alignment: .right)
        let overlay = HUDLayer.plain("orbipom.overlay"), overlayText = HUDTextLayer(style: Self.overlayTitle, alignment: .center)
        let ring = HUDLayer.plain("orbipom.ring")
        HUDMotion.perform {
            board.frame = frame; board.masksToBounds = true
            board.backgroundColor = HUDColor.rgb(0x141414)
            grid.frame = board.bounds; grid.fillColor = HUDColor.white(0.06)
            grid.path = HUDShape.tickLadder(width: frame.width, height: frame.height, pitch: 20)
            wind.frame = board.bounds; wind.fillColor = HUDColor.rgb(0x22A6F2, 0.10)
            wind.strokeColor = HUDColor.rgb(0x22A6F2, 0.45); wind.lineWidth = 1; wind.isHidden = true
            let redLine = Self.boardPoint(x: 0, y: 8).y
            line.frame = board.bounds; line.fillColor = nil; line.lineWidth = 1
            line.strokeColor = HUDColor.rgb(0xE5383B, 0.45); line.lineDashPattern = [5, 4]
            let path = CGMutablePath(); path.move(to: CGPoint(x: 0, y: redLine)); path.addLine(to: CGPoint(x: frame.width, y: redLine))
            line.path = path
            guide.backgroundColor = HUDColor.yellow.copy(alpha: 0.25); guide.isHidden = true
            pieces.frame = board.bounds
            ghost.layer.opacity = 0.5; ghost.layer.isHidden = true
            countdown.lineFrame = CGRect(x: frame.width - 6 - 80, y: 22, width: 80, height: 15); countdown.isHidden = true
            overlay.frame = board.bounds; overlay.backgroundColor = HUDColor.black(0.6); overlay.isHidden = true
            overlayText.lineFrame = CGRect(x: 0, y: (frame.height - 22) / 2, width: frame.width, height: 22)
            overlay.addSublayer(overlayText)
            ring.frame = board.bounds; ring.borderWidth = 1; ring.borderColor = HUDColor.ringField
            [grid, wind, guide, line, pieces, ghost.layer, countdown, overlay, ring].forEach(board.addSublayer)
        }
        self.pieces = pieces; self.ghost = ghost; self.guide = guide; self.danger = line
        self.countdown = countdown; self.wind = wind; self.overlay = overlay; self.overlayText = overlayText
        env.applyScale(to: board)
        return board
    }

    /// The HUD column (spec §3.11), narrowed to the board's right: SCORE,
    /// NEXT, SP, the four skills stacked, then Pause with an icon-only Restart.
    private func buildColumn(_ page: HUDDesignPage) {
        let x = Self.column.minX, width = Self.column.width
        var y = Self.column.minY
        let score = HUDStatPlate(width: width, valueStyle: HUDType.statValue.sized(32).weighted(.light), env: env)
        score.valueMinimumScale = 0.7
        let best = HUDTextLayer(style: HUDType.label, alignment: .right)
        HUDMotion.perform {
            score.layer.frame.origin = CGPoint(x: x, y: y)
            best.lineFrame = CGRect(x: 52, y: 9, width: width - 64, height: 10)
            score.layer.addSublayer(best)
            page.layer.addSublayer(score.layer)
        }
        scorePlate = score; bestText = best
        page.stagger(score.layer)
        y += score.layer.bounds.height + 6

        let next = HUDStatPlate(width: width, valueStyle: HUDType.statValue.sized(32), env: env)
        let well = HUDLayer.plain("orbipom.next")
        let piece = OrbiPomPiece(env: env)
        HUDMotion.perform {
            next.layer.frame.origin = CGPoint(x: x, y: y)
            well.frame = CGRect(x: 12, y: 22, width: width - 24, height: 38)
            well.addSublayer(piece.layer)
            next.layer.addSublayer(well)
            page.layer.addSublayer(next.layer)
        }
        next.set(label: "NEXT", value: "")
        nextWell = well; nextPiece = piece
        page.stagger(next.layer)
        y += next.layer.bounds.height + 6

        let sp = HUDLayer.plain("orbipom.sp")
        let label = HUDTextLayer("SP", style: HUDType.label)
        let energy = HUDTextLayer(style: HUDType.label.colored(HUDColor.legible(HUDColor.cyan)), alignment: .right)
        let bar = HUDCells(count: 12, width: width, capacity: false, height: 10, env: env)
        HUDMotion.perform {
            sp.frame = CGRect(x: x, y: y, width: width, height: 26)
            label.lineFrame = CGRect(x: 0, y: 0, width: 60, height: 10)
            energy.lineFrame = CGRect(x: width - 80, y: 0, width: 80, height: 10)
            bar.layer.frame.origin = CGPoint(x: 0, y: 16)
            [label, energy, bar.layer].forEach(sp.addSublayer)
            page.layer.addSublayer(sp)
        }
        energyText = energy; cells = bar
        page.stagger(sp)
        y += 26 + 8

        let skillHeight: CGFloat = 34, skillGap: CGFloat = 6
        let skills = HUDLayer.plain("orbipom.skills")
        let count = CGFloat(OrbiPomSkill.allCases.count)
        HUDMotion.perform {
            skills.frame = CGRect(x: x, y: y, width: width, height: count * skillHeight + (count - 1) * skillGap)
            page.layer.addSublayer(skills)
        }
        for (index, skill) in OrbiPomSkill.allCases.enumerated() {
            let name = Self.skillName(skill)
            let button = OrbiPomSkillButton(id: skill.rawValue, title: L10n.text(name.english, name.chinese),
                                            icon: OrbiPomArtwork.skill(skill, points: 22, scale: env.renderScale),
                                            size: CGSize(width: width, height: skillHeight), env: env)
            button.onActivate = { [weak self] in self?.perform(skill.rawValue) }
            HUDMotion.perform {
                button.layer.frame.origin = CGPoint(x: 0, y: CGFloat(index) * (skillHeight + skillGap))
                skills.addSublayer(button.layer)
            }
            skillButtons[skill] = button
            page.add(button)
        }
        page.stagger(skills)
        y += skills.bounds.height + 8

        let actions = HUDLayer.plain("orbipom.actions")
        let restart = HUDIconButton(id: "restart", label: L10n.text("Restart", "重新开始"), icon: .reset, side: 32, env: env)
        let flexible = width - 6 - 32
        let pause = HUDButton(id: "pause", title: L10n.text("Pause", "暂停"), icon: .pause, kind: .primary, env: env)
        let resume = HUDButton(id: "resume", title: L10n.text("Resume", "继续"), icon: .play, kind: .primary, env: env)
        pause.place(at: .zero, width: flexible); resume.place(at: .zero, width: flexible)
        HUDMotion.perform { restart.layer.frame.origin = CGPoint(x: width - 32, y: 0) }
        pause.onActivate = { [weak self] in self?.togglePause() }
        resume.onActivate = { [weak self] in self?.togglePause() }
        restart.onActivate = { [weak self] in self?.requestRestart() }
        HUDMotion.perform {
            actions.frame = CGRect(x: x, y: y, width: width, height: 32)
            [pause.layer, resume.layer, restart.layer].forEach(actions.addSublayer)
            page.layer.addSublayer(actions)
        }
        pauseButton = pause; resumeButton = resume
        page.add([pause, resume, restart])
        page.stagger(actions)
        env.applyScale(to: page.layer)
    }

    // MARK: Rendering (in place; the page is never rebuilt per frame)

    private func render() {
        guard pieces != nil else { return }
        let s = session.snapshot
        announce(s)
        let best = max(session.bestScore, s.highScore, s.score)
        let overlayTitle: String?
        if session.error != nil { overlayTitle = "UNAVAILABLE" }
        else if s.state == "over" { overlayTitle = "GAME OVER" }
        else if session.manuallyPaused || restartConfirmation { overlayTitle = "PAUSED" }
        else { overlayTitle = nil }
        let dangerText = s.isPlaying ? s.dangerSeconds.map { String(format: "%.1f", max(0, $0)) } : nil
        let controls = "\(s.state):\(s.paused):\(session.manuallyPaused):\(restartConfirmation):\(s.energy):\(s.swapCharge):"
            + "\(s.skill?.rawValue ?? ""):\(s.skillPhase ?? ""):\(session.error ?? "")"
        let progress = s.energy >= 3 ? 12 : Int(min(12, max(0, s.energyProgress.rounded(.down))))
        let previous = rendered
        rendered = (s.score, best, s.energy, progress, s.nextLevel, s.currentLevel, dangerText, overlayTitle, controls)
        if previous?.score != s.score { scorePlate?.set(label: "SCORE", value: HUDFormat.grouped(s.score)) }
        if previous?.best != best { HUDMotion.perform { bestText?.text = "BEST " + HUDFormat.grouped(best) } }
        if previous?.energy != s.energy { HUDMotion.perform { energyText?.text = HUDFormat.pad2(s.energy) + "/03" } }
        if previous?.progress != progress || previous?.energy != s.energy {
            cells?.set(filled: progress, tone: (HUDColor.cyan, HUDColor.cyan), animated: previous != nil)
        }
        if previous?.next != s.nextLevel, let well = nextWell {
            nextPiece?.configure(level: s.nextLevel, side: min(38, OrbiPomPiece.side(level: s.nextLevel)))
            HUDMotion.perform { nextPiece?.layer.position = CGPoint(x: well.bounds.midX, y: well.bounds.midY) }
        }
        if previous?.overlay != overlayTitle { renderOverlay(overlayTitle) }
        if previous?.danger != dangerText || dangerShown != (dangerText != nil && s.canAdvance) {
            renderDanger(dangerText, pulsing: dangerText != nil && s.canAdvance)
        }
        if previous?.controls != controls { renderControls(s) }
        renderBoard(s, ghostChanged: previous?.ghost != s.currentLevel)
    }

    /// Toasts for results: skills that were cast, and the end of a game.
    private func announce(_ s: OrbiPomSnapshot) {
        if let skill = s.skill { lastSkill = skill }
        if s.skillUseCount > lastSkillUses, let skill = lastSkill {
            let name = Self.skillName(skill)
            toast(icon: Self.skillIcon(skill), name.english, name.chinese, detail: L10n.text("Skill activated", "技能已发动"))
        }
        lastSkillUses = s.skillUseCount
        if s.state == "over", lastState == "playing" {
            toast(icon: .x, "Game over", "游戏结束", detail: HUDFormat.grouped(s.score))
        }
        lastState = s.state
    }

    static func skillName(_ skill: OrbiPomSkill) -> (english: L10n.Text, chinese: L10n.Text) {
        switch skill {
        case .clear: return ("Eliminate", "消除")
        case .wind: return ("Wind", "风场")
        case .shake: return ("Shake", "震动")
        case .swap: return ("Swap", "交换")
        }
    }

    static func skillIcon(_ skill: OrbiPomSkill) -> HUDIcon {
        switch skill { case .clear: return .trash; case .wind: return .right; case .shake: return .refresh; case .swap: return .reset }
    }

    /// The board stays dark in both themes: its overlay title keeps light ink.
    private static var overlayTitle: HUDTextStyle { HUDType.emptyTitle.sized(22).colored(HUDColor.microTagText) }

    private func renderOverlay(_ title: String?) {
        HUDMotion.perform {
            overlay?.isHidden = title == nil
            let bracket = Self.overlayTitle.colored(HUDColor.yellow)
            overlayText?.runs = [HUDText.Run("[ ", bracket), HUDText.Run(title ?? "", Self.overlayTitle), HUDText.Run(" ]", bracket)]
        }
        if let overlay, title != nil { HUDMotion.animate(overlay, "opacity", to: Float(1), duration: 0.16, timing: HUDEasing.ease, env: env) }
    }

    /// The red line pulses between #E5383B and #661b1d while a countdown runs.
    private func renderDanger(_ text: String?, pulsing: Bool) {
        guard let danger else { return }
        dangerShown = pulsing
        HUDMotion.perform {
            countdown?.isHidden = text == nil
            countdown?.text = text ?? ""
            danger.strokeColor = text != nil ? HUDColor.red : HUDColor.rgb(0xE5383B, 0.45)
            danger.lineDashPattern = text != nil ? nil : [5, 4]
            danger.lineWidth = text != nil ? 1.5 : 1
        }
        guard pulsing, !env.reducesMotion else { danger.removeAnimation(forKey: "hud.danger"); return }
        guard danger.animation(forKey: "hud.danger") == nil else { return }
        let pulse = CABasicAnimation(keyPath: "strokeColor")
        pulse.fromValue = HUDColor.red; pulse.toValue = HUDColor.rgb(0x661B1D)
        pulse.duration = 1.0 / 3; pulse.autoreverses = true; pulse.repeatCount = .infinity
        pulse.timingFunction = HUDEasing.easeInOut
        danger.add(pulse, forKey: "hud.danger")
    }

    private func renderControls(_ s: OrbiPomSnapshot) {
        let paused = session.manuallyPaused
        HUDMotion.perform {
            pauseButton?.layer.isHidden = paused; resumeButton?.layer.isHidden = !paused
        }
        pauseButton?.isEnabled = s.isPlaying && !restartConfirmation
        resumeButton?.isEnabled = s.isPlaying && !restartConfirmation
        for (skill, button) in skillButtons {
            button.cost = skill == .swap ? "\(min(6, s.swapCharge))/6" : String(skill.energyCost)
            button.isOn = s.skill == skill
            button.isEnabled = !restartConfirmation && (s.skill == skill || s.canUse(skill))
        }
    }

    private func renderBoard(_ s: OrbiPomSnapshot, ghostChanged: Bool) {
        guard let pieces else { return }
        let wanted = Set(s.bodies.map(\.id))
        HUDMotion.perform {
            for id in Array(bodies.keys) where !wanted.contains(id) {
                bodies.removeValue(forKey: id)?.layer.removeFromSuperlayer(); renderedBodies[id] = nil
            }
            for body in s.bodies {
                let piece: OrbiPomPiece
                if let existing = bodies[body.id] { piece = existing }
                else {
                    piece = OrbiPomPiece(env: env)
                    piece.configure(level: body.level, side: CGFloat(body.size) * Self.unit)
                    pieces.addSublayer(piece.layer); bodies[body.id] = piece
                }
                if renderedBodies[body.id] != body {
                    piece.place(at: Self.boardPoint(x: body.x, y: body.y), angle: CGFloat(body.angle),
                                scale: CGFloat(body.scale), opacity: Float(body.opacity))
                    renderedBodies[body.id] = body
                }
                piece.isMarked = s.selectedBodyIDs.contains(body.id) || s.hoverBodyID == body.id
            }
            let showGhost = s.previewVisible && s.canAdvance && s.skill == nil
            if let ghost {
                if ghostChanged { ghost.configure(level: s.currentLevel, side: CGFloat(s.previewSize) * Self.unit) }
                ghost.layer.isHidden = !showGhost
                if showGhost { ghost.place(at: Self.boardPoint(x: s.previewX, y: s.previewY), angle: 0, scale: CGFloat(s.previewScale), opacity: 0.5) }
            }
            if let guide {
                guide.isHidden = !showGhost
                let x = Self.boardPoint(x: s.previewX, y: 0).x
                guide.frame = CGRect(x: x - 0.5, y: 20, width: 1, height: Self.board.height - 20)
            }
            if let wind {
                wind.isHidden = s.windSurfaceY == nil
                if let surface = s.windSurfaceY {
                    let top = Self.boardPoint(x: 0, y: surface).y
                    wind.path = CGPath(rect: CGRect(x: 0, y: top, width: Self.board.width, height: max(0, Self.board.height - top)), transform: nil)
                }
            }
        }
    }
}

/// The "?" instructions (spec §2.17 pop-over, two columns): the goal, the
/// controls and the lose line on the left; scoring, SP and the skills on the
/// right. Every rule is the original controller's (orbipom.js): merges score
/// 1, 3, 6 … 66, 12 merges give 1 SP up to 3, a 5 s countdown above the red
/// line, the skill costs and Swap's charge from SP spent on the other skills.
private enum OrbiPomGuide {
    static let width: CGFloat = 566, gutter: CGFloat = 20

    static func popover(env: HUDDesignEnvironment) -> HUDPopover {
        let columnWidth = (width - 24 - gutter) / 2
        var left = Column(width: columnWidth), right = Column(width: columnWidth)
        left.heading(L10n.text("Goal", "目标"))
        left.paragraph(L10n.text("Drop pieces into the box. Two identical pieces that touch merge into the next, larger piece. Two of the largest (level 11) merge and vanish.",
                                 "将山团团投入容器。两个相同的山团团接触后，会合成更大一级的山团团。两个最大的山团团（第 11 级）合成后会消失。"))
        left.heading(L10n.text("Controls", "操作"))
        left.key(L10n.text("Aim", "瞄准"), L10n.text("Move the pointer over the box, or the arrow keys", "在容器上移动指针，或使用方向键"))
        left.key(L10n.text("Drop", "投放"), L10n.text("Click, or Return", "点击，或按 Return 键"))
        left.key(L10n.text("Skill", "技能"), L10n.text("Click a skill, or 1–4", "点击技能，或按 1–4"))
        left.key(L10n.text("Cancel skill", "取消技能"), L10n.text("Esc, or click the skill again", "按 Esc，或再次点击该技能"))
        left.key(L10n.text("Pause", "暂停"), L10n.text("Space or P", "空格键或 P"))
        left.heading(L10n.text("Game over", "游戏结束"))
        left.paragraph(L10n.text("Pieces resting above the red dashed line start a 5-second countdown. Get them back below the line in time, or the game ends.",
                                 "山团团停在红色虚线上方时，会开始 5 秒倒计时。倒计时结束前让它们回到线下，否则游戏结束。"))
        right.heading(L10n.text("Score", "计分"))
        right.paragraph(L10n.text("Bigger merges score more: 1, 3, 6, 10 … up to 66 points. New pieces come from the five smallest sizes; NEXT shows the one after the current piece. BEST keeps your highest score.",
                                  "合成的山团团越大，得分越高：1、3、6、10……最高 66 分。新的山团团来自最小的五种；NEXT 显示当前之后的一个。BEST 记录最高分。"))
        right.heading(L10n.text("SP and skills", "能量与技能"))
        right.paragraph(L10n.text("Each merge fills one SP cell; 12 cells give 1 SP, up to 3. Choose a skill, then click a piece or the box to use it.",
                                  "每次合成填充一格能量，12 格积满获得 1 点 SP，最多 3 点。选择技能后，点击山团团或容器即可发动。"))
        let costs = [L10n.text("1 SP", "1 SP"), L10n.text("2 SP", "2 SP"), L10n.text("3 SP", "3 SP"), L10n.text("6 SP spent", "消耗 6 SP")]
        let details = [L10n.text("Removes the piece you pick.", "移除选中的山团团。"),
                       L10n.text("A rising wind fills the box for a few seconds and lifts the pieces.", "上升气流充满容器数秒，托起山团团。"),
                       L10n.text("Shakes the box from side to side.", "左右摇晃容器。"),
                       L10n.text("Pick two pieces to trade places. It charges with the SP you spend on the other skills.",
                                 "选择两个山团团交换位置。在其他技能上消耗 SP 即可充能。")]
        for (index, skill) in OrbiPomSkill.allCases.enumerated() {
            right.skill(skill, cost: costs[index], detail: details[index], env: env)
        }
        let popover = HUDPopover(title: L10n.text("How to play", "玩法说明").uppercased(), width: width,
                                 contentHeight: max(left.height, right.height), env: env)
        let rule = HUDLayer.plain("orbipom.guide.rule")
        HUDMotion.perform {
            left.layer.frame = CGRect(x: 0, y: 0, width: columnWidth, height: left.height)
            right.layer.frame = CGRect(x: columnWidth + gutter, y: 0, width: columnWidth, height: right.height)
            rule.frame = CGRect(x: columnWidth + gutter / 2 - 0.5, y: 2, width: 1, height: max(left.height, right.height) - 4)
            rule.backgroundColor = HUDColor.separator
            [left.layer, rule, right.layer].forEach(popover.content.addSublayer)
        }
        return popover
    }

    /// One column, laid out top-down as it is filled.
    struct Column {
        let width: CGFloat
        let layer = HUDLayer.plain("orbipom.guide.column")
        private(set) var height: CGFloat = 0
        static var body: HUDTextStyle { HUDTextStyle(size: 11.5, weight: .regular, color: HUDColor.menuText, lineHeight: 1.45) }
        static var keyStyle: HUDTextStyle { HUDTextStyle(size: 11.5, weight: .regular, color: HUDColor.steel, lineHeight: 1.45) }
        static var detail: HUDTextStyle { HUDTextStyle(size: 11, weight: .regular, color: HUDColor.description, lineHeight: 1.4) }
        static let keyWidth: CGFloat = 76, iconSide: CGFloat = 22

        init(width: CGFloat) { self.width = width }

        mutating func heading(_ text: String) {
            if height > 0 { height += 14 }
            add(text, HUDType.label.colored(HUDColor.accentInk), frame: CGRect(x: 0, y: height, width: width, height: 10))
            height += 10 + 7
        }

        mutating func paragraph(_ text: String) {
            let lines = Self.height(text, Self.body, width: width)
            add(text, Self.body, frame: CGRect(x: 0, y: height, width: width, height: lines))
            height += lines
        }

        mutating func key(_ name: String, _ value: String) {
            let valueWidth = width - Self.keyWidth - 6
            let rows = max(Self.height(name, Self.keyStyle, width: Self.keyWidth), Self.height(value, Self.body, width: valueWidth))
            add(name, Self.keyStyle, frame: CGRect(x: 0, y: height, width: Self.keyWidth, height: Self.height(name, Self.keyStyle, width: Self.keyWidth)))
            add(value, Self.body, frame: CGRect(x: Self.keyWidth + 6, y: height, width: valueWidth, height: Self.height(value, Self.body, width: valueWidth)))
            height += rows + 2
        }

        /// The skill's icon, name and cost on one line, what it does under them.
        mutating func skill(_ skill: OrbiPomSkill, cost: String, detail: String, env: HUDDesignEnvironment) {
            height += 8
            let name = OrbiPomCanvas.skillName(skill), left = Self.iconSide + 8, textWidth = width - left
            let icon = HUDLayer.plain("orbipom.guide.icon")
            let top = height
            HUDMotion.perform {
                icon.frame = CGRect(x: 0, y: top, width: Self.iconSide, height: Self.iconSide)
                icon.contents = OrbiPomArtwork.skill(skill, points: Self.iconSide, scale: env.renderScale)
                icon.contentsGravity = .resizeAspect
                layer.addSublayer(icon)
            }
            add(L10n.text(name.english, name.chinese), HUDTextStyle(size: 12, weight: .medium, color: HUDColor.textOnDark),
                frame: CGRect(x: left, y: top + 1, width: textWidth - 80, height: 12))
            add(cost.uppercased(), HUDTextStyle(size: 10, weight: .bold, color: HUDColor.accentInk, tabular: true),
                frame: CGRect(x: width - 80, y: top + 2, width: 80, height: 10), alignment: .right)
            let lines = Self.height(detail, Self.detail, width: textWidth)
            add(detail, Self.detail, frame: CGRect(x: left, y: top + 17, width: textWidth, height: lines))
            height = top + max(Self.iconSide, 17 + lines)
        }

        private func add(_ text: String, _ style: HUDTextStyle, frame: CGRect, alignment: HUDTextLayer.Alignment = .left) {
            let label = HUDTextLayer(text, style: style, alignment: alignment)
            HUDMotion.perform {
                label.maximumLines = 0; label.truncates = false
                label.lineFrame = frame
                layer.addSublayer(label)
            }
        }

        /// The wrapped height of `text` at `width`.
        static func height(_ text: String, _ style: HUDTextStyle, width: CGFloat) -> CGFloat {
            let string = HUDText.attributed([HUDText.Run(text, style)], language: HUDTextLanguage.tag())
            let lines = HUDText.lines(string, width: width, maximumLines: 0, truncate: false).count
            return CGFloat(max(1, lines)) * (style.size * style.lineHeight).rounded(.up)
        }
    }
}

/// The original OrbiPom artwork (Resources/OrbiPom, as in v1.2.0): each
/// image is decoded once per pixel size, downscaled by ImageIO, and shared by
/// every layer that shows it, so the animating board never decodes.
enum OrbiPomArtwork {
    private static var images: [String: CGImage] = [:]

    static func level(_ level: Int, points: CGFloat, scale: CGFloat) -> CGImage? {
        image("level-\(min(max(level, 1), 11))", points: points, scale: scale)
    }
    static func skill(_ skill: OrbiPomSkill, points: CGFloat, scale: CGFloat) -> CGImage? {
        image("skill-" + skill.rawValue, points: points, scale: scale)
    }
    /// Drops the decoded copies (layers keep what they show).
    static func releaseDecodedImages() { images.removeAll(keepingCapacity: false) }
    static var decodedCount: Int { images.count }

    /// The longest side in pixels, rounded up to 16 so nearby sizes share one image.
    private static func image(_ name: String, points: CGFloat, scale: CGFloat) -> CGImage? {
        let pixels = max(16, Int((points * max(1, scale) / 16).rounded(.up)) * 16)
        let key = name + "@\(pixels)"
        if let cached = images[key] { return cached }
        guard let url = HUDResources.url(for: "OrbiPom/" + name + ".png"),
              let source = CGImageSourceCreateWithURL(url as CFURL, [kCGImageSourceShouldCache: false] as CFDictionary),
              let image = CGImageSourceCreateThumbnailAtIndex(source, 0, [
                kCGImageSourceCreateThumbnailFromImageAlways: true, kCGImageSourceThumbnailMaxPixelSize: pixels,
                kCGImageSourceCreateThumbnailWithTransform: true, kCGImageSourceShouldCacheImmediately: true] as CFDictionary) else { return nil }
        images[key] = image
        return image
    }
}

/// One piece on the board, the drop ghost or the NEXT preview: the level's
/// original artwork fitted to its sprite box (as v1.2.0 drew it). A skill
/// target is marked by a yellow ring on the level's collision circle. The
/// container moves and rotates with the body; the image is set once.
final class OrbiPomPiece {
    let layer = HUDLayer.plain("orbipom.piece")
    private let art = HUDLayer.plain("orbipom.art")
    private let mark = HUDLayer.shape("orbipom.mark")
    private let env: HUDDesignEnvironment
    private var configured: (level: Int, side: CGFloat)?
    var isMarked = false { didSet { if isMarked != oldValue { HUDMotion.perform { mark.isHidden = !isMarked } } } }

    /// Sprite size per level (the original's 24 … 192 × its 0.8 scale) in game units.
    static let sizes: [CGFloat] = [24, 32, 40, 56, 64, 72, 84, 96, 128, 160, 192].map { $0 * 0.8 }
    /// The original collision circle per level: radius and centre as fractions of the sprite box.
    static let circles: [(r: CGFloat, x: CGFloat, y: CGFloat)] = [
        (0.4136, 0.4970, 0.6078), (0.4185, 0.4976, 0.5897), (0.3965, 0.4908, 0.6011), (0.4546, 0.5463, 0.5289),
        (0.4425, 0.5064, 0.5464), (0.4373, 0.4904, 0.5411), (0.4374, 0.4956, 0.5639), (0.4410, 0.5012, 0.5516),
        (0.3287, 0.4991, 0.5464), (0.4510, 0.4942, 0.5473), (0.4451, 0.4895, 0.5491)]

    /// The sprite box of a level in board px.
    static func side(level: Int) -> CGFloat { sizes[min(max(level, 1), sizes.count) - 1] * OrbiPomCanvas.unit }

    init(env: HUDDesignEnvironment) {
        self.env = env
        HUDMotion.perform {
            art.contentsGravity = .resizeAspect
            mark.fillColor = nil; mark.strokeColor = HUDColor.yellow; mark.lineWidth = 1.5; mark.isHidden = true
            layer.addSublayer(art); layer.addSublayer(mark)
        }
    }

    /// - side: the sprite box in board px.
    func configure(level: Int, side: CGFloat) {
        guard configured?.level != level || configured?.side != side else { return }
        configured = (level, side)
        let circle = Self.circles[min(max(level, 1), Self.circles.count) - 1]
        let radius = side * circle.r
        HUDMotion.perform {
            layer.bounds = CGRect(x: 0, y: 0, width: side, height: side)
            art.frame = layer.bounds
            art.contents = OrbiPomArtwork.level(level, points: side, scale: env.renderScale)
            mark.frame = layer.bounds
            mark.path = CGPath(ellipseIn: CGRect(x: side * circle.x - radius, y: side * circle.y - radius,
                                                 width: radius * 2, height: radius * 2), transform: nil)
        }
    }

    func place(at point: CGPoint, angle: CGFloat, scale: CGFloat, opacity: Float) {
        HUDMotion.perform {
            layer.position = point
            layer.transform = CATransform3DScale(CATransform3DMakeRotation(angle, 0, 0, 1), scale, scale, 1)
            layer.opacity = opacity
        }
    }
}

/// A skill key (spec §3.11) as one row of the column: light chamfered plate
/// (8), the original skill icon, the 500 12.5 px label and the cost at the
/// right; yellow while its target is being chosen.
final class OrbiPomSkillButton: HUDControl {
    private let plate = HUDLayer.plain("skill.plate")
    private let brighten = HUDLayer.shape("skill.brighten")
    private let title: HUDTextLayer
    private let costText = HUDTextLayer(style: HUDTextStyle(size: 10, weight: .bold, color: HUDColor.rgb(0x666666), tabular: true), alignment: .right)
    var cost = "" { didSet { if cost != oldValue { HUDMotion.perform { costText.text = cost } } } }
    var isOn = false { didSet { if isOn != oldValue { renderPlate() } } }

    init(id: String, title text: String, icon: CGImage?, size: CGSize, env: HUDDesignEnvironment) {
        title = HUDTextLayer(text, style: HUDTextStyle(size: 12.5, weight: .medium, color: HUDColor.nearBlack))
        super.init(id: id, label: text, env: env)
        let bounds = CGRect(origin: .zero, size: size)
        let shape = HUDShape.chamfer(bounds, cut: 8)
        let iconSide: CGFloat = 22, costWidth: CGFloat = 30, left: CGFloat = 8 + iconSide + 8
        HUDMotion.perform {
            layer.bounds = bounds
            plate.frame = bounds
            brighten.frame = bounds; brighten.path = shape; brighten.fillColor = HUDColor.white(0.07); brighten.opacity = 0
            title.lineFrame = CGRect(x: left, y: (size.height - 12.5) / 2, width: size.width - left - costWidth - 10, height: 12.5)
            costText.lineFrame = CGRect(x: size.width - 10 - costWidth, y: (size.height - 10) / 2, width: costWidth, height: 10)
            [plate, brighten, title, costText].forEach(layer.addSublayer)
            if let icon {
                let picture = HUDLayer.plain("skill.icon")
                picture.frame = CGRect(x: 8, y: (size.height - iconSide) / 2, width: iconSide, height: iconSide)
                picture.contents = icon; picture.contentsGravity = .resizeAspect
                layer.insertSublayer(picture, above: brighten)
            }
        }
        renderPlate()
        setTarget(bounds, path: shape)
        env.applyScale(to: layer)
    }

    private func renderPlate() {
        HUDMotion.perform {
            plate.showArtwork(HUDArtwork.plate(isOn ? HUDColor.primaryButton : HUDColor.lightButton, size: layer.bounds.size, cut: 8,
                                               scale: env.renderScale), scale: env.renderScale)
        }
    }

    override func hoverChanged() { applyHoverMotion(lift: 2, pressScale: 0.965, brighten: brighten) }
    override func pressChanged() { applyHoverMotion(lift: 2, pressScale: 0.965, brighten: brighten) }
    override func enabledChanged() { HUDMotion.perform { layer.opacity = isEnabled ? 1 : 0.4 }; hoverChanged() }
    override var accessibilityRole: HUDAccessibilityItem.Role? { .toggle(isOn) }
}
