import AppKit
import QuartzCore

/// The redesign's icon set (spec §1.13): 24 × 24 vector glyphs in the current
/// colour. Paths are parsed once from the spec's SVG data and scaled per size.
enum HUDIcon: String, CaseIterable {
    case check, x, power, plus, minus, copy, text, link, link2, image, file, refresh, reset, rotate
    case play, pause, prev, next, vol, mute, trash, folder, open, ext, export, todo, pen, eraser
    case timer, watch, devices, apps, bolt, back, left, right, mirror, lyric, bookmark, gear
    case eye, eyeoff, user, help, grid, lock, unlock, warn, rec, pin

    struct Part {
        enum Mode: Equatable { case stroke(CGFloat, square: Bool), fill }
        let svg: String
        let mode: Mode
        /// A fixed colour (the warning glyph's dark mark); nil follows the tint.
        var color: CGColor? = nil
        var dash: [CGFloat]? = nil
    }

    var parts: [Part] {
        func s(_ d: String, _ width: CGFloat = 2.6, square: Bool = false) -> Part { Part(svg: d, mode: .stroke(width, square: square)) }
        func f(_ d: String) -> Part { Part(svg: d, mode: .fill) }
        func rect(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> String { "M\(x) \(y)h\(w)v\(h)h\(-w)z" }
        func circle(_ cx: CGFloat, _ cy: CGFloat, _ r: CGFloat) -> String {
            "M\(cx - r) \(cy)a\(r) \(r) 0 1 0 \(2 * r) 0a\(r) \(r) 0 1 0 \(-2 * r) 0z"
        }
        switch self {
        case .check: return [s("M4 12.5l5 5L20 6.5", 3, square: true)]
        case .x: return [s("M5.5 5.5l13 13M18.5 5.5l-13 13", 3, square: true)]
        case .power: return [s("M12 3v8", 3, square: true), s("M6.6 6.8a8 8 0 1 0 10.8 0", 3)]
        case .plus: return [s("M12 4v16M4 12h16", 3)]
        case .minus: return [s("M4 12h16", 3)]
        case .copy: return [s(rect(8, 8, 12, 12)), s("M4 16V4h12")]
        case .text: return [s("M4 5h16M12 5v15M8 20h8", 3)]
        case .link: return [s("M10 14l4-4M8.5 11.5l-2 2a3.5 3.5 0 0 0 5 5l2-2M15.5 12.5l2-2a3.5 3.5 0 0 0-5-5l-2 2")]
        case .link2: return [s("M9 15l6-6M7 12l-2 2a3.5 3.5 0 0 0 5 5l2-2M17 12l2-2a3.5 3.5 0 0 0-5-5l-2 2")]
        case .image: return [s(rect(3, 4, 18, 16)), f("M5 18l5-6 4 4 2-2 3 4z"), f(rect(14, 7, 3, 3))]
        case .file: return [s("M5 3h9l5 5v13H5zM14 3v5h5")]
        case .refresh: return [s("M19 12a7 7 0 1 1-2-4.9", 2.8), f("M19 3v5h-5z")]
        case .reset: return [s("M5 12a7 7 0 1 0 2-4.9", 2.8), f("M5 3v5h5z")]
        case .rotate: return [s("M20 12a8 8 0 1 1-3-6.2"), f("M20 3v6h-6z")]
        case .play: return [f("M7 4l13 8-13 8z")]
        case .pause: return [f("M6 4h4v16H6zM14 4h4v16h-4z")]
        case .prev: return [f("M6 4h3v16H6zM20 4L10 12l10 8z")]
        case .next: return [f("M15 4h3v16h-3zM4 4l10 8-10 8z")]
        case .vol: return [f("M3 9h4l6-5v16l-6-5H3z"), s("M16 8.5a5 5 0 0 1 0 7M18.5 6a8.5 8.5 0 0 1 0 12", 2.4)]
        case .mute: return [f("M3 9h4l6-5v16l-6-5H3z"), s("M16 9l5 6M21 9l-5 6", 2.4)]
        case .trash: return [s("M4 6h16M9 6V3h6v3M6 6l1 15h10l1-15")]
        case .folder: return [s("M3 5h7l2 2h9v13H3z")]
        case .open: return [s("M3 6h7l2 2h9v11H3z"), s("M12 11v6M9 14l3-3 3 3", 2.4)]
        case .ext: return [s("M14 4h6v6M20 4l-9 9M18 14v6H4V6h6")]
        case .export: return [s("M12 15V3M7 8l5-5 5 5M4 14v7h16v-7")]
        case .todo: return [s(rect(3, 3, 18, 18) + "M7 12l3 3 7-7")]
        case .pen: return [s("M4 20l1-5L16 4l4 4L9 19z")]
        case .eraser: return [s("M3 17l9-11 8 7-7 8H6zM8 11l7 6")]
        case .timer: return [s(circle(12, 13, 8) + "M12 9v4l3 2M9 2h6")]
        case .watch: return [s(circle(12, 13, 8) + "M12 13V8")]
        case .devices: return [s(rect(2, 5, 14, 10) + "M5 19h8", 2.4), f(rect(18, 8, 4, 11))]
        case .apps: return [f(rect(3, 3, 7, 7) + rect(14, 3, 7, 7) + rect(3, 14, 7, 7)), s(rect(14, 14, 7, 7), 2.4)]
        case .bolt: return [f("M13 2L4 14h7l-1 8 9-12h-7z")]
        case .back: return [s("M15 4l-8 8 8 8", 3.2, square: true)]
        case .left: return [s("M15 4l-8 8 8 8", 3, square: true)]
        case .right: return [s("M9 4l8 8-8 8", 3, square: true)]
        case .mirror: return [Part(svg: "M12 3v18", mode: .stroke(2, square: false), dash: [2, 2]), f("M10 6L3 18h7zM14 6l7 12h-7z")]
        case .lyric: return [s("M4 6h12M4 11h12M4 16h7M19 9v9"), f(circle(17, 18, 2.6))]
        case .bookmark: return [s("M6 3h12v18l-6-5-6 5z")]
        case .gear: return [s(circle(12, 12, 3.4) + "M12 2v4M12 18v4M2 12h4M18 12h4M4.9 4.9l2.8 2.8M16.3 16.3l2.8 2.8M4.9 19.1l2.8-2.8M16.3 7.7l2.8-2.8")]
        case .eye: return [s("M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z", 2.4), f(circle(12, 12, 3))]
        case .eyeoff: return [s("M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z", 2.4), f(circle(12, 12, 3)), s("M4 21L20 3")]
        case .user: return [f(circle(12, 8, 4.5)), f("M3 21c1-5 5-7 9-7s8 2 9 7z")]
        case .help: return [s(circle(12, 12, 9) + "M9.5 9.5a2.5 2.5 0 1 1 3.5 2.3c-.7.4-1 .9-1 1.7V14M12 17v1")]
        case .grid: return [s("M3 3h18v18H3zM3 9h18M3 15h18M9 3v18M15 3v18", 2)]
        case .lock: return [s(rect(4, 10, 16, 11) + "M8 10V7a4 4 0 0 1 8 0v3")]
        case .unlock: return [s(rect(4, 10, 16, 11) + "M8 10V7a4 4 0 0 1 7.5-2")]
        case .warn: return [f("M12 2L23 21H1Z"), Part(svg: "M12 9v6M12 17.5v1", mode: .stroke(2.6, square: false), color: HUDColor.nearBlack)]
        case .rec: return [f(circle(12, 12, 6))]
        case .pin: return [s("M7 3h10M9.5 3v6.5L6 14h12l-3.5-4.5V3M12 14v7", 2.4)]
        }
    }

    private static var pathCache: [String: CGPath] = [:]
    private static let pathLock = NSLock()

    /// The 24-unit path of one part, parsed once.
    static func path(_ svg: String) -> CGPath {
        pathLock.lock(); defer { pathLock.unlock() }
        if let cached = pathCache[svg] { return cached }
        let path = HUDSVGPath.parse(svg)
        pathCache[svg] = path
        return path
    }
}

/// A glyph of HUDIcon at `size` px, tinted by `color`.
final class HUDIconLayer: CALayer {
    private(set) var icon: HUDIcon?
    private var parts: [(CAShapeLayer, HUDIcon.Part)] = []
    var color: CGColor = HUDColor.textOnDark { didSet { applyColor() } }

