import AppKit

/// Native accessibility counterparts share the projected HUD control plane.
final class HUDOrbiPomInteraction {
    let canvas: OrbiPomCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var unproject: ((CGPoint) -> CGPoint?)?
    private(set) var secondaryMenu: NotesRetainedMenu?
    var capturesPointer: Bool { secondaryMenu != nil }
    private var menuOrigin = CGPoint.zero
    private var retiring: CALayer?
    private var retireWork: DispatchWorkItem?
    private var buttons: [String: OrbiPomAXButton] = [:]
    private var active = false
    init(canvas: OrbiPomCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
        canvas.onRequestRules = { [weak self] in self?.showRules() }
    }
    deinit {
        retireWork?.cancel(); retiring?.removeFromSuperlayer(); secondaryMenu?.removeFromSuperview()
        canvas.deactivate(); buttons.values.forEach { $0.removeFromSuperview() }
    }
    func setPresented(_ value: Bool) { canvas.setPresented(value); if !value { setActive(false) } }
    func setActive(_ value: Bool) {
        guard active != value else { return }; active = value; canvas.setActive(value)
        if value { layoutAccessibility() } else { closeMenu(animated: false); buttons.values.forEach { $0.isHidden = true } }
    }
    func deactivate() { setActive(false); canvas.deactivate() }
    func layoutAccessibility() {
        guard active, let host else { return }
        positionMenu()
        for action in canvas.actions {
            let button = buttons[action.id] ?? OrbiPomAXButton(frame:.zero)
            if buttons[action.id] == nil { buttons[action.id] = button; host.addSubview(button) }
            button.isBordered = false; button.title = ""; button.isHidden = secondaryMenu != nil; button.isEnabled = action.enabled
            button.frame = project?(action.rect) ?? action.rect
            var title = action.title
            if action.id == "pause" { title = L10n.text("Pause / resume", "暂停 / 继续") }
            if action.id == "restart" { title = L10n.text("Restart", "重新开始") }
            if action.id == "cancelRestart" || action.id == "cancelSkill" { title = L10n.text("Cancel", "取消") }
            if action.id == "confirmRestart" { title = L10n.text("Confirm", "确认") }
            if action.id == "rules" { title = L10n.text("Rules", "游戏规则") }
            button.setAccessibilityLabel(title); button.onPress = { [weak self] in self?.canvas.perform(action.id) }
        }
        let visible = Set(canvas.actions.map(\.id))
        for (id,button) in buttons where !visible.contains(id) { button.isHidden = true }
        HUDControlHighlightLayer.requestRefresh(on:host)
    }
    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard active else { return false }
        if let menu = secondaryMenu {
            let local = CGPoint(x: point.x - menuOrigin.x,y: point.y - menuOrigin.y)
            if menu.bounds.contains(local) { menu.activate(at: local) } else { closeMenu() }
            return true
        }
        host?.window?.makeFirstResponder(host)
        return canvas.mouseDown(at:point)
    }
    func keyDown(_ event: NSEvent) -> Bool {
        guard active else { return false }
        if let menu = secondaryMenu {
            if event.keyCode == 53 { closeMenu() }
            else if event.keyCode == 125 { (menu as? OrbiPomRulesMenu)?.scroll(delta: 24) }
            else if event.keyCode == 126 { (menu as? OrbiPomRulesMenu)?.scroll(delta: -24) }
            else { menu.keyDown(with: event) }
            return true
        }
        return canvas.keyDown(event)
    }
    @discardableResult func scroll(delta: CGFloat) -> Bool {
        guard active, let menu = secondaryMenu as? OrbiPomRulesMenu else { return false }
        menu.scroll(delta: delta); return true
    }
    func hitTestMenu(at point: CGPoint) -> NSView? { secondaryMenu?.hitTest(point) }
    private func showRules() {
        guard active, let host else { return }
        if secondaryMenu != nil { closeMenu(); return }
        retireWork?.cancel(); retireWork = nil; retiring?.removeFromSuperlayer(); retiring = nil
        let menu = OrbiPomRulesMenu(dark: canvas.darkAppearance)
        secondaryMenu = menu
        let button = canvas.actions.first { $0.id == "rules" }!.rect
        menuOrigin = CGPoint(x: min(max(8,button.maxX-menu.contentSize.width),432-menu.contentSize.width),
                             y: max(42,button.minY-menu.contentSize.height-8))
        host.addSubview(menu); canvas.layer.addSublayer(menu.artwork); menu.artwork.zPosition = 3_000_000
        menu.hostToLocal = { [weak self] point in
            guard let self, let p = self.unproject?(point) ?? (self.unproject == nil ? point : nil) else { return nil }
            return CGPoint(x:p.x-self.menuOrigin.x,y:p.y-self.menuOrigin.y)
        }
        menu.projectLocal = { [weak self] rect in
            guard let self else { return .zero }
            let source = rect.offsetBy(dx:self.menuOrigin.x,dy:self.menuOrigin.y)
            return self.project?(source) ?? source
        }
        menu.onCancel = { [weak self] in self?.closeMenu() }
        positionMenu(); menu.paint(); animate(menu.artwork,from:0,to:1)
        canvas.setRulesPresented(true)
        host.window?.makeFirstResponder(menu); layoutAccessibility()
    }
    private func positionMenu() {
        guard let menu = secondaryMenu else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        menu.frame = project?(CGRect(origin:menuOrigin,size:menu.contentSize)) ?? CGRect(origin:menuOrigin,size:menu.contentSize)
        menu.bounds = CGRect(origin:.zero,size:menu.contentSize); menu.artwork.position = menuOrigin
        menu.layoutAccessibility(); CATransaction.commit()
    }
    private func closeMenu(animated: Bool = true) {
        retireWork?.cancel(); retireWork = nil; retiring?.removeFromSuperlayer(); retiring = nil
        guard let menu = secondaryMenu else { canvas.setRulesPresented(false); return }
        secondaryMenu = nil
        if animated && !HUDRuntimeAppearance.reduceMotion {
            menu.detachInputKeepingArtwork(); retiring = menu.artwork; animate(menu.artwork,from:1,to:0)
            let work = DispatchWorkItem { [weak self] in
                self?.retiring?.removeFromSuperlayer(); self?.retiring = nil; self?.retireWork = nil
            }
            retireWork = work; DispatchQueue.main.asyncAfter(deadline:.now()+0.18,execute:work)
        } else { menu.removeFromSuperview() }
        canvas.setRulesPresented(false); host?.window?.makeFirstResponder(host); layoutAccessibility()
    }
    private func animate(_ layer: CALayer,from: Float,to: Float) {
        CATransaction.begin(); CATransaction.setDisableActions(true); layer.opacity = to; CATransaction.commit()
        guard !HUDRuntimeAppearance.reduceMotion else { return }
        let fade = CABasicAnimation(keyPath:"opacity"); fade.fromValue=from; fade.toValue=to; fade.duration=0.16
        layer.add(fade,forKey:"orbipom.rules")
    }
}

