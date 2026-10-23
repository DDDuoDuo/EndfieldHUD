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
            panel.shadowColor = HUDColor.black(1); panel.shadowOpacity = HUDColor.isLight ? 0.25 : 0.6
            panel.shadowRadius = 20; panel.shadowOffset = CGSize(width: 0, height: 18)
            panel.shadowPath = CGPath(rect: panel.bounds, transform: nil)
            let corner = panel.sublayers?.first { $0.name == "panel.corner" } as? CAShapeLayer ?? {
                let shape = HUDLayer.shape("panel.corner"); panel.addSublayer(shape); return shape
            }()
            corner.frame = CGRect(x: -1, y: -1, width: 10, height: 10); corner.zPosition = 1
            corner.path = HUDShape.cornerMark(corner.bounds, length: 10, width: 2, corner: .topLeft)
            corner.fillColor = HUDColor.accentInk
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
            accentBar.frame = CGRect(x: 0, y: 0, width: 2, height: height); accentBar.backgroundColor = HUDColor.accentInk
            accentBar.setValue(0, forKeyPath: "transform.scale.y")
            caret.bounds = CGRect(x: 0, y: 0, width: 9, height: 6)
            caret.position = CGPoint(x: fieldWidth - 10 - 4.5, y: height / 2)
            caret.path = HUDShape.triangleDown(width: 9, height: 6); caret.fillColor = HUDColor.accentInk
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

    private lazy var artwork = HUDLayer.plain("dropdown.artwork")

    /// Icon choosers: `image` (a cached raster, e.g. `HUDMenu.Icon.raster`)
    /// `height` px tall stands in for the value text, which stays the spoken
    /// value; nil restores the text.
    func setArtwork(_ image: CGImage?, height: CGFloat) {
        HUDMotion.perform {
            if artwork.superlayer == nil { layer.insertSublayer(artwork, below: caret) }
            let size = image.map { CGSize(width: height * CGFloat($0.width) / CGFloat(max(1, $0.height)), height: height) } ?? .zero
            artwork.frame = CGRect(x: 12, y: (layer.bounds.height - size.height) / 2,
                                   width: min(size.width, layer.bounds.width - 12 - 14 - 9 - 10), height: size.height)
            artwork.contents = image; artwork.contentsGravity = .resizeAspect
            artwork.isHidden = image == nil; value.isHidden = image != nil
        }
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
        /// An icon-grid entry: its artwork replaces the title, which VoiceOver
        /// still reads. Icon entries lead the menu; text rows follow below.
        var icon: Icon? = nil
    }

    /// The artwork of an icon-grid tile: the caller's cached raster (never
    /// rebuilt on hover), shown aspect-fit in `frame` of a `size` tile.
    struct Icon: Equatable {
        let image: CGImage?
        var size = CGSize(width: 40, height: 40)
        /// The image's frame in the tile; nil insets it by 6.
        var frame: CGRect? = nil
        /// The tile's fill; nil is the dark field.
        var fill: CGColor? = nil

        static func == (a: Icon, b: Icon) -> Bool {
            a.image === b.image && a.size == b.size && a.frame == b.frame && a.fill === b.fill
        }

        /// `source` drawn once, aspect-fit into `size` (box px) at `scale`;
        /// the result keeps the source's proportions.
        static func raster(_ source: CGImage, fitting size: CGSize, scale: CGFloat) -> CGImage? {
            let ratio = min(size.width / CGFloat(max(1, source.width)), size.height / CGFloat(max(1, source.height)))
            let pixels = max(1, min(8, scale.isFinite ? scale.rounded(.up) : 2))
            let width = max(1, Int((CGFloat(source.width) * ratio * pixels).rounded()))
            let height = max(1, Int((CGFloat(source.height) * ratio * pixels).rounded()))
            guard let space = CGColorSpace(name: CGColorSpace.sRGB),
                  let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0, space: space,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
            ctx.interpolationQuality = .high
            ctx.draw(source, in: CGRect(x: 0, y: 0, width: width, height: height))
            return ctx.makeImage()
        }
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
            if let icon = item.icon { buildTile(icon); return }
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
                    mark.backgroundColor = HUDColor.accentInk
                    let gap = HUDLayer.plain(); gap.frame = CGRect(x: 1, y: 1, width: 10, height: 10); gap.backgroundColor = HUDColor.panel
                    let core = HUDLayer.plain(); core.frame = CGRect(x: 3, y: 3, width: 6, height: 6); core.backgroundColor = HUDColor.accentInk
                    mark.addSublayer(gap); mark.addSublayer(core)
                    layer.addSublayer(mark)
                }
                layer.opacity = item.enabled ? 1 : 0.35
            }
            isEnabled = item.enabled
            setTarget(bounds)
        }

        /// The icon tile: fill, artwork, a 1 px edge, the hover brighten and the
        /// ring 3 px outside it (theme accent on the selection, light while
        /// highlighted from the keyboard or pointer).
        private lazy var ring = HUDLayer.plain("menu.tile.ring")
        private lazy var brighten = HUDLayer.plain("menu.tile.brighten")

        private func buildTile(_ icon: Icon) {
            let bounds = CGRect(origin: .zero, size: icon.size)
            HUDMotion.perform {
                layer.bounds = bounds
                layer.backgroundColor = icon.fill ?? HUDColor.control
                let picture = HUDLayer.plain("menu.tile.icon")
                picture.frame = icon.frame ?? bounds.insetBy(dx: 6, dy: 6)
                picture.contents = icon.image; picture.contentsGravity = .resizeAspect
                picture.minificationFilter = .trilinear
                let edge = HUDLayer.plain("menu.tile.edge")
                edge.frame = bounds; edge.borderWidth = 1; edge.borderColor = icon.fill == nil ? HUDColor.ringField : HUDColor.black(0.25)
                brighten.frame = bounds; brighten.backgroundColor = HUDColor.foreground(0.08); brighten.opacity = 0
                ring.frame = bounds.insetBy(dx: -3, dy: -3); ring.borderWidth = 1.5
                ring.borderColor = item.selected ? HUDColor.accentInk : HUDColor.menuText
                ring.opacity = item.selected ? 1 : 0
                [picture, brighten, edge, ring].forEach(layer.addSublayer)
                layer.opacity = item.enabled ? 1 : 0.35
            }
            isEnabled = item.enabled
            setTarget(bounds)
        }

        fileprivate func setHighlighted(_ on: Bool) {
            if item.icon != nil {
                guard !item.selected else { return }
                HUDMotion.animate(ring, "opacity", to: Float(on ? 0.7 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
                return
            }
            HUDMotion.perform {
                plate.isHidden = !on
                text.color = on ? HUDColor.nearBlack : (item.danger ? HUDColor.danger : HUDColor.menuText)
            }
        }
        override func hoverChanged() {
            if isHovered { menu?.keyboardIndex = nil }
            menu?.renderHighlight()
            if item.icon != nil { applyHoverMotion(lift: 1, pressScale: 0.965, brighten: brighten) }
        }
        override func pressChanged() { if item.icon != nil { applyHoverMotion(lift: 1, pressScale: 0.965, brighten: brighten) } }
        override func activate() { guard isEnabled else { return }; menu?.pick(item) }
        override var accessibilityRole: HUDAccessibilityItem.Role? { .menuItem(selected: item.selected) }
    }

    /// The scroll track of a long menu: press or drag to move the thumb there.
    /// Its bar hides while idle (HUDScrollBarVisibility); the strip stays live.
    final class Track: HUDControl {
        fileprivate weak var menu: HUDMenu?
        override func pointerDown(at point: CGPoint) -> Bool {
            menu?.scrollBar?.setDragging(true); menu?.scrollTrack(to: point.y); return isEnabled
        }
        override func pointerDragged(to point: CGPoint) { menu?.scrollTrack(to: point.y) }
        override func pointerUp(at point: CGPoint) { menu?.scrollBar?.setDragging(false) }
        override func hoverChanged() { menu?.scrollBar?.setHovered(isHovered) }
        override func clearInteractionState() { super.clearInteractionState(); menu?.scrollBar?.reset() }
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
    private let rail = HUDLayer.plain("menu.rail")
    private let thumb = HUDLayer.plain("menu.thumb")
    fileprivate private(set) var scrollBar: HUDScrollBarVisibility?
    private var trackControl: Track?
    private(set) var scrollOffset: CGFloat = 0
    private var contentHeight: CGFloat = 0
    fileprivate(set) var keyboardIndex: Int? { didSet { renderHighlight() } }
    /// Text menus scroll past seven entries, icon grids past the list height.
    let isLong: Bool
    /// The leading icon entries and the grid's column count (0 without icons).
    private let tileCount: Int, columns: Int

    static let tileGap: CGFloat = 6
    static let gridWidthLimit: CGFloat = 300

    init(id: String, items: [Item], minimumWidth: CGFloat, env: HUDDesignEnvironment) {
        self.id = id; self.items = items; self.env = env
        let tiles = items.prefix { $0.icon != nil }
        let texts = items.dropFirst(tiles.count)
        let tile = tiles.reduce(CGSize.zero) { CGSize(width: max($0.width, $1.icon?.size.width ?? 0), height: max($0.height, $1.icon?.size.height ?? 0)) }
        // Balanced rows: as few as fit the width limit, the columns evened out.
        let fit = max(1, Int((Self.gridWidthLimit - 16 + Self.tileGap) / (tile.width + Self.tileGap)))
        let lines = (tiles.count + fit - 1) / fit
        let columns = lines == 0 ? 0 : (tiles.count + lines - 1) / lines
        tileCount = tiles.count; self.columns = columns
        // 8 px around the grid (the list adds 4 above and below).
        let grid = CGSize(width: columns == 0 ? 0 : CGFloat(columns) * (tile.width + Self.tileGap) - Self.tileGap + 16,
                          height: lines == 0 ? 0 : CGFloat(lines) * (tile.height + Self.tileGap) - Self.tileGap + 8)
        let textHeight = texts.reduce(CGFloat(0)) { $0 + Self.rowHeight + ($1.separatorBefore ? 9 : 0) } + (tiles.isEmpty || texts.isEmpty ? 0 : 9)
        let natural = texts.map { 14 + HUDText.width($0.title, HUDType.menuItem, language: $0.language ?? HUDTextLanguage.tag()) + 12 + 24 }.max() ?? 0
        let long = tiles.isEmpty ? items.count > Self.longMenuThreshold : grid.height + textHeight > Self.maximumListHeight
        isLong = long
        let width = ceil(max(minimumWidth, 150, natural + (long ? 12 : 0), grid.width + (long ? 12 : 0)))
        let rowWidth = width - (long ? 12 : 0)
        var y: CGFloat = grid.height
        for (index, item) in items.enumerated() {
            if index < tiles.count, let size = item.icon?.size {
                let left = (rowWidth - grid.width) / 2 + 8
                let column = CGFloat(index % columns), line = CGFloat(index / columns)
                let row = Row(item: item, menuID: id, width: size.width, env: env)
                row.menu = self
                row.layer.frame = CGRect(x: left + column * (tile.width + Self.tileGap) + (tile.width - size.width) / 2,
                                         y: 4 + line * (tile.height + Self.tileGap) + (tile.height - size.height) / 2,
                                         width: size.width, height: size.height)
                content.addSublayer(row.layer); rows.append(row)
                continue
            }
            if item.separatorBefore || (index == tiles.count && index > 0) {
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
                rail.frame = track.bounds; rail.backgroundColor = HUDColor.tone(0x2C2C2C)
                thumb.backgroundColor = HUDColor.accentInk
                rail.addSublayer(thumb); track.addSublayer(rail); layer.addSublayer(track)
                scrollBar = HUDScrollBarVisibility(bar: rail)
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

    /// ↑/↓ move with wrap-around (←/→ too, and ↑/↓ by a row, in an icon
    /// grid), Enter picks, Esc closes; nothing else passes.
    func handleKey(keyCode: UInt16) -> Bool {
        switch keyCode {
        case 123, 124, 125, 126:
            guard let next = nextIndex(keyCode) else { return true }
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

    private func nextIndex(_ keyCode: UInt16) -> Int? {
        let enabled = items.indices.filter { items[$0].enabled }
        guard !enabled.isEmpty else { return nil }
        let horizontal = keyCode == 123 || keyCode == 124, forward = keyCode == 124 || keyCode == 125
        func linear() -> Int {
            let step = forward ? 1 : -1
            let current = keyboardIndex.flatMap { enabled.firstIndex(of: $0) } ?? (step > 0 ? -1 : enabled.count)
            return enabled[(current + step + enabled.count) % enabled.count]
        }
        guard let current = keyboardIndex, current < tileCount else { return horizontal && (tileCount == 0 || keyboardIndex != nil) ? nil : linear() }
        if horizontal { return linear() }
        let target = current + (forward ? columns : -columns)
        if target >= 0, target < tileCount, items[target].enabled { return target }
        // Off the grid: on to the text rows below it, or round to the column's other end.
        let column = Array(stride(from: current % columns, to: tileCount, by: columns)).filter { items[$0].enabled }
        let below = enabled.filter { $0 >= tileCount }
        if forward { return below.first ?? column.first }
        return below.last ?? column.last
    }

    /// Wheel scrolling of a long menu (delta in box px, positive moves down).
    func scroll(by delta: CGFloat) {
        guard isLong else { return }
        let before = scrollOffset
        setScroll(scrollOffset + delta)
        if scrollOffset != before { scrollBar?.noteScroll() }
    }

    fileprivate func renderHighlight() {
        for (index, row) in rows.enumerated() { row.setHighlighted(row.isHovered || index == keyboardIndex) }
    }

    private var maximumScroll: CGFloat { max(0, contentHeight - list.bounds.height) }

    private func rowTop(_ index: Int) -> CGFloat { rows.indices.contains(index) ? rows[index].layer.frame.minY : 0 }

    private func scrollIntoView(_ index: Int) {
        let top = rowTop(index), height = rows.indices.contains(index) ? rows[index].layer.frame.height : Self.rowHeight
        let before = scrollOffset
        if top < scrollOffset { setScroll(top) }
        else if top + height > scrollOffset + list.bounds.height { setScroll(top + height - list.bounds.height) }
        if scrollOffset != before { scrollBar?.noteScroll() }
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

    /// An empty title without a subtitle or close button drops the header row
    /// (the E-Reader settings pop-over).
    init(title: String, subtitle: String? = nil, width: CGFloat = 270, contentHeight: CGFloat, closable: Bool = true,
         env: HUDDesignEnvironment) {
        self.env = env
        let subtitle = subtitle?.isEmpty == true ? nil : subtitle
        let headerless = title.isEmpty && subtitle == nil && !closable
        let height = (headerless ? 12 : 12 + 12 + 10) + contentHeight + 12
        HUDPanelChrome.apply(to: layer, size: CGSize(width: width, height: height))
        HUDMotion.perform {
            layer.zPosition = HUDLayerOrder.menu
            let heading = HUDTextLayer(title, style: HUDTextStyle(size: 12, weight: .bold, color: HUDColor.textOnDark, tracking: 0.16))
            heading.lineFrame = CGRect(x: 12, y: 10, width: heading.preferredWidth + 1, height: 12)
            if !headerless { layer.addSublayer(heading) }
            if let subtitle {
                let cjk = HUDTextLayer(subtitle, style: HUDType.labelCJK)
                cjk.lineFrame = CGRect(x: heading.lineFrame.maxX + 8, y: 10.25, width: cjk.preferredWidth + 1, height: 11.5)
                layer.addSublayer(cjk)
            }
            content.frame = CGRect(x: 12, y: headerless ? 12 : 34, width: width - 24, height: contentHeight)
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
