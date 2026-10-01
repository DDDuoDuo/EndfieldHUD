import Foundation
import AppKit
import IOKit.ps

@main
enum CoreTests {
    static func main() {
        assertionCount += LocalizationTests.run()
        testPowerSourceNormalization()
        testCapacityReadings()
        testBatteryLevelTones()
        testOverlayGeometry()
        testDisplayPolicy()
        testConfigurationPersistence()
        assertionCount += SettingsTests.run() + HUDSettingsCanvasTests.run() + HUDDisplayPolicyTests.run()
        assertionCount += HUDGitHubReleaseTests.run()
        assertionCount += HUDUpdateStateTests.run()
        assertionCount += HUDClockTests.run()
        assertionCount += PowerDataTests.run()
        assertionCount += ShortcutPolicyTests.run()
        assertionCount += SummonShortcutTests.run()
        assertionCount += SystemOverlayStateTests.run() + ShelfDragPresentationStateTests.run() + HUDMotionTests.run()
        assertionCount += HUDMechanicalArtworkTests.run() + HUDDeploymentFlickerTests.run() + HUDControlHighlightTests.run()
        assertionCount += HUDWatchArtworkTests.run()
        assertionCount += HUDSourceSceneTests.run()
        assertionCount += HUDSourceWatchAnimationTests.run()
        assertionCount += HUDSourceImageGeometryTests.run()
        assertionCount += HUDSourceTextGeometryTests.run()
        assertionCount += HUDSourceWatchLayoutTests.run()
        assertionCount += HUDSourceRectClippingTests.run()
        assertionCount += HUDSourceCanvasSortingTests.run()
        assertionCount += HUDSourceWatchButtonAnimationTests.run()
        assertionCount += HUDSourceWatchDomainTests.run()
        assertionCount += HUDSourceWatchCameraTests.run()
        assertionCount += HUDChargeBadgeTests.run()
        assertionCount += HUDModuleTests.run()
        assertionCount += HUDNavigationTests.run()
        assertionCount += NotesStoreTests.run()
        assertionCount += NotesCanvasTests.run()
        assertionCount += FileShelfStoreTests.run()
        assertionCount += FileShelfCanvasTests.run()
        assertionCount += HUDFileShelfInteractionTests.run()
        assertionCount += AppShortcutStoreTests.run()
        assertionCount += ClipboardStoreTests.run()
        assertionCount += ClipboardWatcherTests.run()
        assertionCount += ClipboardCanvasTests.run()
        assertionCount += AudioDeviceControllerTests.run()
        assertionCount += PerAppAudioTests.run()
        assertionCount += VolumeCanvasTests.run()
        assertionCount += SystemActivityMonitorTests.run()
        assertionCount += AppActivityMonitorTests.run()
        assertionCount += SystemStorageSettingsTests.run()
        assertionCount += StorageControllerTests.run()
        assertionCount += TelemetryCanvasTests.run()
        assertionCount += TelemetryGraphTimeTests.run()
        assertionCount += WorkModeTests.run()
        assertionCount += UserProfileStoreTests.run()
        assertionCount += WorkModeFocusTests.run()
        assertionCount += SystemEventLogTests.run()
        assertionCount += SystemEventRecorderTests.run()
        assertionCount += AudioTopologyWatcherTests.run()
        assertionCount += EventLogCanvasTests.run()
        assertionCount += WorldMapStoreTests.run() + WorldMapGeometryTests.run() + WorldMapCanvasTests.run()
        assertionCount += WorldMapTerrainTests.run()
        assertionCount += WorldMapCountriesTests.run() + WorldMapLayerCoordinatesTests.run() + WorldMapPathGeometryTests.run()
        assertionCount += WorldMapRasterPainterTests.run() + WorldMapRasterControllerTests.run()
        assertionCount += HUDSubsectionTransitionTests.run()
        // The native name-editor checks bootstrap NSApplication and its shared
        // field editor. Run them after bounded model/dispatch timing checks so
        // AppKit's global startup and window cleanup cannot consume their drain.
        assertionCount += HUDCanvasLifecycleTests.run()
        assertionCount += HUDNotesInteractionTests.run()
        assertionCount += AppShortcutCanvasTests.run()
        assertionCount += PersonalProfileCanvasTests.run()
        assertionCount += HUDApplicationIconTests.run()
        assertionCount += HUDQuitConfirmationTests.run()
        print("Passed \(assertionCount) EndfieldCharge core assertions.")
    }

