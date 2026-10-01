import Foundation
import simd

enum HUDSourceBannerScrollTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String) {
            count += 1
            if !condition { fatalError(message) }
        }
        func near(_ actual: Float, _ expected: Double, tolerance: Double = 0.0001) -> Bool {
            abs(Double(actual) - expected) < tolerance
        }
        do {
            let geometry = try HUDSourceBannerScroll.Geometry(pageCount: 2, contentWidth: 731.5)
            var scroll = HUDSourceBannerScroll(geometry: geometry)
            check(scroll.position == 0 && scroll.velocity == 0 && !scroll.requiresFrames,
                  "A settled source banner does not demand physical frames")
            try scroll.initializePotentialDrag(screenPosition: .zero)
            check(try scroll.drag(screenPosition: SIMD2(6, 7.9), frameDelta: SIMD2(6, 7.9),
                                  viewportLocalX: 50, currentCenter: 0) == .potential,
                  "Below-ten-pixel diagonal movement remains a potential click")
            check(try scroll.drag(screenPosition: SIMD2(6, 8), frameDelta: SIMD2(0, 0.1),
                                  viewportLocalX: 51, currentCenter: 0) == .began,
                  "Six-eight screen displacement reaches the inclusive ten-pixel threshold")
            check(scroll.isDragging && scroll.position == 0 && scroll.requiresFrames,
                  "Begin captures the threshold-time local pointer without jumping from the initial press")
            check(try scroll.drag(screenPosition: SIMD2(106, 8), frameDelta: SIMD2(100, 0),
                                  viewportLocalX: 151, currentCenter: 0) == .dragged,
                  "Subsequent local pointer movement writes the original drag position")
            // Independent algebra: .1*100=10; 365*(1-365/375)=3650/375.
            check(near(scroll.position, 3650.0 / 375.0),
                  "One-hundred-unit edge drag uses original point-one RubberDelta")
            check(scroll.normalizedPosition < 0,
                  "Elastic overscroll survives normalized sampling instead of clamping to page zero")
            let overscroll = scroll.position
            check(try scroll.drag(screenPosition: SIMD2(106, 8), frameDelta: .zero,
                                  viewportLocalX: 900, currentCenter: 0) == .ignored && scroll.position == overscroll,
                  "A stationary captured pointer emits no Drag event and does not reset the separate Lua hold clock")
            let decision = try scroll.endDrag(screenPosition: SIMD2(106, 8), frameDelta: .zero,
                screenWidth: 1728, panelRectWidth: 1728, currentCenter: 0)
            check(decision?.forceStep == false && decision?.targetIndex == 0 && !scroll.isDragging,
                  "Release delta zero is not replaced by the previous one-hundred-pixel movement")
            try scroll.lateUpdate(unscaledDelta: 0.1)
            check(scroll.position > 0 && scroll.position < overscroll && scroll.velocity < 0,
                  "Released overscroll springs inward without a page tween owned by this helper")
            // Float SmoothDamp at x=2: denominator=1+2+.48*4+.235*8=6.8.
            var springVelocity: Float = 0
            let spring = HUDSourceBannerScroll.smoothDamp(current: 100, target: 0,
                velocity: &springVelocity, smoothTime: 0.1, delta: 0.1)
            check(near(spring, 300.0 / 6.8), "Source spring displacement matches an independently calculated rational step")
            check(near(springVelocity, -4000.0 / 6.8), "Source spring returns its independently calculated velocity")
            var overshootVelocity: Float = 100
            let noOvershoot = HUDSourceBannerScroll.smoothDamp(current: 0, target: 1,
                velocity: &overshootVelocity, smoothTime: 0.1, delta: 0.1)
            check(noOvershoot == 1 && overshootVelocity == 0, "Native finalizer clamps target overshoot and clears velocity")

            scroll.reset()
            try scroll.initializePotentialDrag(screenPosition: .zero)
            _ = try scroll.drag(screenPosition: SIMD2(-10, 0), frameDelta: SIMD2(-10, 0), viewportLocalX: -10, currentCenter: 0)
            _ = try scroll.drag(screenPosition: SIMD2(-60, 0), frameDelta: SIMD2(-50, 0), viewportLocalX: -60, currentCenter: 0)
            try scroll.lateUpdate(unscaledDelta: 0.05)
            // Delta local=-50, dt=.05 =>-1000. Lerp multiplier=.5 =>-500.
            check(near(scroll.velocity, -500), "Dragged velocity uses frame displacement/delta time and dt-times-ten smoothing")
            let flick = try scroll.endDrag(screenPosition: SIMD2(-60, 0), frameDelta: SIMD2(-10, 0),
                screenWidth: 1728, panelRectWidth: 1728, currentCenter: 0)
            check(flick?.targetIndex == 1 && flick?.forceStep == true && flick?.forward == true,
                  "Inclusive last-frame delta ten advances a still-nearest starting page")
            check(near(scroll.velocity, -500), "OnEndDrag retains ScrollRect velocity until another native writer changes it")
            var disabled = scroll
            disabled.onDisable()
            check(disabled.position == scroll.position && disabled.velocity == 0 && !disabled.isDragging,
                  "Actual component disable clears velocity without inventing a page tween or moving content")
            try scroll.lateUpdate(unscaledDelta: 1)
            check(near(scroll.velocity, -67.50000268220901, tolerance: 0.0002) && near(scroll.position, -117.50000268220901, tolerance: 0.0002),
                  "In-bound one-second inertia uses authored point-135 decay and then integrates position")
            let oldPosition = scroll.position
            check(try scroll.setNormalizedPosition(0.5), "The separate page tween may write its normalized position")
            check(scroll.position == -183.25 && scroll.velocity == 0,
                  "A page-tween setter maps half of 366.5 hidden units and cancels velocity")
            check(oldPosition != scroll.position, "The page setter does not preserve an obsolete inertia position")
            let almost = Float(0.5) + Float(0.005) / geometry.hiddenLength
            check(!(try scroll.setNormalizedPosition(almost)) && scroll.position == -183.25,
                  "Normalized setter preserves the original point-zero-one local-position tolerance")
            check(try scroll.setNormalizedPosition(1.2) && scroll.position < -366.5,
                  "The native normalized setter accepts finite out-of-range positions")
            try scroll.lateUpdate(unscaledDelta: 0.1, reduceMotion: true)
            check(scroll.position == -366.5 && scroll.velocity == 0 && !scroll.requiresFrames,
                  "Explicit Mac reduced-motion policy settles the edge without introducing another clock")

            scroll.reset()
            try scroll.initializePotentialDrag(screenPosition: .zero)
            check(try scroll.drag(screenPosition: SIMD2(-100, 0), frameDelta: .zero, viewportLocalX: -100, currentCenter: 0) == .potential,
                  "Threshold comparison requires the source input frame to contain movement")
            _ = try scroll.drag(screenPosition: SIMD2(-100, 0), frameDelta: SIMD2(-1, 0), viewportLocalX: -100, currentCenter: 0)
            let distance = try scroll.endDrag(screenPosition: SIMD2(-109, 0), frameDelta: SIMD2(9, 0),
                screenWidth: 1000, panelRectWidth: 1000, currentCenter: 0)
            check(distance?.forceStep == true && distance?.forward == true && distance?.targetIndex == 1,
                  "Total 360-times-point-three distance determines direction when last-frame delta is below ten")
            try scroll.initializePotentialDrag(screenPosition: .zero)
            _ = try scroll.drag(screenPosition: SIMD2(-10, 0), frameDelta: SIMD2(-10, 0), viewportLocalX: -10, currentCenter: 0)
            let ratio = try scroll.endDrag(screenPosition: SIMD2(-80, 0), frameDelta: .zero,
                screenWidth: 1600, panelRectWidth: 2400, currentCenter: 0)
            check(ratio?.forceStep == true && ratio?.targetIndex == 1,
                  "Total-distance threshold uses Screen.width to owning-panel width rather than local drag length")
            try scroll.initializePotentialDrag(screenPosition: .zero)
            _ = try scroll.drag(screenPosition: SIMD2(-10, 0), frameDelta: SIMD2(-10, 0), viewportLocalX: -10, currentCenter: 0)
            let crossed = try scroll.endDrag(screenPosition: SIMD2(-200, 0), frameDelta: SIMD2(50, 0),
                screenWidth: 1728, panelRectWidth: 1728, currentCenter: 1)
            check(crossed?.targetIndex == 1 && crossed?.forward == false,
                  "A nearest page that already crossed the start center is retained despite the final delta's direction")
            try scroll.initializePotentialDrag(screenPosition: .zero)
            _ = try scroll.drag(screenPosition: SIMD2(-10, 0), frameDelta: SIMD2(-10, 0), viewportLocalX: -10, currentCenter: 1)
            let edge = try scroll.endDrag(screenPosition: SIMD2(-20, 0), frameDelta: SIMD2(-10, 0),
                screenWidth: 1728, panelRectWidth: 1728, currentCenter: 1)
            check(edge?.targetIndex == 1, "Forced next-page decisions clamp at the final original page")

            let one = try HUDSourceBannerScroll.Geometry(pageCount: 1, contentWidth: 365)
            var single = HUDSourceBannerScroll(geometry: one)
            try single.initializePotentialDrag(screenPosition: .zero)
            _ = try single.drag(screenPosition: SIMD2(10, 0), frameDelta: SIMD2(10, 0), viewportLocalX: 10, currentCenter: 0)
            _ = try single.drag(screenPosition: SIMD2(110, 0), frameDelta: SIMD2(100, 0), viewportLocalX: 110, currentCenter: 0)
            check(single.position > 0 && single.normalizedPosition == 0,
                  "One-page content still records elastic anchored displacement even when normalized getter cannot represent it")
            _ = try single.endDrag(screenPosition: SIMD2(110, 0), frameDelta: .zero,
                screenWidth: 1728, panelRectWidth: 1728, currentCenter: 0)
            try single.lateUpdate(unscaledDelta: 0.1)
            check(single.position > 0 && single.position < 10, "A one-page released edge can still run the source spring stage")
            check(try single.setNormalizedPosition(0) && single.position == 0 && single.velocity == 0,
                  "Any original normalized setter maps content-fits geometry to its zero anchored position")

            var rejected = false
            do { _ = try HUDSourceBannerScroll.Geometry(pageCount: 0, contentWidth: 365) } catch { rejected = true }
            check(rejected, "Empty pages do not create an interactive scroll physics instance")
            rejected = false
            do { try scroll.initializePotentialDrag(screenPosition: SIMD2(.nan, 0)) } catch { rejected = true }
            check(rejected, "Nonfinite pointer samples are rejected before entering input capture")
            rejected = false
            do { try scroll.lateUpdate(unscaledDelta: .infinity) } catch { rejected = true }
            check(rejected, "Nonfinite frame delta cannot poison physical position and velocity")
            let unchanged = scroll.position
            try scroll.lateUpdate(unscaledDelta: 0)
            check(scroll.position == unchanged, "Zero-delta host guard does not divide the velocity estimate by zero")
            scroll.reset()
            check(scroll.pointerPhase == .idle && scroll.position == 0 && scroll.velocity == 0 && !scroll.requiresFrames,
                  "Teardown resets all scroll capture and physical frame demand")
        } catch { fatalError("Source Banner scroll fixture: \(error)") }
        return count
    }
}
