import AppKit
import QuartzCore

/// The dark panel shared by menus and pop-overs (spec §2.6, §2.17): #1A1A1A,
/// a 1 px ring, a deep shadow and the yellow corner L at the top-left.
enum HUDPanelChrome {
    static func apply(to panel: CALayer, size: CGSize) {
        HUDMotion.perform {
            panel.bounds = CGRect(origin: .zero, size: size)
            panel.backgroundColor = HUDColor.panel
            panel.borderWidth = 1; panel.borderColor = HUDColor.ringMenu
            panel.shadowColor = HUDColor.black(1); panel.shadowOpacity = 0.6
            panel.shadowRadius = 20; panel.shadowOffset = CGSize(width: 0, height: 18)
            panel.shadowPath = CGPath(rect: panel.bounds, transform: nil)
            let corner = panel.sublayers?.first { $0.name == "panel.corner" } as? CAShapeLayer ?? {
                let shape = HUDLayer.shape("panel.corner"); panel.addSublayer(shape); return shape
            }()
            corner.frame = CGRect(x: -1, y: -1, width: 10, height: 10); corner.zPosition = 1
            corner.path = HUDShape.cornerMark(corner.bounds, length: 10, width: 2, corner: .topLeft)
            corner.fillColor = HUDColor.yellow
        }
    }
}

/// Dropdown field (spec §2.6): dark field, ellipsised value, yellow caret, and
/// a yellow left bar that scales in on hover or while its menu is open.
final class HUDDropdown: HUDControl {
    enum Size { case regular, small }
    private let size: Size
    private let value = HUDTextLayer(style: HUDType.dropdownValue)
    private let accentBar = HUDLayer.plain("dropdown.bar")
    private let caret = HUDLayer.shape("dropdown.caret")
    private(set) var valueText: String
    var isOpen = false { didSet { if isOpen != oldValue { renderState() } } }

    init(id: String, label: String, value text: String, valueLanguage: String? = nil, size: Size = .regular,
         width: CGFloat? = nil, env: HUDDesignEnvironment) {
        self.size = size; valueText = text
        super.init(id: id, label: label, env: env)
        let style = size == .small ? HUDType.dropdownValue.sized(12) : HUDType.dropdownValue
        let height: CGFloat = size == .small ? 26 : 30
        let natural = 12 + HUDText.width(text, style, language: valueLanguage ?? HUDTextLanguage.tag()) + 14 + 9 + 10
        let fieldWidth = width ?? (size == .small ? max(96, natural) : 178)
        let bounds = CGRect(x: 0, y: 0, width: fieldWidth, height: height)
        HUDMotion.perform {
            layer.bounds = bounds
            layer.backgroundColor = HUDColor.control
            layer.borderWidth = 1; layer.borderColor = HUDColor.ringField
            value.textStyle = style
            value.language = valueLanguage
            accentBar.frame = CGRect(x: 0, y: 0, width: 2, height: height); accentBar.backgroundColor = HUDColor.yellow
            accentBar.setValue(0, forKeyPath: "transform.scale.y")
            caret.bounds = CGRect(x: 0, y: 0, width: 9, height: 6)
            caret.position = CGPoint(x: fieldWidth - 10 - 4.5, y: height / 2)
            caret.path = HUDShape.triangleDown(width: 9, height: 6); caret.fillColor = HUDColor.yellow
            [accentBar, value, caret].forEach(layer.addSublayer)
        }
        setValue(text, language: valueLanguage)
        setTarget(bounds)
    }

    func setValue(_ text: String, language: String? = nil) {
        valueText = text
        HUDMotion.perform {
            value.text = text; value.language = language
            let height = layer.bounds.height, size = value.textStyle.size
            value.lineFrame = CGRect(x: 12, y: (height - size) / 2, width: layer.bounds.width - 12 - 14 - 9 - 10, height: size)
        }
        env.applyScale(to: value)
    }

    private func renderState() {
        let engaged = isOpen || (isHovered && isEnabled)
        HUDMotion.animate(accentBar, "transform.scale.y", to: engaged ? 1 : 0, duration: HUDDuration.accentBar, env: env)
        var transform = CATransform3DIdentity
        if isOpen { transform = CATransform3DMakeRotation(.pi, 0, 0, 1) }
        if isHovered && isEnabled { transform = CATransform3DTranslate(transform, 0, isOpen ? -2 : 2, 0) }
        HUDMotion.animate(caret, "transform", to: NSValue(caTransform3D: transform), duration: HUDDuration.caret, env: env)
    }