    override init() { super.init() }
    override init(layer: Any) { super.init(layer: layer) }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }
    override func action(forKey event: String) -> CAAction? { HUDLayer.action(forKey: event, of: self) }

    convenience init(_ icon: HUDIcon, size: CGFloat, color: CGColor) {
        self.init()
        self.color = color
        set(icon, size: size)
    }

    func set(_ icon: HUDIcon, size: CGFloat) {
        guard icon != self.icon || bounds.width != size else { return }
        self.icon = icon
        parts.forEach { $0.0.removeFromSuperlayer() }
        bounds = CGRect(x: 0, y: 0, width: size, height: size)
        var scale = CGAffineTransform(scaleX: size / 24, y: size / 24)
        parts = icon.parts.map { part in
            let shape = HUDLayer.shape()
            shape.frame = bounds
            shape.path = HUDIcon.path(part.svg).copy(using: &scale)
            switch part.mode {
            case .fill: shape.strokeColor = nil
            case .stroke(let width, let square):
                shape.fillColor = nil; shape.lineWidth = width * size / 24
                shape.lineCap = square ? .square : .butt; shape.lineJoin = .miter
                shape.lineDashPattern = part.dash.map { $0.map { NSNumber(value: Double($0 * size / 24)) } }
            }
            addSublayer(shape)
            return (shape, part)
        }
        applyColor()
    }

