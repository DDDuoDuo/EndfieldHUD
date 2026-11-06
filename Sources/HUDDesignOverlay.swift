import AppKit
import AVFoundation
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
    /// `.alert` is a red band with white text (Settings' facility-alert easter egg).
    var style: Style = .standard
    /// The upper pill confirms too and looks like Confirm (two identical
    /// answers); Esc or a press outside the bar still cancels.
    var cancelPillConfirms = false

    enum Style: Equatable { case standard, alert }

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
/// hairlines, a yellow (or red) double-bar marker, title and description on
/// the left, Cancel above Confirm on the right. The spec's pixel-step blocks
/// on the top edge are left out at the owner's request.
final class HUDConfirmBar {
    static let size = CGSize(width: 620, height: 112)
    let layer = HUDLayer.plain("confirm")
    let env: HUDDesignEnvironment
    private let band = HUDLayer.gradient("confirm.band")
    private let column = HUDLayer.plain("confirm.text")
    private let marker = HUDLayer.gradient("confirm.marker")
    private let title = HUDTextLayer(style: HUDType.confirmTitle)
    private let message = HUDTextLayer(style: HUDType.confirmDescription)
    /// The charcoal band of the standard style, restored after an alert.
    private var standardBand: [Any]?
    private var hairlines: [CAGradientLayer] = []
    private(set) var pills: [HUDPill] = []
    var onDecision: ((HUDConfirmState.Decision) -> Void)?

    init(env: HUDDesignEnvironment) {
        self.env = env
        let bounds = CGRect(origin: .zero, size: Self.size)
        HUDMotion.perform {
            layer.bounds = bounds; layer.zPosition = HUDLayerOrder.confirmBar
            band.frame = bounds
            band.startPoint = CGPoint(x: 0, y: 0.5); band.endPoint = CGPoint(x: 1, y: 0.5)
            band.locations = [0, 0.06, 0.94, 1]
            for y in [CGFloat(0), bounds.height - 1] {
                let hairline = HUDLayer.gradient()
                hairline.frame = CGRect(x: bounds.width * 0.05, y: y, width: bounds.width * 0.9, height: 1)
                hairline.startPoint = CGPoint(x: 0, y: 0.5); hairline.endPoint = CGPoint(x: 1, y: 0.5)
                hairline.locations = [0, 0.2, 0.8, 1]
                band.addSublayer(hairline); hairlines.append(hairline)
            }
            marker.frame = CGRect(x: 0, y: 1.5, width: 12, height: 18)
            marker.startPoint = CGPoint(x: 0, y: 0.5); marker.endPoint = CGPoint(x: 1, y: 0.5)
            marker.locations = [0, 0.6, 0.6, 0.75, 0.75, 1]
            column.frame = CGRect(x: 46, y: 18, width: 320, height: 80)
            [marker, title, message].forEach(column.addSublayer)
            layer.addSublayer(band); layer.addSublayer(column)
            layer.isHidden = true
        }
        applyPalette()
    }

    /// The standard band in the current Theme (light: a pale band with dark
    /// ink); `configure` takes it for every request that is not an alert.
    func applyPalette() {
        let charcoal = HUDColor.tone(0x181818, 0.97)
        standardBand = [HUDColor.tone(0x181818, 0), charcoal, charcoal, HUDColor.tone(0x181818, 0)]
        HUDMotion.perform {
            band.colors = standardBand
            for hairline in hairlines {
                hairline.colors = [HUDColor.foreground(0), HUDColor.foreground(0.22), HUDColor.foreground(0.22), HUDColor.foreground(0)]
            }
        }
    }