    override func hoverChanged() { renderState() }
    override func enabledChanged() { HUDMotion.perform { layer.opacity = isEnabled ? 1 : 0.35 } }
    override var accessibilityRole: HUDAccessibilityItem.Role? { .popUp(valueText) }
}

/// The one dropdown/context menu of the redesign (spec §2.6), also used by the
/// Notes and Archive font menus: top-down wipe, plate highlight with a black
/// bar, yellow square on the selection, arrow keys / Enter / Esc, and a
/// draggable yellow scroll bar when there are more than seven entries.
final class HUDMenu {
    struct Item: Equatable {
        let id: String
        let title: String
        var language: String? = nil
        var selected = false
        var danger = false
        var separatorBefore = false
        var enabled = true
    }

    final class Row: HUDControl {
        let item: Item
        fileprivate weak var menu: HUDMenu?
        fileprivate let plate = HUDLayer.plain("menu.item.plate")
        fileprivate let bar = HUDLayer.plain("menu.item.bar")
        fileprivate let text: HUDTextLayer
        fileprivate let mark = HUDLayer.plain("menu.item.mark")

        /// The control id is `menuID:itemID` (accessibility and verification).
        init(item: Item, menuID: String, width: CGFloat, env: HUDDesignEnvironment) {
            self.item = item
            text = HUDTextLayer(item.title, style: HUDType.menuItem.colored(item.danger ? HUDColor.danger : HUDColor.menuText))
            text.language = item.language
            super.init(id: menuID + ":" + item.id, label: item.title, env: env)
            let bounds = CGRect(x: 0, y: 0, width: width, height: HUDMenu.rowHeight)
            HUDMotion.perform {
                layer.bounds = bounds
                plate.frame = bounds; plate.backgroundColor = HUDColor.plate; plate.isHidden = true
                bar.frame = CGRect(x: 0, y: 5, width: 3, height: 20); bar.backgroundColor = HUDColor.nearBlack
                plate.addSublayer(bar)
                text.lineFrame = CGRect(x: 14, y: 8.75, width: width - 14 - 12 - (item.selected ? 24 : 0), height: 12.5)
                [plate, text].forEach(layer.addSublayer)
                if item.selected {
                    mark.frame = CGRect(x: width - 12 - 12, y: 9, width: 12, height: 12)
                    mark.backgroundColor = HUDColor.yellow
                    let gap = HUDLayer.plain(); gap.frame = CGRect(x: 1, y: 1, width: 10, height: 10); gap.backgroundColor = HUDColor.panel
                    let core = HUDLayer.plain(); core.frame = CGRect(x: 3, y: 3, width: 6, height: 6); core.backgroundColor = HUDColor.yellow
                    mark.addSublayer(gap); mark.addSublayer(core)
                    layer.addSublayer(mark)
                }
                layer.opacity = item.enabled ? 1 : 0.35
            }
            isEnabled = item.enabled
            setTarget(bounds)
        }

        fileprivate func setHighlighted(_ on: Bool) {
            HUDMotion.perform {
                plate.isHidden = !on
                text.color = on ? HUDColor.nearBlack : (item.danger ? HUDColor.danger : HUDColor.menuText)
            }
        }
        override func hoverChanged() { if isHovered { menu?.keyboardIndex = nil }; menu?.renderHighlight() }
        override func activate() { guard isEnabled else { return }; menu?.pick(item) }
        override var accessibilityRole: HUDAccessibilityItem.Role? { .menuItem(selected: item.selected) }
    }

    /// The scroll track of a long menu: press or drag to move the thumb there.
    final class Track: HUDControl {
        fileprivate weak var menu: HUDMenu?
        override func pointerDown(at point: CGPoint) -> Bool { menu?.scrollTrack(to: point.y); return isEnabled }
        override func pointerDragged(to point: CGPoint) { menu?.scrollTrack(to: point.y) }
        override func pointerUp(at point: CGPoint) {}
        override var accessibilityRole: HUDAccessibilityItem.Role? { nil }
    }

    static let rowHeight: CGFloat = 30
    static let longMenuThreshold = 7
    static let maximumListHeight: CGFloat = 210

    let id: String
    let layer = HUDLayer.plain("menu")
    let items: [Item]
    let env: HUDDesignEnvironment
    var onPick: ((Item) -> Void)?
    var onClose: (() -> Void)?
    private(set) var rows: [Row] = []
    private let list = HUDLayer.plain("menu.list")
    private let content = HUDLayer.plain("menu.content")
    private let track = HUDLayer.plain("menu.track")
    private let thumb = HUDLayer.plain("menu.thumb")
    private var trackControl: Track?
    private(set) var scrollOffset: CGFloat = 0
    private var contentHeight: CGFloat = 0
    fileprivate(set) var keyboardIndex: Int? { didSet { renderHighlight() } }
    var isLong: Bool { items.count > Self.longMenuThreshold }

