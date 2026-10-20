import AppKit
import QuartzCore

/// One confirm-bar question (spec §2.18). `countdown` replaces the description
/// with the bold yellow number ("Keep this UI scale?").
struct HUDConfirmRequest: Equatable {
    var id: String
    var title: String
    var message: String = ""
    var countdown: Int? = nil
    var confirmTitle: String
    var cancelTitle: String = L10n.text("Cancel", "取消")
    var danger = false
    var icon: HUDIcon? = nil

    var glyph: HUDIcon { icon ?? (danger ? .trash : .check) }
}

/// The confirm bar's decisions as pure state: Esc cancels, Enter confirms,
/// every other key is consumed, and only the first decision counts.
struct HUDConfirmState: Equatable {
    enum Decision: Equatable { case confirm, cancel }
    private(set) var request: HUDConfirmRequest?
    private(set) var decision: Decision?
    var isOpen: Bool { request != nil && decision == nil }

    mutating func present(_ next: HUDConfirmRequest) { request = next; decision = nil }
    /// nil while undecided keys are swallowed; a decision once.
    mutating func key(_ keyCode: UInt16) -> Decision? {
        switch keyCode {
        case 53: return decide(.cancel)
        case 36, 76: return decide(.confirm)
        default: return nil
        }
    }
    mutating func decide(_ value: Decision) -> Decision? {
        guard isOpen else { return nil }
        decision = value
        return value
    }
    mutating func setCountdown(_ value: Int) { if request?.countdown != nil { request?.countdown = value } }
    mutating func close() { request = nil; decision = nil }
}

/// The projected band across the lower disc (spec §2.18): edge-faded charcoal,
/// hairlines, pixel steps, a yellow (or red) double-bar marker, title and
/// description on the left, Cancel above Confirm on the right.
final class HUDConfirmBar {
    static let size = CGSize(width: 620, height: 112)
    let layer = HUDLayer.plain("confirm")
    let env: HUDDesignEnvironment
    private let band = HUDLayer.gradient("confirm.band")
    private let column = HUDLayer.plain("confirm.text")
    private let marker = HUDLayer.gradient("confirm.marker")
    private let title = HUDTextLayer(style: HUDType.confirmTitle)
    private let message = HUDTextLayer(style: HUDType.confirmDescription)
    private(set) var pills: [HUDPill] = []
    var onDecision: ((HUDConfirmState.Decision) -> Void)?

    init(env: HUDDesignEnvironment) {
        self.env = env
        let bounds = CGRect(origin: .zero, size: Self.size)
        HUDMotion.perform {
            layer.bounds = bounds; layer.zPosition = HUDLayerOrder.confirmBar
            band.frame = bounds
            band.startPoint = CGPoint(x: 0, y: 0.5); band.endPoint = CGPoint(x: 1, y: 0.5)
            let charcoal = HUDColor.rgb(0x181818, 0.97)
            band.colors = [HUDColor.rgb(0x181818, 0), charcoal, charcoal, HUDColor.rgb(0x181818, 0)]
            band.locations = [0, 0.06, 0.94, 1]
            for y in [CGFloat(0), bounds.height - 1] {
                let hairline = HUDLayer.gradient()
                hairline.frame = CGRect(x: bounds.width * 0.05, y: y, width: bounds.width * 0.9, height: 1)
                hairline.startPoint = CGPoint(x: 0, y: 0.5); hairline.endPoint = CGPoint(x: 1, y: 0.5)
                hairline.colors = [HUDColor.white(0), HUDColor.white(0.22), HUDColor.white(0.22), HUDColor.white(0)]
                hairline.locations = [0, 0.2, 0.8, 1]
                band.addSublayer(hairline)
            }
            let steps = HUDLayer.shape()
            steps.frame = CGRect(x: bounds.width * 0.62, y: -10, width: 40, height: 10)
            let path = CGMutablePath()
            path.addRect(CGRect(x: 0, y: 5, width: 10, height: 5)); path.addRect(CGRect(x: 10, y: 0, width: 10, height: 10))
            path.addRect(CGRect(x: 25, y: 5, width: 5, height: 5))
            steps.path = path; steps.fillColor = HUDColor.rgb(0x202020)
            band.addSublayer(steps)
            marker.frame = CGRect(x: 0, y: 1.5, width: 12, height: 18)
            marker.startPoint = CGPoint(x: 0, y: 0.5); marker.endPoint = CGPoint(x: 1, y: 0.5)
            marker.locations = [0, 0.6, 0.6, 0.75, 0.75, 1]
            column.frame = CGRect(x: 46, y: 18, width: 320, height: 80)
            [marker, title, message].forEach(column.addSublayer)
            layer.addSublayer(band); layer.addSublayer(column)
            layer.isHidden = true
        }
    }

