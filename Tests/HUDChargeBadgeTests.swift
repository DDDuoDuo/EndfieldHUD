import AppKit
import QuartzCore

enum HUDChargeBadgeTests {
    private final class ManualClock {
        final class Job {
            let deadline: TimeInterval
            let action: () -> Void
            var canceled = false
            init(deadline: TimeInterval, action: @escaping () -> Void) { self.deadline = deadline; self.action = action }
        }
        var time: TimeInterval = 0
        var jobs: [Job] = []
        func schedule(_ delay: TimeInterval, _ action: @escaping () -> Void) -> (() -> Void) {
            let job = Job(deadline: time + delay, action: action)
            jobs.append(job)
            return { job.canceled = true }
        }
        func advance(_ interval: TimeInterval) {
            let end = time + interval
            while let job = jobs.filter({ !$0.canceled && $0.deadline <= end }).min(by: { $0.deadline < $1.deadline }) {
                job.canceled = true
                time = job.deadline
                job.action()
            }
            time = end
        }
    }

    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let previousConfiguration = HUDRuntimeAppearance.configuration
        defer { HUDRuntimeAppearance.configuration = previousConfiguration }
        let clock = ManualClock()
        let badge = HUDChargeBadge(scheduler: clock.schedule)
        let canvas = badge.layer.sublayers!.first!
        let retainedCanvas = ObjectIdentifier(canvas)
        let capacity = BatteryCapacityReading(current: 2400, maximum: 4800, unit: .milliampHours)
        let snapshot = BatterySnapshot(percentage: 50, isPluggedIn: true, isCharging: true,
                                       isFullyCharged: false, hasBattery: true, capacity: capacity)
        var config = AppConfiguration.defaults
        config.scale = 1.6
        config.accentHex = "6EDFE8"
        badge.update(snapshot: snapshot, configuration: config, dark: true, contentsScale: 4)
        badge.setStable()
        check(badge.stage == .compact && badge.contains(HUDChargeBadge.center), "Stable HUD charge control is immediately clickable")
        check(badge.hitRect.minY >= 428 && badge.hitRect.maxY <= 466
              && (210...220).contains(badge.hitRect.width) && (30...32).contains(badge.hitRect.height),
              "The enlarged battery capsule remains above the unchanged center buttons")
        check(!badge.contains(CGPoint(x: badge.hitRect.minX, y: badge.hitRect.minY))
              && !badge.contains(CGPoint(x: badge.hitRect.minX - 5, y: badge.hitRect.midY)),
              "Transparent canvas padding and rounded capsule corners do not steal input")
        check(canvas.superlayer === badge.layer && canvas.name == "hud.chargeBadge.notificationCanvas"
              && badge.layer.sublayers?.count == 1,
              "The badge adopts only the reusable notification canvas instead of nesting an AppKit backing layer")
        check(abs(canvas.contentsScale - 3.28) < 0.001 && abs(canvas.transform.m11 - 0.82) < 0.001,
              "The HUD supplies crisp rendering density without applying notification-size preferences")
        let textLayers = canvas.sublayers?.compactMap { $0 as? CATextLayer } ?? []
        let texts = textLayers.compactMap { ($0.string as? NSAttributedString)?.string }
        check(texts.contains("50%") && texts.contains(where: { $0.contains("2,400/4,800") }),
              "The embedded renderer shows actual capacity and percentage rather than preview data")
        check(badge.accessibilityLabel.contains("50%"), "The badge exposes its actual battery state for accessibility")
        badge.update(snapshot: .unavailable, configuration: config, dark: false, contentsScale: 2)
        let unavailableTexts = textLayers.compactMap { ($0.string as? NSAttributedString)?.string }
        check(unavailableTexts.contains("—") && unavailableTexts.contains("—/—"),
              "Unavailable batteries retain truthful unknown readings")
        let percentageColor = (textLayers.last?.string as? NSAttributedString)?.attribute(.foregroundColor, at: 0, effectiveRange: nil) as? NSColor
        check((percentageColor?.usingColorSpace(.sRGB)?.redComponent ?? 1) < 0.2,
              "The detached renderer uses the HUD's resolved light appearance")
        check(ObjectIdentifier(badge.layer.sublayers!.first!) == retainedCanvas,
              "Data and appearance changes retain the same rendering layer")

