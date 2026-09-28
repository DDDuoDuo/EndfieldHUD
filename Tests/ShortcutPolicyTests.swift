/// Pure registered-hotkey checks; never register a key or request permissions.
enum ShortcutPolicyTests {
    static func run() -> Int {
        var assertions = 0
        func expect<T: Equatable>(_ actual: T, _ expected: T, _ message: String,
                                  file: StaticString = #file, line: UInt = #line) {
            assertions += 1
            guard actual == expected else {
                fatalError("\(message): expected \(expected), got \(actual)", file: file, line: line)
            }
        }
        func input(_ phase: ShortcutPolicy.Phase = .down, key: UInt16 = 50,
                   control: Bool = true, others: Bool = false, repeated: Bool = false) -> ShortcutPolicy.Input {
            ShortcutPolicy.Input(keyCode: key, phase: phase,
                                 modifiers: others ? [.control, .shift] : (control ? [.control] : []), isRepeat: repeated)
        }
        var policy = ShortcutPolicy()
        expect(policy.decision(for: input(control: false)), .passThrough, "Plain backtick is ordinary input")
        expect(policy.decision(for: input(others: true)), .passThrough, "Other modifier combinations are not the binding")
        expect(policy.decision(for: input(key: 12)), .passThrough, "Unregistered keys are untouched")
        expect(policy.decision(for: input(repeated: true)), .passThrough, "An unowned repeat cannot initiate a toggle")
        expect(policy.decision(for: input()), .requestToggle, "Control plus backtick requests the overlay")
        expect(policy.ownsPress, true, "The registered chord owns its full press immediately")
        expect(policy.decision(for: input()), .consume, "Repeated Carbon pressed events do not retrigger")
        expect(policy.decision(for: input(repeated: true)), .consume, "Held-key repeat remains consumed")
        expect(policy.decision(for: input(control: false, others: true)), .consume, "Modifier changes do not split an owned press")
        expect(policy.decision(for: input(.up, key: 12)), .passThrough, "Unrelated releases do not affect ownership")
        expect(policy.ownsPress, true, "Unrelated key-up retains ownership")
        expect(policy.decision(for: input(.up, control: false)), .consume, "Release is paired even if Control was released first")
        expect(policy.ownsPress, false, "Release clears ownership")
        expect(policy.decision(for: input(.up)), .passThrough, "An unmatched release does not create ownership")
        expect(policy.decision(for: input()), .requestToggle, "A new press can close the overlay")
        // The caller may decline this request during animation, editing or sleep;
        // Carbon still reserves the chord and repeats must remain suppressed.
        expect(policy.decision(for: input()), .consume, "Declining an action cannot make repeats retry it")
        policy.reset()
        expect(policy.ownsPress, false, "Stopping registration clears stale held state")
        expect(policy.decision(for: input()), .requestToggle, "After re-registration a new press works")
        expect(policy.decision(for: input(.up)), .consume, "Re-registered press keeps its release paired")
        // There is deliberately no AX/text-field focus policy: Carbon delivers
        // the same registered chord from editors and other applications.
        for _ in 0..<25 {
            expect(policy.decision(for: input()), .requestToggle, "Repeated complete cycles remain usable")
            expect(policy.decision(for: input()), .consume, "Each physical press produces at most one action")
            expect(policy.decision(for: input(.up)), .consume, "Every owned cycle releases cleanly")
        }
        policy.reset()
        policy.shortcut = SummonShortcut(keyCode: 40, modifiers: [.control, .option])
        expect(policy.decision(for: input()), .passThrough, "Old binding no longer activates after replacement")
        let replacement = ShortcutPolicy.Input(keyCode: 40, phase: .down, modifiers: [.control, .option], isRepeat: false)
        expect(policy.decision(for: replacement), .requestToggle, "Any validated physical chord can activate")
        expect(policy.decision(for: replacement), .consume, "Replacement chord still suppresses repeats")
        expect(policy.decision(for: ShortcutPolicy.Input(keyCode: 40, phase: .up, modifiers: [], isRepeat: false)), .consume,
               "Custom chord releases even after modifiers change")
        return assertions
    }
}
