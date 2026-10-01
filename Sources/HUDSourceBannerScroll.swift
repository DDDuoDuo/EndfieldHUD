import Foundation
import simd

/// Original horizontal Banner ScrollRect stages. This does not own the Lua
/// hold clock, selected page, or UIStep's independent OutSine page tween.
/// Callers explicitly order input, normalized tween setters and LateUpdate;
/// their complete installed PlayerLoop ordering has not been observed.
struct HUDSourceBannerScroll {
    enum Failure: Error { case invalidGeometry, invalidInput, invalidDelta, invalidCenter }
    enum PointerPhase: Equatable { case idle, potential, dragging }
    enum DragResult: Equatable { case ignored, potential, began, dragged }
    struct Geometry {
        let pageCount: Int
        let viewWidth: Float
        let contentWidth: Float
        let cellWidth: Float
        var hiddenLength: Float { contentWidth - viewWidth }

        init(pageCount: Int, viewWidth: Float = 365, contentWidth: Float,
             cellWidth: Float = 360) throws {
            guard pageCount > 0, viewWidth.isFinite, contentWidth.isFinite,
                  cellWidth.isFinite, viewWidth > 0, cellWidth > 0,
                  contentWidth >= viewWidth else { throw Failure.invalidGeometry }
            self.pageCount = pageCount; self.viewWidth = viewWidth
            self.contentWidth = contentWidth; self.cellWidth = cellWidth
        }
    }
    struct EndSnapDecision {
        let targetIndex: Int
        let forceStep: Bool
        let forward: Bool
        let startCenterIndex: Int
    }

    let geometry: Geometry
    private(set) var position: Float = 0
    private(set) var velocity: Float = 0
    private(set) var pointerPhase: PointerPhase = .idle
    private var previousPosition: Float = 0
    private var pressPosition: SIMD2<Float>?
    private var dragStartLocalX: Float = 0
    private var dragStartPosition: Float = 0
    private var startCenter = 0

    static let dragThreshold: Float = 10
    static let lastDeltaThreshold: Float = 10
    static let dragDistanceRatio: Float = 0.30000001192092896
    static let rubberCoefficient: Float = 0.10000000149011612
    static let elasticity: Float = 0.10000000149011612
    static let decelerationRate: Float = 0.13500000536441803
    static let boundsTolerance: Float = 0.0010000000474974513
    static let normalizedSetterTolerance: Float = 0.009999999776482582

    init(geometry: Geometry) { self.geometry = geometry }

    /// Matches the source horizontal getter, including its content-fits branch.
    /// Overscroll is retained; it is not clamped to the range zero through one.
    var normalizedPosition: Float {
        if Double(geometry.viewWidth) + 0.01 < Double(geometry.contentWidth) {
            return -position / geometry.hiddenLength
        }
        return position < 0 ? 1 : 0
    }
    var isDragging: Bool { pointerPhase == .dragging }
    var requiresFrames: Bool { isDragging || velocity != 0 || offset(at: position) != 0 }

    /// ScrollRect.OnInitializePotentialDrag clears velocity without cancelling
    /// the list's page tween or disabling the EventSystem drag threshold.
    mutating func initializePotentialDrag(screenPosition: SIMD2<Float>) throws {
        guard Self.finite(screenPosition) else { throw Failure.invalidInput }
        pressPosition = screenPosition; pointerPhase = .potential; velocity = 0
    }

