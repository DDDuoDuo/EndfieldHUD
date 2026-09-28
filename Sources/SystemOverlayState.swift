import Foundation

enum SystemOverlayPhase: String { case closed, opening, open, closing }

/// Serialized transitions; external focus loss may queue a close during deployment.
/// Repeated shortcut strokes during either transition never create a second animation.
struct SystemOverlayState {
    enum Action: Equatable { case none, open(Int), close(Int) }
    private(set) var phase: SystemOverlayPhase = .closed
    private(set) var generation = 0
    private(set) var closeAfterOpening = false
    var isActive: Bool { phase != .closed }

    mutating func toggle() -> Action {
        switch phase {
        case .closed:
            generation += 1
            closeAfterOpening = false
            phase = .opening
            return .open(generation)
        case .open: return requestClose()
        case .opening, .closing: return .none
        }
    }

    mutating func requestClose() -> Action {
        switch phase {
        case .opening: closeAfterOpening = true; return .none
        case .open:
            phase = .closing
            return .close(generation)
        case .closed, .closing: return .none
        }
    }

    mutating func didOpen(_ token: Int) -> Action {
        guard token == generation, phase == .opening else { return .none }
        phase = .open
        if closeAfterOpening { return requestClose() }
        return .none
    }

    @discardableResult mutating func didClose(_ token: Int) -> Bool {
        guard token == generation, phase == .closing else { return false }
        phase = .closed
        closeAfterOpening = false
        return true
    }

    mutating func forceClose() {
        generation += 1
        phase = .closed
        closeAfterOpening = false
    }
}
