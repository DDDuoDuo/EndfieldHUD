import AppKit
import QuartzCore

/// A short mechanical reveal for a module's own pages or filters. Only the
/// supplied content moves; surrounding headings, tabs and the outer HUD stay
/// attached. Model values always expose the destination, including on cancel.
final class HUDSubsectionTransition {
    static let duration: TimeInterval = 0.26
    static let movementKey = "subsection.depth"
    static let revealKey = "subsection.reveal"
    private weak var content: CALayer?
    private let viewport: CGRect
    private let mask = CAShapeLayer()
    var animationCount: Int {
        (content?.animation(forKey: Self.movementKey) == nil ? 0 : 1)
            + (mask.animation(forKey: Self.revealKey) == nil ? 0 : 1)
    }

    init(content: CALayer, viewport: CGRect) {
        self.content = content; self.viewport = viewport
        CATransaction.begin(); CATransaction.setDisableActions(true)
        mask.name = "subsection.mask"; mask.frame = content.bounds
        mask.fillColor = NSColor.black.cgColor
        mask.path = CGPath(rect: viewport, transform: nil)
        content.mask = mask
        CATransaction.commit()
    }
    deinit { settle() }

    func reveal(direction: CGFloat, animated: Bool) {
        settle()
        guard animated, let content else { return }
        let sign: CGFloat = direction < 0 ? -1 : 1
        let depth = CABasicAnimation(keyPath: "sublayerTransform")
        var start = CATransform3DIdentity; start.m34 = -1 / 720
        start = CATransform3DTranslate(start, 18 * sign, 0, -16)
        depth.fromValue = NSValue(caTransform3D: start)
        depth.toValue = NSValue(caTransform3D: CATransform3DIdentity)
        depth.duration = Self.duration
        depth.timingFunction = CAMediaTimingFunction(controlPoints: 0.16, 0.78, 0.25, 1)
        content.add(depth, forKey: Self.movementKey)

        let reveal = CAKeyframeAnimation(keyPath: "path")
        reveal.values = [CGFloat(0), 0.38, 0.72, 1].map { shutter(progress: $0, direction: sign) }
        reveal.keyTimes = [0, 0.3, 0.68, 1]
        reveal.timingFunctions = Array(repeating: CAMediaTimingFunction(name: .easeOut), count: 3)
        reveal.duration = Self.duration
        mask.add(reveal, forKey: Self.revealKey)
    }

    func settle() {
        content?.removeAnimation(forKey: Self.movementKey)
        mask.removeAnimation(forKey: Self.revealKey)
    }

    private func shutter(progress: CGFloat, direction: CGFloat) -> CGPath {
        let path = CGMutablePath(), lags: [CGFloat] = [0.06, 0.18, 0, 0.12]
        let height = viewport.height / CGFloat(lags.count)
        for (index, lag) in lags.enumerated() {
            let width = viewport.width * min(1, max(0, progress * 1.2 - lag))
            let cut = min(5, width * 0.12) * (1 - progress), y = viewport.minY + CGFloat(index) * height
            let left = direction > 0 ? viewport.maxX - width : viewport.minX
            let right = left + width
            path.move(to: CGPoint(x: left + cut, y: y))
            path.addLine(to: CGPoint(x: right, y: y))
            path.addLine(to: CGPoint(x: right, y: y + height - cut))
            path.addLine(to: CGPoint(x: right - cut, y: y + height))
            path.addLine(to: CGPoint(x: left, y: y + height))
            path.addLine(to: CGPoint(x: left, y: y + cut))
            path.closeSubpath()
        }
        return path
    }
}

/// Exchanges two retained pages behind one shared moving edge. A destination
/// reveal on its own would first hide the source and expose the empty canvas;
/// complementary masks keep exactly one page visible throughout the hand-off.
final class HUDSubsectionHandoff {
    private let pages: [CALayer]
    private let masks: [CAShapeLayer]
    private let viewport: CGRect
    private(set) var selectedIndex = 0
    private var completion: DispatchWorkItem?
    private var generation = 0
    var isTransitioning: Bool { completion != nil }
    var animationCount: Int {
        pages.reduce(0) { $0 + ($1.animation(forKey: HUDSubsectionTransition.movementKey) == nil ? 0 : 1) }
            + masks.reduce(0) { $0 + ($1.animation(forKey: HUDSubsectionTransition.revealKey) == nil ? 0 : 1) }
    }

