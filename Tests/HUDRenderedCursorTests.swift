import AppKit
import QuartzCore

enum HUDRenderedCursorTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let bitmap = CGContext(data: nil, width: 32, height: 48, bitsPerComponent: 8,
            bytesPerRow: 128, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        let image = bitmap.makeImage()!
        var calls: [String] = []
        var cursor: HUDRenderedCursor? = HUDRenderedCursor(hideCursor: { calls.append("hide") },
                                                          unhideCursor: { calls.append("unhide") })
        let layer = cursor!.layer, parent = CALayer(), otherParent = CALayer(), root = CALayer()
        root.addSublayer(parent)
        check(layer.isHidden && layer.superlayer == nil && calls.isEmpty,
              "An unused cursor neither attaches artwork nor hides the hardware cursor")
        check(layer.zPosition.isFinite && layer.zPosition > 3_000_000
              && layer.zPosition < CGFloat(Float.greatestFiniteMagnitude),
              "The cursor sorts above HUD overlays within Core Animation's finite depth range")
        var positionChanges = 0
        let observation = layer.observe(\.position) { _, _ in positionChanges += 1 }
        defer { observation.invalidate() }
        func show(size: CGSize = CGSize(width: 16, height: 24), hotspot: CGPoint = CGPoint(x: 3, y: 4),
                  point: CGPoint = CGPoint(x: 100, y: 80), parent target: CALayer? = nil) {
            cursor!.show(image: image, size: size, hotspot: hotspot, point: point, parent: target ?? parent)
        }
        show()
        check(calls == ["hide"] && !layer.isHidden && layer.superlayer === parent,
              "Showing attaches one cursor image and acquires exactly one hardware-hide lease")
        check(layer.contents as AnyObject? === image && layer.bounds.size == CGSize(width: 16, height: 24)
              && layer.position == CGPoint(x: 97, y: 76) && layer.anchorPoint == .zero,
              "The supplied point size and hotspot position the original bitmap without a transform")
        let initialMoves = positionChanges
        for _ in 0..<10 { show() }
        check(calls == ["hide"] && parent.sublayers?.count == 1 && positionChanges == initialMoves,
              "Repeated identical events neither add layers, move artwork nor hide the cursor again")
        show(point: CGPoint(x: 101, y: 82))
        check(layer.position == CGPoint(x: 98, y: 78) && positionChanges == initialMoves + 1,
              "A changed pointer moves the existing layer exactly once")
        show(size: CGSize(width: 32, height: 48), hotspot: CGPoint(x: 6, y: 8))
        check(layer.bounds.size == CGSize(width: 32, height: 48) && layer.position == CGPoint(x: 94, y: 72)
              && calls == ["hide"], "A backing-scale change updates point size and hotspot without acquiring another lease")
        check(layer.animationKeys()?.isEmpty != false,
              "Pointer and size changes introduce no implicit animations")
        show(parent: otherParent)
        check(layer.superlayer === otherParent && parent.sublayers?.contains(layer) != true
              && calls == ["hide"], "Reparenting transfers the same layer without duplicating cursor hiding")
        layer.removeFromSuperlayer()
        show(parent: otherParent)
        check(layer.superlayer === otherParent && otherParent.sublayers?.count == 1 && calls == ["hide"],
              "A detached cursor layer can be reattached while retaining its existing hide lease")
        layer.removeFromSuperlayer()
        cursor!.hide(); cursor!.hide()
        check(layer.isHidden && layer.superlayer == nil && calls == ["hide", "unhide"],
              "Explicit teardown balances once even when the layer has already been detached")
        show()
        parent.removeFromSuperlayer()
        cursor!.hide()
        check(calls == ["hide", "unhide", "hide", "unhide"] && layer.isHidden,
              "Detaching a parent does not prevent its owner's explicit hide from balancing the lease")
        cursor!.hide()
        check(calls.count == 4, "Repeated hidden lifecycle callbacks cannot over-unhide the cursor")
        show()
        cursor!.show(image: image, size: .zero, hotspot: .zero, point: .zero, parent: parent)
        check(layer.isHidden && calls.count == 6 && calls.last == "unhide",
              "An invalid visible size safely releases hardware hiding")
        show()
        weak var released = cursor
        cursor = nil
        check(released == nil && layer.superlayer == nil && layer.isHidden && calls.count == 8 && calls.last == "unhide",
              "Destruction removes artwork and balances a still-owned hide lease exactly once")
        var unused: HUDRenderedCursor? = HUDRenderedCursor(hideCursor: { calls.append("hide") },
                                                          unhideCursor: { calls.append("unhide") })
        unused?.hide(); unused = nil
        check(calls.count == 8, "Destroying an unused hidden cursor never unhides another owner's cursor")
        check(layer.animationKeys()?.isEmpty != false, "Showing, hiding and disposal leave no animation clocks")
        return count
    }
}
