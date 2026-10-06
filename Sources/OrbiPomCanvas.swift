import AppKit
import QuartzCore

/// Retained native artwork around the unchanged original simulation. Body
/// layers are reused by ID, and HUD controls only refresh on state changes.
final class OrbiPomCanvas: HUDModuleContentFactory {
    struct Action { let id: String, title: String; let rect: CGRect; let enabled: Bool }
    static let board = CGRect(x: 87, y: 88, width: 253, height: 308)
    static let playArea = CGRect(x: 87, y: 55, width: 253, height: 341)
    let layer = CALayer()
    let session: OrbiPomSession
    private let face = CALayer(), pieces = CALayer(), controls = CALayer()
    private let preview = CALayer(), next = CALayer(), danger = CAShapeLayer(), wind = CAShapeLayer()
    private let score = CATextLayer(), best = CATextLayer(), energy = CATextLayer(), status = CATextLayer(), charge = CAShapeLayer()
    private let boardDimmer = CALayer(), dangerLabel = CATextLayer()
    private var boardDimmed = false
    private var bodies: [Int: CALayer] = [:]
    private var bodyLevels: [Int: Int] = [:]
    private var renderedBodies: [Int: OrbiPomBody] = [:]
    private var renderedEnergyProgress = -1.0
    private var timer: Timer?, lastTime: TimeInterval = 0
    private var presented = false, active = false, foreground = true
    private var artworkPrepared = false
    private var manualPause: Bool { session.manuallyPaused }
    private var dark = true, accent = HUDRuntimeAppearance.accent, renderScale: CGFloat = 2
    private var controlKey = "", previewLevel = 0, nextLevel = 0
    private var notifications: [NSObjectProtocol] = []
    private let foregroundProvider: () -> Bool
    private var pointerDown = false
    private var keyboardPoint = CGPoint(x: 115, y: 120)
    private(set) var actions: [Action] = []
    private(set) var restartConfirmation = false
    private(set) var rulesPresented = false
    var onRequestRules: (() -> Void)?
    var darkAppearance: Bool { dark }
    var onChange: (() -> Void)?
    var hasFrameTimer: Bool { timer != nil }
    var bodyLayerCount: Int { bodies.count }
    var isActive: Bool { active }
    var automaticallySchedulesFrames = true