    init(first: CALayer, second: CALayer, viewport: CGRect) {
        pages = [first, second]; masks = [CAShapeLayer(), CAShapeLayer()]; self.viewport = viewport
        withoutActions {
            for (page, mask) in zip(pages, masks) {
                mask.name = "subsection.handoff.mask"; mask.frame = page.bounds
                mask.fillColor = NSColor.black.cgColor; mask.path = CGPath(rect: viewport, transform: nil)
                page.mask = mask
            }
        }
        settle()
    }
    deinit { settle() }

    func select(_ index: Int, direction: CGFloat, animated: Bool) {
        guard pages.indices.contains(index) else { return }
        let previous = selectedIndex
        settle(); selectedIndex = index
        guard previous != index, animated else { settle(); return }
        let sign: CGFloat = direction < 0 ? -1 : 1
        let incoming = pages[index], outgoing = pages[previous]
        let collapsedIncoming = CGRect(x: sign > 0 ? viewport.maxX : viewport.minX, y: viewport.minY, width: 0, height: viewport.height)
        let collapsedOutgoing = CGRect(x: sign > 0 ? viewport.minX : viewport.maxX, y: viewport.minY, width: 0, height: viewport.height)
        let timing = CAMediaTimingFunction(controlPoints: 0.2, 0.7, 0.3, 1)
        withoutActions {
            incoming.isHidden = false; outgoing.isHidden = false
            masks[previous].path = CGPath(rect: collapsedOutgoing, transform: nil)
            outgoing.sublayerTransform = CATransform3DMakeTranslation(-14 * sign, 0, 0)
        }
        func animateMask(_ mask: CAShapeLayer, from: CGRect, to: CGRect) {
            let animation = CABasicAnimation(keyPath: "path")
            animation.fromValue = CGPath(rect: from, transform: nil); animation.toValue = CGPath(rect: to, transform: nil)
            animation.duration = HUDSubsectionTransition.duration; animation.timingFunction = timing
            mask.add(animation, forKey: HUDSubsectionTransition.revealKey)
        }
        func animatePage(_ page: CALayer, from: CGFloat, to: CGFloat) {
            let animation = CABasicAnimation(keyPath: "sublayerTransform")
            animation.fromValue = NSValue(caTransform3D: CATransform3DMakeTranslation(from, 0, 0))
            animation.toValue = NSValue(caTransform3D: CATransform3DMakeTranslation(to, 0, 0))
            animation.duration = HUDSubsectionTransition.duration; animation.timingFunction = timing
            page.add(animation, forKey: HUDSubsectionTransition.movementKey)
        }
        // Rectangular paths have identical topology. Their matching easing and
        // durations keep the reveal boundary shared in either direction.
        animateMask(masks[index], from: collapsedIncoming, to: viewport)
        animateMask(masks[previous], from: viewport, to: collapsedOutgoing)
        animatePage(incoming, from: 14 * sign, to: 0)
        animatePage(outgoing, from: 0, to: -14 * sign)
        let token = generation
        let work = DispatchWorkItem { [weak self] in
            guard let self, self.generation == token else { return }; self.settle()
        }
        completion = work
        DispatchQueue.main.asyncAfter(deadline: .now() + HUDSubsectionTransition.duration, execute: work)
    }

    func settle() {
        generation += 1; completion?.cancel(); completion = nil
        withoutActions {
            for index in pages.indices {
                pages[index].removeAnimation(forKey: HUDSubsectionTransition.movementKey)
                masks[index].removeAnimation(forKey: HUDSubsectionTransition.revealKey)
                pages[index].sublayerTransform = CATransform3DIdentity
                masks[index].path = CGPath(rect: viewport, transform: nil)
                pages[index].isHidden = index != selectedIndex
            }
        }
    }
    private func withoutActions(_ body: () -> Void) {
        CATransaction.begin(); CATransaction.setDisableActions(true); body(); CATransaction.commit()
    }
}