    private static var assertionCount = 0

    private static func expect<T: Equatable>(
        _ actual: T,
        _ expected: T,
        _ message: String,
        file: StaticString = #file,
        line: UInt = #line
    ) {
        assertionCount += 1
        guard actual == expected else {
            fatalError("\(message): expected \(expected), received \(actual)", file: file, line: line)
        }
    }

    private static func source(
        current: Double = 50,
        maximum: Double = 100,
        pluggedIn: Bool = false,
        charging: Bool = false
    ) -> [String: Any] {
        [
            kIOPSTypeKey: kIOPSInternalBatteryType,
            kIOPSTransportTypeKey: kIOPSInternalType,
            kIOPSIsPresentKey: true,
            kIOPSCurrentCapacityKey: current,
            kIOPSMaxCapacityKey: maximum,
            kIOPSPowerSourceStateKey: pluggedIn ? kIOPSACPowerValue : kIOPSBatteryPowerValue,
            kIOPSIsChargingKey: charging
        ]
    }

    private static func snapshot(
        percentage: Int = 50,
        pluggedIn: Bool,
        charging: Bool
    ) -> BatterySnapshot {
        BatterySnapshot(
            percentage: percentage,
            isPluggedIn: pluggedIn,
            isCharging: charging,
            isFullyCharged: pluggedIn && !charging && percentage == 100,
            hasBattery: true
        )
    }

    private static func testPowerSourceNormalization() {
        expect(BatterySnapshot.fromPowerSources([]), .unavailable, "Desktop without batteries")

        var ups = source()
        ups[kIOPSTypeKey] = kIOPSUPSType
        expect(BatterySnapshot.fromPowerSources([ups]), .unavailable, "UPS is not a laptop battery")

        var absent = source()
        absent[kIOPSIsPresentKey] = false
        expect(BatterySnapshot.fromPowerSources([absent]), .unavailable, "Removed battery is skipped")
        expect(BatterySnapshot.fromPowerSources([ups, absent, source(current: 37)]).percentage, 37,
               "Select a present internal battery after external/absent sources")

        var legacy = source()
        legacy.removeValue(forKey: kIOPSTypeKey)
        expect(BatterySnapshot.fromPowerSources([legacy]).hasBattery, true, "Legacy internal transport fallback")
        legacy.removeValue(forKey: kIOPSTransportTypeKey)
        expect(BatterySnapshot.fromPowerSources([legacy]), .unavailable, "Unidentified power source is not guessed")

        expect(BatterySnapshot.fromPowerSources([source(current: 2750, maximum: 5000)]).percentage, 55,
               "Capacity units are normalized to percent")
        expect(BatterySnapshot.fromPowerSources([source(current: 120)]).percentage, 100, "Overfull capacity clamps")
        expect(BatterySnapshot.fromPowerSources([source(current: 0)]).percentage, 0, "Empty capacity is valid")
        expect(BatterySnapshot.fromPowerSources([source(current: -1)]).percentage, nil, "Negative capacity is unknown")
        expect(BatterySnapshot.fromPowerSources([source(maximum: 0)]).percentage, nil, "Zero maximum does not divide by zero")
        expect(BatterySnapshot.fromPowerSources([source(current: .nan)]).percentage, nil, "NaN capacity is rejected")
        expect(BatterySnapshot.fromPowerSources([source(maximum: .infinity)]).percentage, nil, "Infinite capacity is rejected")

        var partial = source()
        partial.removeValue(forKey: kIOPSCurrentCapacityKey)
        expect(BatterySnapshot.fromPowerSources([partial]).percentage, nil, "Missing capacity remains unknown")
        expect(BatterySnapshot.fromPowerSources([partial]).hasBattery, true, "Partial report still has a battery")

        expect(BatterySnapshot.fromPowerSources([source(pluggedIn: true, charging: false)]).isPluggedIn, true,
               "Optimized charging pause stays connected")
        expect(BatterySnapshot.fromPowerSources([source(pluggedIn: false, charging: true)]).isCharging, false,
               "Explicit battery power wins over inconsistent charging flag")
        var chargingWithoutState = source(charging: true)
        chargingWithoutState.removeValue(forKey: kIOPSPowerSourceStateKey)
        expect(BatterySnapshot.fromPowerSources([chargingWithoutState]).isPluggedIn, true,
               "Charging without source state implies external power")

        var charged = source(current: 97, pluggedIn: true)
        charged[kIOPSIsChargedKey] = true
        expect(BatterySnapshot.fromPowerSources([charged]).isFullyCharged, true, "OS full-charge flag is respected below 100")
        expect(BatterySnapshot.fromPowerSources([source(current: 100, pluggedIn: true)]).isFullyCharged, true,
               "100% connected without charging is full")
        expect(BatterySnapshot.fromPowerSources([source(current: 100)]).isFullyCharged, false,
               "Full means charged on external power, not unplugged")
        charged[kIOPSIsChargingKey] = true
        expect(BatterySnapshot.fromPowerSources([charged]).isFullyCharged, false,
               "Active charging wins over an inconsistent charged flag")
    }