    init(id: String, items: [Item], minimumWidth: CGFloat, env: HUDDesignEnvironment) {
        self.id = id; self.items = items; self.env = env
        let natural = items.map { 14 + HUDText.width($0.title, HUDType.menuItem, language: $0.language ?? HUDTextLanguage.tag()) + 12 + 24 }.max() ?? 0
        let long = items.count > Self.longMenuThreshold
        let width = ceil(max(minimumWidth, 150, natural + (long ? 12 : 0)))
        let rowWidth = width - (long ? 12 : 0)
        var y: CGFloat = 0
        for item in items {
            if item.separatorBefore {
                let separator = HUDLayer.plain("menu.separator")
                separator.frame = CGRect(x: 10, y: y + 4, width: rowWidth - 20, height: 1); separator.backgroundColor = HUDColor.separator
                content.addSublayer(separator); y += 9
            }
            let row = Row(item: item, menuID: id, width: rowWidth, env: env)
            row.menu = self
            row.layer.frame = CGRect(x: 0, y: y, width: rowWidth, height: Self.rowHeight)
            content.addSublayer(row.layer); rows.append(row)
            y += Self.rowHeight
        }
        contentHeight = y
        let listHeight = long ? min(Self.maximumListHeight, y) : y
        HUDPanelChrome.apply(to: layer, size: CGSize(width: width, height: listHeight + 8))
        HUDMotion.perform {
            layer.zPosition = HUDLayerOrder.menu
            list.frame = CGRect(x: 0, y: 4, width: rowWidth, height: listHeight); list.masksToBounds = true
            content.frame = CGRect(x: 0, y: 0, width: rowWidth, height: y)
            list.addSublayer(content); layer.insertSublayer(list, at: 0)
            if long {
                track.frame = CGRect(x: width - 4 - 5, y: 8, width: 5, height: layer.bounds.height - 16)
                track.backgroundColor = HUDColor.rgb(0x2C2C2C)
                thumb.backgroundColor = HUDColor.yellow
                track.addSublayer(thumb); layer.addSublayer(track)
                let control = Track(id: id + ".track", env: env, layer: track)
                control.menu = self; control.setTarget(track.bounds)
                trackControl = control
            }
        }
        if let selected = items.firstIndex(where: \.selected) {
            keyboardIndex = selected
            setScroll(rowTop(selected) - 60)
        } else { setScroll(0) }
        env.applyScale(to: layer)
    }

    /// Rows and the scroll track, for the canvas's router.
    var controls: [HUDControl] { rows + (trackControl.map { [$0] } ?? []) }

    /// Below the anchor (+4), left-aligned; shifted left to stay in the box;
    /// flipped above (−4) when it would pass the box's bottom by 20.
    func place(anchor: CGRect, in box: CGSize) {
        let size = layer.bounds.size
        var x = anchor.minX, y = anchor.maxY + 4
        if x + size.width > box.width { x = max(0, box.width - size.width) }
        if y + size.height > box.height + 20 { y = anchor.minY - 4 - size.height }
        HUDMotion.perform { layer.frame = CGRect(origin: CGPoint(x: x, y: y), size: size) }
    }

    func playEntrance() {
        HUDMotion.wipe(layer, from: .top, duration: HUDDuration.menuWipe, fadeFrom: 0.4, env: env)
    }

    fileprivate func pick(_ item: Item) { guard item.enabled else { return }; onPick?(item) }

    /// ↑/↓ move with wrap-around, Enter picks, Esc closes; nothing else passes.
    func handleKey(keyCode: UInt16) -> Bool {
        switch keyCode {
        case 125, 126:
            let enabled = items.indices.filter { items[$0].enabled }
            guard !enabled.isEmpty else { return true }
            let step = keyCode == 125 ? 1 : -1
            let current = keyboardIndex.flatMap { enabled.firstIndex(of: $0) } ?? (step > 0 ? -1 : enabled.count)
            let next = enabled[(current + step + enabled.count) % enabled.count]
            keyboardIndex = next
            scrollIntoView(next)
        case 36, 76:
            if let index = keyboardIndex { pick(items[index]) }
        case 53:
            onClose?()
        default: break
        }
        return true
    }

