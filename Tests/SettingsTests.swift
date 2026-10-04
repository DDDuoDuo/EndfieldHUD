import Foundation

enum SettingsTests {
    private final class TestTimer: HUDSettingsTimer {
        let action: () -> Void
        private(set) var invalidated = false
        init(action: @escaping () -> Void) { self.action = action }
        func invalidate() { invalidated = true }
        func fire() { if !invalidated { action() } }
    }

    static func run() -> Int {
        var count = 0
        func check(_ condition: @autoclosure () -> Bool, _ message: String) {
            count += 1
            precondition(condition(), message)
        }
        let suite = "EndfieldHUD.SettingsTests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        let oldLanguage = L10n.language
        defer {
            defaults.removePersistentDomain(forName: suite)
            L10n.language = oldLanguage
        }
        let first = ConfigurationStore(defaults: defaults)
        check(first.configuration == .defaults, "Fresh preferences are complete defaults")
        check(first.configuration.clockFormat == .twentyFourHour, "The header clock defaults to 24-hour time")
        check(first.configuration.hudScale == 1 && first.configuration.displayDuration == 3,
              "Fresh HUD scale and battery duration have requested defaults")
        check(abs(first.configuration.backgroundBrightness - 0.37) < 0.000001
              && first.configuration.blurAmount == 0.75,
              "Fresh appearance defaults are 37 percent brightness and 75 percent blur")
        check(first.configuration.ambientAnimation && first.configuration.launchAtLogin
              && first.configuration.closeOnFocusLost && first.configuration.openOnActiveDisplay,
              "Default system behavior is enabled without invoking OS services during construction")
        check(first.configuration.summonShortcut == .default && first.configuration.accentHex == "FAD41F",
              "Fresh shortcut and accent match the HUD defaults")

        defaults.removePersistentDomain(forName: suite)
        defaults.set("D9F36B", forKey: "accentHex")
        defaults.set(5, forKey: "displayDuration")
        defaults.set(1.35, forKey: "scale")
        defaults.set("simplifiedChinese", forKey: "language")
        let migrated = ConfigurationStore(defaults: defaults)
        check(migrated.configuration.accentHex == "FAD41F", "Legacy ignored accent default migrates to existing HUD yellow")
        check(migrated.configuration.displayDuration == 5 && migrated.configuration.scale == 1.35,
              "Migration preserves prior battery popup duration and scale")
        check(migrated.configuration.language == .simplifiedChinese && migrated.configuration.hudScale == 1,
              "Migration retains language and independently initializes HUD size")
        check(migrated.configuration.hudOffsetX == 0 && migrated.configuration.hudOffsetY == 0,
              "Existing installations retain their default HUD position without a migration jump")
        migrated.update({ var c = migrated.configuration; c.accentHex = "D9F36B"; return c }())
        check(ConfigurationStore(defaults: defaults).configuration.accentHex == "D9F36B",
              "An intentionally chosen old accent is not migrated repeatedly")
        defaults.removeObject(forKey: "hudSettingsSchemaVersion")
        defaults.set("01AEEF", forKey: "accentHex")
        check(ConfigurationStore(defaults: defaults).configuration.accentHex == "01AEEF",
              "Custom accent survives schema migration")

        var c = AppConfiguration.defaults
        c.hudScale = .nan
        c.hudOffsetX = .infinity
        c.hudOffsetY = .nan
        c.parallaxIntensity = .infinity
        c.perspectiveIntensity = -.infinity
        c.backgroundDarkness = .nan
        c.blurAmount = .infinity
        check(c.normalized.hudScale == 1 && c.normalized.parallaxIntensity == 1
              && c.normalized.perspectiveIntensity == 1 && c.normalized.backgroundDarkness == 0.63
              && c.normalized.blurAmount == 0.75, "Nonfinite display inputs cannot reach projection or filter math")
        check(c.normalized.hudOffsetX == 0 && c.normalized.hudOffsetY == 0,
              "Nonfinite HUD position offsets fall back to the safe center")
        c.hudScale = -4
        c.hudOffsetX = -2
        c.hudOffsetY = 2
        c.parallaxIntensity = -4
        c.perspectiveIntensity = 8
        c.backgroundDarkness = 8
        c.blurAmount = -4
        check(c.normalized.hudScale == 0.2 && c.normalized.parallaxIntensity == 0
              && c.normalized.perspectiveIntensity == 2 && c.normalized.backgroundDarkness == 1
              && c.normalized.blurAmount == 0, "Display ranges clamp independently")
        check(c.normalized.hudOffsetX == -0.5 && c.normalized.hudOffsetY == 0.5,
              "HUD offsets are bounded independently to half the display size")
        c.hudScale = 5
        c.parallaxIntensity = 3
        c.perspectiveIntensity = -1
        c.backgroundDarkness = -1
        c.blurAmount = 3
        check(c.normalized.hudScale == 2 && c.normalized.parallaxIntensity == 2
              && c.normalized.perspectiveIntensity == 0 && c.normalized.backgroundDarkness == 0
              && c.normalized.blurAmount == 1, "All opposite display endpoints clamp safely")

        defaults.removePersistentDomain(forName: suite)
        let store = ConfigurationStore(defaults: defaults)
        c = .defaults
        c.hudScale = 1.15
        c.hudOffsetX = 0.1
        c.hudOffsetY = -0.15
        c.parallaxIntensity = 0.6
        c.perspectiveIntensity = 1.4
        c.backgroundDarkness = 0.85
        c.blurAmount = 0.45
        c.reduceMotion = true
        c.ambientAnimation = false
        c.closeOnFocusLost = false
        c.openOnActiveDisplay = false
        c.launchAtLogin = false
        c.batteryAlertsEnabled = false
        c.devicePopupEnabled = false
        c.lowPowerVisualMode = true
        c.clockFormat = .twelveHour
        c.summonShortcut = SummonShortcut(keyCode: 40, modifiers: [.control, .option])
        store.update(c)
        check(ConfigurationStore(defaults: defaults).configuration == c,
              "Every added display/system/shortcut preference round-trips together")
        defaults.set("unsupported", forKey: "clockFormat")
        check(ConfigurationStore(defaults: defaults).configuration.clockFormat == .twentyFourHour,
              "Unknown clock formats recover to 24-hour time")
        defaults.set(Data([0xFF, 0x00]), forKey: "summonShortcut")
        check(ConfigurationStore(defaults: defaults).configuration.summonShortcut == .default,
              "Corrupt shortcut data recovers to a usable default")
        defaults.set(try! JSONEncoder().encode(SummonShortcut(keyCode: 50, modifiers: [])), forKey: "summonShortcut")
        check(ConfigurationStore(defaults: defaults).configuration.summonShortcut == .default,
              "Invalid decoded shortcut cannot disable safe access to the HUD")
        let fixedDisplay = "A0000000-1111-2222-3333-444444444444"
        var fixedConfig = store.configuration
        fixedConfig.hudDisplayUUID = fixedDisplay
        fixedConfig.hudDisplayName = "Studio Display"
        store.update(fixedConfig)
        let restoredDisplay = ConfigurationStore(defaults: defaults).configuration
        check(restoredDisplay.hudDisplayUUID == fixedDisplay && restoredDisplay.hudDisplayName == "Studio Display",
              "A fixed display UUID and name persist across launches")
        defaults.set("not-a-display", forKey: "hudDisplayUUID")
        check(ConfigurationStore(defaults: defaults).configuration.hudDisplayUUID == nil
              && ConfigurationStore(defaults: defaults).configuration.hudDisplayName == nil,
              "Corrupt display targets fall back to the legacy automatic preference")
        store.update(.defaults)
        check(ConfigurationStore(defaults: defaults).configuration.hudDisplayUUID == nil
              && ConfigurationStore(defaults: defaults).configuration.openOnActiveDisplay,
              "Restore defaults clears a fixed display and restores pointer selection")

        var now: TimeInterval = 100
        var timers: [TestTimer] = []
        let controller = HUDSettingsController(store: store, clock: { now }, scheduleTimer: { _, action in
            let timer = TestTimer(action: action)
            timers.append(timer)
            return timer
        })
        var changed: [AppConfiguration] = []
        controller.onConfigurationChange = { changed.append($0) }
        var subscriberA = 0, subscriberB = 0
        let tokenA = controller.addObserver { subscriberA += 1 }
        _ = controller.addObserver { subscriberB += 1 }
        controller.previewScale(2)
        check(controller.configuration.hudScale == 2 && controller.scaleConfirmationRemaining == 12,
              "Large UI preview applies immediately with visible countdown")
        check(store.configuration.hudScale == 1
              && ConfigurationStore(defaults: defaults).configuration.hudScale == 1,
              "Unconfirmed UI scale never reaches persisted preferences")
        check(changed.last?.hudScale == 2 && subscriberA == 1 && subscriberB == 1,
              "Root and all settings canvases receive preview state once")
        controller.update { $0.backgroundDarkness = 0.4 }
        check(store.configuration.backgroundDarkness == 0.4 && store.configuration.hudScale == 1
              && controller.configuration.hudScale == 2,
              "Unrelated changes during preview save normally without committing preview size")
        now += 5.25
        timers.last?.fire()
        check(controller.scaleConfirmationRemaining == 7 && controller.configuration.hudScale == 2,
              "Countdown uses elapsed time rather than assuming timer frequency")
        controller.confirmScale()
        check(store.configuration.hudScale == 2 && controller.scaleConfirmationRemaining == nil,
              "Confirmation persists requested size and removes the guard")
        check(timers.last?.invalidated == true, "Confirmation disposes the only countdown timer")
        check(ConfigurationStore(defaults: defaults).configuration.hudScale == 2,
              "Confirmed size survives relaunch")

        controller.previewScale(0.2)
        let staleTimer = timers.last!
        now += 4
        controller.previewScale(0.3)
        check(staleTimer.invalidated && controller.scaleConfirmationRemaining == 12,
              "Changing preview size replaces its timer and starts a fresh safety deadline")
        now += 12.1
        timers.last?.fire()
        check(controller.configuration.hudScale == 2 && controller.scaleConfirmationRemaining == nil
              && controller.status != nil, "Timeout returns to last confirmed scale and reports restoration")
        check(store.configuration.hudScale == 2 && timers.allSatisfy(\.invalidated),
              "Rollback changes no persisted size and leaves no active safety timer")
        controller.previewScale(0.25)
        now += 12
        controller.confirmScale()
        check(controller.configuration.hudScale == 2 && store.configuration.hudScale == 2,
              "A click queued past the deadline cannot commit an expired preview")
        controller.previewScale(1.5)
        controller.close()
        check(controller.configuration.hudScale == 2 && controller.scaleConfirmationRemaining == nil,
              "Closing HUD restores the safe size immediately")
        controller.update { $0.hudScale = 0.2 }
        check(controller.configuration.hudScale == 0.2 && store.configuration.hudScale == 2,
              "Generic display edits also route UI size through confirmation")
        controller.revertScale()
        check(controller.configuration.hudScale == 2, "Explicit discard preserves the last confirmed scale")
        controller.previewScale(1.6)
        controller.previewScale(2)
        check(controller.scaleConfirmationRemaining == nil && timers.last?.invalidated == true,
              "Returning a slider to saved size cancels unnecessary confirmation")

        controller.previewPosition(x: 0.35, y: -0.2)
        check(controller.configuration.hudOffsetX == 0.35 && controller.configuration.hudOffsetY == -0.2
              && controller.layoutConfirmationRemaining == 12 && controller.isPositionPreviewPending && !controller.isScalePreviewPending,
              "Position-only preview applies both axes and receives the same independent recovery guard")
        check(store.configuration.hudOffsetX == 0 && store.configuration.hudOffsetY == 0
              && ConfigurationStore(defaults: defaults).configuration.hudOffsetX == 0,
              "Unconfirmed position is absent from persisted preferences and relaunch state")
        controller.update { $0.blurAmount = 0.2 }
        check(store.configuration.blurAmount == 0.2 && store.configuration.hudOffsetX == 0
              && controller.configuration.hudOffsetX == 0.35,
              "Changing unrelated display preferences cannot accidentally commit position preview")
        _ = controller.setShortcut(.default)
        check(store.configuration.hudOffsetX == 0 && store.configuration.hudOffsetY == 0
              && controller.configuration.hudOffsetY == -0.2,
              "Shortcut changes also retain the confirmed layout on disk")
        now += 12.1
        timers.last?.fire()
        check(controller.configuration.hudOffsetX == 0 && controller.configuration.hudOffsetY == 0
              && controller.layoutConfirmationRemaining == nil && !controller.isPositionPreviewPending,
              "Position timeout restores both axes and releases the recovery control")

        controller.update { $0.hudScale = 1.25; $0.hudOffsetX = 0.1; $0.hudOffsetY = 0.2 }
        check(controller.isScalePreviewPending && controller.isPositionPreviewPending
              && store.configuration.hudScale == 2 && store.configuration.hudOffsetX == 0,
              "Generic layout changes combine scale and position into one uncommitted transaction")
        controller.confirmLayout()
        check(store.configuration.hudScale == 1.25 && store.configuration.hudOffsetX == 0.1
              && store.configuration.hudOffsetY == 0.2 && controller.layoutConfirmationRemaining == nil,
              "Confirmation saves both position axes and size atomically")
        check(ConfigurationStore(defaults: defaults).configuration == store.configuration,
              "Confirmed position and scale survive a fresh configuration store")
        controller.previewPosition(x: -0.5, y: 0.5)
        controller.close()
        check(controller.configuration.hudOffsetX == 0.1 && controller.configuration.hudOffsetY == 0.2
              && controller.configuration.hudScale == 1.25,
              "HUD close restores the last confirmed layout after a position preview")
        controller.previewScale(0.7)
        controller.previewPosition(x: 0.3, y: 0.4)
        controller.revertScale()
        check(controller.configuration.hudScale == 1.25 && controller.configuration.hudOffsetX == 0.1
              && controller.configuration.hudOffsetY == 0.2 && controller.layoutConfirmationRemaining == nil,
              "Legacy safety actions discard the entire pending layout together")
        controller.previewScale(0.6)
        controller.previewPosition(x: 0.4, y: 0.4)
        controller.previewPosition(x: 0.1, y: 0.2)
        check(controller.isScalePreviewPending && !controller.isPositionPreviewPending
              && controller.configuration.hudScale == 0.6 && controller.layoutConfirmationRemaining != nil,
              "Returning position to saved values keeps an independent pending size change guarded")
        controller.confirmScale()
        check(store.configuration.hudScale == 0.6 && store.configuration.hudOffsetX == 0.1
              && store.configuration.hudOffsetY == 0.2,
              "Legacy confirmation commits the combined-layout transaction correctly")

        let beforeA = subscriberA
        controller.removeObserver(tokenA)
        controller.update { $0.ambientAnimation = false }
        check(subscriberA == beforeA && subscriberB > beforeA,
              "Independent canvases can unsubscribe without disabling other observers")
        let previousUpdates = changed.count
        controller.update { $0.ambientAnimation = false }
        check(changed.count == previousUpdates, "No-op changes do not reapply runtime configuration")
        var external = store.configuration
        external.perspectiveIntensity = 0.8
        store.update(external)
        check(controller.configuration.perspectiveIntensity == 0.8 && changed.last?.perspectiveIntensity == 0.8,
              "Changes from legacy or external settings are visible in every HUD category")

        var loginRequests: [Bool] = []
        controller.onLaunchAtLoginChange = { value in loginRequests.append(value); return "Registration unavailable" }
        controller.update { $0.launchAtLogin = false; $0.reduceMotion = true }
        check(loginRequests == [false] && store.configuration.launchAtLogin,
              "Failed login registration does not lie by saving the requested state")
        check(store.configuration.reduceMotion && controller.status == "Registration unavailable",
              "A platform failure leaves independent display edits intact and supplies actionable status")
        controller.onLaunchAtLoginChange = { value in loginRequests.append(value); return nil }
        controller.loginStatusProvider = { "Enabled in macOS" }
        controller.refreshExternalStatus()
        check(controller.loginStatus == "Enabled in macOS", "OS login state is reported through an injected live provider")
        let afterExternalStatus = subscriberB
        controller.refreshExternalStatus()
        check(subscriberB == afterExternalStatus,
              "Reading unchanged external status does not republish every retained settings canvas")
        controller.update { $0.launchAtLogin = false }
        check(!store.configuration.launchAtLogin && loginRequests == [false, false],
              "Successful login operation saves preference once")

        let customShortcut = SummonShortcut(keyCode: 40, modifiers: [.control, .option])
        var shortcutRequests = 0
        controller.onShortcutChange = { _ in shortcutRequests += 1; return "Shortcut is already registered" }
        check(controller.setShortcut(customShortcut) == "Shortcut is already registered"
              && store.configuration.summonShortcut == .default && shortcutRequests == 1,
              "Shortcut conflicts report failure while retaining old usable chord")
        let invalidShortcut = SummonShortcut(keyCode: 40, modifiers: [])
        check(controller.setShortcut(invalidShortcut) != nil && shortcutRequests == 1,
              "Invalid shortcuts are rejected before platform registration")
        controller.onShortcutChange = { _ in shortcutRequests += 1; return nil }
        check(controller.setShortcut(customShortcut) == nil && store.configuration.summonShortcut == customShortcut,
              "Accepted shortcut is committed only after successful registration")
        let registrationCount = shortcutRequests
        controller.onShortcutChange = { _ in shortcutRequests += 1; return "Same stored chord is unavailable" }
        check(controller.setShortcut(customShortcut) == "Same stored chord is unavailable"
              && shortcutRequests == registrationCount + 1 && store.configuration.summonShortcut == customShortcut,
              "Explicit retry rechecks an unchanged stored shortcut and reports registration failure")
        controller.onShortcutChange = { _ in shortcutRequests += 1; return nil }
        check(controller.setShortcut(customShortcut) == nil && shortcutRequests == registrationCount + 2,
              "Successful unchanged-chord retry registers exactly once")
        controller.shortcutRegistrationStatusProvider = { "Global shortcut unavailable" }
        controller.refreshExternalStatus()
        check(controller.shortcutStatus == "Global shortcut unavailable",
              "Startup registration failures can be shown without mutating saved preferences")
        controller.shortcutRegistrationStatusProvider = { nil }
        controller.refreshExternalStatus()
        check(controller.shortcutStatus == nil, "Successful platform retry clears stale registration status")
        var captureEvents: [Bool] = []
        controller.onShortcutCaptureChange = { captureEvents.append($0) }
        controller.beginShortcutCapture()
        controller.beginShortcutCapture()
        check(controller.isCapturingShortcut && captureEvents == [true], "Capture starts once despite repeated clicks")
        controller.close()
        check(!controller.isCapturingShortcut && captureEvents == [true, false],
              "Closing settings resumes ordinary global shortcut handling")

        defaults.set("separate-user-data", forKey: "notes-test-sentinel")
        controller.previewScale(0.2)
        controller.restoreDefaults()
        check(store.configuration == .defaults && controller.configuration == .defaults
              && controller.scaleConfirmationRemaining == nil,
              "Restore defaults safely resets display, system and shortcut settings together")
        check(defaults.string(forKey: "notes-test-sentinel") == "separate-user-data",
              "Restore defaults does not clear unrelated user data")
        check(timers.allSatisfy(\.invalidated), "Every preview timer is finished by end of settings lifecycle")
        let registrationsBeforeRestore = shortcutRequests
        controller.onShortcutChange = { _ in shortcutRequests += 1; return "Default shortcut conflict" }
        controller.restoreDefaults()
        check(shortcutRequests == registrationsBeforeRestore + 1 && controller.shortcutStatus == "Default shortcut conflict"
              && store.configuration.summonShortcut == .default,
              "Restore defaults revalidates its unchanged chord instead of claiming registration succeeded")
        check(controller.about.name == "EndfieldHUD" && controller.about.repositoryURL == nil,
              "About uses renamed app and does not invent a GitHub repository")
        check(controller.about.author == "DDDuoDuo" && controller.about.licenseName == "MIT",
              "About uses the supplied author and concise license label")
        L10n.language = .english
        check(controller.about.credits.first?.name == "Arknights: Endfield"
              && controller.about.credits.first?.role == "Unofficial fan project",
              "The game credit appears first with only the requested fan-project role")
        check(controller.about.credits[1].role == "Inspiration" && controller.about.credits[2].role == "Inspiration",
              "Both original creators receive the simplified inspiration credit")
        L10n.language = .simplifiedChinese
        check(controller.about.credits.first?.role == "非官方同人项目"
              && controller.about.credits[1].role == "灵感来源" && controller.about.credits[2].role == "灵感来源",
              "The revised About roles are localized consistently")
        check(controller.about.credits.count == 4 && controller.presetAccentHexes.count == 5,
              "About preserves known references and Display supplies five theme presets")
        return count
    }
}