    private func applyColor() {
        for (shape, part) in parts {
            let tint = part.color ?? color
            if case .fill = part.mode { shape.fillColor = tint } else { shape.strokeColor = tint }
        }
    }
}

/// Minimal SVG path data reader for the icon set: M L H V C S A Z, absolute
/// and relative, with implicit repeats and compact number syntax.
enum HUDSVGPath {
    static func parse(_ data: String) -> CGPath {
        let path = CGMutablePath()
        var scanner = Scanner(Array(data.utf8))
        var current = CGPoint.zero, start = CGPoint.zero, lastControl: CGPoint?
        var command: UInt8 = 0
        while let next = scanner.nextCommand(previous: command) {
            command = next
            let relative = command >= 97
            let base = relative ? current : .zero
            func point() -> CGPoint? {
                guard let x = scanner.number(), let y = scanner.number() else { return nil }
                return CGPoint(x: base.x + x, y: base.y + y)
            }
            switch command | 0x20 {
            case UInt8(ascii: "m"):
                guard let p = point() else { return path }
                path.move(to: p); current = p; start = p; lastControl = nil
                command = relative ? UInt8(ascii: "l") : UInt8(ascii: "L")
            case UInt8(ascii: "l"):
                guard let p = point() else { return path }
                path.addLine(to: p); current = p; lastControl = nil
            case UInt8(ascii: "h"):
                guard let x = scanner.number() else { return path }
                current = CGPoint(x: (relative ? current.x : 0) + x, y: current.y); path.addLine(to: current); lastControl = nil
            case UInt8(ascii: "v"):
                guard let y = scanner.number() else { return path }
                current = CGPoint(x: current.x, y: (relative ? current.y : 0) + y); path.addLine(to: current); lastControl = nil
            case UInt8(ascii: "c"):
                guard let c1 = point(), let c2 = point(), let p = point() else { return path }
                path.addCurve(to: p, control1: c1, control2: c2); current = p; lastControl = c2
            case UInt8(ascii: "s"):
                guard let c2 = point(), let p = point() else { return path }
                let c1 = lastControl.map { CGPoint(x: 2 * current.x - $0.x, y: 2 * current.y - $0.y) } ?? current
                path.addCurve(to: p, control1: c1, control2: c2); current = p; lastControl = c2
            case UInt8(ascii: "a"):
                guard let rx = scanner.number(), let ry = scanner.number(), let rotation = scanner.number(),
                      let large = scanner.flag(), let sweep = scanner.flag(), let p = point() else { return path }
                addArc(to: path, from: current, to: p, rx: rx, ry: ry, rotation: rotation, large: large, sweep: sweep)
                current = p; lastControl = nil
            case UInt8(ascii: "z"):
                path.closeSubpath(); current = start; lastControl = nil
            default: return path
            }
        }
        return path
    }

