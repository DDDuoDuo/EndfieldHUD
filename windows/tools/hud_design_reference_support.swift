import AppKit
import CoreText
import QuartzCore

// Shared recorder of the design-library oracles (LIBRARY-DESIGN.md section 14): JSON helpers, the
// ModuleReferenceLayerEncoder hook for the design layer classes (HUDTextLayer runs and line layout,
// HUDIconLayer, HUDHatchLayer, highlight targets, animation records), recorded HUDText widths and line
// metrics, the paused host layer and the control/accessibility rows. Compiled with
// tools/module_reference_layers.swift into tools/hud_design_reference.swift (the component library) and the
// module canvas oracles (tools/utility_canvases_reference.swift: Clipboard, Event Log, Volume).

// MARK: - JSON helpers

/// Floats are rounded to 1e-9 (stable across runs, exact enough for 7 / 5.5 style ratios).
func round6(_ value: Double) -> Double { value.isFinite && abs(value) < 1e6 ? (value * 1e9).rounded() / 1e9 : value }
/// `exact` keeps full double precision (the token section: constants, no layout noise).
func sanitize(_ any: Any, exact: Bool = false) -> Any {
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
func rgba(_ color: CGColor?) -> Any {
    guard let color, let converted = color.converted(to: CGColorSpace(name: CGColorSpace.sRGB)!, intent: .defaultIntent, options: nil),
          let parts = converted.components else { return NSNull() }
    return (parts.count == 2 ? [parts[0], parts[0], parts[0], parts[1]] : parts).map { Double($0) }
}
func hex(_ color: CGColor) -> String {
    guard let parts = color.converted(to: CGColorSpace(name: CGColorSpace.sRGB)!, intent: .defaultIntent, options: nil)?.components,
          parts.count >= 3 else { return "?" }
    return parts[0...2].map { String(format: "%02X", Int((min(1, max(0, $0)) * 255).rounded())) }.joined()
}
func rect(_ r: CGRect) -> [Double] { [r.minX, r.minY, r.width, r.height].map { round6(Double($0)) } }
func point(_ p: CGPoint) -> [Double] { [round6(Double(p.x)), round6(Double(p.y))] }
func pathJSON(_ p: CGPath?) -> Any {
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
func styleJSON(_ s: HUDTextStyle) -> [String: Any] {
    var row: [String: Any] = ["size": s.size, "weight": s.weight.rawValue, "color": rgba(s.color)]
    if s.weight.resolved != s.weight { row["resolvedWeight"] = s.weight.resolved.rawValue }
    if s.lineHeight != 1 { row["lineHeight"] = s.lineHeight }
    if s.tracking != 0 { row["tracking"] = s.tracking }
    if s.uppercase { row["uppercase"] = true }
    if s.tabular { row["tabular"] = true }
    return row
}
func timingJSON(_ f: CAMediaTimingFunction) -> [Double] {
    var one: [Float] = [0, 0], two: [Float] = [0, 0]
    f.getControlPoint(at: 1, values: &one); f.getControlPoint(at: 2, values: &two)
    return [one[0], one[1], two[0], two[1]].map { round6(Double($0)) }
}
func mirrored<T>(_ object: Any, _ name: String, as: T.Type) -> T? {
    var mirror: Mirror? = Mirror(reflecting: object)
    while let current = mirror {
        if let value = current.children.first(where: { $0.label == name })?.value { return value as? T }
        mirror = current.superclassMirror
    }
    return nil
}
func languageName(_ language: AppLanguage) -> String {
    switch language {
    case .english: return "en"
    case .simplifiedChinese: return "zh-Hans"
    case .traditionalChinese: return "zh-Hant"
    case .japanese: return "ja"
    case .korean: return "ko"
    case .system: return "system"
    }
}
func key(_ code: UInt16, _ characters: String = "", flags: NSEvent.ModifierFlags = []) -> NSEvent {
    // Constructed only to call the canvas's keyDown directly; never posted to any queue.
    NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: flags, timestamp: 0, windowNumber: 0, context: nil,
                     characters: characters, charactersIgnoringModifiers: characters, isARepeat: false, keyCode: code)!
}

// MARK: - Recorder: layer trees, measured widths and line metrics

final class Recorder {
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
func host(_ layers: CALayer..., paused: Bool = true) -> CALayer {
    let root = HUDLayer.plain("host")
    HUDMotion.perform {
        root.bounds = CGRect(origin: .zero, size: HUDMetric.box); layers.forEach(root.addSublayer)
        if paused { root.speed = 0; root.timeOffset = 0 }
    }
    return root
}
func accessibility(_ items: [HUDAccessibilityItem]) -> [[String: Any]] {
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
func controlState(_ c: HUDControl, in root: CALayer) -> [String: Any] {
    ["id": c.id, "label": c.label, "hovered": c.isHovered, "pressed": c.isPressed, "enabled": c.isEnabled,
     "rect": rect(c.rect(in: root)), "accessibility": accessibility(c.accessibilityItems(in: root))]
}
func centre(_ c: HUDControl, in root: CALayer) -> CGPoint { let r = c.rect(in: root); return CGPoint(x: r.midX, y: r.midY) }