    func configure(_ request: HUDConfirmRequest) {
        pills.forEach { $0.layer.removeFromSuperlayer() }
        let cancel = HUDPill(id: request.id + ":cancel", title: request.cancelTitle, icon: .x, kind: .cancel, env: env)
        let confirm = HUDPill(id: request.id + ":confirm", title: request.confirmTitle, icon: request.glyph,
                              kind: request.danger ? .danger : .confirm, env: env)
        cancel.onActivate = { [weak self] in self?.onDecision?(.cancel) }
        confirm.onActivate = { [weak self] in self?.onDecision?(.confirm) }
        pills = [cancel, confirm]
        let tone = request.danger ? HUDColor.red : HUDColor.yellow
        HUDMotion.perform {
            marker.colors = [tone, tone, HUDColor.clear, HUDColor.clear, tone, tone]
            let columnWidth = Self.size.width - 46 - 34 - 16 - max(cancel.layer.bounds.width, confirm.layer.bounds.width)
            column.frame.size.width = columnWidth
            title.text = request.title
            title.lineFrame = CGRect(x: 22, y: 0, width: columnWidth - 22, height: 17 * 1.25)
            if let countdown = request.countdown {
                message.runs = [HUDText.Run("\(countdown)", HUDType.confirmCountdown)]
                message.lineFrame = CGRect(x: 0, y: 21.25 + 8, width: columnWidth, height: 22)
                message.maximumLines = 1
            } else {
                message.text = request.message
                message.textStyle = HUDType.confirmDescription
                message.maximumLines = 2
                message.lineFrame = CGRect(x: 0, y: 21.25 + 8, width: columnWidth, height: 36)
            }
            var y: CGFloat = 18
            for pill in pills {
                let size = pill.layer.bounds.size
                pill.layer.frame = CGRect(x: Self.size.width - 34 - size.width, y: y, width: size.width, height: size.height)
                layer.addSublayer(pill.layer)
                y += size.height + 11
            }
        }
        env.applyScale(to: layer)
    }

    func setCountdown(_ value: Int) {
        HUDMotion.perform { message.runs = [HUDText.Run("\(value)", HUDType.confirmCountdown)] }
    }

    /// bandIn (.3 s SNAP from the centre), then text from the left and pills from the right.
    func show() {
        HUDMotion.perform { layer.isHidden = false; layer.opacity = 1 }
        layer.removeAnimation(forKey: "hud.bandOut")
        HUDMotion.wipe(band, from: .centerX, duration: HUDDuration.bandIn, timing: HUDEasing.snap, env: env)
        HUDMotion.enter(column, from: CGPoint(x: -12, y: 0), duration: HUDDuration.confirmText, delay: HUDDuration.confirmTextDelay, env: env)
        for (index, pill) in pills.enumerated() {
            HUDMotion.enter(pill.layer, from: CGPoint(x: 12, y: 0), duration: HUDDuration.confirmText,
                            delay: HUDDuration.pillDelay + Double(index) * HUDDuration.pillStep, env: env)
        }
    }