    private static func testDisplayPolicy() {
        let disconnected = snapshot(pluggedIn: false, charging: false)
        let connected = snapshot(pluggedIn: true, charging: true)
        let paused = snapshot(pluggedIn: true, charging: false)
        let updated = snapshot(percentage: 51, pluggedIn: true, charging: true)
        let fullyCharged = snapshot(percentage: 100, pluggedIn: true, charging: false)
        let dischargedUpdate = snapshot(percentage: 49, pluggedIn: false, charging: false)

        expect(DisplayPolicy.action(for: connected, previous: nil, mode: .whenChargingStarts), .hide,
               "Launching while connected does not fabricate a charge event")
        expect(DisplayPolicy.action(for: disconnected, previous: nil, mode: .whenChargingStarts), .hide,
               "Launching on battery power does not fabricate a charging-stop event")
        expect(DisplayPolicy.action(for: fullyCharged, previous: nil, mode: .whenChargingStarts), .hide,
               "Launching already fully charged does not fabricate a charging-stop event")
        expect(DisplayPolicy.action(for: disconnected, previous: nil, mode: .always), .showPersistent,
               "Always mode displays on battery power")
        expect(DisplayPolicy.action(for: connected, previous: nil, mode: .always), .showPersistent,
               "Always mode displays on external power")
        expect(DisplayPolicy.action(for: .unavailable, previous: connected, mode: .always), .hide,
               "Always mode still hides on desktops")
        expect(DisplayPolicy.action(for: .unavailable, previous: connected, mode: .whenChargingStarts), .hide,
               "Missing battery hides a transient display")
        expect(DisplayPolicy.action(for: connected, previous: .unavailable, mode: .whenChargingStarts), .hide,
               "Recovering a battery report does not fabricate an event")
        expect(DisplayPolicy.action(for: disconnected, previous: .unavailable, mode: .whenChargingStarts), .hide,
               "Recovering an unplugged battery report establishes a baseline without a stop popup")
        expect(DisplayPolicy.action(for: connected, previous: disconnected, mode: .whenChargingStarts), .showTransient,
               "Connecting and charging shows once")
        expect(DisplayPolicy.action(for: paused, previous: disconnected, mode: .whenChargingStarts), .showTransient,
               "Connecting still shows when optimized charging is paused")
        expect(DisplayPolicy.action(for: connected, previous: paused, mode: .whenChargingStarts), .showTransient,
               "Charging resumption shows once")
        expect(DisplayPolicy.action(for: updated, previous: connected, mode: .whenChargingStarts), .keepCurrent,
               "Percentage updates do not extend the display deadline")
        expect(DisplayPolicy.action(for: connected, previous: connected, mode: .whenChargingStarts), .keepCurrent,
               "Identical reports and unchanged wake do not redisplay")
        expect(DisplayPolicy.action(for: paused, previous: connected, mode: .whenChargingStarts), .showTransient,
               "Charging pause shows a charging-stop popup")
        expect(DisplayPolicy.action(for: fullyCharged, previous: connected, mode: .whenChargingStarts), .showTransient,
               "Reaching full charge and stopping charging shows a popup")
        expect(DisplayPolicy.action(for: disconnected, previous: connected, mode: .whenChargingStarts), .showTransient,
               "Unplugging while charging shows one stop popup")
        expect(DisplayPolicy.action(for: disconnected, previous: paused, mode: .whenChargingStarts), .showTransient,
               "Unplugging a paused charger still shows the power transition")
        expect(DisplayPolicy.action(for: dischargedUpdate, previous: disconnected, mode: .whenChargingStarts), .keepCurrent,
               "Unplugged percentage changes neither hide the stop popup nor extend its deadline")
        expect(DisplayPolicy.action(for: disconnected, previous: disconnected, mode: .whenChargingStarts), .keepCurrent,
               "An unchanged unplugged report or wake neither hides nor repeats a stop popup")
        expect(DisplayPolicy.action(for: fullyCharged, previous: fullyCharged, mode: .whenChargingStarts), .keepCurrent,
               "Repeated fully charged reports do not replay the stop popup")
        expect(DisplayPolicy.action(for: fullyCharged, previous: paused, mode: .whenChargingStarts), .keepCurrent,
               "A full flag or percentage change without charging or connection transition does not redisplay")
        expect(DisplayPolicy.action(for: disconnected, previous: connected, mode: .always), .showPersistent,
               "Disconnect keeps always mode visible")
        expect(DisplayPolicy.action(for: paused, previous: paused, mode: .whenChargingStarts), .keepCurrent,
               "Repeated paused reports do not redisplay")
        let rawCapacityUpdate = BatterySnapshot(percentage: 50, isPluggedIn: false, isCharging: false,
                                                isFullyCharged: false, hasBattery: true,
                                                capacity: BatteryCapacityReading(current: 2001, maximum: 4000, unit: .milliampHours))
        expect(DisplayPolicy.action(for: rawCapacityUpdate, previous: disconnected, mode: .whenChargingStarts), .keepCurrent,
               "Raw capacity updates on battery power do not extend the stop popup")
        expect(DisplayMode.whenChargingStarts.rawValue, "whenChargingStarts",
               "Expanded charging-change mode preserves the saved mode key")
    }