    /// SVG endpoint arc → centre parameterization (SVG 1.1, F.6.5).
    private static func addArc(to path: CGMutablePath, from p0: CGPoint, to p1: CGPoint, rx: CGFloat, ry: CGFloat,
                               rotation: CGFloat, large: Bool, sweep: Bool) {
        var rx = abs(rx), ry = abs(ry)
        guard rx > 0, ry > 0, p0 != p1 else { path.addLine(to: p1); return }
        let phi = rotation * .pi / 180, cosPhi = cos(phi), sinPhi = sin(phi)
        let dx = (p0.x - p1.x) / 2, dy = (p0.y - p1.y) / 2
        let x1 = cosPhi * dx + sinPhi * dy, y1 = -sinPhi * dx + cosPhi * dy
        let lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry)
        if lambda > 1 { rx *= lambda.squareRoot(); ry *= lambda.squareRoot() }
        let numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
        let denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1
        var factor = (max(0, numerator) / denominator).squareRoot()
        if large == sweep { factor = -factor }
        let cx1 = factor * rx * y1 / ry, cy1 = -factor * ry * x1 / rx
        let center = CGPoint(x: cosPhi * cx1 - sinPhi * cy1 + (p0.x + p1.x) / 2,
                             y: sinPhi * cx1 + cosPhi * cy1 + (p0.y + p1.y) / 2)
        func angle(_ ux: CGFloat, _ uy: CGFloat, _ vx: CGFloat, _ vy: CGFloat) -> CGFloat {
            let sign: CGFloat = ux * vy - uy * vx < 0 ? -1 : 1
            let dot = (ux * vx + uy * vy) / ((ux * ux + uy * uy).squareRoot() * (vx * vx + vy * vy).squareRoot())
            return sign * acos(min(1, max(-1, dot)))
        }
        let theta = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry)
        var delta = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry)
        if !sweep && delta > 0 { delta -= 2 * .pi } else if sweep && delta < 0 { delta += 2 * .pi }
        let transform = CGAffineTransform(translationX: center.x, y: center.y).rotated(by: phi).scaledBy(x: 1, y: ry / rx)
        path.addRelativeArc(center: .zero, radius: rx, startAngle: theta, delta: delta, transform: transform)
    }

    private struct Scanner {
        let bytes: [UInt8]
        var index = 0
        init(_ bytes: [UInt8]) { self.bytes = bytes }

        mutating func skipSeparators() {
            while index < bytes.count, bytes[index] == 32 || bytes[index] == 44 || bytes[index] == 10 || bytes[index] == 9 { index += 1 }
        }
        /// A new command letter, or the previous command again when numbers follow.
        mutating func nextCommand(previous: UInt8) -> UInt8? {
            skipSeparators()
            guard index < bytes.count else { return nil }
            let byte = bytes[index]
            if (65...90).contains(byte) || (97...122).contains(byte) { index += 1; return byte }
            return previous == 0 || previous | 0x20 == UInt8(ascii: "z") ? nil : previous
        }
        mutating func flag() -> Bool? {
            skipSeparators()
            guard index < bytes.count, bytes[index] == 48 || bytes[index] == 49 else { return nil }
            defer { index += 1 }
            return bytes[index] == 49
        }
        mutating func number() -> CGFloat? {
            skipSeparators()
            let begin = index
            if index < bytes.count, bytes[index] == 45 || bytes[index] == 43 { index += 1 }
            var seenDot = false, seenDigit = false
            while index < bytes.count {
                let byte = bytes[index]
                if (48...57).contains(byte) { seenDigit = true; index += 1 }
                else if byte == 46, !seenDot { seenDot = true; index += 1 }
                else if (byte == 101 || byte == 69), seenDigit {
                    index += 1
                    if index < bytes.count, bytes[index] == 45 || bytes[index] == 43 { index += 1 }
                } else { break }
            }
            guard seenDigit, let text = String(bytes: bytes[begin..<index], encoding: .ascii), let value = Double(text) else {
                index = begin; return nil
            }
            return CGFloat(value)
        }
    }
}