    /// screenPosition/frameDelta are Unity screen-pixel coordinates; the host
    /// supplies viewportLocalX via unbounded ray/plane projection using the
    /// current viewport transform and the source press camera. Ten pixels must
    /// not silently become ten AppKit points. frameDelta is the current input
    /// frame's delta, including zero, rather than the last nonzero movement.
    mutating func drag(screenPosition: SIMD2<Float>, frameDelta: SIMD2<Float>,
                       viewportLocalX: Float, currentCenter: Int) throws -> DragResult {
        guard Self.finite(screenPosition), Self.finite(frameDelta), viewportLocalX.isFinite else {
            throw Failure.invalidInput
        }
        try validateCenter(currentCenter)
        guard let pressPosition, pointerPhase != .idle else { return .ignored }
        // ProcessPointerButtonDrag exits before both BeginDrag and Drag when
        // the current input frame's Float delta has zero squared magnitude.
        let deltaSquared = frameDelta.x * frameDelta.x + frameDelta.y * frameDelta.y
        guard deltaSquared > 0 else { return pointerPhase == .potential ? .potential : .ignored }
        var began = false
        if pointerPhase == .potential {
            let displacement = screenPosition - pressPosition
            let squared = displacement.x * displacement.x + displacement.y * displacement.y
            let threshold = Double(Self.dragThreshold) * Double(Self.dragThreshold)
            guard Double(squared) >= threshold else { return .potential }
            // Native OnBeginDrag captures eventData.position at threshold time,
            // not pressPosition, so an in-bound first drag does not jump ten px.
            pointerPhase = .dragging; startCenter = currentCenter
            dragStartLocalX = viewportLocalX; dragStartPosition = position; began = true
        }
        var proposed = dragStartPosition + (viewportLocalX - dragStartLocalX)
        let correction = offset(at: proposed)
        proposed += correction
        if correction != 0 { proposed -= Self.rubberDelta(correction, viewWidth: geometry.viewWidth) }
        position = proposed
        return began ? .began : .dragged
    }

    /// Returns UIStep's decision without starting or owning its page tween.
    /// Native UIScrollRect.OnEndDrag leaves velocity intact; the later page
    /// tween's normalized setter may zero it when that setter moves the content.
    mutating func endDrag(screenPosition: SIMD2<Float>, frameDelta: SIMD2<Float>,
                          screenWidth: Float, panelRectWidth: Float,
                          currentCenter: Int) throws -> EndSnapDecision? {
        guard Self.finite(screenPosition), Self.finite(frameDelta), screenWidth.isFinite,
              panelRectWidth.isFinite, screenWidth > 0, panelRectWidth > 0 else { throw Failure.invalidInput }
        try validateCenter(currentCenter)
        guard pointerPhase == .dragging, let pressPosition else {
            cancelPointer(); return nil
        }
        let forceByDelta = abs(frameDelta.x) >= Self.lastDeltaThreshold
        var force = forceByDelta
        var forward = frameDelta.x < 0
        if !forceByDelta {
            let screenDistance = abs(screenPosition.x - pressPosition.x)
            let localDistance = screenDistance / screenWidth * panelRectWidth
            force = localDistance >= geometry.cellWidth * Self.dragDistanceRatio
            forward = pressPosition.x > screenPosition.x
        }
        var target = currentCenter
        if force && target == startCenter {
            target = min(geometry.pageCount - 1, max(0, target + (forward ? 1 : -1)))
        }
        let decision = EndSnapDecision(targetIndex: target, forceStep: force,
                                       forward: forward, startCenterIndex: startCenter)
        cancelPointer()
        return decision
    }

    /// Original SetNormalizedPosition changes only this axis, only when the
    /// resulting local-position difference exceeds Float .01, then zeros its
    /// velocity. The normalized input need not lie inside zero through one.
    @discardableResult
    mutating func setNormalizedPosition(_ normalized: Float) throws -> Bool {
        guard normalized.isFinite else { throw Failure.invalidInput }
        let desiredMin = -normalized * geometry.hiddenLength
        // Native setter adds the desired bounds-min displacement to the
        // current local position before subtracting the current bounds min.
        let next = (position + desiredMin) - position
        guard next.isFinite else { throw Failure.invalidInput }
        guard abs(position - next) > Self.normalizedSetterTolerance else { return false }
        position = next; velocity = 0
        return true
    }

