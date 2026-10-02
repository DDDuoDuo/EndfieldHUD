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
        L10n.language = .english
        view.setContent(title: "Keep this position?", message: "Reverts in 15s", cancel: "Revert", confirm: "Keep", focusConfirm: true)
        view.show()
        check(actions.map(\.title) == ["Revert", "Keep"] && window.firstResponder === actions[1],
              "Layout recovery shares the card while retaining Keep as its keyboard default")
        _ = view.handleKey(key(36))
        check(confirmed == 2, "Recovery Return confirms its preview")
        view.dismiss(animated: false); view.show()
        view.setPointer(CGPoint(x: 1, y: -1), parallax: 2, perspective: 2)
        let firstPose = view.cardTransformForVerification
        check(!CATransform3DIsIdentity(firstPose), "Confirmation follows the pointer")
        view.setPointer(CGPoint(x: -1, y: 1), parallax: 2, perspective: 2)
        check(!CATransform3DEqualToTransform(firstPose, view.cardTransformForVerification), "Opposite pointer positions produce opposite poses")
        let card = actions[0].superview!
        check(card.layer?.anchorPoint == CGPoint(x: 0.5, y: 0.5)
              && card.layer?.position == CGPoint(x: card.frame.midX, y: card.frame.midY),
              "AppKit's default corner anchor is replaced by a centered, compensated pivot")
        for button in actions {
            let plate = card.layer?.presentation() ?? card.layer!
            let actual = plate.convert(CGPoint(x: button.frame.midX, y: button.frame.midY), to: plate.superlayer)
            let expected = view.actionPointForVerification(confirm: button === actions[1])
            check(abs(actual.x - expected.x) < 0.0001 && abs(actual.y - expected.y) < 0.0001,
                  "The action location matches actual flipped Core Animation geometry")
            let screen = window.convertPoint(toScreen: view.convert(actual, to: nil))
            check(button.accessibilityFrame().contains(screen), "The accessible action frame contains its rendered center")
        }
        func click(confirm: Bool) {
            let button = confirm ? actions[1] : actions[0]
            let plate = card.layer?.presentation() ?? card.layer!
            let visible = plate.convert(CGPoint(x: button.frame.midX, y: button.frame.midY), to: plate.superlayer)
            let point = view.convert(visible, to: nil)
            let down = NSEvent.mouseEvent(with: .leftMouseDown, location: point, modifierFlags: [], timestamp: 0,
                windowNumber: window.windowNumber, context: nil, eventNumber: 1, clickCount: 1, pressure: 1)!
            let up = NSEvent.mouseEvent(with: .leftMouseUp, location: point, modifierFlags: [], timestamp: 0.01,
                windowNumber: window.windowNumber, context: nil, eventNumber: 2, clickCount: 1, pressure: 0)!
            view.mouseDown(with: down); view.mouseUp(with: up)
        }
        click(confirm: true)
        check(confirmed == 3, "Projected confirmation routes the visible button hit exactly once")
        view.dismiss(animated: false); view.show()
        view.setPointer(CGPoint(x: 1, y: 1), parallax: 2, perspective: 2)
        click(confirm: false)
        check(cancelled == 3, "Opposite projected action still routes to Cancel")
        view.dismiss(animated: false); view.show()
        reduceMotion = true
        view.setPointer(CGPoint(x: 1, y: -1), parallax: 2, perspective: 2)
        check(CATransform3DIsIdentity(view.cardTransformForVerification), "Reduce Motion removes modal pointer tilt")
        view.dismiss(animated: false)
        check(CATransform3DIsIdentity(view.cardTransformForVerification), "A dismissed card cannot retain a stale pointer pose")
        return count
    }
}
