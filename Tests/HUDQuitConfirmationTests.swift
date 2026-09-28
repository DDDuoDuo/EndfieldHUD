import AppKit
import QuartzCore

enum HUDQuitConfirmationTests {
    static func run() -> Int {
        _ = NSApplication.shared
        var count = 0
        func check(_ condition: @autoclosure () -> Bool, _ message: String) {
            count += 1; precondition(condition(), message)
        }
        let language = L10n.language
        L10n.language = .english
        defer { L10n.language = language }
        var reduceMotion = true
        let view = HUDQuitConfirmationView(frame: CGRect(x: 0, y: 0, width: 900, height: 650), reduceMotion: { reduceMotion })
        let window = NSWindow(contentRect: view.frame, styleMask: .borderless, backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false
        window.contentView = view
        defer { window.contentView = nil; window.close() }
        check(!view.isPresented && view.isHidden, "Quit confirmation starts inactive")
        check(view.hitTest(CGPoint(x: 20, y: 20)) == nil, "Hidden confirmation cannot intercept the HUD")
        func key(_ code: UInt16, repeated: Bool = false) -> NSEvent {
            NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0,
                windowNumber: window.windowNumber, context: nil, characters: "", charactersIgnoringModifiers: "", isARepeat: repeated, keyCode: code)!
        }
        check(!view.handleKey(key(36)), "Inactive confirmation leaves keyboard routing unchanged")
        var cancelled = 0, confirmed = 0, pointerUpdates = 0
        view.onCancel = { cancelled += 1 }
        view.onConfirm = { confirmed += 1 }
        view.onPointerMove = { pointerUpdates += 1 }
        view.show()
        check(view.isPresented && !view.isHidden, "Showing activates the retained view")
        check(view.hitTest(CGPoint(x: 20, y: 20)) === view, "The entire scrim consumes background clicks")
        func buttons(_ parent: NSView) -> [NSButton] {
            parent.subviews.flatMap { ($0 as? NSButton).map { [$0] } ?? buttons($0) }
        }
        let actions = buttons(view)
        check(actions.count == 2 && actions.map(\.title) == ["Cancel", "Quit"], "Exactly two labeled native accessible actions are available")
        check(actions[0].accessibilityLabel() == "Cancel quitting EndfieldHUD"
              && actions[1].accessibilityLabel() == "Quit EndfieldHUD application", "Accessibility distinguishes application exit from overlay close")
        check(window.firstResponder === actions[0], "Cancel receives default keyboard focus")
        check(view.layer?.animationKeys()?.isEmpty != false, "Reduce Motion skips entrance animations")
        _ = view.handleKey(key(36))
        check(cancelled == 1 && confirmed == 0, "Return initially cancels and cannot accidentally quit")
        view.confirm()
        check(confirmed == 0, "A submitted response blocks later duplicate callbacks")
        view.dismiss(animated: false)
        check(!view.isPresented && view.isHidden, "Immediate dismissal unblocks the underlying HUD")
        view.show()
        _ = view.handleKey(key(48)); _ = view.handleKey(key(36))
        check(confirmed == 1, "Moving focus to Quit then pressing Return deliberately confirms")
        view.confirm(); view.cancel()
        check(confirmed == 1 && cancelled == 1, "Actions are emitted only once per presentation")
        view.dismiss(animated: false); view.show()
        _ = view.handleKey(key(53))
        check(cancelled == 2, "Escape cancels the quit request")
        view.dismiss(animated: false); view.show()
        check(view.handleKey(key(12)) && cancelled == 2 && confirmed == 1, "Other keys are consumed without affecting background sections")
        _ = view.handleKey(key(36, repeated: true))
        check(cancelled == 2, "Held keys cannot trigger confirmation actions")
        view.mouseMoved(with: key(0))
        check(pointerUpdates == 1, "Pointer movement is forwarded so HUD tilt can continue")
        L10n.language = .simplifiedChinese
        view.configure(dark: false, accent: .systemPurple)
        check(actions.map(\.title) == ["取消", "退出"], "Confirmation follows current language and theme")
        view.dismiss(animated: false)
        reduceMotion = false; view.show()
        check(view.layer?.animation(forKey: "quit.reveal") != nil, "Normal motion adds the short reveal")
        var completions = 0
        view.dismiss { completions += 1 }
        check(view.isPresented && view.hitTest(CGPoint(x: 20, y: 20)) != nil,
              "The scrim keeps blocking while dismissal is animating")
        view.show()
        RunLoop.current.run(until: Date().addingTimeInterval(0.18))
        check(view.isPresented && !view.isHidden && completions == 0,
              "A newer presentation invalidates old dismissal completion")
        view.dismiss(animated: false) { completions += 1 }
        check(!view.isPresented && completions == 1, "Current dismissal completes exactly once")
        return count
    }
}