    /// bandOut: collapses to a line and fades (.18 s ease-in), then hides.
    func hide(animated: Bool, completion: (() -> Void)? = nil) {
        let finish = { [weak self] in
            guard let self else { return }
            HUDMotion.perform { self.layer.isHidden = true; self.layer.removeAnimation(forKey: "hud.bandOut") }
            self.pills.forEach { $0.clearInteractionState() }
            completion?()
        }
        guard animated, !env.reducesMotion, !layer.isHidden else { finish(); return }
        let collapse = CABasicAnimation(keyPath: "transform.scale.y"); collapse.toValue = 0.04
        let fade = CABasicAnimation(keyPath: "opacity"); fade.toValue = 0
        let group = CAAnimationGroup(); group.animations = [collapse, fade]
        group.duration = HUDDuration.bandOut; group.timingFunction = HUDEasing.easeIn
        group.fillMode = .forwards; group.isRemovedOnCompletion = false
        CATransaction.begin(); CATransaction.setCompletionBlock(finish)
        layer.add(group, forKey: "hud.bandOut")
        CATransaction.commit()
    }

    var titleTextForTesting: String { title.text }
    var messageTextForTesting: String { message.text }
}

/// One result popup (spec §2.19, Appendix C).
struct HUDToastRequest: Equatable {
    var icon: HUDIcon
    var title: String
    var caption: String
    var captionLanguage: String? = nil
}

/// Toast lifetime as pure state: a new toast replaces the current one, and a
/// timer only ends the toast it was started for.
struct HUDToastState: Equatable {
    private(set) var current: HUDToastRequest?
    private(set) var generation = 0
    mutating func show(_ toast: HUDToastRequest) -> Int { current = toast; generation += 1; return generation }
    mutating func expire(_ token: Int) -> Bool {
        guard token == generation, current != nil else { return false }
        current = nil
        return true
    }
    mutating func clear() { current = nil; generation += 1 }
}

/// The 获得奖励 result popup (spec §2.19): line extending both ways, the icon
/// box popping in, title and caption sliding up, a short yellow underline.
final class HUDToast {
    static let size = CGSize(width: 600, height: 150)
    let layer = HUDLayer.plain("toast")
    let env: HUDDesignEnvironment
    private let line = HUDLayer.gradient("toast.line")
    private let box = HUDLayer.plain("toast.box")
    private let glyph = HUDIconLayer()
    private let title = HUDTextLayer(style: HUDType.toastTitle, alignment: .center)
    private let caption = HUDTextLayer(style: HUDType.toastCaption, alignment: .center)
    private let underline = HUDLayer.plain("toast.underline")

    init(env: HUDDesignEnvironment) {
        self.env = env
        let width = Self.size.width
        HUDMotion.perform {
            layer.bounds = CGRect(origin: .zero, size: Self.size); layer.zPosition = HUDLayerOrder.toast; layer.isHidden = true
            line.frame = CGRect(x: 0, y: 44, width: width, height: 1)
            line.startPoint = CGPoint(x: 0, y: 0.5); line.endPoint = CGPoint(x: 1, y: 0.5)
            line.colors = [HUDColor.white(0), HUDColor.white(0.65), HUDColor.white(0.65), HUDColor.white(0)]
            line.locations = [0, 0.3, 0.7, 1]
            for x in [width * 0.2, width * 0.8] {
                let tick = HUDLayer.plain(); tick.frame = CGRect(x: x, y: -3, width: 1, height: 7); tick.backgroundColor = HUDColor.white(1)
                line.addSublayer(tick)
            }
            box.frame = CGRect(x: width / 2 - 30, y: 14, width: 60, height: 60)
            box.backgroundColor = HUDColor.microTagBlack; box.borderWidth = 1; box.borderColor = HUDColor.rgb(0x4A4A4A)
            let corner = HUDLayer.shape(); corner.frame = CGRect(x: 48, y: 0, width: 12, height: 12)
            let path = CGMutablePath(); path.addLines(between: [.zero, CGPoint(x: 12, y: 0), CGPoint(x: 12, y: 12)]); path.closeSubpath()
            corner.path = path; corner.fillColor = HUDColor.yellow
            glyph.color = HUDColor.white(1)
            box.addSublayer(glyph); box.addSublayer(corner)
            title.lineFrame = CGRect(x: 0, y: 88, width: width, height: 14)
            caption.lineFrame = CGRect(x: 0, y: 110, width: width, height: 11.5)
            underline.frame = CGRect(x: width / 2 - 11, y: 130, width: 22, height: 2); underline.backgroundColor = HUDColor.yellow
            [line, box, title, caption, underline].forEach(layer.addSublayer)
        }
    }

