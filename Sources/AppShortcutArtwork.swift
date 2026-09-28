import AppKit
import QuartzCore

/// Shared game artwork for matching presets, with functional vector glyphs
/// retained for actions which have no corresponding asset in the wiki.
enum AppShortcutArtwork {
    static func gameIcon(for icon: AppShortcutIcon) -> EndfieldGameIcon? {
        switch icon {
        case .bolt: return .power
        case .star: return .medal
        case .terminal: return .environmentMonitoring
        case .globe: return .store
        case .folder: return .depot
        case .code: return .aic
        case .game: return .operatorProfile
        case .grid: return .worldMap
        case .textBubble: return .baker
        default: return nil
        }
    }
    static func path(for icon: AppShortcutIcon, in rect: CGRect) -> CGPath {
        let p = CGMutablePath()
        func line(_ points: [CGPoint], closed: Bool = false) { guard let first = points.first else { return }; p.move(to: first); for point in points.dropFirst() { p.addLine(to: point) }; if closed { p.closeSubpath() } }
        func points(_ values: [CGFloat]) -> [CGPoint] { stride(from: 0, to: values.count, by: 2).map { CGPoint(x: values[$0], y: values[$0 + 1]) } }
        switch icon {
        case .original, .grid:
            for x in [CGFloat(3), 16] { for y in [CGFloat(3), 16] { p.addRect(CGRect(x: x, y: y, width: 9, height: 9)) } }
        case .bolt: line(points([16,1,5,16,13,16,11,27,24,11,16,11]), closed: true)
        case .star:
            for i in 0..<10 { let angle = CGFloat(i) * .pi / 5 - .pi / 2; let radius: CGFloat = i % 2 == 0 ? 12 : 5.2; let point = CGPoint(x: 14 + cos(angle) * radius, y: 14 + sin(angle) * radius); if i == 0 { p.move(to: point) } else { p.addLine(to: point) } }; p.closeSubpath()
        case .terminal:
            p.addRoundedRect(in: CGRect(x: 2, y: 4, width: 24, height: 20), cornerWidth: 2, cornerHeight: 2)
            line(points([7,10,11,14,7,18])); line(points([14,19,21,19]))
        case .globe:
            p.addEllipse(in: CGRect(x: 2, y: 2, width: 24, height: 24)); p.addEllipse(in: CGRect(x: 9, y: 2, width: 10, height: 24))
            line(points([2,14,26,14])); line(points([5,7,23,7])); line(points([5,21,23,21]))
        case .folder: line(points([2,7,11,7,14,10,26,10,24,24,3,24,2,7]), closed: true); line(points([2,7,2,4,11,4,14,7,24,7,24,10]))
        case .music: line(points([11,21,11,6,24,3,24,18])); line(points([11,10,24,7])); p.addEllipse(in: CGRect(x: 3, y: 18, width: 8, height: 6)); p.addEllipse(in: CGRect(x: 16, y: 15, width: 8, height: 6))
        case .play: p.addEllipse(in: CGRect(x: 2, y: 2, width: 24, height: 24)); line(points([11,8,21,14,11,20]), closed: true)
        case .brush: line(points([9,17,22,3,26,7,13,21]), closed: true); p.move(to: CGPoint(x: 12,y: 20)); p.addCurve(to: CGPoint(x: 2,y: 26), control1: CGPoint(x: 12,y: 28), control2: CGPoint(x: 7,y: 22)); p.addCurve(to: CGPoint(x: 9,y: 17), control1: CGPoint(x: 9,y: 24), control2: CGPoint(x: 3,y: 15))
        case .code: line(points([8,6,1,14,8,22])); line(points([20,6,27,14,20,22])); line(points([17,3,11,25]))
        case .game: p.addRoundedRect(in: CGRect(x: 1, y: 7, width: 26, height: 16), cornerWidth: 5, cornerHeight: 5); line(points([5,15,13,15])); line(points([9,11,9,19])); p.addEllipse(in: CGRect(x: 18, y: 12, width: 2, height: 2)); p.addEllipse(in: CGRect(x: 22, y: 16, width: 2, height: 2))
        case .camera: line(points([2,8,8,8,10,4,18,4,20,8,26,8,26,24,2,24]), closed: true); p.addEllipse(in: CGRect(x: 8, y: 10, width: 12, height: 12))
        case .textBubble:
            p.move(to: CGPoint(x: 6, y: 4)); p.addLine(to: CGPoint(x: 22, y: 4))
            p.addQuadCurve(to: CGPoint(x: 26, y: 8), control: CGPoint(x: 26, y: 4))
            p.addLine(to: CGPoint(x: 26, y: 18))
            p.addQuadCurve(to: CGPoint(x: 22, y: 22), control: CGPoint(x: 26, y: 22))
            p.addLine(to: CGPoint(x: 13, y: 22)); p.addLine(to: CGPoint(x: 6, y: 26))
            p.addLine(to: CGPoint(x: 7, y: 22)); p.addLine(to: CGPoint(x: 6, y: 22))
            p.addQuadCurve(to: CGPoint(x: 2, y: 18), control: CGPoint(x: 2, y: 22))
            p.addLine(to: CGPoint(x: 2, y: 8))
            p.addQuadCurve(to: CGPoint(x: 6, y: 4), control: CGPoint(x: 2, y: 4)); p.closeSubpath()
            line(points([7,10,21,10])); line(points([7,15,17,15]))
        }
        var transform = CGAffineTransform(translationX: rect.minX, y: rect.minY)
            .scaledBy(x: rect.width / 28, y: rect.height / 28)
        return p.copy(using: &transform) ?? p
    }

    static func makeGlyph(_ icon: AppShortcutIcon, rect: CGRect, color: NSColor, contentsScale: CGFloat) -> CAShapeLayer {
        let shape = CAShapeLayer(); shape.frame = rect
        if let image = gameIcon(for: icon)?.cgImage(size: max(64, rect.width * contentsScale), tint: color) {
            shape.contents = image; shape.contentsGravity = .resizeAspect
            shape.contentsScale = contentsScale
            return shape
        }
        shape.path = path(for: icon, in: CGRect(origin: .zero, size: rect.size))
        shape.fillColor = nil; shape.strokeColor = color.cgColor
        shape.lineWidth = max(1, rect.width / 17); shape.lineCap = .round; shape.lineJoin = .round
        shape.contentsScale = contentsScale
        return shape
    }
}