    private static func testCapacityReadings() {
        let raw: [String: Any] = ["AppleRawCurrentCapacity": 3480, "AppleRawMaxCapacity": 3999,
                                  "CurrentCapacity": 88, "MaxCapacity": 100]
        let reading = BatteryCapacityReading(current: 3480, maximum: 3999, unit: .milliampHours)
        let percent = BatteryCapacityReading(current: 88, maximum: 100, unit: .percent)
        expect(BatteryCapacityReading.fromRegistry(raw, percentage: 88), reading,
               "Matching raw capacities are mAh, independent of macOS's smoothed percentage")
        expect(BatteryCapacityReading.fromRegistry(["CurrentCapacity": 2400, "MaxCapacity": 4000], percentage: 60),
               BatteryCapacityReading(current: 2400, maximum: 4000, unit: .milliampHours),
               "Legacy registry capacity keeps its mAh units")
        expect(BatteryCapacityReading.fromRegistry(["CurrentCapacity": 88, "MaxCapacity": 100], percentage: 88),
               percent, "Apple Silicon percentage registers must not be relabelled mAh")
        expect(BatteryCapacityReading.fromRegistry(["AppleRawCurrentCapacity": 1, "AppleRawMaxCapacity": 1], percentage: 88),
               percent, "Masked raw telemetry does not produce a false one-mAh battery")
        expect(BatteryCapacityReading.fromRegistry(["AppleRawCurrentCapacity": 3480, "NominalChargeCapacity": 4126,
                                                    "DesignCapacity": 4563], percentage: 88),
               percent, "Do not mix raw current with nominal or design capacity")
        expect(BatteryCapacityReading.fromRegistry(["AppleRawCurrentCapacity": 0, "AppleRawMaxCapacity": 5000], percentage: 0),
               BatteryCapacityReading(current: 0, maximum: 5000, unit: .milliampHours), "An empty raw battery is valid")
        expect(BatteryCapacityReading.fromRegistry([:], percentage: nil), nil, "Unknown capacity stays unknown")
        expect(BatteryCapacityReading.fromRegistry(raw, percentage: nil), reading,
               "Valid raw capacity can be shown even when percentage is unknown")
        expect(BatteryCapacityReading.fromRegistry([:], percentage: -1), nil, "Negative fallback percentage is unknown")
        expect(BatteryCapacityReading.fromRegistry([:], percentage: 101), nil, "Out-of-range fallback percentage is unknown")

        let invalidPairs: [[String: Any]] = [
            ["AppleRawCurrentCapacity": -1, "AppleRawMaxCapacity": 5000],
            ["AppleRawCurrentCapacity": 5500, "AppleRawMaxCapacity": 5000],
            ["AppleRawCurrentCapacity": 3000, "AppleRawMaxCapacity": 0],
            ["AppleRawCurrentCapacity": Double.nan, "AppleRawMaxCapacity": 5000],
            ["AppleRawCurrentCapacity": 3000, "AppleRawMaxCapacity": Double.infinity],
            ["AppleRawCurrentCapacity": 3.5, "AppleRawMaxCapacity": 5000],
            ["AppleRawCurrentCapacity": UInt64.max, "AppleRawMaxCapacity": 5000],
            ["AppleRawCurrentCapacity": true, "AppleRawMaxCapacity": 5000],
            ["AppleRawCurrentCapacity": "3000", "AppleRawMaxCapacity": 5000],
            ["AppleRawCurrentCapacity": 3000]
        ]
        for (index, properties) in invalidPairs.enumerated() {
            expect(BatteryCapacityReading.fromRegistry(properties, percentage: 88), percent,
                   "Invalid raw pair \(index) safely falls back to explicit percent units")
        }
        let unavailable = BatterySnapshot(percentage: nil, isPluggedIn: false, isCharging: false,
                                          isFullyCharged: false, hasBattery: false, capacity: reading)
        expect(unavailable.capacity, nil, "A desktop or missing battery cannot inherit raw capacity")
    }