    func show(_ toast: HUDToastRequest) {
        layer.removeAllAnimations()
        HUDMotion.perform {
            glyph.set(toast.icon, size: 26); glyph.position = CGPoint(x: 30, y: 30)
            title.text = toast.title
            caption.text = toast.caption; caption.language = toast.captionLanguage
            layer.isHidden = false; layer.opacity = 1
            layer.setValue(0, forKeyPath: "transform.translation.y")
        }
        env.applyScale(to: layer)
        HUDMotion.grow(line, duration: 0.35, env: env)
        HUDMotion.pop(box, duration: HUDDuration.pop, delay: 0.08, env: env)
        HUDMotion.enter(title, from: CGPoint(x: 0, y: 8), duration: 0.3, delay: 0.14, env: env)
        HUDMotion.enter(caption, from: CGPoint(x: 0, y: 8), duration: 0.3, delay: 0.2, env: env)
        HUDMotion.grow(underline, duration: 0.3, delay: 0.25, env: env)
    }

    /// fadeOut (.25 s ease-in, rising 6 px), removed at 240 ms.
    func hide(animated: Bool, completion: (() -> Void)? = nil) {
        guard animated, !env.reducesMotion, !layer.isHidden else {
            HUDMotion.perform { layer.isHidden = true }; completion?(); return
        }
        CATransaction.begin()
        CATransaction.setCompletionBlock { [weak self] in
            HUDMotion.perform { self?.layer.isHidden = true }
            completion?()
        }
        HUDMotion.animate(layer, "opacity", to: Float(0), duration: 0.24, timing: HUDEasing.easeIn, env: env)
        HUDMotion.animate(layer, "transform.translation.y", to: -6, duration: 0.24, timing: HUDEasing.easeIn, env: env)
        CATransaction.commit()
    }

    var textsForTesting: (String, String) { (title.text, caption.text) }
}

/// The confirm bar, toast and the centre dim on the tilted plane, above the
/// module content (spec §1.5). One instance per HUD, shared by every module.
/// Coordinates are content-box px; the host places it like the content box.
final class HUDCenterOverlay {
    let layer = HUDLayer.plain("center.overlay")
    let env: HUDDesignEnvironment
    let confirmBar: HUDConfirmBar
    let toast: HUDToast
    private let shade = HUDLayer.plain("center.shade")
    private let dim = HUDLayer.gradient("center.dim")
    private(set) var confirmState = HUDConfirmState()
    private(set) var toastState = HUDToastState()
    private var confirmHandlers: (confirm: () -> Void, cancel: (() -> Void)?)?
    private var toastCompletion: (() -> Void)?
    private var toastTimer: DispatchWorkItem?
    private var dimmed = false
    private let schedule: (TimeInterval, DispatchWorkItem) -> Void
    /// Called when the overlay's input state changes (stand-ins, input locks).
    var onChange: (() -> Void)?
    let router = HUDInteractionRouter()

    init(env: HUDDesignEnvironment,
         schedule: @escaping (TimeInterval, DispatchWorkItem) -> Void = { DispatchQueue.main.asyncAfter(deadline: .now() + $0, execute: $1) }) {
        self.env = env; self.schedule = schedule
        confirmBar = HUDConfirmBar(env: env)
        toast = HUDToast(env: env)
        HUDMotion.perform {
            layer.bounds = CGRect(origin: .zero, size: HUDMetric.box)
            shade.frame = layer.bounds; shade.backgroundColor = HUDColor.black(0.5); shade.opacity = 0
            // The 850 px dim circle centred on the disc (stage 975, 575 → box 295, 305).
            dim.type = .radial
            dim.frame = CGRect(x: 295 - 425, y: 305 - 425, width: 850, height: 850)
            dim.startPoint = CGPoint(x: 0.5, y: 0.5); dim.endPoint = CGPoint(x: 1, y: 1)
            dim.colors = [HUDColor.black(0.62), HUDColor.black(0.62), HUDColor.black(0)]
            dim.locations = [0, 0.821, 0.99]
            dim.opacity = 0; dim.zPosition = HUDLayerOrder.dim
            // Stage (665, 640) and (675, 460) relative to the box at (680, 270).
            confirmBar.layer.frame = CGRect(origin: CGPoint(x: -15, y: 370), size: HUDConfirmBar.size)
            toast.layer.frame = CGRect(origin: CGPoint(x: -5, y: 190), size: HUDToast.size)
            [shade, dim, confirmBar.layer, toast.layer].forEach(layer.addSublayer)
        }
        confirmBar.onDecision = { [weak self] in _ = self?.decide($0) }
    }

