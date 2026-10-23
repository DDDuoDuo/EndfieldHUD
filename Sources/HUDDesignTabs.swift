import AppKit
import QuartzCore

/// The 行动手册 tab bar (spec §2.9): Q / E keycap labels at both ends, a yellow
/// active tab that rises 3 px and opens from the centre, separators hidden
/// around it, an underline below it and an optional orange news diamond.
final class HUDTabs {
    final class Tab: HUDControl {
        fileprivate weak var bar: HUDTabs?
        fileprivate let index: Int
        fileprivate let fill = HUDLayer.plain("tab.fill")
        fileprivate let hoverFill = HUDLayer.plain("tab.hover")
        fileprivate let text: HUDTextLayer
        fileprivate let glyph: HUDIconLayer?
        fileprivate let underline = HUDLayer.plain("tab.underline")
        fileprivate let dot = HUDLayer.plain("tab.dot")
        var hasNews = false { didSet { HUDMotion.perform { dot.isHidden = !hasNews } } }

        init(id: String, title: String, icon: HUDIcon?, index: Int, compact: Bool, env: HUDDesignEnvironment) {
            self.index = index
            text = HUDTextLayer(title, style: HUDType.tab.sized(compact ? 11 : 13).weighted(.medium), alignment: .left)
            glyph = icon.map { HUDIconLayer($0, size: 14, color: HUDColor.tabText) }
            if compact { text.textStyle = HUDTextStyle(size: 11, weight: .medium, color: HUDColor.tabText) }
            super.init(id: id, label: title, env: env)
            HUDMotion.perform {
                hoverFill.backgroundColor = HUDColor.foreground(0.06); hoverFill.opacity = 0
                underline.backgroundColor = HUDColor.accentInk; underline.isHidden = true
                dot.backgroundColor = HUDColor.orange; dot.isHidden = true
                dot.bounds = CGRect(x: 0, y: 0, width: 7, height: 7)
                dot.setValue(CGFloat.pi / 4, forKeyPath: "transform.rotation.z")
                [hoverFill, fill, underline].forEach(layer.addSublayer)
                glyph.map(layer.addSublayer)
                layer.addSublayer(text); layer.addSublayer(dot)
            }
        }

        fileprivate func layout(width: CGFloat, active: Bool) {
            let height: CGFloat = active ? 37 : 34
            let bounds = CGRect(x: 0, y: 0, width: width, height: height)
            HUDMotion.perform {
                layer.bounds = bounds
                fill.frame = bounds; hoverFill.frame = bounds
                fill.showArtwork(active ? HUDArtwork.plate([HUDColor.yellowHigh, HUDColor.yellow, HUDColor.yellow], locations: [0, 0.45, 1],
                                                           size: bounds.size, cut: 0, scale: env.renderScale) : nil, scale: env.renderScale)
                fill.isHidden = !active
                let style = text.textStyle
                text.textStyle = style.weighted(active ? .bold : .medium).colored(active ? HUDColor.nearBlack : HUDColor.tabText)
                let textWidth = text.preferredWidth, iconWidth: CGFloat = glyph == nil ? 0 : 14 + 7
                let start = (width - textWidth - iconWidth) / 2
                glyph?.frame = CGRect(x: start, y: (height - 14) / 2, width: 14, height: 14)
                glyph?.color = active ? HUDColor.nearBlack : HUDColor.tabText
                text.lineFrame = CGRect(x: start + iconWidth, y: (height - style.size) / 2, width: textWidth + 1, height: style.size)
                underline.frame = CGRect(x: width * 0.32, y: height + 3, width: width * 0.36, height: 3)
                underline.isHidden = !active
                dot.position = CGPoint(x: width - 6 - 3.5, y: 6 + 3.5)
            }
            setTarget(bounds)
            env.applyScale(to: text)
        }

        override func hoverChanged() {
            guard let bar, bar.selectedIndex != index else { return }
            let on = isHovered && isEnabled
            HUDMotion.animate(hoverFill, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
            HUDMotion.animate(layer, "transform.translation.y", to: on ? -1 : 0, duration: HUDDuration.hover, env: env)
            HUDMotion.perform { text.color = on ? HUDColor.foreground(1) : HUDColor.tabText }
        }
        override func activate() { guard isEnabled else { return }; bar?.select(index, notify: true) }
        override var accessibilityRole: HUDAccessibilityItem.Role? { .tab(selected: bar?.selectedIndex == index) }
    }