        let previousLanguage = L10n.language
        let notification = ChargeIndicatorView(frame: CGRect(origin: .zero, size: ChargeIndicatorView.canvasSize))
        notification.setStage(.supercharge)
        let notificationTexts = notification.embeddedContentLayer.sublayers?.compactMap { $0 as? CATextLayer } ?? []
        let languages: [(AppLanguage, String, String)] = [
            (.english, "CHARGE MODE", "BATTERY MODE"),
            (.simplifiedChinese, "超充模式", "电池模式"),
            (.traditionalChinese, "超充模式", "電池模式"),
            (.japanese, "充電モード", "バッテリーモード"),
            (.korean, "충전 모드", "배터리 모드")
        ]
        for (language, chargingTitle, batteryTitle) in languages {
            L10n.language = language
            for (plugged, charging, full) in [(true, true, false), (false, false, false), (true, false, true)] {
                let value = BatterySnapshot(percentage: full ? 100 : 50, isPluggedIn: plugged,
                                            isCharging: charging, isFullyCharged: full, hasBattery: true, capacity: capacity)
                notification.set(snapshot: value, configuration: config)
                badge.update(snapshot: value, configuration: config, dark: true, contentsScale: 2)
                for layers in [notificationTexts, textLayers] {
                    let subtitle = layers[0].string as! NSAttributedString
                    let title = layers[1].string as! NSAttributedString
                    check(subtitle.string == "// " + (charging ? "CHARGE MODE" : "BATTERY MODE")
                          && title.string == (charging ? chargingTitle : batteryTitle),
                          "Notification and HUD use the current charging state and selected language: \(language)")
                    check(title.size().width <= layers[1].bounds.width && title.size().height <= layers[1].bounds.height,
                          "Localized mode title fits the existing banner in \(language)")
                }
            }
        }
        L10n.language = previousLanguage

        var reduced = config; reduced.reduceMotion = true
        HUDRuntimeAppearance.configuration = reduced
        var completions = 0
        badge.animateEntrance { completions += 1 }
        check(badge.stage == .compact && badge.animationCount == 0 && completions == 1,
              "Reduced Motion reveals the compact badge and completes without animated tracks")
        clock.advance(2.99)
        check(badge.stage == .compact, "Compact remains visible for the complete fixed three-second hold")
        clock.advance(0.01)
        check(badge.stage == .circle && badge.animationCount == 0
              && abs(badge.hitRect.width - HUDChargeBadge.circleHitRect.width) < 0.001
              && abs(badge.hitRect.midX - HUDChargeBadge.center.x) < 0.001,
              "The fixed deadline collapses to a stationary circle under Reduced Motion")
        check(!badge.contains(CGPoint(x: HUDChargeBadge.compactHitRect.minX + 10, y: HUDChargeBadge.center.y)),
              "Collapsed badge does not claim the old capsule's empty horizontal area")
        let body = canvas.sublayers![1]
        let restingFill = body.backgroundColor
        badge.setHovered(true)
        check(badge.stage == .compact && badge.isHovered && badge.contains(CGPoint(x: HUDChargeBadge.compactHitRect.maxX + 1, y: HUDChargeBadge.center.y)),
              "Hover opens the capsule and supplies a small boundary allowance")
        let highlight = NSColor(cgColor: body.borderColor!)!.usingColorSpace(.sRGB)!
        check(body.borderWidth == 1.5 && abs(highlight.redComponent - 110.0 / 255) < 0.01
              && abs(highlight.greenComponent - 223.0 / 255) < 0.01 && abs(highlight.blueComponent - 232.0 / 255) < 0.01,
              "Hover outline follows the configured theme color")
        check(body.backgroundColor == restingFill, "Hover changes only the outline, not the battery fill")
        badge.setHovered(false)
        check(badge.stage == .circle && !badge.isHovered && body.borderWidth == 0.5,
              "Leaving after the deadline returns to the circle and clears its outline")
        badge.setStable()
        badge.setHovered(true)
        clock.advance(1)
        badge.setHovered(false)
        check(badge.stage == .compact, "Leaving during the initial hold does not shorten the fixed duration")
        badge.setHovered(true)
        clock.advance(2)
        check(badge.stage == .compact, "Hover keeps the capsule open beyond the deadline")
        badge.setHovered(false)
        check(badge.stage == .circle, "A held-open capsule shrinks immediately when hover ends")
        badge.animateExit { completions += 1 }
        check(badge.stage == .hidden && badge.animationCount == 0 && completions == 2 && !badge.contains(HUDChargeBadge.center),
              "Reduced Motion hides the badge and ends hit-testing synchronously")
        badge.setStable()
        badge.cancelAnimations()
        clock.advance(10)
        check(badge.stage == .compact, "Cancellation invalidates the pending collapse without mutating the visible stage")
        HUDRuntimeAppearance.configuration = .defaults
        if !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
            badge.animateEntrance()
            check(badge.stage == .hidden && badge.animationCount == 0 && !badge.contains(HUDChargeBadge.center),
                  "The battery opening waits until the HUD shell is already deployed")
            clock.advance(HUDChargeBadge.entranceDelay - 0.01)
            check(badge.stage == .hidden, "The delayed entrance cannot begin early")
            clock.advance(0.01)
            check(badge.stage == .circle && badge.animationCount > 0,
                  "The delayed reveal begins the notification renderer's original circle animation")
            check(canvas.animation(forKey: HUDDeploymentFlicker.animationKey) != nil,
                  "The delayed badge has its own brief deployment flicker after the shell animation")
            badge.cancelAnimations()
            check(badge.animationCount == 0, "Cancel removes all finite embedded charge animations")
            badge.animateEntrance { completions += 100 }
            badge.animateExit { completions += 1 }
            clock.advance(10)
            check(badge.stage == .hidden && completions == 3,
                  "Closing during the reveal delay cancels entrance and never flashes an exit circle")
        }
        badge.setStable(visible: false)
        check(badge.stage == .hidden && badge.animationCount == 0, "A hidden stable reset cannot retain an old presentation")
        return count
    }
}
