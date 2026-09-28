import Foundation

enum SystemOverlayStateTests {
    static func run() -> Int {
        var count = 0
        func expect(_ value: Bool) { count += 1; precondition(value) }
        var state = SystemOverlayState()
        expect(!state.isActive)
        for cycle in 1...1000 {
            expect(state.toggle() == .open(cycle))
            expect(state.phase == .opening)
            for _ in 0..<5 { expect(state.toggle() == .none) }
            expect(state.didOpen(cycle - 1) == .none && state.phase == .opening)
            expect(state.didOpen(cycle) == .none && state.phase == .open)
            expect(state.toggle() == .close(cycle))
            for _ in 0..<5 { expect(state.toggle() == .none) }
            expect(!state.didClose(cycle - 1) && state.phase == .closing)
            expect(state.didClose(cycle) && !state.isActive)
        }
        let next = state.generation + 1
        expect(state.toggle() == .open(next))
        expect(state.requestClose() == .none && state.closeAfterOpening)
        expect(state.didOpen(next) == .close(next))
        expect(state.didClose(next) && !state.isActive)
        _ = state.toggle()
        let interrupted = state.generation
        state.forceClose()
        expect(state.didOpen(interrupted) == .none && !state.isActive)
        _ = state.toggle()
        expect(state.didOpen(interrupted) == .none && state.phase == .opening)
        state.forceClose()
        expect(!state.didClose(interrupted) && !state.isActive)
        return count
    }
}
