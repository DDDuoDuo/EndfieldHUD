import AppKit
import QuartzCore

enum HUDClockStyleTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        func animations(_ layer: CALayer) -> Int {
            (layer.animationKeys()?.count ?? 0) + (layer.sublayers ?? []).reduce(0) { $0 + animations($1) }
        }
        check(HUDClockStyle.allCases.count == 5 && Set(HUDClockStyle.allCases.map(\.rawValue)).count == 5,
              "All five selectable clock styles retain distinct saved identifiers")
        for style in HUDClockStyle.allCases {
            for delta in [-11, -6, -5, -1, 0, 1, 5, 6, 11, Int.min, Int.max] {
                let index = (style.index + delta % 5 + 5) % 5
                check(style.advanced(delta) == HUDClockStyle.allCases[index],
                      "Clock style movement wraps both directions without overflowing large input")
            }
            let rect = HUDClockStyleArtwork.indicatorRect(style.index)
            check(HUDClockStyleArtwork.hitBounds.contains(rect)
                  && HUDClockStyleArtwork.indicator(at: CGPoint(x: rect.midX, y: rect.midY)) == style.index,
                  "Every visible indicator selects its matching style inside the shared hit bounds")
            for other in HUDClockStyle.allCases where other != style {
                check(!rect.intersects(HUDClockStyleArtwork.indicatorRect(other.index)),
                      "Adjacent indicator targets never overlap")
            }
        }
        for point in [CGPoint(x: 0, y: 131), CGPoint(x: 73, y: 131), CGPoint(x: 330, y: 131),
                      CGPoint(x: 40, y: 120), CGPoint(x: 40, y: 144)] {
            check(HUDClockStyleArtwork.indicator(at: point) == nil,
                  "Clock-body clicks and the gaps between indicators cannot select a style")
        }

        let targets = [HUDClockStyleArtwork.body] + HUDClockStyle.allCases.map { HUDClockStyleArtwork.indicatorRect($0.index) }
        var affine = CATransform3DIdentity
        affine.m11 = 0.6; affine.m22 = 0.8; affine.m41 = 635; affine.m42 = 74
        var perspective = affine
        perspective.m12 = -0.05; perspective.m21 = 0.12
        perspective.m14 = 0.00035; perspective.m24 = -0.00018
        for rect in targets {
            let bounds = HUDClockStyleArtwork.projectedBounds(of: rect, through: affine)
            let expected = CGRect(x: 635 + rect.minX * 0.6, y: 74 + rect.minY * 0.8,
                                  width: rect.width * 0.6, height: rect.height * 0.8)
            check(abs(bounds.minX - expected.minX) < 0.000001 && abs(bounds.minY - expected.minY) < 0.000001
                  && abs(bounds.width - expected.width) < 0.000001 && abs(bounds.height - expected.height) < 0.000001,
                  "Clock accessibility projects the banner's local origin without adding a legacy HUD-center offset")
            let tiltedBounds = HUDClockStyleArtwork.projectedBounds(of: rect, through: perspective)
            for fraction in [CGFloat(0), 0.25, 0.5, 0.75, 1] {
                let local = CGPoint(x: rect.minX + rect.width * fraction, y: rect.minY + rect.height * fraction)
                // Independent homography values include perspective division;
                // the active clock surface can translate, skew and tilt at once.
                let w = 1 + local.x * 0.00035 - local.y * 0.00018
                let point = CGPoint(x: (635 + 0.6 * local.x + 0.12 * local.y) / w,
                                    y: (74 - 0.05 * local.x + 0.8 * local.y) / w)
                check(tiltedBounds.insetBy(dx: -0.000001, dy: -0.000001).contains(point),
                      "Every visible clock target remains inside its projected accessibility frame under perspective")
            }
        }

        let gestures = HUDClockStyleArtwork()
        check(gestures.scroll(delta: 12, phase: .began, momentum: [], at: 1) == nil,
              "A small initial movement does not prematurely switch styles")
        check(gestures.scroll(delta: 15, phase: .changed, momentum: [], at: 1.02) == nil,
              "Subthreshold trackpad deltas accumulate within a gesture")
        check(gestures.scroll(delta: 1, phase: .changed, momentum: [], at: 1.04) == 1,
              "A completed horizontal movement selects exactly one next style")
        check(gestures.scroll(delta: 120, phase: .changed, momentum: [], at: 1.06) == nil,
              "Further movement in the same gesture cannot cascade through styles")
        check(gestures.scroll(delta: 120, phase: .changed, momentum: [], at: 1.6) == nil,
              "Pausing while fingers remain down cannot reset the one-style-per-gesture limit")
        check(gestures.scroll(delta: 0, phase: .ended, momentum: [], at: 1.61) == nil,
              "Zero-delta gesture-end events carry lifecycle without a new selection")
        check(gestures.scroll(delta: 0, phase: .began, momentum: [], at: 1.62) == nil,
              "A second gesture may begin with AppKit's zero-delta event")
        check(gestures.scroll(delta: -28, phase: .changed, momentum: [], at: 1.63) == -1,
              "A rapid second gesture works immediately instead of waiting for a timeout")
        check(gestures.scroll(delta: 500, phase: [], momentum: .began, at: 2.0) == nil
              && gestures.scroll(delta: -500, phase: [], momentum: .changed, at: 2.4) == nil,
              "Trackpad momentum never changes the selected style")
        gestures.resetGesture()
        check(gestures.scroll(delta: 16, phase: [], momentum: [], at: 4) == nil
              && gestures.scroll(delta: 12, phase: [], momentum: [], at: 4.1) == 1,
              "Phase-less wheel events support bounded accumulation")
        check(gestures.scroll(delta: 40, phase: [], momentum: [], at: 4.2) == nil
              && gestures.scroll(delta: -28, phase: [], momentum: [], at: 4.6) == -1,
              "A new wheel burst can select another style after the bounded quiet interval")
        gestures.resetGesture()
        check(gestures.scroll(delta: 20, phase: .began, momentum: [], at: 5) == nil,
              "A partial gesture remains pending before cancellation")
        _ = gestures.scroll(delta: 0, phase: .cancelled, momentum: [], at: 5.01)
        _ = gestures.scroll(delta: 0, phase: .began, momentum: [], at: 5.02)
        check(gestures.scroll(delta: 8, phase: .changed, momentum: [], at: 5.03) == nil,
              "Canceled gesture distance cannot leak into the next gesture")
        for invalid in [CGFloat.nan, .infinity, -.infinity] {
            check(gestures.scroll(delta: invalid, phase: .changed, momentum: [], at: 5.04) == nil,
                  "Invalid input cannot corrupt the gesture accumulator")
        }

        let artwork = HUDClockStyleArtwork(), time = CATextLayer(), date = CATextLayer()
        date.font = NSFont.systemFont(ofSize: 13)
        let seconds = artwork.layer.sublayers!.compactMap { $0 as? CATextLayer }.first!
        let retainedLayers = artwork.layer.sublayers!.map(ObjectIdentifier.init)
        for value in ["23:59:59", "00:00:00", "12:59:59 AM", "12:59:59 PM"] {
            for style in HUDClockStyle.allCases {
                artwork.update(style: style, reading: HUDClockReading(time: value, date: "WED Sep 30"),
                               time: time, date: date, accent: .systemYellow, scale: 2)
                let expected = style == .split ? value.split(separator: ":", maxSplits: 2).prefix(2).joined(separator: ":") : value
                check(time.string as? String == expected && date.string as? String == "WED Sep 30",
                      "Every layout preserves the supplied wall-clock reading and date")
                if style == .split {
                    check(!seconds.isHidden && seconds.string as? String == value.split(separator: ":", maxSplits: 2).last.map(String.init),
                          "Split layout retains seconds and AM/PM instead of dropping the 12-hour suffix")
                } else { check(seconds.isHidden, "Switching away from split cannot retain duplicate seconds") }
                for label in [time, date, seconds] where !label.isHidden {
                    let content = label.string as? String ?? ""
                    let base = label.font as? NSFont ?? NSFont.systemFont(ofSize: label.fontSize)
                    let font = NSFont(descriptor: base.fontDescriptor, size: label.fontSize)!
                    let size = (content as NSString).size(withAttributes: [.font: font])
                    check(size.width <= label.bounds.width + 0.5 && size.height <= label.bounds.height + 0.5,
                          "12/24-hour text fits the authored frame without clipping in \(style)")
                    check(HUDClockStyleArtwork.body.contains(label.frame), "Clock labels remain inside the banner body")
                    check(HUDClockPageViewport.contentRect.contains(label.frame),
                          "Every 12/24-hour style fits the inner page without clipping its text")
                }
                check(artwork.layer.sublayers!.map(ObjectIdentifier.init) == retainedLayers,
                      "Clock samples and style changes reuse their existing artwork layers")
                let selectedBounds = artwork.selection.path!.boundingBoxOfPath
                let target = HUDClockStyleArtwork.indicatorRect(style.index)
                check(selectedBounds.minX == target.minX && selectedBounds.maxX <= target.maxX
                      && selectedBounds.minY >= target.minY && selectedBounds.maxY <= target.maxY,
                      "Selected indicator artwork and hit geometry identify the same style")
                check(animations(artwork.layer) == 0 && animations(time) == 0 && animations(date) == 0,
                      "Clock samples add no implicit or perpetual animation tracks")
            }
        }

        let viewport = HUDClockPageViewport(), panel = CALayer(), badge = CATextLayer()
        panel.frame = HUDClockStyleArtwork.hitBounds
        panel.addSublayer(viewport.layer); panel.addSublayer(artwork.selection); panel.addSublayer(badge)
        viewport.install(time: time, date: date, artwork: artwork.layer)
        let retainedPage = viewport.page.sublayers!.map(ObjectIdentifier.init)
        check(retainedPage.count == 3 && viewport.page.superlayer === viewport.layer && viewport.layer.masksToBounds,
              "A single clipped page retains time, date and instrument artwork")
        check(artwork.selection.superlayer === panel && badge.superlayer === panel,
              "Selected-style bars and the Work Mode badge remain outside the moving page")
        for forward in [true, false, true] {
            viewport.transition(forward: forward, animated: true)
            let transition = viewport.page.animation(forKey: kCATransition) as? CATransition
            check(viewport.hasContainedHorizontalTransition && transition?.subtype == (forward ? .fromRight : .fromLeft),
                  "Both page directions use one finite horizontal push under a clipping ancestor")
            check(transition?.repeatCount == 0 && transition?.isRemovedOnCompletion == true
                  && viewport.page.animationKeys() == [kCATransition],
                  "Repeated style changes replace their transition without accumulating animation tracks")
            check([panel, viewport.layer, artwork.selection, badge, time, date, artwork.layer].allSatisfy {
                $0.animation(forKey: kCATransition) == nil
            }, "The fixed frame, controls and badge do not animate, and page children move together")
            viewport.install(time: time, date: date, artwork: artwork.layer)
            check(viewport.page.sublayers!.map(ObjectIdentifier.init) == retainedPage,
                  "Clock refreshes reuse the same page and three content layers")
        }
        viewport.transition(forward: false, animated: false)
        check(!viewport.hasContainedHorizontalTransition && animations(panel) == 0,
              "Reduce Motion removes an in-flight transition and changes the style immediately")
        viewport.transition(forward: true, animated: true); viewport.cancelTransition()
        check(animations(panel) == 0, "Closing or falling back cancels the page transition without a timer")

        // Exercise the actual Core Graphics clipping of translated page artwork.
        // A synthetic opaque page makes even one leaked pixel observable, while
        // the fixed viewport matches the production hierarchy and geometry.
        let clipped = HUDClockPageViewport(), root = CALayer()
        root.frame = HUDClockStyleArtwork.hitBounds; root.addSublayer(clipped.layer)
        clipped.page.backgroundColor = NSColor.white.cgColor
        for offset in [CGFloat(-200), -100, 0, 100, 200] {
            CATransaction.begin(); CATransaction.setDisableActions(true)
            clipped.page.setAffineTransform(CGAffineTransform(translationX: offset, y: 0))
            CATransaction.commit()
            let width = 340, height = 145
            let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
                bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue)!
            root.render(in: context)
            let bytes = context.data!.assumingMemoryBound(to: UInt8.self)
            var outsideInk = false, insideInk = false
            for y in 0..<height {
                for x in 0..<width where bytes[(y * width + x) * 4 + 3] != 0 {
                    if x < Int(HUDClockPageViewport.contentRect.minX) || x >= Int(HUDClockPageViewport.contentRect.maxX) {
                        outsideInk = true
                    } else { insideInk = true }
                }
            }
            check(insideInk && !outsideInk,
                  "A page translated in either direction remains visible only inside the clock box")
        }
        return count
    }
}