    var isConfirming: Bool { confirmState.isOpen }
    var capturesInput: Bool { confirmState.request != nil }

    func confirm(_ request: HUDConfirmRequest, onConfirm: @escaping () -> Void, onCancel: (() -> Void)? = nil) {
        clearToast()
        confirmState.present(request)
        confirmHandlers = (onConfirm, onCancel)
        confirmBar.configure(request)
        router.set(confirmBar.pills)
        confirmBar.show()
        setDimmed(true)
        onChange?()
    }

    func setCountdown(_ value: Int) {
        confirmState.setCountdown(value)
        confirmBar.setCountdown(value)
    }

    /// Esc / Enter while open; every other key is swallowed (returns true).
    func handleKey(_ keyCode: UInt16) -> Bool {
        guard capturesInput else { return false }
        if let decision = confirmState.key(keyCode) { finish(decision) }
        return true
    }

    @discardableResult func decide(_ decision: HUDConfirmState.Decision) -> Bool {
        guard confirmState.decide(decision) != nil else { return false }
        finish(decision)
        return true
    }

    /// Cancel hides then calls back at once; Confirm calls back after 120 ms.
    private func finish(_ decision: HUDConfirmState.Decision) {
        let handlers = confirmHandlers
        confirmHandlers = nil
        router.set([])
        confirmBar.hide(animated: true) { [weak self] in
            guard let self, self.confirmState.decision != nil else { return }
            self.confirmState.close()
            self.onChange?()
        }
        if toastState.current == nil { setDimmed(false) }
        onChange?()
        switch decision {
        case .cancel: handlers?.cancel?()
        case .confirm:
            guard let confirm = handlers?.confirm else { return }
            if env.reducesMotion { confirm() } else { schedule(HUDDuration.confirmCallback, DispatchWorkItem(block: confirm)) }
        }
    }

    /// Closes an open bar without a decision (module change, HUD close).
    func dismissConfirm() {
        guard confirmState.request != nil else { return }
        confirmHandlers = nil
        confirmState.close()
        router.set([])
        confirmBar.hide(animated: false)
        if toastState.current == nil { setDimmed(false) }
        onChange?()
    }

    func showToast(_ request: HUDToastRequest, completion: (() -> Void)? = nil) {
        guard !confirmState.isOpen else { return }
        toastTimer?.cancel()
        let previous = toastCompletion
        toastCompletion = completion
        previous?()
        let token = toastState.show(request)
        toast.show(request)
        setDimmed(true)
        let timer = DispatchWorkItem { [weak self] in self?.expireToast(token) }
        toastTimer = timer
        schedule(HUDDuration.toastVisible, timer)
    }

    private func expireToast(_ token: Int) {
        guard toastState.expire(token) else { return }
        toastTimer = nil
        let completion = toastCompletion
        toastCompletion = nil
        toast.hide(animated: true) { completion?() }
        if !confirmState.isOpen { setDimmed(false) }
    }

    func clearToast() {
        toastTimer?.cancel(); toastTimer = nil
        guard toastState.current != nil else { return }
        toastState.clear()
        toast.hide(animated: false)
        let completion = toastCompletion
        toastCompletion = nil
        completion?()
    }

    /// Radial dim and the content's brightness .5 (.22 / .25 s E).
    private func setDimmed(_ on: Bool) {
        guard on != dimmed else { return }
        dimmed = on
        HUDMotion.animate(dim, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.dim, env: env)
        HUDMotion.animate(shade, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.dimFilter, env: env)
    }

    var isDimmed: Bool { dimmed }

    /// Everything off at once (HUD close).
    func reset() {
        dismissConfirm(); clearToast()
        HUDMotion.perform { dim.opacity = 0; shade.opacity = 0 }
        dimmed = false
    }
}