    /// Wheel scrolling of a long menu (delta in box px, positive moves down).
    func scroll(by delta: CGFloat) { if isLong { setScroll(scrollOffset + delta) } }

    fileprivate func renderHighlight() {
        for (index, row) in rows.enumerated() { row.setHighlighted(row.isHovered || index == keyboardIndex) }
    }

    private var maximumScroll: CGFloat { max(0, contentHeight - list.bounds.height) }

    private func rowTop(_ index: Int) -> CGFloat { rows.indices.contains(index) ? rows[index].layer.frame.minY : 0 }

    private func scrollIntoView(_ index: Int) {
        let top = rowTop(index)
        if top < scrollOffset { setScroll(top) }
        else if top + Self.rowHeight > scrollOffset + list.bounds.height { setScroll(top + Self.rowHeight - list.bounds.height) }
    }

    func setScroll(_ value: CGFloat) {
        scrollOffset = min(maximumScroll, max(0, value))
        HUDMotion.perform {
            content.frame.origin.y = -scrollOffset
            guard isLong else { return }
            let trackHeight = track.bounds.height
            let thumbHeight = max(18, trackHeight * list.bounds.height / max(1, contentHeight))
            let fraction = maximumScroll > 0 ? scrollOffset / maximumScroll : 0
            thumb.frame = CGRect(x: 0, y: fraction * (trackHeight - thumbHeight), width: 5, height: thumbHeight)
        }
    }

    /// The thumb centre follows the pointer: (y − thumb/2) / (track − thumb).
    fileprivate func scrollTrack(to y: CGFloat) {
        let trackHeight = track.bounds.height, thumbHeight = thumb.bounds.height
        let fraction = min(1, max(0, (y - thumbHeight / 2) / max(1, trackHeight - thumbHeight)))
        setScroll(fraction * maximumScroll)
    }
}

/// Pop-over panel (spec §2.17): header with an optional CJK label and close
/// button, content below. Placed right-aligned above its anchor.
final class HUDPopover {
    let layer = HUDLayer.plain("popover")
    let content = HUDLayer.plain("popover.content")
    let env: HUDDesignEnvironment
    private(set) var close: HUDIconButton?
    var onClose: (() -> Void)?

    init(title: String, subtitle: String? = nil, width: CGFloat = 270, contentHeight: CGFloat, closable: Bool = true,
         env: HUDDesignEnvironment) {
        self.env = env
        let height = 12 + 12 + 10 + contentHeight + 12
        HUDPanelChrome.apply(to: layer, size: CGSize(width: width, height: height))
        HUDMotion.perform {
            layer.zPosition = HUDLayerOrder.menu
            let heading = HUDTextLayer(title, style: HUDTextStyle(size: 12, weight: .bold, color: HUDColor.textOnDark, tracking: 0.16))
            heading.lineFrame = CGRect(x: 12, y: 10, width: heading.preferredWidth + 1, height: 12)
            layer.addSublayer(heading)
            if let subtitle {
                let cjk = HUDTextLayer(subtitle, style: HUDType.labelCJK)
                cjk.lineFrame = CGRect(x: heading.lineFrame.maxX + 8, y: 10.25, width: cjk.preferredWidth + 1, height: 11.5)
                layer.addSublayer(cjk)
            }
            content.frame = CGRect(x: 12, y: 34, width: width - 24, height: contentHeight)
            layer.addSublayer(content)
        }
        if closable {
            let button = HUDIconButton(id: "popover.close", label: L10n.text("Close", "关闭"), icon: .x, style: .back,
                                       side: 24, iconSize: 10, env: env)
            HUDMotion.perform { button.layer.frame = CGRect(x: width - 12 - 24, y: 6, width: 24, height: 24) }
            button.onActivate = { [weak self] in self?.onClose?() }
            close = button; layer.addSublayer(button.layer)
        }
        env.applyScale(to: layer)
    }

    /// Right-aligned to the anchor's right edge, above it (−8); below when there is no room.
    func place(anchor: CGRect, in box: CGSize) {
        let size = layer.bounds.size
        let x = min(max(0, anchor.maxX - size.width), box.width - size.width)
        var y = anchor.minY - 8 - size.height
        if y < 0 { y = anchor.maxY + 8 }
        HUDMotion.perform { layer.frame = CGRect(origin: CGPoint(x: x, y: y), size: size) }
    }

    func playEntrance() { HUDMotion.wipe(layer, from: .top, duration: HUDDuration.menuWipe, fadeFrom: 0.4, env: env) }
}