    private static func testBatteryLevelTones() {
        expect(BatteryLevelTone.forPercentage(100), .green, "Full battery uses green")
        expect(BatteryLevelTone.forPercentage(51), .green, "Above fifty percent uses green")
        expect(BatteryLevelTone.forPercentage(50), .yellow, "Exactly fifty percent uses yellow")
        expect(BatteryLevelTone.forPercentage(20), .yellow, "Exactly twenty percent uses yellow")
        expect(BatteryLevelTone.forPercentage(19), .red, "Below twenty percent uses red")
        expect(BatteryLevelTone.forPercentage(0), .red, "Empty battery uses red")
        expect(BatteryLevelTone.forPercentage(nil), nil, "Unknown percentage has no misleading warning color")
        expect(BatteryLevelTone.forPercentage(-1), nil, "Negative percentage has no misleading warning color")
        expect(BatteryLevelTone.forPercentage(101), nil, "Out-of-range percentage is unknown")
    }

    private static func testOverlayGeometry() {
        let external = NSRect(x: -1920, y: -1080, width: 1920, height: 1055)
        var configuration = AppConfiguration.defaults
        expect(OverlayGeometry.anchor(configuration: configuration, screen: external, editing: false),
               NSPoint(x: -960, y: -55), "Top-center placement handles external displays with negative origins")
        configuration.placement = .custom
        configuration.customPosition = OverlayPosition(screenID: 17, x: 0.25, y: 0.75)
        expect(OverlayGeometry.anchor(configuration: configuration, screen: external, editing: false),
               NSPoint(x: -1440, y: -288.75), "Custom positions are fractions of the selected display")
        expect(OverlayGeometry.clampedAnchor(NSPoint(x: -10_000, y: -10_000), screen: external, scale: 1, editing: false),
               NSPoint(x: -1770, y: -1050), "Normal overlay retains margins at the bottom-left edge")
        expect(OverlayGeometry.clampedAnchor(NSPoint(x: -10_000, y: -10_000), screen: external, scale: 1, editing: true),
               NSPoint(x: -1770, y: -1022), "Placement editing leaves space for confirm and cancel controls")
        expect(OverlayGeometry.clampedAnchor(NSPoint(x: 10_000, y: 10_000), screen: external, scale: 1, editing: false),
               NSPoint(x: -150, y: -55), "Normal overlay retains margins at the top-right edge")
        expect(OverlayGeometry.clampedAnchor(NSPoint(x: CGFloat.nan, y: CGFloat.infinity), screen: external, scale: .nan, editing: false),
               NSPoint(x: -960, y: -552.5), "Invalid anchors and scale cannot generate nonfinite window coordinates")

        let tiny = NSRect(x: 10, y: 20, width: 100, height: 40)
        expect(OverlayGeometry.clampedAnchor(NSPoint(x: 1000, y: -1000), screen: tiny, scale: 1.6, editing: true),
               NSPoint(x: 60, y: 40), "A tiny usable display area keeps the anchor centered instead of inverting bounds")
        let screen = OverlayScreen(id: 17, visibleFrame: external)
        expect(OverlayGeometry.normalizedPosition(anchor: NSPoint(x: external.midX, y: external.midY), screen: screen),
               OverlayPosition(screenID: 17, x: 0.5, y: 0.5), "Stored custom positions retain selected display identity")
        expect(OverlayGeometry.normalizedPosition(anchor: NSPoint(x: -10_000, y: 10_000), screen: screen),
               OverlayPosition(screenID: 17, x: 0, y: 1), "Stored off-screen positions clamp to the display area")
        expect(OverlayGeometry.normalizedPosition(anchor: NSPoint(x: CGFloat.nan, y: CGFloat.infinity), screen: screen),
               OverlayPosition(screenID: 17, x: 0.5, y: 0.9), "Nonfinite saved anchors use the same defaults as preferences")
        expect(OverlayGeometry.normalizedPosition(anchor: .zero, screen: OverlayScreen(id: 17, visibleFrame: .zero)),
               OverlayPosition(), "An unavailable display cannot divide by zero or save a misleading screen ID")

        configuration = AppConfiguration.defaults
        configuration.scale = .nan
        expect(OverlayGeometry.anchor(configuration: configuration, screen: external, editing: false),
               NSPoint(x: -960, y: -55), "Invalid scale preserves top-center placement using the default scale")
        configuration.scale = -20
        expect(OverlayGeometry.anchor(configuration: configuration, screen: external, editing: false),
               NSPoint(x: -960, y: -44.5), "Out-of-range scale is normalized before calculating the top offset")
        configuration.scale = 20
        expect(OverlayGeometry.anchor(configuration: configuration, screen: external, editing: false),
               NSPoint(x: -960, y: -73), "Oversized scale uses the configured maximum before calculating the top offset")
    }