    let layer = HUDLayer.plain("tabs")
    let env: HUDDesignEnvironment
    let tabs: [Tab]
    let keycaps: Bool
    private(set) var selectedIndex: Int
    private var separators: [CALayer] = []
    /// (new index, direction): +1 forward (enters from the right), −1 back.
    var onSelect: ((Int, CGFloat) -> Void)?

    init(id: String, titles: [(String, HUDIcon?)], selectedIndex: Int, width: CGFloat, keycaps: Bool = true,
         compact: Bool = false, env: HUDDesignEnvironment) {
        self.env = env; self.keycaps = keycaps; self.selectedIndex = selectedIndex
        tabs = titles.enumerated().map { Tab(id: id + ":\($0.offset)", title: $0.element.0, icon: $0.element.1, index: $0.offset,
                                              compact: compact, env: env) }
        HUDMotion.perform {
            layer.bounds = CGRect(x: 0, y: 0, width: width, height: 34)
            layer.backgroundColor = HUDColor.tabBar
            // A layer's own border draws above its sublayers and would cross the
            // raised active tab; this frame sits beneath the tabs instead.
            let frame = HUDLayer.plain("tabs.frame")
            frame.frame = layer.bounds; frame.borderWidth = 1; frame.borderColor = HUDColor.ringTab
            layer.addSublayer(frame)
            if keycaps {
                for (index, key) in ["Q", "E"].enumerated() {
                    let cell = HUDLayer.plain("tabs.keycap")
                    cell.frame = CGRect(x: index == 0 ? 6.5 : width - 21.5, y: 9.5, width: 15, height: 15)
                    cell.borderWidth = 1; cell.borderColor = HUDColor.tone(0x6A6A6A)
                    let text = HUDTextLayer(key, style: HUDType.tabKeycap, alignment: .center)
                    text.lineFrame = CGRect(x: 0, y: 3, width: 15, height: 9)
                    cell.addSublayer(text); layer.addSublayer(cell)
                }
            }
        }
        for tab in tabs { tab.bar = self; layer.addSublayer(tab.layer) }
        layoutTabs()
        env.applyScale(to: layer)
    }

    private func layoutTabs() {
        let inset: CGFloat = keycaps ? 28 : 0
        let width = (layer.bounds.width - inset * 2) / CGFloat(max(1, tabs.count))
        HUDMotion.perform {
            separators.forEach { $0.removeFromSuperlayer() }; separators = []
            for tab in tabs {
                let active = tab.index == selectedIndex
                tab.layout(width: width, active: active)
                tab.layer.position = CGPoint(x: inset + width * (CGFloat(tab.index) + 0.5), y: active ? 34 / 2 - 1.5 : 17)
                tab.layer.zPosition = active ? 1 : 0
                if tab.index > 0, tab.index != selectedIndex, tab.index - 1 != selectedIndex {
                    let line = HUDLayer.plain("tabs.separator")
                    line.frame = CGRect(x: inset + width * CGFloat(tab.index), y: 10, width: 1, height: 14)
                    line.backgroundColor = HUDColor.tone(0x4A4A4A)
                    layer.addSublayer(line); separators.append(line)
                }
            }
        }
    }

    func select(_ index: Int, notify: Bool) {
        guard tabs.indices.contains(index), index != selectedIndex else { return }
        let direction: CGFloat = index > selectedIndex ? 1 : -1
        selectedIndex = index
        layoutTabs()
        HUDMotion.wipe(tabs[index].fill, from: .centerX, duration: HUDDuration.tabOn, env: env)
        if notify { onSelect?(index, direction) }
    }

    /// Q = previous, E = next, wrapping around.
    func cycle(_ step: Int) {
        guard !tabs.isEmpty else { return }
        let next = (selectedIndex + step + tabs.count) % tabs.count
        guard next != selectedIndex else { return }
        let direction: CGFloat = step > 0 ? 1 : -1
        selectedIndex = next
        layoutTabs()
        HUDMotion.wipe(tabs[next].fill, from: .centerX, duration: HUDDuration.tabOn, env: env)
        onSelect?(next, direction)
    }

    var controls: [HUDControl] { tabs }
}
