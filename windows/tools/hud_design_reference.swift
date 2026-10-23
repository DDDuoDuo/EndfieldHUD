import AppKit
import CoreText
import QuartzCore

// Actual-source oracle of the redesign's component library (Sources/HUDDesign*.swift, HUDControlHighlightLayer,
// HUDRenderScale, HUDRuntimeAppearance, Localization*) for the Windows port's design library
// (windows/modules/hud_design_*, build/windows-1.3/LIBRARY-DESIGN.md section 14). Every component is built by the
// unchanged Mac code in scripted states and exported as its model layer tree (ModuleReferenceLayerEncoder with the
// design hook: HUDTextLayer runs/line layout, HUDIconLayer, HUDHatchLayer, highlight targets, animation records).
// Isolation: temporary CFFIXED_USER_HOME, no window, no input posted (NSEvents are constructed, never sent), no
// timers that outlive a scenario, no store read or written. Layer keys equal to a fresh layer's value are left out
// of each row (`layerDefaults` lists them), floats are rounded to 1e-9, so the fixture stays small and stable.

private var reduce = false
private var currentAccent: UInt32 = 0xFAD41F
private final class Log { var items: [String] = []; func append(_ item: String) { items.append(item) } }
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { reduce })

// MARK: - JSON helpers

/// Floats are rounded to 1e-9 (stable across runs, exact enough for 7 / 5.5 style ratios).
private func round6(_ value: Double) -> Double { value.isFinite && abs(value) < 1e6 ? (value * 1e9).rounded() / 1e9 : value }
/// `exact` keeps full double precision (the token section: constants, no layout noise).
private func sanitize(_ any: Any, exact: Bool = false) -> Any {
    switch any {
    case let number as NSNumber where CFGetTypeID(number) == CFBooleanGetTypeID(): return number.boolValue
    case let number as NSNumber:
        let double = number.doubleValue
        if !double.isFinite { return double > 0 ? "inf" : double < 0 ? "-inf" : "nan" }
        return exact ? double : round6(double)
    case let array as [Any]: return array.map { sanitize($0, exact: exact) }
    case let dictionary as [String: Any]: return dictionary.mapValues { sanitize($0, exact: exact) }
    default: return any
    }
}
private func rgba(_ color: CGColor?) -> Any {
    guard let color, let converted = color.converted(to: CGColorSpace(name: CGColorSpace.sRGB)!, intent: .defaultIntent, options: nil),
          let parts = converted.components else { return NSNull() }
    return (parts.count == 2 ? [parts[0], parts[0], parts[0], parts[1]] : parts).map { Double($0) }
}
private func hex(_ color: CGColor) -> String {
    guard let parts = color.converted(to: CGColorSpace(name: CGColorSpace.sRGB)!, intent: .defaultIntent, options: nil)?.components,
          parts.count >= 3 else { return "?" }
    return parts[0...2].map { String(format: "%02X", Int((min(1, max(0, $0)) * 255).rounded())) }.joined()
}
private func rect(_ r: CGRect) -> [Double] { [r.minX, r.minY, r.width, r.height].map { round6(Double($0)) } }
private func point(_ p: CGPoint) -> [Double] { [round6(Double(p.x)), round6(Double(p.y))] }
private func pathJSON(_ p: CGPath?) -> Any {
    guard let p else { return NSNull() }
    var elements: [[String: Any]] = []
    p.applyWithBlock { pointer in
        let e = pointer.pointee
        let (kind, count): (String, Int)
        switch e.type {
        case .moveToPoint: (kind, count) = ("move", 1)
        case .addLineToPoint: (kind, count) = ("line", 1)
        case .addQuadCurveToPoint: (kind, count) = ("quadratic", 2)
        case .addCurveToPoint: (kind, count) = ("cubic", 3)
        case .closeSubpath: (kind, count) = ("close", 0)
        @unknown default: (kind, count) = ("unknown", 0)
        }
        elements.append(["op": kind, "points": (0..<count).map { point(e.points[$0]) }])
    }
    return elements
}
/// A text style; lineHeight 1, tracking 0, uppercase/tabular false and resolvedWeight == weight are left out.
private func styleJSON(_ s: HUDTextStyle) -> [String: Any] {
    var row: [String: Any] = ["size": s.size, "weight": s.weight.rawValue, "color": rgba(s.color)]
    if s.weight.resolved != s.weight { row["resolvedWeight"] = s.weight.resolved.rawValue }
    if s.lineHeight != 1 { row["lineHeight"] = s.lineHeight }
    if s.tracking != 0 { row["tracking"] = s.tracking }
    if s.uppercase { row["uppercase"] = true }
    if s.tabular { row["tabular"] = true }
    return row
}
private func timingJSON(_ f: CAMediaTimingFunction) -> [Double] {
    var one: [Float] = [0, 0], two: [Float] = [0, 0]
    f.getControlPoint(at: 1, values: &one); f.getControlPoint(at: 2, values: &two)
    return [one[0], one[1], two[0], two[1]].map { round6(Double($0)) }
}
private func mirrored<T>(_ object: Any, _ name: String, as: T.Type) -> T? {
    var mirror: Mirror? = Mirror(reflecting: object)
    while let current = mirror {
        if let value = current.children.first(where: { $0.label == name })?.value { return value as? T }
        mirror = current.superclassMirror
    }
    return nil
}
private func languageName(_ language: AppLanguage) -> String {
    switch language {
    case .english: return "en"
    case .simplifiedChinese: return "zh-Hans"
    case .traditionalChinese: return "zh-Hant"
    case .japanese: return "ja"
    case .korean: return "ko"
    case .system: return "system"
    }
}
private let languages: [AppLanguage] = [.english, .simplifiedChinese, .traditionalChinese, .japanese, .korean]
private let accents = ["FAD41F", "22A6F2", "E5383B", "8CE03C"]
private func key(_ code: UInt16, _ characters: String = "", flags: NSEvent.ModifierFlags = []) -> NSEvent {
    // Constructed only to call the canvas's keyDown directly; never posted to any queue.
    NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: flags, timestamp: 0, windowNumber: 0, context: nil,
                     characters: characters, charactersIgnoringModifiers: characters, isARepeat: false, keyCode: code)!
}

// MARK: - Recorder: layer trees, measured widths and line metrics

private final class Recorder {
    let encoder: ModuleReferenceLayerEncoder
    private(set) var widths: [String: [String: Any]] = [:]
    private(set) var metrics: [String: [String: Any]] = [:]
    private var defaults: [String: Data] = [:]
    private(set) var layerDefaults: [String: Any] = [:]
    private static let kept: Set<String> = ["id", "name", "class", "kind", "children", "bounds", "position", "frame"]

    init(output: URL) throws {
        encoder = try ModuleReferenceLayerEncoder(output: output)
        encoder.recordsAnimations = true
        let plain = try encoder.encode(CALayer(), id: "default")
        for (name, value) in plain where !Self.kept.contains(name) {
            defaults[name] = try JSONSerialization.data(withJSONObject: ["v": sanitize(value)], options: [.sortedKeys])
            layerDefaults[name] = sanitize(value)
        }
        encoder.layerHook = { [unowned self] layer, _, row in self.hook(layer, &row) }
    }

    /// Records HUDText.width for these runs (the source of the Windows tests' recorded-width measurer).
    @discardableResult func width(_ runs: [HUDText.Run], language: String) -> CGFloat {
        let value = HUDText.width(runs, language: language)
        let runRows: [[String: Any]] = runs.filter { !$0.text.isEmpty }.map { run in
            ["text": run.text, "displayed": HUDText.displayed(run.text, run.style), "size": run.style.size,
             "weight": run.style.weight.resolved.rawValue, "tracking": run.style.tracking, "tabular": run.style.tabular,
             "uppercase": run.style.uppercase]
        }
        let entry: [String: Any] = ["runs": runRows, "language": language, "width": value]
        if let data = try? JSONSerialization.data(withJSONObject: sanitize(["r": runRows, "l": language]), options: [.sortedKeys]),
           let k = String(data: data, encoding: .utf8) { widths[k] = entry }
        return value
    }
    @discardableResult func width(_ text: String, _ style: HUDTextStyle, language: String = HUDTextLanguage.tag()) -> CGFloat {
        width([HUDText.Run(text, style)], language: language)
    }
    func lineMetrics(_ style: HUDTextStyle, language: String, scale: CGFloat = 1) -> [String: Any] {
        let m = HUDText.metrics(style, language: language, scale: scale)
        let row: [String: Any] = ["size": style.size, "weight": style.weight.resolved.rawValue, "lineHeight": style.lineHeight,
            "tabular": style.tabular, "language": language, "scale": scale,
            "height": m.height, "ascent": m.ascent, "descent": m.descent, "baseline": m.baseline]
        metrics["\(style.size)|\(style.weight.resolved.rawValue)|\(style.lineHeight)|\(style.tabular)|\(language)|\(scale)"] = row
        return row
    }

    private func hook(_ layer: CALayer, _ row: inout [String: Any]) -> Bool {
        switch layer {
        case let text as HUDTextLayer: row["kind"] = "designText"; row["designText"] = designText(text); return true
        case let icon as HUDIconLayer:
            row["icon"] = ["name": icon.icon?.rawValue as Any? ?? NSNull(), "size": icon.bounds.width, "color": rgba(icon.color)]
            return true
        case let hatch as HUDHatchLayer:
            row["hatch"] = ["kind": hatch.kind == .light ? "light" : "dark", "artworkScale": hatch.artworkScale]; return true
        case let target as HUDControlHighlightLayer:
            row["target"] = ["container": target.isContainer,
                             "enabled": mirrored(target, "enabled", as: Bool.self) as Any? ?? NSNull(),
                             "highlighted": mirrored(target, "highlighted", as: Bool.self) as Any? ?? NSNull(),
                             "pressed": mirrored(target, "pressed", as: Bool.self) as Any? ?? NSNull(),
                             "path": mirrored(target, "controlPath", as: CGPath.self).map { $0 == CGPath(rect: target.bounds, transform: nil) ? "bounds" as Any : pathJSON($0) } as Any? ?? NSNull()]
            return true
        case let text as HUDStandardTextLayer:
            row["standardText"] = ["weight": text.weight.rawValue, "tabular": text.tabular, "oblique": text.oblique,
                                   "face": text.face?.fontName as Any? ?? NSNull()]
            return true
        case is HUDDecorativeContentLayer: row["decorative"] = true; return true
        default:
            guard NSStringFromClass(type(of: layer)).hasSuffix("HUDStyledTextLayer"),
                  let text = mirrored(layer, "text", as: NSAttributedString.self) else { return false }
            var runs: [[String: Any]] = []
            text.enumerateAttributes(in: NSRange(location: 0, length: text.length)) { attributes, range, _ in
                var item: [String: Any] = ["utf16Range": [range.location, range.length]]
                if let font = attributes[.font] as? NSFont { item["font"] = [font.fontName, font.pointSize] }
                if let color = attributes[.foregroundColor] as? NSColor { item["color"] = rgba(color.cgColor) }
                runs.append(item)
            }
            row["styledText"] = ["string": text.string, "runs": runs]
            return true
        }
    }

    /// HUDTextLayer as data, with the layout HUDTextLayer.draw makes (shrink scale, CTLines).
    private func designText(_ t: HUDTextLayer) -> [String: Any] {
        let resolved = t.language ?? HUDTextLanguage.tag()
        let padding = mirrored(t, "framePadding", as: Optional<CGSize>.self).flatMap { $0 }
        let size = t.runs.map(\.style.size).max() ?? 12
        let inset = padding ?? CGSize(width: ceil(size * 0.25), height: ceil(size * 0.45))
        let box = t.bounds.insetBy(dx: inset.width, dy: inset.height)
        let preferred = width(t.runs, language: resolved)
        var scale: CGFloat = 1
        if t.maximumLines == 1, t.minimumScale < 1, preferred > box.width, box.width > 0 { scale = max(t.minimumScale, box.width / preferred) }
        var lines: [[String: Any]] = []
        if !t.runs.isEmpty, box.width > 0 {
            let string = HUDText.attributed(t.runs, language: resolved, scale: scale)
            for line in HUDText.lines(string, width: box.width, maximumLines: t.maximumLines, truncate: t.truncates) {
                let range = CTLineGetStringRange(line)
                lines.append(["utf16Range": [range.location, range.length], "width": CTLineGetTypographicBounds(line, nil, nil, nil)])
            }
        }
        let alignment: String
        switch t.alignment { case .left: alignment = "left"; case .center: alignment = "center"; case .right: alignment = "right" }
        if let first = t.runs.first { _ = lineMetrics(first.style, language: resolved, scale: scale) }
        return ["runs": t.runs.map { run -> [String: Any] in
                    var item: [String: Any] = ["text": run.text, "style": styleJSON(run.style)]
                    let displayed = HUDText.displayed(run.text, run.style)
                    if displayed != run.text { item["displayed"] = displayed }
                    return item },
                "language": t.language as Any? ?? NSNull(), "resolvedLanguage": resolved, "alignment": alignment,
                "maximumLines": t.maximumLines, "truncates": t.truncates, "minimumScale": t.minimumScale,
                "lineFrame": rect(t.lineFrame), "framePadding": padding.map { [$0.width, $0.height] } as Any? ?? NSNull(),
                "preferredWidth": preferred, "shrinkScale": scale, "lines": lines]
    }