/// The same retained personal-card menu face, projected hit testing, finite
/// animation and native accessibility surface used by the existing HUD.
private final class OrbiPomRulesMenu: NotesRetainedMenu {
    private let paragraphs: [String]
    private let heights: [CGFloat]
    private let totalHeight: CGFloat
    private var offset: CGFloat = 0
    private let content = CALayer()
    init(dark: Bool) {
        paragraphs = [
            L10n.text("Move to aim; click or press Space to drop.", "移动鼠标瞄准，点击或按空格投放。"),
            L10n.text("Merge matching levels. Two level-11 OrbiPoms disappear when merged.", "相同等级相碰即可合成；两个11级山团团合成后消失。"),
            L10n.text("Points by source level: 1 / 3 / 6 / 10 / 15 / 21 / 28 / 36 / 45 / 55 / 66.", "各等级合成得分：1／3／6／10／15／21／28／36／45／55／66。"),
            L10n.text("12 merges grant 1 SP, up to 3. Spend 6 SP to charge Swap.", "每合成12次获得1点技力，最多3点；累计消耗6点技力可使用交换。"),
            L10n.text("Partial SP charge can decay while idle.", "闲置时，未充满的技力进度可能衰减。"),
            L10n.text("Eliminate (1 SP): select one target. Wind (2 SP) lifts the stack; Shake (3 SP) shakes it.", "消除（1技力）：选择一个目标。风场（2技力）托起堆叠；震动（3技力）摇动堆叠。"),
            L10n.text("Select a skill, then click the board to confirm. Swap needs two different targets.", "选择技能后点击游戏区确认；交换需要选择两个不同目标。"),
            L10n.text("A settled overflow above the red line starts a 5-second countdown.", "静止的山团团超过红线时，会开始5秒倒计时。"),
            L10n.text("P pauses. Escape cancels a selected skill.", "按P暂停，按Esc取消已选择的技能。")
        ]
        let font = NSFont.systemFont(ofSize:10,weight:.semibold)
        heights = paragraphs.map { max(18,ceil(NSAttributedString(string:$0,attributes:[.font:font]).boundingRect(with:CGSize(width:294,height:1000),options:[.usesLineFragmentOrigin,.usesFontLeading]).height)+3) }
        totalHeight = heights.reduce(0,+) + CGFloat(heights.count-1) * 9
        super.init(size:CGSize(width:322,height:min(348,48+totalHeight)),dark:dark)
        items = [Item(id:"close",title:"×",rect:CGRect(x:291,y:8,width:23,height:23))]
        setAccessibilityLabel(L10n.text("Rules", "游戏规则")); setAccessibilityValue(paragraphs.joined(separator:"\n"))
        paint()
    }
    required init?(coder:NSCoder) { nil }
    override func paintContent(on parent:CALayer) {
        text(HUDSectionHeading.text(L10n.text("Rules", "游戏规则")),rect:CGRect(x:12,y:11,width:269,height:20),size:12,parent:parent)
        let clip = CALayer(); clip.frame=CGRect(x:12,y:40,width:298,height:contentSize.height-49); clip.masksToBounds=true; parent.addSublayer(clip)
        content.sublayers?.forEach { $0.removeFromSuperlayer() }; content.anchorPoint = .zero; content.position=CGPoint(x:0,y:-offset); clip.addSublayer(content)
        var y:CGFloat=0
        for (paragraph,height) in zip(paragraphs,heights) {
            text(paragraph,rect:CGRect(x:0,y:y,width:294,height:height),size:10,parent:content)
            if let label=content.sublayers?.last as? CATextLayer { label.isWrapped=true; label.truncationMode = .none }
            y += height+9
        }
    }
    func scroll(delta:CGFloat) {
        let next = min(max(0,totalHeight-(contentSize.height-49)),max(0,offset+delta))
        guard next != offset else { return }; offset=next
        let from=content.presentation()?.position ?? content.position
        CATransaction.begin(); CATransaction.setDisableActions(true); content.position=CGPoint(x:0,y:-offset); CATransaction.commit()
        guard !HUDRuntimeAppearance.reduceMotion else { return }
        let motion=CABasicAnimation(keyPath:"position"); motion.fromValue=NSValue(point:from); motion.toValue=NSValue(point:content.position)
        motion.duration=0.13; motion.timingFunction=CAMediaTimingFunction(name:.easeOut); content.add(motion,forKey:"orbipom.rulesScroll")
    }
    override func scrollWheel(with event:NSEvent) { scroll(delta:-event.scrollingDeltaY) }
}
private final class OrbiPomAXButton: NSButton {
    var onPress: (() -> Void)?
    override var acceptsFirstResponder: Bool { true }
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityPerformPress() -> Bool {
        guard isEnabled, !isHidden else { return false }; onPress?(); return true
    }
    override func keyDown(with event: NSEvent) {
        if [36,49,76].contains(event.keyCode) { _ = accessibilityPerformPress() } else { super.keyDown(with:event) }
    }
}