    private static func testConfigurationPersistence() {
        let suite = "EndfieldChargeTests.\(UUID().uuidString)"
        guard let defaults = UserDefaults(suiteName: suite) else {
            fatalError("Cannot create isolated test preferences")
        }
        defer { defaults.removePersistentDomain(forName: suite) }

        expect(ConfigurationStore(defaults: defaults).configuration, AppConfiguration.defaults,
               "First launch has complete default preferences")

        defaults.set("unrecognized-mode", forKey: "displayMode")
        defaults.set(-50, forKey: "displayDuration")
        defaults.set("not-a-color", forKey: "accentHex")
        let store = ConfigurationStore(defaults: defaults)
        expect(store.configuration.displayMode, .whenChargingStarts, "Unknown saved mode has safe fallback")
        expect(store.configuration.displayDuration, 1, "Invalid saved duration cannot create negative timers")
        expect(store.configuration.accentHex, AppConfiguration.defaults.accentHex, "Invalid saved color has safe fallback")

        var changeCount = 0
        store.onChange = { _ in changeCount += 1 }
        let next = AppConfiguration(displayMode: .always, displayDuration: 12, accentHex: " #00aaff ")
        store.update(next)
        expect(store.configuration.accentHex, "00AAFF", "Color input accepts a hash and normalizes case")
        expect(changeCount, 1, "Changed preferences notify the controller once")
        store.update(next)
        expect(changeCount, 1, "Equivalent preference updates do not redraw the app")
        expect(ConfigurationStore(defaults: defaults).configuration, store.configuration,
               "Preferences survive construction of a new store")

        expect(AppConfiguration(displayMode: .always, displayDuration: .infinity, accentHex: "000000")
            .normalized.displayDuration, AppConfiguration.defaults.displayDuration,
               "Nonfinite duration cannot reach a timer")
        expect(AppConfiguration(displayMode: .always, displayDuration: 1_000_000, accentHex: "FFFFFF")
            .normalized.displayDuration, 60, "Duration stays within the settings range")

        var customized = store.configuration
        customized.theme = .light
        customized.scale = 1.4
        customized.placement = .custom
        customized.customPosition = OverlayPosition(screenID: 42, x: 0.25, y: 0.75)
        customized.language = .simplifiedChinese
        store.update(customized)
        expect(ConfigurationStore(defaults: defaults).configuration, customized,
               "Theme, scale, custom placement and language persist together")
        expect(L10n.text("Settings", "设置"), "设置", "Language changes apply immediately")
        customized.language = .english
        customized.customPosition.screenID = nil
        store.update(customized)
        expect(L10n.text("Settings", "设置"), "Settings", "Language switches back to English immediately")
        expect(ConfigurationStore(defaults: defaults).configuration.customPosition.screenID, nil,
               "Clearing a custom display ID removes the old saved ID")
        customized.scale = .nan
        customized.customPosition = OverlayPosition(screenID: nil, x: .infinity, y: .nan)
        expect(customized.normalized.scale, 1, "Nonfinite scale cannot reach drawing transforms")
        expect(customized.normalized.customPosition, OverlayPosition(), "Nonfinite positions have safe defaults")
        customized.scale = 0
        customized.customPosition = OverlayPosition(screenID: nil, x: -20, y: 30)
        expect(customized.normalized.scale, 0.65, "Minimum scale remains legible")
        expect(customized.normalized.customPosition, OverlayPosition(screenID: nil, x: 0, y: 1),
               "Saved positions stay inside the available screen area")
        customized.scale = 100
        expect(customized.normalized.scale, 1.6, "Maximum scale stays within settings range")

        defaults.set("obsolete-theme", forKey: "theme")
        defaults.set("unsupported-language", forKey: "language")
        defaults.set("missing-placement", forKey: "placement")
        defaults.set(-1, forKey: "customScreenID")
        let recovered = ConfigurationStore(defaults: defaults).configuration
        expect(recovered.theme, .dark, "Unknown saved theme has a safe fallback")
        expect(recovered.language, .system, "Unknown saved language follows the system")
        expect(recovered.placement, .topCenter, "Unknown saved placement returns to the top center")
        expect(recovered.customPosition.screenID, nil, "Negative display IDs are not wrapped to unsigned values")
        defaults.set(Double(UInt32.max) + 1, forKey: "customScreenID")
        expect(ConfigurationStore(defaults: defaults).configuration.customPosition.screenID, nil,
               "Oversized display IDs are ignored")
        defaults.set(1.5, forKey: "customScreenID")
        expect(ConfigurationStore(defaults: defaults).configuration.customPosition.screenID, nil,
               "Fractional display IDs are ignored")
        L10n.language = .system
    }
}