    init(session: OrbiPomSession, foregroundProvider: @escaping () -> Bool = { NSApp?.isActive ?? true }) {
        self.session = session
        self.foregroundProvider = foregroundProvider
        layer.name = "orbipom.canvas"; layer.bounds = CGRect(x: 0, y: 0, width: 440, height: 440)
        layer.addSublayer(face)
        pieces.frame = Self.playArea; pieces.bounds = Self.playArea; pieces.masksToBounds = true
        layer.addSublayer(pieces); pieces.addSublayer(wind); pieces.addSublayer(preview); pieces.addSublayer(danger)
        layer.addSublayer(next)
        boardDimmer.name = "orbipom.boardDimmer"; boardDimmer.frame = Self.board
        boardDimmer.cornerRadius = 7; boardDimmer.backgroundColor = NSColor.black.withAlphaComponent(0.74).cgColor
        boardDimmer.opacity = 0; layer.addSublayer(boardDimmer); layer.addSublayer(controls)
        for name in [NSApplication.didResignActiveNotification, NSApplication.didBecomeActiveNotification] {
            notifications.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self] note in
                guard let self else { return }; self.foreground = note.name == NSApplication.didBecomeActiveNotification; self.reconcileClock()
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
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        artworkPrepared = true
        dark = style.dark; accent = style.accent; renderScale = style.contentsScale
        rebuild(); render(); reconcileClock(); return layer
    }
    func updateRenderScale(_ value: CGFloat) {
        renderScale = value
        func visit(_ l: CALayer) { l.contentsScale = value; l.sublayers?.forEach(visit) }
        visit(layer)
    }
    func setPresented(_ value: Bool) {
        guard presented != value else { return }; presented = value
        if value { prepareArtworkIfNeeded() }
        else { active = false; pointerDown = false; restartConfirmation = false; rulesPresented = false }
        reconcileClock()
    }
    func setActive(_ value: Bool) {
        guard active != value else { return }; active = value
        if value { presented = true; foreground = foregroundProvider(); prepareArtworkIfNeeded() }
        else { pointerDown = false }
        reconcileClock(); onChange?()
    }
    func deactivate() { setActive(false); setPresented(false) }
    func setRulesPresented(_ value: Bool) {
        guard rulesPresented != value else { return }
        rulesPresented = value; pointerDown = false; reconcileClock(); onChange?()
    }
    private func reconcileClock() {
        let shouldRun = presented && active && foreground && !manualPause && !restartConfirmation && !rulesPresented && session.error == nil
        let wasPaused = session.snapshot.paused
        if session.runtime != nil && wasPaused == shouldRun { session.pause(!shouldRun) }
        let interval = HUDRuntimeAppearance.configuration.lowPowerVisualMode ? 1.0 / 30 : 1.0 / 60
        let wantsTimer = shouldRun && session.snapshot.isPlaying && automaticallySchedulesFrames
        if !wantsTimer || timer.map({ abs($0.timeInterval - interval) > 0.001 }) == true {
            timer?.invalidate(); timer = nil; lastTime = 0
        }
        if wantsTimer && timer == nil {
            lastTime = ProcessInfo.processInfo.systemUptime
            let clock = Timer(timeInterval: interval, repeats: true) { [weak self] _ in
                guard let self else { return }; let now = ProcessInfo.processInfo.systemUptime
                let elapsed = now - self.lastTime; self.lastTime = now; self.advance(seconds: elapsed)
            }
            clock.tolerance = 0.001; timer = clock; RunLoop.main.add(clock, forMode: .common)
        }
        render()
    }
    func advance(seconds: Double) {
        guard presented, active, foreground, !manualPause, !restartConfirmation, !rulesPresented else { return }
        session.advance(seconds: seconds); render()
        if !session.snapshot.canAdvance || session.error != nil { reconcileClock() }
    }
    func perform(_ id: String) {
        guard active else { return }
        switch id {
        case "start": _ = session.start(); reconcileClock()
        case "pause": session.setManuallyPaused(!manualPause); reconcileClock()
        case "rules": onRequestRules?()
        case "restart": restartConfirmation = true; reconcileClock()
        case "cancelRestart": restartConfirmation = false; reconcileClock()
        case "confirmRestart": restartConfirmation = false; _ = session.start(); reconcileClock()
        case "cancelSkill": session.runtime?.cancelSkill(); render()
        default:
            if let skill = OrbiPomSkill(rawValue: id), session.snapshot.canUse(skill) { _ = session.runtime?.activate(skill); render() }
        }
    }
    private func world(_ point: CGPoint) -> CGPoint {
        CGPoint(x: (point.x - Self.board.minX) / 1.1, y: (point.y - Self.board.minY) / 1.1)
    }
    func movePointer(_ point: CGPoint) {
        guard active, !restartConfirmation, !rulesPresented, Self.playArea.contains(point) else { return }
        let p = world(point); keyboardPoint = p; session.runtime?.movePointer(x: Double(p.x), y: Double(p.y)); render()
    }
    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard active, layer.bounds.contains(point) else { return false }
        if let action = actions.last(where: { $0.rect.contains(point) }) { if action.enabled { perform(action.id) }; return true }
        if !restartConfirmation && !rulesPresented && Self.playArea.contains(point) { pointerDown = true; movePointer(point) }
        return true
    }
    func mouseUp(at point: CGPoint?) {
        defer { pointerDown = false }
        guard active, pointerDown, let point, Self.playArea.contains(point), !restartConfirmation, !rulesPresented else { return }
        let p = world(point); session.runtime?.pointerUp(x: Double(p.x), y: Double(p.y)); render()
    }
    func keyDown(_ event: NSEvent) -> Bool {
        guard active, !rulesPresented, event.modifierFlags.intersection([.command,.control,.option]).isEmpty else { return false }
        if event.keyCode == 53 {
            if restartConfirmation { perform("cancelRestart"); return true }
            if session.snapshot.skill != nil { perform("cancelSkill"); return true }
            return false
        }
        if event.keyCode == 49 {
            if event.isARepeat { return true }
            if session.snapshot.state == "idle" { perform("start") }
            else if session.snapshot.skill != nil { session.runtime?.pointerUp(x:Double(keyboardPoint.x),y:Double(keyboardPoint.y)); render() }
            else { _ = session.runtime?.drop(); render() }; return true
        }
        if (123...126).contains(event.keyCode) {
            let s = session.snapshot
            if s.skill == nil { keyboardPoint.x = CGFloat(s.previewX) }
            if event.keyCode == 123 { keyboardPoint.x -= 6 }; if event.keyCode == 124 { keyboardPoint.x += 6 }
            if event.keyCode == 125 { keyboardPoint.y += 6 }; if event.keyCode == 126 { keyboardPoint.y -= 6 }
            keyboardPoint.x = min(230,max(0,keyboardPoint.x)); keyboardPoint.y = min(280,max(0,keyboardPoint.y))
            session.runtime?.movePointer(x: Double(keyboardPoint.x), y: Double(keyboardPoint.y)); render(); return true
        }
        if let c = event.charactersIgnoringModifiers?.lowercased() {
            if c == "p" { if !event.isARepeat { perform("pause") }; return true }
            if let n = Int(c), (1...4).contains(n) { if !event.isARepeat { perform(OrbiPomSkill.allCases[n - 1].rawValue) }; return true }
        }
        return false
    }
    private func prepareArtworkIfNeeded() {
        guard !artworkPrepared else { return }
        artworkPrepared = true; rebuild(); render()
    }
    private var ink: NSColor { NSColor(white: dark ? 0.94 : 0.12, alpha: 1) }
    private func rebuild() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        face.sublayers?.forEach { $0.removeFromSuperlayer() }
        text(HUDSectionHeading.text(HUDModule.minigame.title), rect: CGRect(x: 12,y: 7,width: 340,height: 24), size: 15, parent: face)
        let line = CALayer(); line.frame = CGRect(x: 12,y: 35,width: 416,height: 1); line.backgroundColor = ink.withAlphaComponent(0.22).cgColor; face.addSublayer(line)
        let plate = CAShapeLayer(); plate.path = CGPath(roundedRect: Self.board, cornerWidth: 7, cornerHeight: 7, transform: nil)
        plate.fillColor = NSColor(white: dark ? 0.05 : 0.9, alpha: 0.86).cgColor; plate.strokeColor = accent.withAlphaComponent(0.7).cgColor; plate.lineWidth = 1; face.addSublayer(plate)
        text(L10n.text("Score", "得分"), rect: CGRect(x: 87,y: 43,width: 74,height: 16), size: 9,parent: face)
        text(L10n.text("Best", "最高分"), rect: CGRect(x: 235,y: 43,width: 105,height: 16), size: 9,parent: face)
        configure(score, rect: CGRect(x: 87,y: 59,width: 100,height: 23),size: 16); configure(best, rect: CGRect(x: 235,y: 59,width: 105,height: 23),size: 16)
        face.addSublayer(score); face.addSublayer(best)
        text(L10n.text("Next", "下一个"), rect: CGRect(x: 12,y: 51,width: 65,height: 16),size: 9,parent: face)
        next.frame = CGRect(x: 21,y: 71,width: 45,height: 45); next.contentsGravity = .resizeAspect
        configure(energy,rect: CGRect(x: 352,y: 58,width: 75,height: 24),size: 13); face.addSublayer(energy)
        text(L10n.text("SP", "技力"),rect: CGRect(x: 352,y: 42,width: 75,height: 16),size: 9,parent: face)
        charge.fillColor = accent.cgColor; face.addSublayer(charge)
        configure(status,rect: CGRect(x: Self.board.minX + 7,y: Self.board.midY - 12,width: Self.board.width - 14,height: 24),size: 15)
        status.name = "orbipom.status"; status.alignmentMode = .center; status.isWrapped = true
        status.foregroundColor = NSColor(white: 0.96, alpha: 1).cgColor
        configure(dangerLabel,rect: CGRect(x: 354,y: 218,width: 68,height: 55),size: 36)
        dangerLabel.name = "orbipom.dangerCountdown"; dangerLabel.alignmentMode = .center
        dangerLabel.foregroundColor = NSColor.systemRed.cgColor
        layer.addSublayer(status); layer.addSublayer(dangerLabel); layer.addSublayer(controls)
        danger.fillColor = nil; danger.strokeColor = NSColor.systemRed.cgColor; danger.lineDashPattern = [5,4]; danger.lineWidth = 1
        wind.fillColor = accent.withAlphaComponent(0.10).cgColor; wind.strokeColor = accent.withAlphaComponent(0.65).cgColor
        controlKey = ""; updateRenderScale(renderScale)
        CATransaction.commit()
    }
    private func configure(_ label: CATextLayer,rect: CGRect,size: CGFloat) {
        label.frame = rect; label.font = NSFont.monospacedSystemFont(ofSize: size, weight: .semibold); label.fontSize = size
        label.foregroundColor = ink.cgColor; label.contentsScale = renderScale; label.truncationMode = .end
    }
    private func text(_ value: String,rect: CGRect,size: CGFloat,parent: CALayer) {
        let l = CATextLayer(); configure(l,rect:rect,size:size); l.string=value; parent.addSublayer(l)
    }
    private func image(_ original: NSImage?) -> CGImage? { original?.cgImage(forProposedRect: nil, context: nil, hints: nil) }
    private func render() {
        guard artworkPrepared else { return }
        let s = session.snapshot
        CATransaction.begin(); CATransaction.setDisableActions(true)
        if score.string as? String != String(s.score) { score.string = String(s.score) }
        let high = String(max(session.bestScore,s.highScore)); if best.string as? String != high { best.string = high }
        let power = "\(s.energy) / 3"; if energy.string as? String != power { energy.string = power }
        if renderedEnergyProgress != s.energyProgress {
            renderedEnergyProgress = s.energyProgress
            charge.path = CGPath(rect: CGRect(x: 352,y: 82,width: 72 * min(1,max(0,s.energyProgress / 12)),height: 3), transform:nil)
        }
        let wanted = Set(s.bodies.map(\.id))
        for id in Array(bodies.keys) where !wanted.contains(id) { bodies.removeValue(forKey:id)?.removeFromSuperlayer(); bodyLevels[id] = nil; renderedBodies[id] = nil }
        for b in s.bodies {
            let node: CALayer
            if let old = bodies[b.id] { node = old } else { node = CALayer(); node.contentsGravity = .resizeAspect; pieces.addSublayer(node); bodies[b.id] = node }
            if bodyLevels[b.id] != b.level { node.contents = image(OrbiPomArtwork.image(level:b.level)); bodyLevels[b.id] = b.level }
            let size = CGFloat(b.size * b.scale) * 1.1
            if renderedBodies[b.id] != b {
                node.bounds = CGRect(x:0,y:0,width:size,height:size); node.position = CGPoint(x:Self.board.minX + CGFloat(b.x) * 1.1,y:Self.board.minY + CGFloat(b.y) * 1.1)
                node.setAffineTransform(CGAffineTransform(rotationAngle: CGFloat(b.angle))); node.opacity = Float(b.opacity)
                renderedBodies[b.id] = b
            }
            let selected = s.selectedBodyIDs.contains(b.id) || s.hoverBodyID == b.id
            node.borderWidth = selected ? 1.5 : 0; node.borderColor = accent.cgColor; node.cornerRadius = size / 2
        }
        preview.isHidden = !s.previewVisible || !s.isPlaying
        if previewLevel != s.currentLevel { preview.contents = image(OrbiPomArtwork.image(level:s.currentLevel)); previewLevel = s.currentLevel }
        let size = CGFloat(s.previewSize * s.previewScale) * 1.1
        preview.bounds = CGRect(x:0,y:0,width:size,height:size); preview.position = CGPoint(x:Self.board.minX + CGFloat(s.previewX) * 1.1,y:Self.board.minY + CGFloat(s.previewY) * 1.1); preview.contentsGravity = .resizeAspect
        if nextLevel != s.nextLevel { next.contents = image(OrbiPomArtwork.image(level:s.nextLevel)); nextLevel = s.nextLevel }
        let line = CGMutablePath(); line.move(to:CGPoint(x:Self.board.minX,y:Self.board.minY + 8.8)); line.addLine(to:CGPoint(x:Self.board.maxX,y:Self.board.minY + 8.8)); danger.path = line; danger.isHidden = s.dangerSeconds == nil
        wind.isHidden = s.windSurfaceY == nil
        if let y = s.windSurfaceY { wind.path = CGPath(rect:CGRect(x:Self.board.minX,y:Self.board.minY + CGFloat(y) * 1.1,width:Self.board.width,height:max(0,CGFloat(280-y) * 1.1)),transform:nil) }
        let message: String
        if let error = session.error { message = error }
        else if restartConfirmation { message = L10n.text("Restart the game?", "重新开始游戏？") }
        else if s.state == "idle" { message = L10n.text("Merge! OrbiPom!", "融合！山团团！") }
        else if !s.isPlaying { message = L10n.text("Game over", "游戏结束") }
        else if manualPause { message = L10n.text("Paused", "已暂停") }
        else { message = "" }
        if status.string as? String != message { status.string = message }; status.isHidden = message.isEmpty
        let countdown = s.dangerSeconds.map { String(Int(ceil($0))) } ?? ""
        if dangerLabel.string as? String != countdown { dangerLabel.string = countdown }
        dangerLabel.isHidden = countdown.isEmpty || !s.isPlaying
        let dimmed = manualPause || restartConfirmation || s.state == "over" || session.error != nil
        status.foregroundColor = (dimmed ? NSColor(white:0.96,alpha:1) : ink).cgColor
        if boardDimmed != dimmed {
            boardDimmed = dimmed
            let previous = boardDimmer.presentation()?.opacity ?? boardDimmer.opacity
            boardDimmer.opacity = dimmed ? 1 : 0
            if !HUDRuntimeAppearance.reduceMotion {
                let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = previous; fade.toValue = boardDimmer.opacity
                fade.duration = 0.16; boardDimmer.add(fade,forKey: "orbipom.boardDimming")
            }
        }
        refreshControls(s)
        CATransaction.commit()
    }
    private func refreshControls(_ s: OrbiPomSnapshot) {
        let key = "\(s.state):\(s.paused):\(manualPause):\(restartConfirmation):\(rulesPresented):\(s.energy):\(s.swapCharge):\(s.skill?.rawValue ?? ""):\(s.skillPhase ?? ""):\(active):\(session.error ?? "")"
        guard key != controlKey else { return }; controlKey = key
        controls.sublayers?.forEach { $0.removeFromSuperlayer() }; actions = []
        func button(_ id: String,_ title: String,_ rect: CGRect,enabled: Bool = true,skill: OrbiPomSkill? = nil) {
            actions.append(Action(id:id,title:title,rect:rect,enabled:enabled))
            let node = CALayer(); node.frame = rect; node.opacity = enabled ? 1 : 0.4; controls.addSublayer(node)
            let p = CGMutablePath(); p.move(to:CGPoint(x:5,y:0)); p.addLine(to:CGPoint(x:rect.width,y:0)); p.addLine(to:CGPoint(x:rect.width,y:rect.height-5)); p.addLine(to:CGPoint(x:rect.width-5,y:rect.height)); p.addLine(to:CGPoint(x:0,y:rect.height)); p.addLine(to:CGPoint(x:0,y:5)); p.closeSubpath()
            let plate = CAShapeLayer(); plate.path=p; plate.fillColor=NSColor(white:dark ? 0.16 : 0.9,alpha:0.96).cgColor; plate.strokeColor=accent.withAlphaComponent(0.45).cgColor; node.addSublayer(plate)
            let label = CATextLayer(); configure(label,rect:CGRect(x:3,y:(rect.height-16)/2,width:rect.width-6,height:16),size:11); label.string=title; label.alignmentMode = .center
            if let skill {
                let art = CALayer(); art.frame=CGRect(x:9,y:4,width:rect.width-18,height:32); art.contents=image(OrbiPomArtwork.skill(skill)); art.contentsGravity = .resizeAspect; node.addSublayer(art)
                label.frame=CGRect(x:2,y:37,width:rect.width-4,height:13); label.fontSize=8
            }
            node.addSublayer(label); HUDControlHighlightLayer.add(to:node,rect:node.bounds,shape:.cutCorner,enabled:enabled,framed:true)
        }
        if restartConfirmation {
            button("cancelRestart","×",CGRect(x:160,y:256,width:48,height:32)); button("confirmRestart","✓",CGRect(x:224,y:256,width:48,height:32))
        } else if !s.isPlaying {
            button("start",s.state == "idle" ? L10n.text("Start", "开始") : L10n.text("Play again", "再来一局"),CGRect(x:150,y:263,width:140,height:33),enabled:session.error == nil || session.runtime == nil)
        } else {
            button("pause",manualPause ? "▶" : "Ⅱ",CGRect(x:352,y:104,width:33,height:30))
            button("restart","↻",CGRect(x:393,y:104,width:33,height:30))
        }
        let titles = [L10n.text("Eliminate", "消除"),L10n.text("Wind", "风场"),L10n.text("Shake", "震动"),L10n.text("Swap", "交换")]
        for (i,skill) in OrbiPomSkill.allCases.enumerated() {
            let suffix = skill == .swap ? " \(s.swapCharge)/6" : " \(skill.energyCost)"
            button(skill.rawValue,titles[i] + suffix,CGRect(x:12,y:132+CGFloat(i)*65,width:61,height:53),enabled:!restartConfirmation && s.canUse(skill),skill:skill)
        }
        if s.skill != nil && !restartConfirmation { button("cancelSkill","×",CGRect(x:358,y:161,width:54,height:30)) }
        button("rules","?",CGRect(x:392,y:399,width:34,height:30),enabled:!restartConfirmation)
        onChange?()
    }
}