    /// The layer tree with default-valued keys left out.
    func tree(_ layer: CALayer, id: String) throws -> [String: Any] {
        prune(sanitize(try encoder.encode(layer, id: id)) as! [String: Any])
    }
    /// Icon glyph paths are exported once per icon ("icon.<name>" components); inside other components an icon
    /// row keeps its name, size and part colours only.
    var fullIcons = false
    private static let animationDefaults: [String: Any] = ["timeOffset": 0.0, "speed": 1.0, "repeatCount": 0.0, "repeatDuration": 0.0,
        "autoreverses": false, "removedOnCompletion": true, "cumulative": false, "additive": false, "fillMode": "removed",
        "calculationMode": "linear", "delay": 0.0]
    /// The encoder's colour records become their sRGB components [r, g, b, a].
    private func compact(_ value: Any) -> Any {
        switch value {
        case let dictionary as [String: Any]:
            if let srgb = dictionary["sRGB"], dictionary["sourceColorSpace"] != nil { return srgb }
            return dictionary.mapValues(compact)
        case let array as [Any]: return array.map(compact)
        default: return value
        }
    }
    private func animationRow(_ value: Any) -> Any {
        guard var row = compact(value) as? [String: Any] else { return value }
        for (name, expected) in Self.animationDefaults where (row[name] as? NSObject)?.isEqual(expected) == true { row[name] = nil }
        if row["by"] is NSNull { row["by"] = nil }
        if (row["timingFunctions"] as? [Any])?.isEmpty == true { row["timingFunctions"] = nil }
        if row["timing"] is NSNull { row["timing"] = nil }
        if let children = row["children"] as? [Any] { row["children"] = children.map(animationRow) }
        return row
    }
    private func prune(_ row: [String: Any]) -> [String: Any] {
        var result: [String: Any] = [:]
        for (name, value) in row {
            if name == "children" { if let children = value as? [[String: Any]], !children.isEmpty { result[name] = children.map(prune) }; continue }
            if name == "mask", let mask = value as? [String: Any] { result[name] = prune(mask); continue }
            if name == "id" || name == "animationKeys" { continue }
            if !Self.kept.contains(name), let expected = defaults[name],
               let data = try? JSONSerialization.data(withJSONObject: ["v": value], options: [.sortedKeys]), data == expected { continue }
            if name == "animations" {
                if let list = value as? [Any], !list.isEmpty { result[name] = list.map(animationRow) }
                continue
            }
            result[name] = compact(value)
        }
        // A design text layer's frame is its lineFrame outset by framePadding (both in "designText").
        if let text = result["designText"] as? [String: Any], let line = text["lineFrame"] as? [Double], line[2] > 0 || line[3] > 0,
           result["transform"] == nil { result["frame"] = nil; result["bounds"] = nil; result["position"] = nil }
        if result["class"] as? String == "CALayer" { result["class"] = nil }
        if result["kind"] as? String == "layer" { result["kind"] = nil }
        if result["transform"] == nil, result["anchorPoint"] == nil { result["position"] = nil }
        if !fullIcons, result["icon"] != nil, let children = result["children"] as? [[String: Any]] {
            result["children"] = nil
            result["partColors"] = children.map { child -> [Any] in
                let shape = child["shape"] as? [String: Any]
                return [shape?["fillColor"] ?? NSNull(), shape?["strokeColor"] ?? NSNull()]
            }
        }
        return result
    }
}

// MARK: - Scenario harness

/// A host layer standing in for the canvas root (590 x 510), so hover targets, conversions and
/// HUDMotion.animate (which needs a superlayer) behave as in a module. Its local time is frozen at 0
/// (speed 0), so an animation's beginTime is exactly its delay and the records are deterministic.
private func host(_ layers: CALayer..., paused: Bool = true) -> CALayer {
    let root = HUDLayer.plain("host")
    HUDMotion.perform {
        root.bounds = CGRect(origin: .zero, size: HUDMetric.box); layers.forEach(root.addSublayer)
        if paused { root.speed = 0; root.timeOffset = 0 }
    }
    return root
}
private func accessibility(_ items: [HUDAccessibilityItem]) -> [[String: Any]] {
    items.map { item in
        var row: [String: Any] = ["id": item.id, "label": item.label, "rect": rect(item.rect), "enabled": item.enabled]
        switch item.role {
        case .button: row["role"] = "button"
        case .toggle(let on): row["role"] = "toggle"; row["on"] = on
        case .popUp(let value): row["role"] = "popUp"; row["value"] = value
        case .menuItem(let selected): row["role"] = "menuItem"; row["on"] = selected
        case .tab(let selected): row["role"] = "tab"; row["on"] = selected
        case .slider(let value, let minimum, let maximum, let step, let description):
            row["role"] = "slider"; row["number"] = value; row["minimum"] = minimum; row["maximum"] = maximum
            row["step"] = step; row["value"] = description
        case .text(let value): row["role"] = "text"; row["value"] = value
        }
        return row
    }
}
private func controlState(_ c: HUDControl, in root: CALayer) -> [String: Any] {
    ["id": c.id, "label": c.label, "hovered": c.isHovered, "pressed": c.isPressed, "enabled": c.isEnabled,
     "rect": rect(c.rect(in: root)), "accessibility": accessibility(c.accessibilityItems(in: root))]
}
private func centre(_ c: HUDControl, in root: CALayer) -> CGPoint { let r = c.rect(in: root); return CGPoint(x: r.midX, y: r.midY) }

private final class FakeCanvas: HUDDesignCanvas {
    var depth = 0, tab = 0, rows = 12, builds = 0
    var events: [String] = []
    var dropdown: HUDDropdown?
    var slider: HUDSlider?
    var keys: [String] = []
    override func buildPage(_ page: HUDDesignPage) {
        builds += 1
        let action = HUDButton(id: "fake.add", title: L10n.text("Add", "添加"), icon: .plus, kind: .secondary, size: .small, env: env)
        action.onActivate = { [unowned self] in self.events.append("add") }
        header.configure(crumbs: depth == 0 ? ["CLIPBOARD"] : ["CLIPBOARD", "DETAIL"], code: "MOD-03",
                         title: HUDModuleTitle("Clipboard", "剪贴板缓存"), counter: HUDFormat.pad2(rows), showsBack: depth > 0,
                         actions: [action])
        let tabs = HUDTabs(id: "fake.tabs", titles: [(L10n.text("All", "全部"), nil), (L10n.text("Text", "文本"), .text),
                                                    (L10n.text("Links", "链接"), .link)], selectedIndex: tab, width: HUDMetric.box.width, env: env)
        tabs.onSelect = { [unowned self] index, direction in self.tab = index; self.events.append("tab \(index) \(direction)"); self.navigate(dx: direction) }
        HUDMotion.perform { tabs.layer.frame.origin = CGPoint(x: 0, y: HUDMetric.headerHeight); page.layer.addSublayer(tabs.layer) }
        page.tabs = tabs; page.add(tabs.controls); page.stagger(tabs.layer)
        let menu = HUDDropdown(id: "fake.menu", label: L10n.text("Language", "语言"), value: "Item 3", env: env)
        HUDMotion.perform { menu.layer.frame.origin = CGPoint(x: 0, y: 130); page.layer.addSublayer(menu.layer) }
        menu.onActivate = { [unowned self, unowned menu] in
            let items = (0..<12).map { HUDMenu.Item(id: "i\($0)", title: "Item \($0)", selected: $0 == 3) }
            self.openMenu(id: "fake.menu.list", items: items, anchor: menu) { [unowned self] item in self.events.append("pick \(item.id)") }
        }
        dropdown = menu; page.add(menu); page.stagger(menu.layer)
        let slider = HUDSlider(id: "fake.slider", label: "Volume", value: 40, range: .init(minimum: 0, maximum: 100, step: 1),
                               format: .percent, width: 360, env: env)
        slider.onChange = { [unowned self] value, final in self.events.append("slider \(value) \(final)") }
        HUDMotion.perform { slider.layer.frame.origin = CGPoint(x: 200, y: 130); page.layer.addSublayer(slider.layer) }
        hostBubble(slider); self.slider = slider; page.add(slider); page.stagger(slider.layer)
        let scroll = page.scrollArea(id: "fake.rows", frame: CGRect(x: 0, y: 170, width: HUDMetric.box.width, height: 340))
        var y: CGFloat = 0
        for index in 0..<rows {
            let row = HUDRowPlate(id: "fake.row.\(index)", title: "Row \(index)", sublabel: index == 0 ? "Detail" : nil,
                                  variant: index % 4 == 3 ? .nav(chevron: "›") : .standard, width: scroll.contentWidth, env: env)
            if index % 4 == 0 {
                let toggle = HUDToggle(id: "fake.toggle.\(index)", label: "Row \(index)", isOn: index == 0, env: env)
                toggle.onChange = { [unowned self] on in self.events.append("toggle \(index) \(on)") }
                row.setCluster([toggle.layer]); page.add(toggle)
            }
            row.onActivate = { [unowned self] in self.events.append("row \(index)"); self.navigate(dx: 1) }
            HUDMotion.perform { row.layer.frame.origin = CGPoint(x: 0, y: y); scroll.content.addSublayer(row.layer) }
            page.add(row); page.stagger(row.layer)
            y += row.layer.bounds.height + HUDMetric.rowGap
        }
        scroll.setContentHeight(y - HUDMetric.rowGap)
        page.restoreOffset(of: scroll)
        if depth > 0 { wireBack(page) { [unowned self] in self.depth -= 1; self.events.append("back"); self.navigate(dx: -1) } }
    }
    override func handleKey(_ event: NSEvent) -> Bool {
        if event.charactersIgnoringModifiers == "k" { keys.append("handleKey k"); return true }
        return false
    }
    override func interceptKey(_ event: NSEvent) -> Bool {
        if event.charactersIgnoringModifiers == "i" { keys.append("interceptKey i"); return true }
        return false
    }
    override func handleExtraKey(_ event: NSEvent) -> Bool {
        if event.charactersIgnoringModifiers == "x" { keys.append("handleExtraKey x"); return true }
        return false
    }
    override func handleScroll(at point: CGPoint, delta: CGPoint, event: NSEvent?) -> Bool {
        guard point.y < 20 else { return false }
        events.append("handleScroll"); return true
    }
    override func didActivate() { events.append("didActivate") }
    override func didDeactivate() { events.append("didDeactivate") }
    override func willLeave() { events.append("willLeave") }
}

