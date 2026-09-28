/// The native drag owns its source until it ends. Its result may arrive before
/// or after the HUD has retracted; neither case may skip that animation.
struct ShelfDragPresentationState {
    enum Phase { case idle, retracting, hidden, restoring }
    enum Action: Equatable { case none, retract(Int), hide, restore(Int), close }
    private(set) var phase: Phase = .idle
    private(set) var generation = 0
    private var delivered: Bool?
    var isActive: Bool { phase != .idle }

    mutating func begin() -> Action {
        guard phase == .idle else { return .none }
        generation += 1
        delivered = nil
        phase = .retracting
        return .retract(generation)
    }

    mutating func ended(delivered: Bool) -> Action {
        guard phase == .retracting || phase == .hidden, self.delivered == nil else { return .none }
        self.delivered = delivered
        return phase == .hidden ? resolve() : .none
    }

    mutating func didRetract(_ token: Int) -> Action {
        guard token == generation, phase == .retracting else { return .none }
        phase = .hidden
        return delivered == nil ? .hide : resolve()
    }

    private mutating func resolve() -> Action {
        if delivered == true { return .close }
        phase = .restoring
        return .restore(generation)
    }

    mutating func didRestore(_ token: Int) -> Bool {
        guard token == generation, phase == .restoring else { return false }
        reset()
        return true
    }

    mutating func reset() {
        generation += 1
        phase = .idle
        delivered = nil
    }
}