    /// Original ScrollRect LateUpdate horizontal axis, using unscaled delta.
    /// Scrolling changes only spring smoothTime to elasticity*3. Nonpositive
    /// delta skips sampling as a host guard, rather than claiming Unity's zero
    /// delta/division behavior. Reduce Motion is an explicit Mac adaptation.
    mutating func lateUpdate(unscaledDelta: Float, scrolling: Bool = false,
                             reduceMotion: Bool = false) throws {
        guard unscaledDelta.isFinite, unscaledDelta >= 0 else { throw Failure.invalidDelta }
        guard unscaledDelta > 0 else { return }
        let correction = offset(at: position)
        if !isDragging {
            if reduceMotion {
                position += correction; velocity = 0
            } else if correction != 0 {
                let smoothTime = scrolling ? Self.elasticity * 3 : Self.elasticity
                position = Self.smoothDamp(current: position, target: position + correction,
                    velocity: &velocity, smoothTime: smoothTime, delta: unscaledDelta)
                if abs(velocity) < 1 { velocity = 0 }
            } else {
                // Installed LateUpdate calls a Float power implementation.
                // Cross-platform libm last-bit identity is not claimed here.
                velocity *= powf(Self.decelerationRate, unscaledDelta)
                if abs(velocity) < 1 { velocity = 0 }
                position += velocity * unscaledDelta
            }
        } else {
            let measured = (position - previousPosition) / unscaledDelta
            let t = min(1, max(0, unscaledDelta * 10))
            velocity = velocity + (measured - velocity) * t
        }
        previousPosition = position
    }

    /// Cancels input capture without inventing an OnEndDrag snap or advancing
    /// the separate page/Lua clocks. The host may choose reset for full teardown.
    mutating func cancelPointer() { pointerPhase = .idle; pressPosition = nil }
    /// ScrollRect.OnDisable clears dragging/scrolling and both-axis velocity.
    /// The co-located UIStep component snaps through a separate normalized
    /// setter; the original cross-component OnDisable order is unobserved.
    mutating func onDisable() { cancelPointer(); velocity = 0 }
    mutating func reset() {
        position = 0; previousPosition = 0; velocity = 0
        dragStartLocalX = 0; dragStartPosition = 0; startCenter = 0; cancelPointer()
    }

    private func validateCenter(_ value: Int) throws {
        guard value >= 0, value < geometry.pageCount else { throw Failure.invalidCenter }
    }
    private static func finite(_ point: SIMD2<Float>) -> Bool { point.x.isFinite && point.y.isFinite }
    /// Canonical horizontal content bounds, with the original +/- .001 dead
    /// zone. InternalCalculateOffset checks the start excess before the end
    /// deficit; the validated effective content width is at least view width.
    private func offset(at value: Float) -> Float {
        let startExcess = -value
        if startExcess < -Self.boundsTolerance { return startExcess }
        let endDeficit = geometry.viewWidth - (geometry.contentWidth + value)
        if endDeficit > Self.boundsTolerance { return endDeficit }
        return 0
    }
    static func rubberDelta(_ overStretch: Float, viewWidth: Float) -> Float {
        let scaled = abs(overStretch) * rubberCoefficient / viewWidth
        let magnitude = (1 - 1 / (scaled + 1)) * viewWidth
        return magnitude * (overStretch >= 0 ? 1 : -1)
    }
    /// Installed Mathf.SmoothDamp182f81350. The source caller supplies +Infinity
    /// maxSpeed, so its change clamp is inactive for these finite inputs.
    static func smoothDamp(current: Float, target: Float, velocity: inout Float,
                           smoothTime: Float, delta: Float) -> Float {
        let time = max(Float(0.00009999999747378752), smoothTime)
        let omega: Float = 2 / time
        let x = omega * delta
        var quadratic = x * Float(0.47999998927116394)
        quadratic *= x
        var cubic = x * Float(0.23499999940395355)
        cubic *= x; cubic *= x
        let denominator = quadratic + (x + 1) + cubic
        let decay: Float = 1 / denominator
        let change = current - target
        let temp = (change * omega + velocity) * delta
        let output = (change + temp) * decay + (current - change)
        velocity = (velocity - temp * omega) * decay
        if (target - current > 0) == (output > target) {
            velocity = (target - target) / delta
            return target
        }
        return output
    }
}