@main private enum HUDDesignReference {
    static func main() throws {
        precondition(ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil)
        precondition(CommandLine.arguments.count >= 3 && CommandLine.arguments[1] == "--ui-test")
        let output = URL(fileURLWithPath: CommandLine.arguments[2])
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.reduceMotion = false
        configuration.accentHex = "FAD41F"
        HUDRuntimeAppearance.configuration = configuration
        L10n.language = .english
        let recorder = try Recorder(output: output)
        func setLanguage(_ language: AppLanguage) { L10n.language = language }
        func setPalette(_ accent: String, light: Bool) { HUDColor.apply(accentHex: accent, light: light) }
        var payload: [String: Any] = ["schema": 1]

        // MARK: Tokens: palettes, theme, tones, legible, type styles, metrics, easings, durations, geometry, render scale
        let tones: [UInt32] = [0x8A8A8A, 0xF2F2F2, 0xE9E9E9, 0xDDDDDD, 0x111111, 0x888888, 0xECECEC, 0x161616, 0x2A2A2A, 0xE2E0E1,
                               0x1D1D1D, 0x4A4A4A, 0xEDEDED, 0x1B1B1B, 0x606060, 0x181818, 0x000000, 0xCFCFCF, 0x5A5A5A, 0x222222,
                               0x232323, 0x6A6A6A, 0x4C4C4C, 0x2C2C2C, 0x262414, 0xFFFFFF, 0x808080, 0x515151, 0x525252, 0x8D8E89,
                               0xFFFA00, 0x22A6F2, 0x3A3A3A, 0x333333]
        let typeStyles: [(String, () -> HUDTextStyle)] = [
            ("moduleTitle", { HUDType.moduleTitle }), ("titleSubtitle", { HUDType.titleSubtitle }), ("breadcrumb", { HUDType.breadcrumb }),
            ("ruleCounter", { HUDType.ruleCounter }), ("rowLabel", { HUDType.rowLabel }), ("rowSublabel", { HUDType.rowSublabel }),
            ("button", { HUDType.button }), ("pill", { HUDType.pill }), ("toggleLabel", { HUDType.toggleLabel }),
            ("dropdownValue", { HUDType.dropdownValue }), ("menuItem", { HUDType.menuItem }), ("segmented", { HUDType.segmented }),
            ("tab", { HUDType.tab }), ("tabKeycap", { HUDType.tabKeycap }), ("chip", { HUDType.chip }), ("newBadge", { HUDType.newBadge }),
            ("stamp", { HUDType.stamp }), ("numeral", { HUDType.numeral }), ("numeralUnit", { HUDType.numeralUnit }),
            ("label", { HUDType.label }), ("labelCJK", { HUDType.labelCJK }), ("statValue", { HUDType.statValue }),
            ("statUnit", { HUDType.statUnit }), ("readout", { HUDType.readout }), ("readoutBracket", { HUDType.readoutBracket }),
            ("sliderBubble", { HUDType.sliderBubble }), ("field", { HUDType.field }), ("emptyTitle", { HUDType.emptyTitle }),
            ("keycap", { HUDType.keycap }), ("toastTitle", { HUDType.toastTitle }), ("toastCaption", { HUDType.toastCaption }),
            ("confirmTitle", { HUDType.confirmTitle }), ("confirmDescription", { HUDType.confirmDescription }),
            ("confirmCountdown", { HUDType.confirmCountdown }), ("microTag", { HUDType.microTag })]
        var palettes: [[String: Any]] = []
        var themesByMode: [String: Any] = [:], stylesByMode: [String: Any] = [:]
        func fullStyle(_ s: HUDTextStyle) -> [String: Any] {
            ["size": s.size, "weight": s.weight.rawValue, "resolvedWeight": s.weight.resolved.rawValue, "color": rgba(s.color),
             "lineHeight": s.lineHeight, "tracking": s.tracking, "uppercase": s.uppercase, "tabular": s.tabular]
        }
        for accent in accents {
            for light in [false, true] {
                setPalette(accent, light: light)
                let p = HUDColor.self
                let theme: [String: Any] = ["foreground": rgba(p.foreground(1)), "panel": rgba(p.panel), "field": rgba(p.field),
                    "control": rgba(p.control), "toggleTrack": rgba(p.toggleTrack), "tabBar": rgba(p.tabBar), "iconButton": rgba(p.iconButton),
                    "secondaryButton": rgba(p.secondaryButton), "secondaryButtonHover": rgba(p.secondaryButtonHover),
                    "ringField": rgba(p.ringField), "ringTab": rgba(p.ringTab), "ringToggle": rgba(p.ringToggle), "ringIcon": rgba(p.ringIcon),
                    "ringMenu": rgba(p.ringMenu), "separator": rgba(p.separator), "titleText": rgba(p.titleText),
                    "crumbCurrent": rgba(p.crumbCurrent), "textOnDark": rgba(p.textOnDark), "tabText": rgba(p.tabText),
                    "menuText": rgba(p.menuText), "segmentedText": rgba(p.segmentedText), "muted": rgba(p.muted), "caption": rgba(p.caption),
                    "description": rgba(p.description), "steel": rgba(p.steel), "danger": rgba(p.danger)]
                let legibleInputs: [CGColor] = [p.yellow, p.green, p.cyan, p.orange, p.red, p.rgb(0x8A8A8A), p.rgb(0x7F3FBF), p.rgb(0x22A6F2, 0.5), p.white(1)]
                if accent == "FAD41F" {
                    themesByMode[light ? "light" : "dark"] = theme
                    stylesByMode[light ? "light" : "dark"] = Dictionary(uniqueKeysWithValues: typeStyles.map { ($0.0, fullStyle($0.1())) })
                }
                palettes.append(["accent": accent, "light": light, "generation": HUDColor.paletteGeneration, "isLight": HUDColor.isLight,
                    "yellow": rgba(p.yellow), "yellowHigh": rgba(p.yellowHigh), "yellowLow": rgba(p.yellowLow), "yellowDeep": rgba(p.yellowDeep),
                    "accentInk": rgba(p.accentInk), "ink": rgba(p.accentInk), "primaryButton": p.primaryButton.map(rgba), "theme": theme,
                    "tones": Dictionary(uniqueKeysWithValues: tones.map { (String(format: "%06X", $0), rgba(p.tone($0))) }),
                    "foreground": [0, 0.22, 0.35, 0.65, 1].map { ["alpha": $0, "color": rgba(p.foreground(CGFloat($0)))] },
                    "legible": legibleInputs.map { ["in": rgba($0), "out": rgba(p.legible($0))] },
                    "types": Dictionary(uniqueKeysWithValues: typeStyles.map { ($0.0, styleJSON($0.1())) })])
            }
        }
        setPalette("FAD41F", light: false)
        let normalisation = ["FAD41F", "#22a6f2", " 22A6F2\n", "22A6F", "XYZXYZ", "", "##E5383B", "8ce03c"].map {
            ["input": $0, "accent": String(format: "%06X", HUDAccentPalette(accentHex: $0).accent)]
        }
        var lightTones: [String: String] = [:]
        for value in stride(from: 0, through: 255, by: 5) {
            let grey = UInt32(value) << 16 | UInt32(value) << 8 | UInt32(value)
            lightTones[String(format: "%06X", grey)] = String(format: "%06X", HUDTheme.lightTone(grey))
        }
        for extra: UInt32 in [0x262414, 0xE2E0E1, 0x1A1A1A, 0x3D3D3D, 0xFF8A8C, 0x6C6D68, 0x123456] {
            lightTones[String(format: "%06X", extra)] = String(format: "%06X", HUDTheme.lightTone(extra))
        }
        let easings: [String: [Double]] = ["standard": timingJSON(HUDEasing.standard), "snap": timingJSON(HUDEasing.snap),
            "ease": timingJSON(HUDEasing.ease), "easeIn": timingJSON(HUDEasing.easeIn), "easeOut": timingJSON(HUDEasing.easeOut),
            "easeInOut": timingJSON(HUDEasing.easeInOut), "linear": timingJSON(HUDEasing.linear)]
        let d = HUDDuration.self
        let durations: [String: Double] = ["hover": d.hover, "press": d.press, "sheen": d.sheen, "entrance": d.entrance,
            "staggerStep": d.staggerStep, "staggerBase": d.staggerBase, "staggerCap": Double(d.staggerCap), "leave": d.leave,
            "titleWipe": d.titleWipe, "ruleWipe": d.ruleWipe, "ruleDelay": d.ruleDelay, "menuWipe": d.menuWipe, "tabOn": d.tabOn,
            "knob": d.knob, "toggleLabel": d.toggleLabel, "caret": d.caret, "accentBar": d.accentBar, "thumb": d.thumb,
            "bubbleFade": d.bubbleFade, "bubbleScale": d.bubbleScale, "bandIn": d.bandIn, "bandOut": d.bandOut,
            "confirmText": d.confirmText, "confirmTextDelay": d.confirmTextDelay, "pillDelay": d.pillDelay, "pillStep": d.pillStep,
            "confirmCallback": d.confirmCallback, "dim": d.dim, "dimFilter": d.dimFilter, "toastVisible": d.toastVisible,
            "toastOut": d.toastOut, "pop": d.pop, "taskFlip": d.taskFlip, "perspective": d.perspective, "scaleConfirmation": d.scaleConfirmation]
        let scaleProbe = (HUDLayer.plain(), HUDTextLayer(), CATextLayer())
        let renderScale = [0.5, 1, 1.25, 1.5, 2, 2.5, 3, 3.3, 4, 5, 8, Double.infinity].map { base -> [String: Any] in
            ["base": base.isFinite ? base as Any : "inf", "layer": HUDRenderScale.contentScale(for: scaleProbe.0, baseScale: CGFloat(base)),
             "designText": HUDRenderScale.contentScale(for: scaleProbe.1, baseScale: CGFloat(base)),
             "caText": HUDRenderScale.contentScale(for: scaleProbe.2, baseScale: CGFloat(base))]
        }
        payload["tokens"] = ["palettes": palettes, "themes": themesByMode, "styles": stylesByMode, "accentNormalisation": normalisation, "lightTone": lightTones,
            "easings": easings, "durations": durations,
            "metric": ["box": [HUDMetric.box.width, HUDMetric.box.height], "headerHeight": HUDMetric.headerHeight, "bodyHeight": HUDMetric.bodyHeight,
                       "rowHeight": HUDMetric.rowHeight, "compactRowHeight": HUDMetric.compactRowHeight, "rowGap": HUDMetric.rowGap,
                       "rowChamfer": HUDMetric.rowChamfer, "buttonChamfer": HUDMetric.buttonChamfer, "sliderLabelColumn": HUDMetric.sliderLabelColumn],
            "order": ["content": HUDLayerOrder.content, "dim": HUDLayerOrder.dim, "confirmBar": HUDLayerOrder.confirmBar,
                      "toast": HUDLayerOrder.toast, "bootLog": HUDLayerOrder.bootLog, "menu": HUDLayerOrder.menu, "bubble": HUDLayerOrder.bubble],
            "geometry": ["baseUnit": HUDDesignGeometry.baseUnit, "boxRect": rect(HUDDesignGeometry.boxRect(unit: HUDDesignGeometry.baseUnit)),
                         "boxRectUnit1": rect(HUDDesignGeometry.boxRect(unit: 1))],
            "renderScale": renderScale,
            "formats": [0, 7, 42, 99.5, -3, 1234.5].flatMap { v -> [[String: Any]] in
                let formats: [(String, HUDValueFormat)] = [("scale", .scale), ("percent", .percent), ("signedPercent", .signedPercent),
                    ("balance", .balance), ("seconds", .seconds), ("pixels", .pixels), ("page", .page), ("signed", .signed),
                    ("integer", .integer), ("clock", .clock), ("decimal", .decimal)]
                return formats.map { ["format": $0.0, "value": v, "string": $0.1.string(v)] }
            }]

        // MARK: Motion verbs on probe layers (animation records), stagger delays, shell positions
        var verbs: [[String: Any]] = []
        func verb(_ name: String, _ make: (CALayer) -> Void) throws {
            for reduced in [false, true] {
                reduce = reduced
                let layer = HUDLayer.plain("probe"); let root = host(layer)
                HUDMotion.perform { layer.frame = CGRect(x: 10, y: 20, width: 100, height: 40); layer.opacity = 0.8 }
                make(layer)
                verbs.append(["verb": name, "reduceMotion": reduced, "layer": try recorder.tree(layer, id: name)])
                _ = root
            }
            reduce = false
        }
        try verb("animate.opacity") { HUDMotion.animate($0, "opacity", to: Float(0.2), duration: 0.22, timing: HUDEasing.ease, env: env) }
        try verb("animate.translationX.delay") { HUDMotion.animate($0, "transform.translation.x", to: 3, duration: 0.22, delay: 0.1, env: env) }
        try verb("animate.backgroundColor") { HUDMotion.animate($0, "backgroundColor", to: HUDColor.yellowLow, duration: 0.22, timing: HUDEasing.ease, env: env) }
        try verb("animate.sameValue") { HUDMotion.animate($0, "opacity", to: Float(0.8), duration: 0.22, env: env) }
        try verb("enter") { HUDMotion.enter($0, from: CGPoint(x: 0, y: 8), duration: 0.3, delay: 0.14, env: env) }
        try verb("enter.xy") { HUDMotion.enter($0, from: CGPoint(x: -31.1, y: 31.1), env: env) }
        try verb("leave") { HUDMotion.leave($0, toward: CGPoint(x: 44, y: 0), env: env) }
        try verb("hold") { HUDMotion.hold($0, key: "hud.page.hold") }
        try verb("wipe.left.fade") { HUDMotion.wipe($0, from: .left, duration: 0.45, fadeFrom: 0, env: env) }
        try verb("wipe.top.fade") { HUDMotion.wipe($0, from: .top, duration: 0.18, fadeFrom: 0.4, env: env) }
        try verb("wipe.centerX.snap") { HUDMotion.wipe($0, from: .centerX, duration: 0.3, timing: HUDEasing.snap, env: env) }
        try verb("wipe.delay") { HUDMotion.wipe($0, from: .left, duration: 0.35, delay: 0.1, env: env) }
        try verb("grow") { HUDMotion.grow($0, duration: 0.42, delay: 0.05, env: env) }
        try verb("pop") { HUDMotion.pop($0, delay: 0.08, env: env) }
        let modules: [HUDModule] = HUDModule.allCases
        payload["motion"] = ["verbs": verbs,
            "staggerDelays": (-1...20).map { ["index": $0, "delay": HUDMotion.staggerDelay($0)] },
            "subpageOffsets": [(1.0, 0.0), (-1, 0), (0, 1), (0, -1), (1, 1), (0, 0)].map { ["dx": $0.0, "dy": $0.1,
                "offset": point(HUDShellPosition.subpageOffset(dx: CGFloat($0.0), dy: CGFloat($0.1)))] },
            "shellPositions": Dictionary(uniqueKeysWithValues: modules.map { ($0.rawValue, point(HUDShellPosition.position($0))) }),
            "entranceOffsets": ([nil] + modules.map(Optional.some)).flatMap { from in modules.map { to -> [String: Any] in
                ["from": from?.rawValue as Any? ?? NSNull(), "to": to.rawValue, "offset": point(HUDShellPosition.entranceOffset(from: from, to: to))] } }]

        // MARK: Components in their states
        var components: [[String: Any]] = []
        func emit(_ name: String, _ state: String, _ layer: CALayer, root: CALayer, controls: [HUDControl] = [], extra: [String: Any] = [:]) throws {
            var row: [String: Any] = ["component": name, "state": state, "light": HUDColor.isLight,
                "accent": String(format: "%06X", currentAccent), "language": languageName(L10n.resolvedLanguage),
                "reduceMotion": reduce, "frame": rect(layer.frame), "layer": try recorder.tree(layer, id: name),
                "controls": controls.map { controlState($0, in: root) }]
            for (k, v) in extra { row[k] = v }
            components.append(row)
        }
        func themed(_ accent: String = "FAD41F", light: Bool = false, _ body: () throws -> Void) rethrows {
            setPalette(accent, light: light); currentAccent = HUDAccentPalette(accentHex: accent).accent
            try body()
            setPalette("FAD41F", light: false); currentAccent = 0xFAD41F
        }
        /// rest -> hovered -> pressed -> released -> left, through HUDControlHighlightLayer.update like the host.
        var fullStates = true
        func interact(_ name: String, _ make: () -> HUDControl, extraState: ((HUDControl, CALayer) throws -> Void)? = nil) throws {
            let control = make(); let root = host(control.layer)
            try emit(name, "rest", control.layer, root: root, controls: [control])
            let p = centre(control, in: root)
            HUDControlHighlightLayer.update(in: root, point: p, pressed: false)
            try emit(name, "hovered", control.layer, root: root, controls: [control])
            guard fullStates else { return }
            HUDControlHighlightLayer.update(in: root, point: p, pressed: true)
            try emit(name, "pressed", control.layer, root: root, controls: [control])
            HUDControlHighlightLayer.update(in: root, point: nil, pressed: false)
            try emit(name, "left", control.layer, root: root, controls: [control])
            try extraState?(control, root)
            control.isEnabled = false
            try emit(name, "disabled", control.layer, root: root, controls: [control])
        }
        let themes: [(String, Bool)] = [("FAD41F", false), ("FAD41F", true), ("22A6F2", false), ("E5383B", false), ("8CE03C", false), ("22A6F2", true)]
        for (accent, light) in themes {
            try themed(accent, light: light) {
                let full = accent == "FAD41F" && !light
                fullStates = full
                try interact("row.standard") { HUDRowPlate(id: "row", title: "Launch at login", sublabel: "Starts with macOS", env: env) }
                if full {
                    try interact("row.compact") { HUDRowPlate(id: "row", title: "Compact row", variant: .compact, env: env) }
                    try interact("row.nav") { HUDRowPlate(id: "row", title: "Navigation row", variant: .nav(chevron: "›"), env: env) }
                    try interact("row.navExternal") { HUDRowPlate(id: "row", title: "Open folder", variant: .nav(chevron: "↗"), width: 400, env: env) }
                    try interact("row.custom56") { HUDRowPlate(id: "row", title: "Custom height", sublabel: "56 px", height: 56, env: env) }
                    let row = HUDRowPlate(id: "row", title: "Slider row", variant: .slider, env: env)
                    let toggle = HUDToggle(id: "row.toggle", label: "Toggle", isOn: true, env: env)
                    let button = HUDButton(id: "row.button", title: "Open", kind: .secondary, size: .small, env: env)
                    row.setCluster([button.layer, toggle.layer]); row.titleInset = 56
                    let root = host(row.layer)
                    try emit("row.cluster", "rest", row.layer, root: root, controls: [row, toggle, button],
                             extra: ["controlArea": rect(row.controlArea), "titleFrame": rect(row.titleFrame)])
                    HUDControlHighlightLayer.update(in: root, point: centre(toggle, in: root), pressed: false)
                    try emit("row.cluster", "toggleHovered", row.layer, root: root, controls: [row, toggle, button])
                    HUDControlHighlightLayer.update(in: root, point: centre(toggle, in: root), pressed: true)
                    try emit("row.cluster", "togglePressed", row.layer, root: root, controls: [row, toggle, button])
                }
                try interact("toggle.off") { HUDToggle(id: "toggle", label: "Ambient", isOn: false, env: env) } extraState: { control, root in
                    control.activate()
                    try emit("toggle.off", "activated", control.layer, root: root, controls: [control])
                    (control as! HUDToggle).setOn(false, animated: false)
                    try emit("toggle.off", "setOffUnanimated", control.layer, root: root, controls: [control])
                }
                try interact("toggle.on") { HUDToggle(id: "toggle", label: "Ambient", isOn: true, env: env) }
                let segmented = { HUDSegmentedControl(id: "seg", label: "Mode", options: ["Dark", "Light", "System"], selectedIndex: 1, width: 300, env: env) }
                do {
                    let control = segmented(); let root = host(control.layer)
                    try emit("segmented", "rest", control.layer, root: root, controls: [control] + control.segments)
                    HUDControlHighlightLayer.update(in: root, point: centre(control.segments[0], in: root), pressed: false)
                    if full { try emit("segmented", "hovered0", control.layer, root: root, controls: [control] + control.segments)
                    control.select(2, notify: true)
                    try emit("segmented", "selected2", control.layer, root: root, controls: [control] + control.segments) }
                    let yellow = HUDSegmentedControl(id: "seg", label: "Mode", options: ["25", "50", "Custom"], selectedIndex: 0, width: 240, yellow: true, env: env)
                    try emit("segmented.yellow", "rest", yellow.layer, root: host(yellow.layer), controls: [yellow] + yellow.segments)
                }
                for (kind, kindName) in [(HUDButton.Kind.primary, "primary"), (.secondary, "secondary"), (.light, "light")] {
                    for (size, sizeName) in [(HUDButton.Size.small, "small"), (.regular, "regular"), (.large, "large")] where full || size == .regular {
                        try interact("button.\(kindName).\(sizeName)") { HUDButton(id: "button", title: "Export", icon: .export, kind: kind, size: size, env: env) }
                    }
                    if full {
                        try interact("button.\(kindName).untitledIcon") { HUDButton(id: "button", title: "", icon: .plus, kind: kind, size: .small, env: env) }
                        let flex = HUDButton(id: "button", title: "Flex", icon: .check, kind: kind, env: env)
                        flex.place(at: CGPoint(x: 12, y: 30), width: 220)
                        try emit("button.\(kindName).flex", "rest", flex.layer, root: host(flex.layer), controls: [flex])
                    }
                }
                for (style, styleName) in [(HUDIconButton.Style.dark, "dark"), (.on, "on"), (.light, "light"), (.back, "back")] {
                    try interact("iconButton.\(styleName)") { HUDIconButton(id: "icon", label: "Pin", icon: .pin, style: style, env: env) }
                    if full {
                        try interact("iconButton.\(styleName).round") { HUDIconButton(id: "icon", label: "Play", icon: .play, style: style, round: true, env: env) }
                        try interact("iconButton.\(styleName).big") { HUDIconButton(id: "icon", label: "Play", icon: .play, style: style, big: true, env: env) }
                        try interact("iconButton.\(styleName).side24") { HUDIconButton(id: "icon", label: "X", icon: .x, style: style, side: 24, iconSize: 10, env: env) }
                    }
                }
                if full {
                    let toggled = HUDIconButton(id: "icon", label: "Mute", icon: .vol, env: env)
                    toggled.isOn = true; toggled.setIcon(.mute)
                    try emit("iconButton.dark.isOn", "rest", toggled.layer, root: host(toggled.layer), controls: [toggled])
                }
                for (kind, kindName) in [(HUDPill.Kind.cancel, "cancel"), (.confirm, "confirm"), (.danger, "danger")] {
                    try interact("pill.\(kindName)") { HUDPill(id: "pill", title: kind == .cancel ? "Cancel" : "Delete everything now", icon: kind == .cancel ? .x : .trash, kind: kind, env: env) }
                }
                try interact("swatch.colour") { HUDSwatch(id: "swatch", label: "Cyan", color: HUDColor.cyan, env: env) } extraState: { control, root in
                    (control as! HUDSwatch).isSelected = true
                    try emit("swatch.colour", "selected", control.layer, root: root, controls: [control])
                }
                if full {
                    try interact("swatch.custom") { HUDSwatch(id: "swatch", label: "Custom", color: nil, size: 28, env: env) }
                    try interact("grip") { HUDDragGrip(id: "grip", size: CGSize(width: 30, height: 40), dotsAt: CGPoint(x: 12, y: 15), env: env) }
                }
                for (dark, variant) in [(false, "plate"), (true, "dark")] where full || dark || light {
                    try interact("slider.\(variant)") {
                        HUDSlider(id: "slider", label: "Opacity", value: 62, range: .init(minimum: 0, maximum: 100, step: 1), format: .percent,
                                  width: 300, dark: dark, env: env)
                    } extraState: { control, root in
                        let slider = control as! HUDSlider
                        let top = HUDLayer.plain("design.top")
                        HUDMotion.perform { top.frame = root.bounds; top.zPosition = HUDLayerOrder.menu; root.addSublayer(top) }
                        slider.bubbleHost = top
                        var changes: [[Any]] = []
                        slider.onChange = { changes.append([$0, $1]) }
                        let r = slider.rect(in: root)
                        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: r.minX + 30, y: r.midY), pressed: true)
                        _ = slider.pointerDown(at: slider.layer.convert(CGPoint(x: r.minX + 30, y: r.midY), from: root))
                        slider.pointerDragged(to: slider.layer.convert(CGPoint(x: r.minX + 150, y: r.midY), from: root))
                        try emit("slider.\(variant)", "dragging", slider.layer, root: root, controls: [slider],
                                 extra: ["bubble": try recorder.tree(top, id: "top"), "changes": changes])
                        slider.pointerUp(at: slider.layer.convert(CGPoint(x: r.minX + 150, y: r.midY), from: root))
                        HUDControlHighlightLayer.update(in: root, point: nil, pressed: false)
                        slider.nudge(1)
                        try emit("slider.\(variant)", "released", slider.layer, root: root, controls: [slider],
                                 extra: ["bubble": try recorder.tree(top, id: "top"), "changes": changes, "value": slider.value])
                    }
                }
                if full {
                    let zero = HUDSlider(id: "slider", label: "Balance", value: -20, range: .init(minimum: -100, maximum: 100, step: 5, zero: 0),
                                         format: .balance, width: 260, showsReadout: true, showsTicks: false, env: env)
                    try emit("slider.zero", "rest", zero.layer, root: host(zero.layer), controls: [zero])
                    let bare = HUDSlider(id: "slider", label: "Seek", value: 0.25, range: .init(minimum: 0, maximum: 1, step: 0), format: .decimal,
                                         width: 200, showsReadout: false, env: env)
                    try emit("slider.noReadout", "rest", bare.layer, root: host(bare.layer), controls: [bare])
                }
                try interact("dropdown.regular") { HUDDropdown(id: "dropdown", label: "Language", value: "简体中文", valueLanguage: "zh-Hans", env: env) } extraState: { control, root in
                    (control as! HUDDropdown).isOpen = true
                    try emit("dropdown.regular", "open", control.layer, root: root, controls: [control])
                }
                if full {
                    try interact("dropdown.small") { HUDDropdown(id: "dropdown", label: "Sort", value: "Newest first", size: .small, env: env) }
                    try interact("dropdown.width") { HUDDropdown(id: "dropdown", label: "Screen", value: "A very long display name that truncates", width: 160, env: env) }
                }
                // Menus (short, long, icon grid) and the pop-over, placed as a canvas places them.
                let menus: [(String, [HUDMenu.Item], CGFloat)] = [
                    ("menu.short", [HUDMenu.Item(id: "a", title: "English"), HUDMenu.Item(id: "b", title: "简体中文", language: "zh-Hans", selected: true),
                                    HUDMenu.Item(id: "c", title: "繁體中文", language: "zh-Hant"), HUDMenu.Item(id: "d", title: "日本語", language: "ja"),
                                    HUDMenu.Item(id: "e", title: "한국어", language: "ko", separatorBefore: true),
                                    HUDMenu.Item(id: "f", title: "Delete", danger: true), HUDMenu.Item(id: "g", title: "Disabled", enabled: false)], 178),
                    ("menu.long", (0..<12).map { HUDMenu.Item(id: "i\($0)", title: "Display \($0)", selected: $0 == 9) }, 178),
                    ("menu.grid", (0..<9).map { HUDMenu.Item(id: "t\($0)", title: "Icon \($0)", selected: $0 == 4,
                                                            icon: HUDMenu.Icon(image: nil, size: CGSize(width: 40, height: 40))) }
                                  + [HUDMenu.Item(id: "custom", title: "Custom…")], 120)]
                for (name, items, minimum) in menus where full || name == "menu.short" {
                    let menu = HUDMenu(id: name, items: items, minimumWidth: minimum, env: env)
                    let root = host(); menu.place(anchor: CGRect(x: 380, y: 300, width: 178, height: 30), in: HUDMetric.box)
                    HUDMotion.perform { root.addSublayer(menu.layer) }
                    var picks: [String] = []
                    menu.onPick = { picks.append($0.id) }; menu.onClose = { picks.append("close") }
                    menu.playEntrance()
                    try emit(name, "open", menu.layer, root: root, controls: menu.controls,
                             extra: ["isLong": menu.isLong, "scroll": menu.scrollOffset, "keyboardIndex": menu.keyboardIndex as Any? ?? NSNull()])
                    if full {
                        HUDControlHighlightLayer.update(in: root, point: centre(menu.controls[min(2, menu.controls.count - 1)], in: root), pressed: false)
                        try emit(name, "hovered2", menu.layer, root: root, controls: menu.controls)
                        var keys: [[String: Any]] = []
                        for (code, label) in [(UInt16(125), "down"), (125, "down"), (126, "up"), (124, "right"), (123, "left"), (12, "q"), (36, "return"), (53, "esc")] {
                            let consumed = menu.handleKey(keyCode: code)
                            keys.append(["key": label, "consumed": consumed, "keyboardIndex": menu.keyboardIndex as Any? ?? NSNull(),
                                         "scroll": menu.scrollOffset, "picks": picks])
                        }
                        menu.scroll(by: 1000)
                        try emit(name, "scrolledToEnd", menu.layer, root: root, controls: menu.controls,
                                 extra: ["keys": keys, "scroll": menu.scrollOffset])
                        menu.setScroll(37)
                        try emit(name, "setScroll37", menu.layer, root: root, controls: menu.controls, extra: ["scroll": menu.scrollOffset])
                    }
                }
                for (anchor, name) in [(CGRect(x: 400, y: 300, width: 30, height: 30), "popover.above"), (CGRect(x: 40, y: 90, width: 30, height: 30), "popover.below")]
                where full || name == "popover.above" {
                    let popover = HUDPopover(title: "Pen", subtitle: "画笔", contentHeight: 120, env: env)
                    popover.place(anchor: anchor, in: HUDMetric.box)
                    let root = host(popover.layer); popover.playEntrance()
                    try emit(name, "open", popover.layer, root: root, controls: popover.close.map { [$0] } ?? [])
                }
                if full {
                    let headerless = HUDPopover(title: "", contentHeight: 80, closable: false, env: env)
                    headerless.place(anchor: CGRect(x: 300, y: 200, width: 30, height: 30), in: HUDMetric.box)
                    try emit("popover.headerless", "open", headerless.layer, root: host(headerless.layer))
                }
                // Tabs
                for (compact, keycaps, name) in [(false, true, "tabs"), (true, true, "tabs.compact"), (false, false, "tabs.noKeycaps")] where full || name == "tabs" {
                    let tabs = HUDTabs(id: "tabs", titles: [("All", nil), ("Text", .text), ("Links", .link), ("Images", .image)], selectedIndex: 0,
                                       width: HUDMetric.box.width, keycaps: keycaps, compact: compact, env: env)
                    var selections: [[Double]] = []
                    tabs.onSelect = { selections.append([Double($0), Double($1)]) }
                    let root = host(tabs.layer)
                    try emit(name, "rest", tabs.layer, root: root, controls: tabs.controls)
                    HUDControlHighlightLayer.update(in: root, point: centre(tabs.controls[2], in: root), pressed: false)
                    if full { try emit(name, "hovered2", tabs.layer, root: root, controls: tabs.controls) }
                    HUDControlHighlightLayer.update(in: root, point: nil, pressed: false)
                    tabs.controls[2].activate()
                    try emit(name, "selected2", tabs.layer, root: root, controls: tabs.controls, extra: ["selections": selections])
                    tabs.cycle(1); tabs.cycle(1); tabs.cycle(-1)
                    tabs.tabs[1].hasNews = true
                    if full { try emit(name, "cycled", tabs.layer, root: root, controls: tabs.controls,
                             extra: ["selections": selections, "selectedIndex": tabs.selectedIndex]) }
                }
                // Chips, badges, keycaps, field frames
                let chipVariants: [(String, HUDChip.Variant)] = [("yellow", .yellow), ("dark", .dark), ("light", .light), ("outline", .outline),
                                                                  ("cyan", .cyan), ("custom", .custom(fill: HUDColor.red, text: HUDColor.white(1)))]
                for (variantName, variant) in chipVariants where full || variantName == "outline" || variantName == "yellow" {
                    let chip = HUDChip("Pinned", variant: variant, icon: variantName == "dark" ? .pin : nil, env: env)
                    try emit("chip.\(variantName)", "rest", chip.layer, root: host(chip.layer))
                }
                try emit("badge.new", "rest", HUDBadge.new(env: env), root: host())
                try emit("badge.stamp", "rest", HUDBadge.stamp("Classified", env: env), root: host())
                if full { try emit("badge.diamond", "rest", HUDBadge.diamond(), root: host()) }
                do {
                    let keycaps = HUDKeycapGroup(id: "keys", label: "Summon", keys: ["⌥", "Space"], interactive: true, env: env)
                    let root = host(keycaps.layer)
                    if full { try emit("keycaps", "rest", keycaps.layer, root: root, controls: [keycaps]) }
                    keycaps.setRecording(true, prompt: L10n.text("Press a shortcut", "按下快捷键"))
                    try emit("keycaps", "recording", keycaps.layer, root: root, controls: [keycaps])
                    if full {
                    reduce = true; keycaps.setRecording(false, prompt: ""); keycaps.setRecording(true, prompt: "Reduced")
                    try emit("keycaps", "recordingReduced", keycaps.layer, root: root, controls: [keycaps]); reduce = false
                        keycaps.setRecording(false, prompt: ""); keycaps.setKeys(["⌃", "⇧", "K"])
                        try emit("keycaps", "setKeys", keycaps.layer, root: root, controls: [keycaps])
                        let display = HUDKeycapGroup(id: "keys", label: "Shortcut", keys: ["Esc"], interactive: false, env: env)
                        try emit("keycaps.display", "rest", display.layer, root: host(display.layer), controls: [display])
                    }
                }
                for (lightField, plain, name) in [(false, false, "field"), (true, false, "field.light"), (false, true, "field.plain")] where full || name == "field" {
                    let field = HUDFieldFrame(size: CGSize(width: 240, height: 34), light: lightField, plain: plain)
                    let root = host(field.layer)
                    for state in [HUDFieldFrame.State.normal, .focused, .invalid] {
                        field.setState(state)
                        try emit(name, "\(state)", field.layer, root: root)
                    }
                }
                // Text fields (idle faces)
                do {
                    let field = HUDDesignTextField(id: "name", label: "Name", text: "Endministrator", placeholder: "Name", size: CGSize(width: 260, height: 34), env: env)
                    try emit("textField", "idle", field.layer, root: host(field.layer), controls: [field], extra: ["idleText": field.idleTextForTesting])
                    if full {
                        let empty = HUDDesignTextField(id: "name", label: "Name", text: "", placeholder: "Search", size: CGSize(width: 260, height: 34), env: env)
                        try emit("textField.placeholder", "idle", empty.layer, root: host(empty.layer), controls: [empty], extra: ["idleText": empty.idleTextForTesting])
                        var secure = HUDDesignTextField.Options(); secure.secure = true
                        let password = HUDDesignTextField(id: "pw", label: "Password", text: "hunter2", size: CGSize(width: 260, height: 34), options: secure, env: env)
                        try emit("textField.secure", "idle", password.layer, root: host(password.layer), controls: [password], extra: ["idleText": password.idleTextForTesting])
                        var multi = HUDDesignTextField.Options(); multi.multiline = true; multi.light = true
                        let note = HUDDesignTextField(id: "note", label: "Note", text: "First line\nSecond line that is long enough to wrap in the field",
                                                      size: CGSize(width: 220, height: 90), options: multi, env: env)
                        try emit("textField.multilineLight", "idle", note.layer, root: host(note.layer), controls: [note], extra: ["idleText": note.idleTextForTesting])
                        note.setInvalid(true, shake: true)
                        try emit("textField.multilineLight", "invalidShake", note.layer, root: host(note.layer), controls: [note])
                    }
                }
                // Numerals, stat plates, cells
                let numeral = HUDBigNumeral(env: env); numeral.set("72", unit: "%")
                try emit("bigNumeral", "rest", numeral.layer, root: host(numeral.layer))
                let stat = HUDStatPlate(width: 180, env: env); stat.set(label: "Cycle count", value: "412", unit: "cycles", highlighted: true)
                try emit("statPlate", "highlighted", stat.layer, root: host(stat.layer))
                if full {
                    stat.set(label: "Temperature", value: "31.5", unit: "°C", tone: HUDColor.orange)
                    try emit("statPlate", "toned", stat.layer, root: host(stat.layer))
                    for capacity in [true, false] {
                        let cells = HUDCells(count: 10, width: 300, capacity: capacity, env: env)
                        cells.set(filled: 6, animated: false)
                        try emit("cells.\(capacity ? "capacity" : "level")", "six", cells.layer, root: host(cells.layer))
                        cells.set(filled: 7, pulsing: true, animated: true)
                        try emit("cells.\(capacity ? "capacity" : "level")", "pulsing", cells.layer, root: host(cells.layer))
                    }
                }
                // Cards
                try interact("taskCard") { HUDTaskCard(id: "task", title: "Calibrate the relay", done: false, attention: true, width: 280, env: env) } extraState: { control, root in
                    (control as! HUDTaskCard).toggle()
                    try emit("taskCard", "toggledDone", control.layer, root: root, controls: [control])
                }
                if full {
                    try interact("taskCard.redacted") { HUDTaskCard(id: "task", title: "Secret", done: true, redacted: true, width: 280, env: env) }
                    for kind in [HUDListCard.Kind.text, .link, .image, .file] {
                        fullStates = kind == .text
                        try interact("listCard.\(kind.rawValue)") {
                            let pin = HUDIconButton(id: "pin", label: "Pin", icon: .pin, side: 24, iconSize: 12, env: env)
                            return HUDListCard(id: "card", kind: kind, title: "A clipboard entry with a title long enough to wrap onto two lines",
                                               meta: "Safari · 12 KB", age: "2m", pinned: kind == .link, width: 580, actions: [pin], env: env)
                        } extraState: { control, root in
                            (control as! HUDListCard).flashCopied()
                            try emit("listCard.\(kind.rawValue)", "flashCopied", control.layer, root: root, controls: [control])
                        }
                    }
                    fullStates = false
                    try interact("listCard.redacted") { HUDListCard(id: "card", kind: .text, title: "Secret", meta: "", age: "now", pinned: false, width: 580, redacted: true, env: env) }
                    for (ext, style) in [(HUDArchiveTile.Extension.pdf, HUDArchiveTile.Style.standard), (.txt, .classified), (.img, .standard)] {
                        fullStates = ext == .pdf
                        try interact("archiveTile.\(ext.rawValue).\(style)") {
                            HUDArchiveTile(id: "tile", number: 7, title: "Operation manual", ext: ext, size: "2.4 MB", date: "2026.10.22", width: 136, style: style, env: env)
                        } extraState: { control, root in
                            (control as! HUDArchiveTile).setSelected(true)
                            try emit("archiveTile.\(ext.rawValue).\(style)", "selected", control.layer, root: root, controls: [control])
                        }
                    }
                }
                fullStates = full
                // Scroll area: bar hidden at rest, shown on scroll, offset, contentWidth
                if full {
                    let area = HUDScrollArea(frame: CGRect(x: 0, y: 125, width: 590, height: 300))
                    let root = host(area.layer)
                    try emit("scrollArea", "short", area.layer, root: root, extra: ["contentWidth": area.contentWidth, "maximumOffset": area.maximumOffset])
                    area.setContentHeight(900)
                    try emit("scrollArea", "long", area.layer, root: root, extra: ["contentWidth": area.contentWidth, "maximumOffset": area.maximumOffset])
                    let moved = area.scroll(by: 120)
                    try emit("scrollArea", "scrolled120", area.layer, root: root, extra: ["moved": moved, "offset": area.offset, "shown": area.scrollBar.isShown])
                    area.setOffset(10_000, animated: true, env: env)
                    try emit("scrollArea", "glideToEnd", area.layer, root: root, extra: ["offset": area.offset])
                    area.reveal(CGRect(x: 0, y: 100, width: 10, height: 40))
                    try emit("scrollArea", "reveal100", area.layer, root: root, extra: ["offset": area.offset, "atEnd": area.scroll(by: 50)])
                    area.scrollBar.reset()
                }
                // Redaction
                if full {
                    let line = HUDRedaction.bar(replacing: CGRect(x: 20, y: 10, width: 300, height: 18.6), style: HUDType.rowLabel, width: HUDRedaction.width(for: "Endministrator"), env: env)
                    try emit("redaction.line", "rest", line, root: host(line), extra: ["widths": ["", "a", "Endministrator", String(repeating: "x", count: 60), "管理员"].map { ["text": $0, "width": HUDRedaction.width(for: $0)] }])
                    let short = HUDRedaction.bar(size: CGSize(width: 50, height: 14), env: env)
                    try emit("redaction.short", "rest", short, root: host(short))
                    let block = HUDRedaction.block("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Supercalifragilisticexpialidociousnessandmore words",
                                                   size: CGSize(width: 400, height: 120), env: env)
                    let root = host(block)
                    HUDRedaction.playEntrance(in: [block], env: env)
                    try emit("redaction.block", "entrance", block, root: root,
                             extra: ["wrapped": HUDRedaction.wrapped("Lorem ipsum dolor sit amet, consectetur adipiscing elit, Supercalifragilisticexpialidociousnessandmorewordsandmorewords", limit: 46)])
                }
            }
        }
        // Icons: every glyph at 24 (and two sizes), in the text colour.
        fullStates = true; recorder.fullIcons = true
        for icon in HUDIcon.allCases {
            let layer = HUDIconLayer(icon, size: 24, color: HUDColor.textOnDark)
            try emit("icon.\(icon.rawValue)", "24", layer, root: host(layer))
        }
        for size: CGFloat in [10, 15] { let layer = HUDIconLayer(.warn, size: size, color: HUDColor.yellow); try emit("icon.warn", "\(Int(size))", layer, root: host(layer)) }
        recorder.fullIcons = false
        // Language-dependent components: header, empty state.
        for language in languages {
            setLanguage(language)
            for (light, accent) in [(false, "FAD41F"), (true, "FAD41F"), (false, "22A6F2")] where language == .english || (!light && accent == "FAD41F") {
                try themed(accent, light: light) {
                    let header = HUDModuleHeader(env: env)
                    let action = HUDButton(id: "header.clear", title: L10n.text("Clear", "清空"), icon: .trash, kind: .secondary, size: .small, env: env)
                    let pin = HUDIconButton(id: "header.pin", label: "Pin", icon: .pin, env: env)
                    header.configure(crumbs: ["CLIPBOARD", "Detail"], code: "MOD-03", title: HUDModuleTitle("Clipboard", "剪贴板缓存"),
                                     counter: "07", showsBack: true, actions: [action, pin])
                    let root = host(header.layer)
                    try emit("header", "configured", header.layer, root: root, controls: header.controls,
                             extra: ["titles": [header.titleTextForTesting.0, header.titleTextForTesting.1], "counter": header.counterTextForTesting,
                                     "crumbs": header.crumbTextsForTesting])
                    if language == .english && !light && accent == "FAD41F" {
                        header.setCounter("1,284")
                        header.playEntrance()
                        try emit("header", "counterAndEntrance", header.layer, root: root, controls: header.controls)
                        header.configure(crumbs: [], code: "SYS-01", title: HUDModuleTitle("System", "系统"), counter: "", showsBack: false)
                        try emit("header", "reconfiguredNoBack", header.layer, root: root, controls: header.controls)
                        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: 15, y: 35), pressed: false)
                    }
                    let empty = HUDEmptyState.bilingual("NO RECORDS", "剪贴板为空", env: env)
                    try emit("emptyState.bilingual", "rest", empty, root: host(empty))
                    if language == .english && !light && accent == "FAD41F" {
                        let made = HUDEmptyState.make(title: "NOTHING HERE", subtitle: "暂无内容", subtitleLanguage: "zh-Hans",
                                                      size: CGSize(width: 400, height: 200), reserving: 40, env: env)
                        try emit("emptyState.make", "reserving40", made, root: host(made),
                                 extra: ["textHeight": [HUDEmptyState.textHeight(subtitle: true), HUDEmptyState.textHeight(subtitle: false)],
                                         "textBottom": HUDEmptyState.textBottom(size: CGSize(width: 400, height: 200), subtitle: true, reserving: 40)])
                        let crosshair = HUDEmptyState.crosshair()
                        try emit("emptyState.crosshair", "rest", crosshair, root: host(crosshair))
                    }
                }
            }
        }
        setLanguage(.english)
        payload["components"] = components

        // MARK: The shared overlay: confirm bar, toast, dim, decisions
        var overlayRows: [String: Any] = [:]
        var scheduled: [(TimeInterval, DispatchWorkItem)] = []
        /// Traces run on live time (their completion blocks must fire); geometry rows on the frozen host.
        func makeOverlay(paused: Bool = true) -> HUDCenterOverlay {
            scheduled = []
            let overlay = HUDCenterOverlay(env: env, schedule: { scheduled.append(($0, $1)) })
            _ = host(overlay.layer, paused: paused)
            return overlay
        }
        func runScheduled() { let due = scheduled; scheduled = []; due.forEach { if !$0.1.isCancelled { $0.1.perform() } } }
        func completions() { CATransaction.flush(); RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.02)) }
        let dimLayer: (HUDCenterOverlay) -> CALayer = { $0.layer.sublayers!.first { $0.name == "center.dim" }! }
        func barSummary(_ overlay: HUDCenterOverlay) -> [String: Any] {
            let bar = overlay.confirmBar
            let column = bar.layer.sublayers!.first { $0.name == "confirm.text" }!
            let title = column.sublayers!.compactMap { $0 as? HUDTextLayer }.first!
            let message = column.sublayers!.compactMap { $0 as? HUDTextLayer }.last!
            return ["columnWidth": column.frame.width, "titleSize": title.textStyle.size, "titleLines": title.maximumLines,
                    "titleLineFrame": rect(title.lineFrame), "title": bar.titleTextForTesting, "message": bar.messageTextForTesting,
                    "messageLineFrame": rect(message.lineFrame), "messageLines": message.maximumLines,
                    "titlePreferredWidth17": recorder.width(bar.titleTextForTesting, HUDType.confirmTitle),
                    "pills": bar.pills.map { ["id": $0.id, "label": $0.label, "frame": rect($0.layer.frame), "kind": "\($0.kind)"] }]
        }
        // Confirm bar geometry per request and language.
        var bars: [[String: Any]] = []
        func requests() -> [(String, HUDConfirmRequest)] {
            [("standard", HUDConfirmRequest(id: "restore", title: L10n.text("Restore default settings?", "恢复默认设置？"),
                                            message: L10n.text("Notes, files and shortcuts are kept.", "保留便笺、文件与应用快捷方式。"),
                                            confirmTitle: L10n.text("Restore", "恢复"), icon: .reset)),
             ("danger", HUDConfirmRequest(id: "clipboard.clear", title: L10n.text("Clear clipboard history?", "清空剪贴板记录？"),
                                          message: L10n.text("\(12) entries", "\(12) 条记录"), confirmTitle: L10n.text("Clear", "清空"), danger: true, icon: .trash)),
             ("countdown", HUDConfirmRequest(id: "layout", title: L10n.text("Keep this UI scale?", "保留此缩放？"), countdown: 12,
                                             confirmTitle: L10n.text("Keep", "保留"), cancelTitle: L10n.text("Revert", "恢复"), icon: .check)),
             ("alertTwin", HUDConfirmRequest(id: "facilityOff", title: L10n.text("Are you sure you want to disable the Shipwide Defense System?", "是否关闭全舰防御系统？"),
                                             confirmTitle: L10n.text("Disable Shipwide Defense System", "关闭全舰防御系统"),
                                             cancelTitle: L10n.text("Disable Shipwide Defense System", "关闭全舰防御系统"), icon: .power, style: .alert, cancelPillConfirms: true)),
             ("noMessage3Lines", HUDConfirmRequest(id: "long", title: "Remove the selected forty-two documents from this category and move them to Uncategorized?",
                                                   confirmTitle: "Remove", danger: true)),
             ("noMessage14px", HUDConfirmRequest(id: "longer", title: String(repeating: "This question is far too long for three lines of the confirm bar at seventeen pixels. ", count: 2),
                                                 confirmTitle: "A considerably wider confirm pill", cancelTitle: "Another wide cancel pill")),
             ("messageLongTitle", HUDConfirmRequest(id: "ellipsis", title: "A title that would need more than one line because a message is present below it",
                                                    message: "Message present: the title stays on one line.", confirmTitle: "OK"))]
        }
        for language in languages {
            setLanguage(language)
            for (light, accent) in [(false, "FAD41F"), (true, "FAD41F"), (false, "E5383B")] where language == .english || (!light && accent == "FAD41F") {
                try themed(accent, light: light) {
                    for (name, request) in requests() where (language == .english && !light && accent == "FAD41F")
                        || ["standard", "countdown", "alertTwin"].contains(name) || (language == .english && name == "danger") {
                        let overlay = makeOverlay()
                        overlay.confirm(request, onConfirm: {})
                        var row: [String: Any] = ["request": name, "language": languageName(language), "light": light, "accent": accent,
                                                  "summary": barSummary(overlay), "bar": try recorder.tree(overlay.confirmBar.layer, id: "confirm"),
                                                  "router": overlay.router.controls.map(\.id)]
                        if name == "countdown" {
                            overlay.setCountdown(9)
                            row["afterCountdown9"] = ["message": overlay.confirmBar.messageTextForTesting,
                                                      "countdown": overlay.confirmState.request?.countdown as Any? ?? NSNull()]
                        }
                        if name == "standard" && language == .english && !light {
                            row["overlay"] = try recorder.tree(overlay.layer, id: "overlay")
                            // The next request after an alert restores the standard band.
                            overlay.dismissConfirm()
                            overlay.confirm(requests()[3].1, onConfirm: {}); overlay.dismissConfirm()
                            overlay.confirm(request, onConfirm: {})
                            let band = overlay.confirmBar.layer.sublayers!.first { $0.name == "confirm.band" } as! CAGradientLayer
                            row["afterAlertBand"] = (try recorder.tree(band, id: "band")["gradient"] as? [String: Any])?["colors"] ?? NSNull()
                        }
                        bars.append(row)
                    }
                    // Toasts through the canvas helper's rule (English: empty caption) and with a detail.
                    let overlay = makeOverlay()
                    let isEnglish = L10n.resolvedLanguage == .english
                    for (name, detail) in [("pair", nil as String?), ("detail", "⌘⇧C")] where light == false || name == "pair" {
                        let english: L10n.Text = "History cleared", chinese: L10n.Text = "剪贴板记录已清空"
                        let request = HUDToastRequest(icon: .trash, title: L10n.text(english, chinese), caption: detail ?? (isEnglish ? "" : english.value))
                        overlay.showToast(request)
                        overlayRows["toast.\(languageName(language)).\(light ? "light" : "dark").\(accent).\(name)"] = [
                            "texts": [overlay.toast.textsForTesting.0, overlay.toast.textsForTesting.1],
                            "toast": try recorder.tree(overlay.toast.layer, id: "toast"), "dim": try recorder.tree(dimLayer(overlay), id: "dim")]
                        overlay.clearToast()
                    }
                }
            }
        }
        setLanguage(.english)
        overlayRows["bars"] = bars

        // Decision traces (LIB-59..67). Each step records the overlay state after it.
        func trace(_ name: String, reduced: Bool = false, _ script: (HUDCenterOverlay, (String) -> Void, Log) throws -> Void) throws {
            reduce = reduced
            let overlay = makeOverlay(paused: false)
            let events = Log()
            var steps: [[String: Any]] = []
            overlay.onChange = { events.append("onChange") }
            let step: (String) -> Void = { label in
                let state = overlay.confirmState
                steps.append(["step": label, "isConfirming": overlay.isConfirming, "capturesInput": overlay.capturesInput,
                    "dimmed": overlay.isDimmed, "request": state.request?.id as Any? ?? NSNull(),
                    "decision": state.decision.map { "\($0)" } as Any? ?? NSNull(),
                    "toast": overlay.toastState.current?.title as Any? ?? NSNull(), "toastGeneration": overlay.toastState.generation,
                    "barHidden": overlay.confirmBar.layer.isHidden, "toastHidden": overlay.toast.layer.isHidden,
                    "dimOpacity": dimLayer(overlay).opacity, "dimAnimations": (dimLayer(overlay).animationKeys() ?? []).count,
                    "barAnimations": (overlay.confirmBar.layer.animationKeys() ?? []).sorted(),
                    "router": overlay.router.controls.map(\.id), "events": events.items,
                    "scheduled": scheduled.filter { !$0.1.isCancelled }.map { $0.0 }])
            }
            try script(overlay, step, events)
            overlayRows["trace." + name] = steps
            overlay.reset(); reduce = false
        }
        let plain = HUDConfirmRequest(id: "q", title: "Question?", message: "Detail", confirmTitle: "Do it")
        let twin = HUDConfirmRequest(id: "twin", title: "Disable?", confirmTitle: "Disable", cancelTitle: "Disable", icon: .power,
                                     style: .alert, cancelPillConfirms: true)
        try trace("returnConfirms") { o, step, events in
            o.confirm(plain, onConfirm: { events.append("confirm") }, onCancel: { events.append("cancel") }); step("confirm")
            _ = o.handleKey(36); step("return")
            events.append("key a -> \(o.handleKey(0))"); step("swallowedDuringCollapse")
            events.append("key esc -> \(o.handleKey(53))"); step("escAfterDecision")
            completions(); step("collapseDone")
            events.append("key a -> \(o.handleKey(0))"); step("keysPassAfterCollapse")
            runScheduled(); step("after120ms")
        }
        try trace("keypadEnterConfirms") { o, step, events in
            o.confirm(plain, onConfirm: { events.append("confirm") }); step("confirm")
            _ = o.handleKey(76); runScheduled(); step("keypadEnter")
        }
        try trace("escCancels") { o, step, events in
            o.confirm(plain, onConfirm: { events.append("confirm") }, onCancel: { events.append("cancel") }); step("confirm")
            events.append("key space -> \(o.handleKey(49))"); step("otherKeySwallowed")
            _ = o.handleKey(53); step("esc")
            events.append("decide again -> \(o.decide(.confirm))"); step("secondDecisionIgnored")
            completions(); step("collapseDone")
        }
        try trace("reduceMotionConfirmsAtOnce", reduced: true) { o, step, events in
            o.confirm(plain, onConfirm: { events.append("confirm") }); step("confirm")
            _ = o.handleKey(36); step("return")
        }
        try trace("dismissNoCallback") { o, step, events in
            o.confirm(plain, onConfirm: { events.append("confirm") }, onCancel: { events.append("cancel") }); step("confirm")
            o.dismissConfirm(); step("dismiss")
            o.dismissConfirm(); step("dismissAgain")
        }
        try trace("toastReplaceAndExpire") { o, step, events in
            o.showToast(HUDToastRequest(icon: .check, title: "One", caption: ""), completion: { events.append("toast one done") }); step("toast1")
            let first = scheduled
            o.showToast(HUDToastRequest(icon: .copy, title: "Two", caption: "two"), completion: { events.append("toast two done") }); step("toast2Replaces")
            first.forEach { $0.1.perform() }; step("firstTimerFiresIgnored")
            runScheduled(); step("secondTimerFires")
            completions(); step("hideCompleted")
        }
        try trace("toastDroppedDuringConfirm") { o, step, events in
            o.confirm(plain, onConfirm: { events.append("confirm") }); step("confirm")
            o.showToast(HUDToastRequest(icon: .check, title: "Dropped", caption: ""), completion: { events.append("dropped done") }); step("toastDropped")
            _ = o.handleKey(53); completions(); step("cancelled")
        }
        try trace("confirmClearsToast") { o, step, events in
            o.showToast(HUDToastRequest(icon: .check, title: "Shown", caption: ""), completion: { events.append("toast done") }); step("toast")
            o.confirm(plain, onConfirm: { events.append("confirm") }); step("confirmClears")
            runScheduled(); step("staleToastTimer")
            _ = o.handleKey(36); step("return")
            o.showToast(HUDToastRequest(icon: .check, title: "During collapse", caption: ""), completion: { events.append("late toast done") })
            step("toastDuringCollapse")
            completions(); step("collapseDone")
            runScheduled(); step("timersFire")
            completions(); step("toastHidden")
        }
        try trace("twinPills") { o, step, events in
            o.confirm(twin, onConfirm: { events.append("confirm") }, onCancel: { events.append("cancel") }); step("confirm")
            let outside = CGPoint(x: 295, y: 100)
            events.append("pointerDown outside -> \(o.router.pointerDown(at: outside, in: o.layer))")
            o.router.pointerUp(at: outside, in: o.layer); step("outsidePressCancels")
            completions()
            o.confirm(twin, onConfirm: { events.append("confirm") }, onCancel: { events.append("cancel") }); step("confirmAgain")
            let upper = o.confirmBar.pills[0].layer.frame, bar = o.confirmBar.layer.frame
            let p = CGPoint(x: bar.minX + upper.midX, y: bar.minY + upper.midY)
            events.append("pointerDown upper pill -> \(o.router.pointerDown(at: p, in: o.layer))")
            o.router.pointerUp(at: p, in: o.layer); runScheduled(); step("upperPillConfirms")
            completions()
            o.confirm(plain, onConfirm: { events.append("confirm") }, onCancel: { events.append("cancel") })
            events.append("pointerDown outside standard -> \(o.router.pointerDown(at: outside, in: o.layer))"); step("standardHasNoOutside")
            let cancel = o.confirmBar.pills[0].layer.frame
            let q = CGPoint(x: bar.minX + cancel.midX, y: bar.minY + cancel.midY)
            _ = o.router.pointerDown(at: q, in: o.layer); o.router.pointerUp(at: q, in: o.layer); step("cancelPill")
        }
        try trace("countdownAndReset") { o, step, events in
            o.confirm(requests()[2].1, onConfirm: { events.append("confirm") }); step("confirm")
            o.setCountdown(3); step("countdown3")
            o.setCountdown(2); step("countdown2")
            o.showToast(HUDToastRequest(icon: .check, title: "x", caption: "")); step("toastDropped")
            o.reset(); step("reset")
            o.setCountdown(1); step("countdownAfterReset")
        }
        // HUDConfirmState as pure state.
        var state = HUDConfirmState()
        var stateRows: [[String: Any]] = []
        for (label, action) in [("key before present", { _ = state.key(36) }), ("present", { state.present(requests()[2].1) }),
                                ("countdown 5", { state.setCountdown(5) }), ("key a", { _ = state.key(0) }), ("key esc", { _ = state.key(53) }),
                                ("key return", { _ = state.key(36) }), ("countdown 4", { state.setCountdown(4) }), ("close", { state.close() }),
                                ("present plain", { state.present(plain) }), ("countdown 3 (none)", { state.setCountdown(3) })] as [(String, () -> Void)] {
            action()
            stateRows.append(["step": label, "isOpen": state.isOpen, "decision": state.decision.map { "\($0)" } as Any? ?? NSNull(),
                              "countdown": state.request?.countdown as Any? ?? NSNull()])
        }
        overlayRows["confirmState"] = stateRows
        var toastState = HUDToastState()
        let tokenA = toastState.show(HUDToastRequest(icon: .check, title: "A", caption: ""))
        let tokenB = toastState.show(HUDToastRequest(icon: .check, title: "B", caption: ""))
        overlayRows["toastState"] = ["tokens": [tokenA, tokenB], "expireA": toastState.expire(tokenA), "current": toastState.current?.title as Any? ?? NSNull(),
                                     "expireB": toastState.expire(tokenB), "expireBAgain": toastState.expire(tokenB), "generation": toastState.generation,
                                     "afterClear": { toastState.clear(); return toastState.generation }()]
        payload["overlay"] = overlayRows

        // MARK: A fake canvas: lifecycle, navigation, menus, key / pointer / wheel precedence, stale rebuilds
        var canvasRows: [[String: Any]] = []
        scheduled = []
        let overlay = HUDCenterOverlay(env: env, schedule: { scheduled.append(($0, $1)) })
        let context = HUDDesignContext(env: env, overlay: overlay)
        var inputChanges = 0, parallaxLocks = 0
        context.onInputStateChange = { inputChanges += 1 }
        context.onLockParallax = { parallaxLocks += 1 }
        let canvas = FakeCanvas(module: .clipboard, context: context)
        let other = FakeCanvas(module: .eventLog, context: context)
        let moduleHost = HUDLayer.plain("module.host")
        HUDMotion.perform { moduleHost.bounds = CGRect(origin: .zero, size: HUDMetric.box); moduleHost.speed = 0; moduleHost.timeOffset = 0 }
        func record(_ label: String, _ c: FakeCanvas = canvas, tree: Bool = false, extra: [String: Any] = [:]) throws {
            var row: [String: Any] = ["step": label, "module": c.module.rawValue, "active": c.isActive, "transitioning": c.isTransitioning,
                "menuOpen": c.isMenuOpen, "popoverOpen": c.isPopoverOpen, "capturesPointer": c.capturesPointer, "dragging": c.isDragging,
                "inputLocked": c.isInputLocked, "builds": c.builds, "depth": c.depth, "tab": c.tab, "events": c.events, "keys": c.keys,
                "renderedLanguage": c.renderedLanguage.map(languageName) as Any? ?? NSNull(),
                "pageControls": c.page.controls.map(\.id), "staggered": c.page.staggered.count,
                "scrollOffsets": c.page.scrollAreas.map { ["id": $0.id, "offset": $0.area.offset] },
                "accessibility": accessibility(c.accessibilityItems), "inputChanges": inputChanges, "parallaxLocks": parallaxLocks,
                "pageOpacityAnimations": (c.page.layer.animationKeys() ?? []).sorted(),
                "headerAnimations": (c.header.layer.animationKeys() ?? []).sorted(),
                "rootChildren": (c.root.sublayers ?? []).map { $0.name ?? "?" }]
            if tree { row["root"] = try recorder.tree(c.root, id: "root") }
            for (k, v) in extra { row[k] = v }
            canvasRows.append(row)
        }
        _ = canvas.makeContent(for: .clipboard, style: HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2))
        HUDMotion.perform { moduleHost.addSublayer(canvas.root) }
        try record("madeContentInactive")
        canvas.setActive(true)
        try record("activated")
        canvas.playEntrance(from: HUDShellPosition.entranceOffset(from: .notes, to: .clipboard))
        try record("entrance", tree: true)
        // Hover routing: a toggle inside a row plate (row container stays highlighted, toggle wins hover).
        let toggle = canvas.page.controls.first { $0.id == "fake.toggle.0" }!, row0 = canvas.page.controls.first { $0.id == "fake.row.0" }!
        HUDControlHighlightLayer.update(in: canvas.root, point: centre(toggle, in: canvas.root), pressed: false)
        try record("hoverToggleInRow", extra: ["toggle": controlState(toggle, in: canvas.root), "row": controlState(row0, in: canvas.root)])
        HUDControlHighlightLayer.update(in: canvas.root, point: CGPoint(x: 100, y: 195), pressed: true)
        try record("pressRowBody", extra: ["toggle": controlState(toggle, in: canvas.root), "row": controlState(row0, in: canvas.root)])
        HUDControlHighlightLayer.update(in: canvas.root, point: nil, pressed: false)
        // Pointer: toggle press, containsControl, slider drag (parallax lock), wheel into the scroll area.
        let pt = centre(toggle, in: canvas.root)
        try record("containsControl", extra: ["toggle": canvas.containsControl(atBox: pt), "empty": canvas.containsControl(atBox: CGPoint(x: 580, y: 100)),
                                              "header": canvas.containsControl(atBox: CGPoint(x: 300, y: 10))])
        _ = canvas.pointerDown(at: pt); canvas.pointerUp(at: pt)
        try record("toggleClicked")
        let sliderControl = canvas.slider!, sr = sliderControl.rect(in: canvas.root)
        _ = canvas.pointerDown(at: CGPoint(x: sr.minX + 20, y: sr.midY))
        canvas.pointerDragged(to: CGPoint(x: sr.minX + 200, y: sr.midY))
        try record("sliderDragging", extra: ["top": try recorder.tree(canvas.top, id: "top")])
        canvas.pointerUp(at: CGPoint(x: sr.minX + 200, y: sr.midY))
        try record("sliderReleased")
        let wheelPoint = CGPoint(x: 300, y: 300)
        let wheel1 = canvas.scroll(at: wheelPoint, delta: CGPoint(x: 0, y: 120))
        let wheel2 = canvas.scroll(at: wheelPoint, delta: CGPoint(x: 0, y: 10_000))
        let wheel3 = canvas.scroll(at: wheelPoint, delta: CGPoint(x: 0, y: 50))
        let wheel4 = canvas.scroll(at: CGPoint(x: 300, y: 10), delta: CGPoint(x: 0, y: 50))
        let wheel5 = canvas.scroll(at: CGPoint(x: 300, y: 100), delta: CGPoint(x: 0, y: 50))
        try record("wheel", extra: ["results": [wheel1, wheel2, wheel3, wheel4, wheel5]])
        _ = canvas.scroll(at: wheelPoint, delta: CGPoint(x: 0, y: -10_000)); _ = canvas.scroll(at: wheelPoint, delta: CGPoint(x: 0, y: 120))
        // Menu: open from the dropdown, press inside / outside, wheel, keys.
        let dropPoint = centre(canvas.dropdown!, in: canvas.root)
        _ = canvas.pointerDown(at: dropPoint); canvas.pointerUp(at: dropPoint)
        try record("menuOpened", extra: ["dropdownOpen": canvas.dropdown!.isOpen, "top": try recorder.tree(canvas.top, id: "top")])
        let wheelInside = canvas.scroll(at: CGPoint(x: dropPoint.x, y: dropPoint.y + 60), delta: CGPoint(x: 0, y: 40))
        let wheelOutside = canvas.scroll(at: CGPoint(x: 560, y: 480), delta: CGPoint(x: 0, y: 40))
        let rightClick = canvas.rightPointerDown(at: CGPoint(x: 560, y: 480))
        try record("menuWheelAndRightClick", extra: ["wheelInside": wheelInside, "wheelOutside": wheelOutside, "rightClick": rightClick])
        _ = canvas.pointerDown(at: dropPoint); canvas.pointerUp(at: dropPoint)
        let menuKeys = [key(125), key(125), key(14, "e"), key(53)].map { canvas.keyDown($0) }
        try record("menuKeys", extra: ["consumed": menuKeys])
        _ = canvas.pointerDown(at: dropPoint); canvas.pointerUp(at: dropPoint)
        let outside = canvas.pointerDown(at: CGPoint(x: 560, y: 480))
        try record("pressOutsideClosesOnly", extra: ["consumed": outside])
        _ = canvas.pointerDown(at: dropPoint); canvas.pointerUp(at: dropPoint)
        _ = canvas.keyDown(key(125)); _ = canvas.keyDown(key(36))
        try record("menuPickByKeyboard", extra: ["dropdownOpen": canvas.dropdown!.isOpen])
        // Pop-over: Esc closes, other keys pass.
        let popover = HUDPopover(title: "Pen", contentHeight: 60, env: env)
        canvas.openPopover(popover, anchor: canvas.dropdown, controls: [])
        let popKeys = [key(0, "a"), key(14, "e"), key(53)].map { canvas.keyDown($0) }
        try record("popoverKeys", extra: ["consumed": popKeys])
        // Key precedence without popups.
        let plainKeys: [(String, NSEvent)] = [("i", key(34, "i")), ("k", key(40, "k")), ("cmd-e", key(14, "e", flags: .command)), ("esc", key(53)),
                                              ("E", key(14, "E", flags: .shift)), ("q", key(12, "q")), ("x", key(7, "x")), ("z", key(6, "z"))]
        let keyResults = plainKeys.map { ["key": $0.0, "consumed": canvas.keyDown($0.1)] as [String: Any] }
        try record("keyPrecedence", extra: ["results": keyResults])
        RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.25))   // the Q/E sub-navigations land
        try record("afterTabSwaps")
        // Navigation: forward with a scrolled list, retarget, back restores offsets.
        canvas.page.scrollAreas[0].area.setOffset(120)
        canvas.depth = 1
        canvas.navigate(dx: 1)
        try record("navigateForward", tree: true, extra: ["pressDuringSwap": canvas.pointerDown(at: pt), "keyDuringSwap": canvas.keyDown(key(7, "x"))])
        canvas.navigate(dx: 1)
        try record("retarget")
        Thread.sleep(forTimeInterval: 0.2)
        var landed: [String: Any] = [:]
        DispatchQueue.main.async { landed = (try? ["root": recorder.tree(canvas.root, id: "root"), "transitioning": canvas.isTransitioning]) ?? [:] }
        RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.02))
        try record("swapLanded", extra: ["landed": landed])
        canvas.depth = 0
        canvas.navigate(dx: -1)
        canvas.settleSwap()
        try record("backSettled")
        canvas.depth = 1; canvas.navigate(dx: 1)
        canvas.prepareToLeave()
        try record("prepareToLeaveConcealed")
        _ = canvas.makeContent(for: .clipboard, style: HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2))
        try record("revealedByMakeContent")
        canvas.depth = 0
        reduce = true; canvas.navigate(dx: -1); reduce = false
        try record("navigateReduceMotion")
        // Text fields: a press outside an editing field ends editing (no editor here: registration only).
        // Palette, language, recording mode, render scale.
        _ = other.makeContent(for: .eventLog, style: HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2))
        try record("otherBuiltInactive", other)
        HUDColor.apply(accentHex: "22A6F2", light: false); context.refreshPalette()
        try record("paletteChangedActive")
        try record("paletteChangedInactive", other)
        other.setActive(true)
        try record("inactiveRebuildsOnActivate", other)
        other.setActive(false)
        setLanguage(.japanese)
        _ = other.makeContent(for: .eventLog, style: HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2))
        try record("languageStaleRebuildOnShow", other, extra: ["header": try recorder.tree(other.header.layer, id: "header")])
        try record("activeNotRebuiltUntilShown")
        setLanguage(.english); HUDColor.apply(accentHex: "FAD41F", light: false); context.refreshPalette()
        context.setRecordingMode(true)
        try record("recordingModeOn")
        context.update(unit: HUDDesignGeometry.baseUnit * 1.5, renderScale: 2)
        try record("unitChanged", extra: ["renderScale": env.renderScale, "unit": context.unit])
        context.update(unit: HUDDesignGeometry.baseUnit, renderScale: 2)
        // Confirm through the canvas closes popups first; toast helper captions.
        _ = canvas.pointerDown(at: dropPoint); canvas.pointerUp(at: dropPoint)
        canvas.confirm(plain, onConfirm: {})
        try record("confirmClosesMenus", extra: ["capturesInput": overlay.capturesInput])
        overlay.dismissConfirm()
        var toastTexts: [[String: Any]] = []
        for language in languages {
            setLanguage(language)
            canvas.toast(icon: .trash, "History cleared", "剪贴板记录已清空")
            toastTexts.append(["language": languageName(language), "title": overlay.toast.textsForTesting.0, "caption": overlay.toast.textsForTesting.1])
            canvas.toast(icon: .ext, "Opening", "正在打开", detail: "Safari")
            toastTexts.append(["language": languageName(language), "detail": true, "title": overlay.toast.textsForTesting.0, "caption": overlay.toast.textsForTesting.1])
        }
        setLanguage(.english); overlay.reset()
        try record("toastHelper", extra: ["toasts": toastTexts])
        canvas.setActive(false)
        try record("deactivated")
        canvas.setActive(true)
        canvas.navigate(dx: 1); canvas.settleSwap(); canvas.navigate(dx: -1); canvas.settleSwap()
        try record("offsetsForgottenAfterDeactivate")
        payload["canvas"] = canvasRows

        // MARK: Text measurements and provenance
        for language in HUDTextLanguage.regions { for (_, style) in typeStyles { _ = recorder.lineMetrics(style(), language: language) } }
        payload["text"] = ["widths": recorder.widths.keys.sorted().map { recorder.widths[$0]! },
                           "metrics": recorder.metrics.keys.sorted().map { recorder.metrics[$0]! },
                           "systemLanguages": SystemLanguages.current.identifiers, "systemTag": HUDTextLanguage.tag(for: .english),
                           "uppercase": ["straße", "istanbul", "ǆ", "ﬁx", "Ⅻ"].map { ["text": $0, "displayed": HUDText.displayed($0, HUDType.breadcrumb)] }]
        payload["layerDefaults"] = recorder.layerDefaults
        payload["rasters"] = recorder.encoder.rasterAssets.map { ["sha256": $0["sha256"]!, "width": $0["width"]!, "height": $0["height"]!] }
        var categories: [String: Int] = [:]
        for item in recorder.encoder.unsupported { categories[item["category"]! + ": " + item["feature"]!, default: 0] += 1 }
        payload["unsupported"] = categories
        payload["isolation"] = ["actualSources": true, "temporaryHome": true, "windowCreated": !NSApp.windows.isEmpty,
                                "inputPosted": false, "storesTouched": false, "reduceMotionSetting": HUDRuntimeAppearance.reduceMotion]
        precondition(NSApp.windows.isEmpty)
        var document = sanitize(payload) as! [String: Any]
        document["tokens"] = sanitize(payload["tokens"]!, exact: true)
        let data = try JSONSerialization.data(withJSONObject: document, options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("hud-design-source.json"))
        print("HUD design oracle: \(components.count) component states, \(canvasRows.count) canvas steps, \(recorder.widths.count) widths, \(data.count) bytes")
    }
}
