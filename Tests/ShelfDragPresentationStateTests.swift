import Foundation

enum ShelfDragPresentationStateTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool) { count += 1; precondition(condition) }
        for delivered in [false, true] {
            for early in [false, true] {
                var state = ShelfDragPresentationState()
                check(!state.isActive)
                guard case .retract(let token) = state.begin() else { fatalError("Missing retraction") }
                check(state.isActive && state.phase == .retracting)
                check(state.begin() == .none)
                let expected: ShelfDragPresentationState.Action = delivered ? .close : .restore(token)
                if early {
                    check(state.ended(delivered: delivered) == .none)
                    check(state.ended(delivered: !delivered) == .none)
                    check(state.didRetract(token) == expected)
                } else {
                    check(state.didRetract(token) == .hide)
                    check(state.phase == .hidden)
                    check(state.ended(delivered: delivered) == expected)
                }
                check(state.didRetract(token) == .none)
                if !delivered {
                    check(state.phase == .restoring)
                    check(!state.didRestore(token - 1))
                    check(state.didRestore(token))
                    check(!state.isActive)
                    check(!state.didRestore(token))
                }
                state.reset()
                check(!state.isActive)
                check(state.didRetract(token) == .none)
                check(state.ended(delivered: true) == .none)
            }
        }
        var interrupted = ShelfDragPresentationState()
        guard case .retract(let stale) = interrupted.begin() else { fatalError() }
        interrupted.reset()
        guard case .retract(let fresh) = interrupted.begin() else { fatalError() }
        check(stale != fresh)
        check(interrupted.didRetract(stale) == .none)
        check(interrupted.phase == .retracting)
        check(interrupted.didRetract(fresh) == .hide)
        return count
    }
}