    func configure(_ request: HUDConfirmRequest) {
        pills.forEach { $0.layer.removeFromSuperlayer() }
        let kind: HUDPill.Kind = request.danger ? .danger : .confirm
        let twin = request.cancelPillConfirms
        let cancel = HUDPill(id: request.id + ":cancel", title: request.cancelTitle, icon: twin ? request.glyph : .x,
                             kind: twin ? kind : .cancel, env: env)
        let confirm = HUDPill(id: request.id + ":confirm", title: request.confirmTitle, icon: request.glyph, kind: kind, env: env)
        cancel.onActivate = { [weak self] in self?.onDecision?(twin ? .confirm : .cancel) }
        confirm.onActivate = { [weak self] in self?.onDecision?(.confirm) }
        pills = [cancel, confirm]
        let alert = request.style == .alert
        let tone = alert ? HUDColor.white(1) : request.danger ? HUDColor.red : HUDColor.accentInk
        HUDMotion.perform {
            if alert {
                let red = HUDColor.red.copy(alpha: 0.97) ?? HUDColor.red
                band.colors = [HUDColor.red.copy(alpha: 0) ?? HUDColor.clear, red, red, HUDColor.red.copy(alpha: 0) ?? HUDColor.clear]
            } else {
                band.colors = standardBand
            }
            title.textStyle = alert ? HUDType.confirmTitle.colored(HUDColor.white(1)) : HUDType.confirmTitle
            marker.colors = [tone, tone, HUDColor.clear, HUDColor.clear, tone, tone]
            let columnWidth = Self.size.width - 46 - 34 - 16 - max(cancel.layer.bounds.width, confirm.layer.bounds.width)
            column.frame.size.width = columnWidth
            title.text = request.title
            // Without a description the title may wrap, up to three lines; one
            // too long for three (wide twin pills) steps down to 14 px and four.
            var titleLines = request.message.isEmpty && request.countdown == nil ? 3 : 1
            if titleLines == 3, title.preferredWidth > (columnWidth - 22) * 2.7 {
                var small = title.textStyle; small.size = 14; title.textStyle = small
                titleLines = 4
            }
            title.maximumLines = titleLines
            title.lineFrame = CGRect(x: 22, y: 0, width: columnWidth - 22, height: title.textStyle.size * 1.25 * CGFloat(titleLines))
            if let countdown = request.countdown {
                message.runs = [HUDText.Run("\(countdown)", HUDType.confirmCountdown)]
                message.lineFrame = CGRect(x: 0, y: 21.25 + 8, width: columnWidth, height: 22)
                message.maximumLines = 1
            } else {
                message.text = request.message
                message.textStyle = alert ? HUDType.confirmDescription.colored(HUDColor.white(0.9)) : HUDType.confirmDescription
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
    private let corner = HUDLayer.shape()
    private var ticks: [CALayer] = []

    init(env: HUDDesignEnvironment) {
        self.env = env
        let width = Self.size.width
        HUDMotion.perform {
            layer.bounds = CGRect(origin: .zero, size: Self.size); layer.zPosition = HUDLayerOrder.toast; layer.isHidden = true
            line.frame = CGRect(x: 0, y: 44, width: width, height: 1)
            line.startPoint = CGPoint(x: 0, y: 0.5); line.endPoint = CGPoint(x: 1, y: 0.5)
            line.locations = [0, 0.3, 0.7, 1]
            for x in [width * 0.2, width * 0.8] {
                let tick = HUDLayer.plain(); tick.frame = CGRect(x: x, y: -3, width: 1, height: 7)
                line.addSublayer(tick); ticks.append(tick)
            }
            box.frame = CGRect(x: width / 2 - 30, y: 14, width: 60, height: 60)
            box.borderWidth = 1
            corner.frame = CGRect(x: 48, y: 0, width: 12, height: 12)
            let path = CGMutablePath(); path.addLines(between: [.zero, CGPoint(x: 12, y: 0), CGPoint(x: 12, y: 12)]); path.closeSubpath()
            corner.path = path
            box.addSublayer(glyph); box.addSublayer(corner)
            title.lineFrame = CGRect(x: 0, y: 88, width: width, height: 14)
            caption.lineFrame = CGRect(x: 0, y: 110, width: width, height: 11.5)
            underline.frame = CGRect(x: width / 2 - 11, y: 130, width: 22, height: 2)
            [line, box, title, caption, underline].forEach(layer.addSublayer)
        }
        applyPalette()
    }

    /// Recolours the toast after a theme colour or Theme change.
    func applyPalette() {
        HUDMotion.perform {
            line.colors = [HUDColor.foreground(0), HUDColor.foreground(0.65), HUDColor.foreground(0.65), HUDColor.foreground(0)]
            ticks.forEach { $0.backgroundColor = HUDColor.foreground(1) }
            box.backgroundColor = HUDColor.tone(0x161616); box.borderColor = HUDColor.tone(0x4A4A4A)
            corner.fillColor = HUDColor.accentInk; underline.backgroundColor = HUDColor.accentInk
            glyph.color = HUDColor.foreground(1)
            title.textStyle = HUDType.toastTitle; caption.textStyle = HUDType.toastCaption
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
    /// The module content behind the confirm bar, blurred while a confirm is
    /// open (the host's view must use Core Image filters).
    weak var blurTarget: CALayer?
    private static let blurRadius: CGFloat = 1.5
    /// brightness(.5) halves the sRGB values; the exposure works in linear
    /// light, where that is about −2.2 EV (checked on screen against the
    /// radial dim's share of the darkening).
    private static let darkening: CGFloat = -2.2
    /// The module content's blurred, darkened picture while it is dimmed: a
    /// sibling with the content's geometry, holding the picture of the area
    /// the content draws.
    private let still = HUDLayer.plain("center.dim.still")
    private let picture = HUDLayer.plain("center.dim.picture")
    private var stillRestoreOpacity: Float = 1
    /// The live content was faded out under the still: it owes `stillRestoreOpacity`.
    private var stillHidesContent = false
    /// Bumped by every dim change: a picture still due for an older one is dropped.
    private var stillRequest = 0
    private static let stillContext = CIContext(options: [.cacheIntermediates: false])
    let router = HUDInteractionRouter()
    /// Presses outside the bar, which cancel a request whose pills both confirm.
    private lazy var outside: HUDControl = {
        let control = OutsidePress(id: "confirm.outside", env: env, layer: layer)
        control.bar = confirmBar.layer
        control.onActivate = { [weak self] in _ = self?.decide(.cancel) }
        return control
    }()

    private final class OutsidePress: HUDControl {
        weak var bar: CALayer?
        override func contains(_ point: CGPoint) -> Bool { bar.map { !$0.frame.contains(point) } ?? false }
        override var accessibilityRole: HUDAccessibilityItem.Role? { nil }
    }

    init(env: HUDDesignEnvironment,
         schedule: @escaping (TimeInterval, DispatchWorkItem) -> Void = { DispatchQueue.main.asyncAfter(deadline: .now() + $0, execute: $1) }) {
        self.env = env; self.schedule = schedule
        confirmBar = HUDConfirmBar(env: env)
        toast = HUDToast(env: env)
        HUDMotion.perform {
            still.addSublayer(picture)
            layer.bounds = CGRect(origin: .zero, size: HUDMetric.box)
            // The 850 px dim circle centred on the disc (stage 975, 575 → box 295, 305).
            dim.type = .radial
            dim.frame = CGRect(x: 295 - 425, y: 305 - 425, width: 850, height: 850)
            dim.startPoint = CGPoint(x: 0.5, y: 0.5); dim.endPoint = CGPoint(x: 1, y: 1)
            dim.locations = [0, 0.821, 0.99]
            dim.opacity = 0; dim.zPosition = HUDLayerOrder.dim
            // Stage (665, 640) and (675, 460) relative to the box at (680, 270).
            confirmBar.layer.frame = CGRect(origin: CGPoint(x: -15, y: 370), size: HUDConfirmBar.size)
            toast.layer.frame = CGRect(origin: CGPoint(x: -5, y: 190), size: HUDToast.size)
            [dim, confirmBar.layer, toast.layer].forEach(layer.addSublayer)
        }
        confirmBar.onDecision = { [weak self] in _ = self?.decide($0) }
        applyDim()
    }

    /// The theme colour or Theme changed.
    func applyPalette() { toast.applyPalette(); confirmBar.applyPalette(); applyDim() }

    /// The dim behind the confirm bar: black in dark, a pale veil in light.
    private func applyDim() {
        HUDMotion.perform {
            dim.colors = [HUDColor.tone(0x000000, 0.62), HUDColor.tone(0x000000, 0.62), HUDColor.tone(0x000000, 0)]
        }
    }

    var isConfirming: Bool { confirmState.isOpen }
    var capturesInput: Bool { confirmState.request != nil }

    func confirm(_ request: HUDConfirmRequest, onConfirm: @escaping () -> Void, onCancel: (() -> Void)? = nil) {
        clearToast()
        confirmState.present(request)
        confirmHandlers = (onConfirm, onCancel)
        confirmBar.configure(request)
        router.set((request.cancelPillConfirms ? [outside] : []) + confirmBar.pills)
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

    /// Radial dim (.22 s E), the content's brightness .5 and blur (.25 s E).
    private func setDimmed(_ on: Bool) {
        // A hint replacing a hint pictures the module again: under a hint it
        // still takes input, and the picture shows what that input changed.
        guard on != dimmed else { if on { setContentDimmed(true) }; return }
        dimmed = on
        HUDMotion.animate(dim, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.dim, env: env)
        setContentDimmed(on)
    }

    /// The demo's `#center.dimmed{filter: brightness(.5) blur(1.5px)}` on the
    /// module content itself, so empty areas stay clear (no darkened box).
    /// The content is filtered once into a still picture that cross-fades
    /// with it: live filters under the HUD's moving tilt make WindowServer
    /// filter the whole module again on every frame. The picture is taken on
    /// the next turn, after the change that asked for the dim (a deleted pin,
    /// a cleared list, a flipped switch), from where the content is settling
    /// rather than the frame in flight, and again on every new dim. Content a still cannot follow (video, Metal, a mask the
    /// shell moves every frame) keeps the live filters.
    private func setContentDimmed(_ on: Bool) {
        guard let target = blurTarget else { return }
        stillRequest += 1
        if on, target.filters == nil, target.opacity > 0 || still.superlayer != nil, !target.isHidden {
            let request = stillRequest
            DispatchQueue.main.async { [weak self] in self?.captureStill(request) }
            return
        }
        if !on, still.superlayer != nil {
            let request = stillRequest
            stillHidesContent = false
            CATransaction.begin()
            CATransaction.setCompletionBlock { [weak self] in
                guard let self, !self.dimmed, self.stillRequest == request else { return }
                HUDMotion.perform { self.still.removeFromSuperlayer(); self.picture.contents = nil }
            }
            // The content returns in the first half under the still, so the
            // module never thins out to the stage behind it mid-fade.
            HUDMotion.animate(still, "opacity", to: Float(0), duration: HUDDuration.dimFilter, env: env)
            HUDMotion.animate(target, "opacity", to: stillRestoreOpacity, duration: HUDDuration.dimFilter / 2, env: env)
            CATransaction.commit()
            return
        }
        setLiveFilters(on, on: target)
    }

    /// Takes the picture for dim `request` once no page is still leaving (the
    /// page replacing it is built when the leave ends), or the live filters
    /// when it cannot. One transaction: the layers hidden for the capture are
    /// never shown hidden, and the still arrives with its fade.
    private func captureStill(_ request: Int, waits: Int = 0) {
        guard stillRequest == request, dimmed, let target = blurTarget else { return }
        if waits < 4, Self.heldOut(in: target).contains(where: Self.stillLeaving) {
            DispatchQueue.main.asyncAfter(deadline: .now() + HUDDuration.leave) { [weak self] in
                self?.captureStill(request, waits: waits + 1)
            }
            return
        }
        CATransaction.begin()
        // Replacing a still that shows, the live filters arrive already at full strength.
        let replacing = still.superlayer != nil && (still.presentation()?.opacity ?? still.opacity) > stillRestoreOpacity / 2
        if !showStill(of: target) { setLiveFilters(true, on: target, immediately: replacing) }
        CATransaction.commit()
    }

    /// Draws `target` into the still and fades it in over the live content
    /// (again over a still that is still fading out); false when it cannot.
    private func showStill(of target: CALayer) -> Bool {
        guard target.mask == nil, let parent = target.superlayer, let drawn = Self.dimmedStill(of: target) else {
            // A still that can no longer be retaken gives the content back at once.
            HUDMotion.perform {
                if stillHidesContent || still.superlayer != nil { target.removeAnimation(forKey: "hud.opacity"); target.opacity = stillRestoreOpacity }
                still.opacity = 0; still.removeFromSuperlayer(); picture.contents = nil
            }
            stillHidesContent = false
            return false
        }
        if still.superlayer == nil { stillRestoreOpacity = target.opacity }
        HUDMotion.perform {
            picture.contents = drawn.image; picture.contentsScale = drawn.scale; picture.contentsGravity = .resize
            // Drawn y-up (so Core Graphics casts shadows the right way); a flipped tree shows it mirrored back.
            picture.transform = CATransform3DIdentity
            picture.bounds = CGRect(origin: .zero, size: drawn.rect.size)
            picture.position = CGPoint(x: drawn.rect.midX, y: drawn.rect.midY)
            if target.contentsAreFlipped() { picture.setAffineTransform(CGAffineTransform(scaleX: 1, y: -1)) }
            still.bounds = target.bounds; still.anchorPoint = target.anchorPoint; still.position = target.position
            still.transform = target.transform; still.zPosition = target.zPosition
            if still.superlayer == nil { still.opacity = 0; parent.insertSublayer(still, above: target) }
        }
        stillHidesContent = true
        // The content leaves in the second half, once the still covers it.
        HUDMotion.animate(still, "opacity", to: stillRestoreOpacity, duration: HUDDuration.dimFilter, env: env)
        HUDMotion.animate(target, "opacity", to: Float(0), duration: HUDDuration.dimFilter / 2,
                          delay: HUDDuration.dimFilter / 2, env: env)
        return true
    }

    /// The live filters: added at their neutral values, eased in, and removed
    /// once eased out; `immediately` adds them at full strength.
    private func setLiveFilters(_ on: Bool, on target: CALayer, immediately: Bool = false) {
        if on, target.filters == nil {
            guard let blur = CIFilter(name: "CIGaussianBlur"), let dark = CIFilter(name: "CIExposureAdjust") else { return }
            blur.setDefaults(); blur.name = "confirmBlur"; blur.setValue(immediately ? Self.blurRadius : 0, forKey: kCIInputRadiusKey)
            dark.setDefaults(); dark.name = "confirmDim"; dark.setValue(immediately ? Self.darkening : 0, forKey: kCIInputEVKey)
            HUDMotion.perform { target.filters = [blur, dark] }
            if immediately { return }
        }
        guard target.filters != nil else { return }
        CATransaction.begin()
        if !on {
            CATransaction.setCompletionBlock { [weak self, weak target] in
                guard self?.dimmed == false else { return }
                HUDMotion.perform { target?.filters = nil }
            }
        }
        HUDMotion.animate(target, "filters.confirmBlur.inputRadius", to: on ? Self.blurRadius : 0,
                          duration: HUDDuration.dimFilter, env: env)
        HUDMotion.animate(target, "filters.confirmDim.inputEV", to: on ? Self.darkening : 0,
                          duration: HUDDuration.dimFilter, env: env)
        CATransaction.commit()
    }

    /// `target` as it settles, drawn once with the dim's blur and darkening
    /// (the same filters and values, in its own units), and the rect it
    /// covers in `target`'s space: its bounds plus what its visible sublayers
    /// draw outside them (redesigned screens do not clip), up to half its
    /// size on each side. At most 2 px per unit: the blur hides more. The
    /// layer tree holds every animation's end (render(in:) ignores
    /// animations), except what an animation keeps faded out after it ends
    /// (a page that left, a hold): those are left out. Nil when part of it
    /// cannot be drawn into an image.
    static func dimmedStill(of target: CALayer) -> (image: CGImage, rect: CGRect, scale: CGFloat)? {
        let shown = target
        let leftOut = heldOut(in: shown)
        let scale = min(2, max(1, target.contentsScale))
        // Core Graphics casts shadows in pixels, past the raster's scale: scale them for the capture.
        func shadowed(_ layer: CALayer) -> [CALayer] {
            (layer.shadowOpacity > 0 ? [layer] : []) + (layer.sublayers ?? []).flatMap(shadowed)
        }
        let shadows = scale == 1 ? [] : shadowed(shown).map { ($0, $0.shadowOffset, $0.shadowRadius) }
        // render(in:) stacks siblings in array order; Core Animation by zPosition. Sort them for the capture.
        func stacked(_ layer: CALayer) -> [(CALayer, [CALayer])] {
            let children = layer.sublayers ?? []
            let own = Set(children.map(\.zPosition)).count > 1 ? [(layer, children)] : []
            return own + children.flatMap(stacked)
        }
        let restacked = stacked(shown)
        HUDMotion.perform {
            for (layer, children) in restacked {
                layer.sublayers = children.enumerated().sorted { ($0.element.zPosition, $0.offset) < ($1.element.zPosition, $1.offset) }.map(\.element)
            }
            leftOut.forEach { $0.isHidden = true }
            for (layer, offset, radius) in shadows {
                layer.shadowOffset = CGSize(width: offset.width * scale, height: offset.height * scale); layer.shadowRadius = radius * scale
            }
        }
        defer { HUDMotion.perform {
            for (layer, children) in restacked { layer.sublayers = children }
            leftOut.forEach { $0.isHidden = false }
            for (layer, offset, radius) in shadows { layer.shadowOffset = offset; layer.shadowRadius = radius }
        } }
        func live(_ layer: CALayer) -> Bool {
            layer is CAMetalLayer || layer is AVPlayerLayer || layer is AVSampleBufferDisplayLayer
                || (layer.sublayers ?? []).contains(where: live)
        }
        func extent(_ layer: CALayer) -> CGRect {
            (layer.sublayers ?? []).reduce(CGRect.null) { area, sublayer in
                guard !sublayer.isHidden, sublayer.opacity > 0 else { return area }
                var drawn = sublayer.convert(sublayer.bounds, to: shown)
                if !sublayer.masksToBounds { drawn = drawn.union(extent(sublayer)) }
                return area.union(drawn)
            }
        }
        let bounds = shown.bounds
        let reach = bounds.insetBy(dx: -bounds.width / 2, dy: -bounds.height / 2)
        let overflow = shown.masksToBounds ? CGRect.null : extent(shown).intersection(reach)
        let rect = (overflow.isNull ? bounds : bounds.union(overflow)).integral
        let width = Int(ceil(rect.width * scale)), height = Int(ceil(rect.height * scale))
        guard !live(target), width > 0, height > 0, let space = CGColorSpace(name: CGColorSpace.sRGB),
              let raster = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0,
                                     space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
              let blur = CIFilter(name: "CIGaussianBlur"), let dark = CIFilter(name: "CIExposureAdjust") else { return nil }
        raster.scaleBy(x: scale, y: scale)
        raster.translateBy(x: -rect.minX, y: -rect.minY)
        // render(in:) applies the layer's own opacity, which is 0 or part way
        // there while it is dimmed: draw its sublayers in place instead
        // (sorted as Core Animation stacks them).
        let sublayers = (shown.sublayers ?? []).enumerated().sorted { ($0.element.zPosition, $0.offset) < ($1.element.zPosition, $1.offset) }
        if shown.isGeometryFlipped || !CATransform3DIsIdentity(shown.sublayerTransform)
            || sublayers.contains(where: { !CATransform3DIsAffine($0.element.transform) }) {
            shown.render(in: raster)
        } else {
            for (_, sublayer) in sublayers where !sublayer.isHidden {
                let b = sublayer.bounds, anchor = sublayer.anchorPoint
                raster.saveGState()
                raster.concatenate(CGAffineTransform(translationX: -b.minX - anchor.x * b.width, y: -b.minY - anchor.y * b.height)
                    .concatenating(sublayer.affineTransform())
                    .concatenating(CGAffineTransform(translationX: sublayer.position.x, y: sublayer.position.y)))
                sublayer.render(in: raster)
                raster.restoreGState()
            }
        }
        guard let drawn = raster.makeImage() else { return nil }
        let input = CIImage(cgImage: drawn)
        blur.setValue(input, forKey: kCIInputImageKey); blur.setValue(blurRadius * scale, forKey: kCIInputRadiusKey)
        dark.setValue(blur.outputImage?.cropped(to: input.extent), forKey: kCIInputImageKey)
        dark.setValue(darkening, forKey: kCIInputEVKey)
        guard let output = dark.outputImage,
              let image = stillContext.createCGImage(output, from: input.extent, format: .RGBA8, colorSpace: space) else { return nil }
        return (image, rect, scale)
    }

    /// The layers under `root` that an animation keeps faded out after it
    /// ends (a page leaving or left, a hold).
    private static func heldOut(in root: CALayer) -> [CALayer] {
        func fadesOut(_ animation: CAAnimation) -> Bool {
            if let group = animation as? CAAnimationGroup { return (group.animations ?? []).contains(where: fadesOut) }
            if let basic = animation as? CABasicAnimation, basic.keyPath == "opacity" { return (basic.toValue as? NSNumber)?.floatValue == 0 }
            if let frames = animation as? CAKeyframeAnimation, frames.keyPath == "opacity" { return (frames.values?.last as? NSNumber)?.floatValue == 0 }
            return false
        }
        func held(_ layer: CALayer) -> [CALayer] {
            let out = (layer.animationKeys() ?? []).compactMap(layer.animation(forKey:))
                .contains { !$0.isRemovedOnCompletion && fadesOut($0) }
            return out && !layer.isHidden ? [layer] : (layer.sublayers ?? []).flatMap(held)
        }
        return (root.sublayers ?? []).flatMap(held)
    }

    /// A held fade that started visible (a leave, not a hold) and has not ended yet.
    private static func stillLeaving(_ layer: CALayer) -> Bool {
        let now = layer.convertTime(CACurrentMediaTime(), from: nil)
        func visibleAtStart(_ animation: CAAnimation) -> Bool {
            if let group = animation as? CAAnimationGroup { return (group.animations ?? []).contains(where: visibleAtStart) }
            if let basic = animation as? CABasicAnimation, basic.keyPath == "opacity" { return ((basic.fromValue as? NSNumber)?.floatValue ?? 1) > 0 }
            if let frames = animation as? CAKeyframeAnimation, frames.keyPath == "opacity" { return ((frames.values?.first as? NSNumber)?.floatValue ?? 1) > 0 }
            return false
        }
        return (layer.animationKeys() ?? []).compactMap(layer.animation(forKey:)).contains {
            !$0.isRemovedOnCompletion && visibleAtStart($0) && ($0.beginTime == 0 || now < $0.beginTime + $0.duration)
        }
    }

    var isDimmed: Bool { dimmed }

    /// Everything off at once (HUD close).
    func reset() {
        dismissConfirm(); clearToast(); stillRequest += 1
        HUDMotion.perform {
            dim.opacity = 0; blurTarget?.filters = nil
            if stillHidesContent || still.superlayer != nil {
                blurTarget?.removeAnimation(forKey: "hud.opacity"); blurTarget?.opacity = stillRestoreOpacity
            }
            still.removeAllAnimations(); still.opacity = 0; still.removeFromSuperlayer(); picture.contents = nil
        }
        stillHidesContent = false
        dimmed = false
    }
}
