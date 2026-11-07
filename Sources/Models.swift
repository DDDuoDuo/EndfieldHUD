import AppKit

enum OverlayTheme: String, CaseIterable {
    case dark
    case light
    case system
}

enum OverlayPlacement: String, CaseIterable {
    case topCenter
    case custom
}

enum AppLanguage: String, CaseIterable {
    case system
    case english
    case simplifiedChinese
    case traditionalChinese
    case japanese
    case korean
}

enum HUDClockFormat: String, CaseIterable {
    case twentyFourHour
    case twelveHour
}

/// Coordinates describe the overlay center within the screen's available area.
/// Screen identity is optional so a saved position survives display changes.
struct OverlayPosition: Equatable {
    var screenID: UInt32? = nil
    var x: Double = 0.5
    var y: Double = 0.9

    var normalized: OverlayPosition {
        OverlayPosition(screenID: screenID,
                        x: x.isFinite ? min(1, max(0, x)) : 0.5,
                        y: y.isFinite ? min(1, max(0, y)) : 0.9)
    }
}

struct AppConfiguration: Equatable {
    var displayMode: DisplayMode
    var displayDuration: Double
    var accentHex: String
    var theme: OverlayTheme = .dark
    var scale: Double = 1
    var placement: OverlayPlacement = .topCenter
    var customPosition: OverlayPosition = OverlayPosition()
    /// The compact battery bar's length as a fraction of its display's usable
    /// width (1 = edge to edge; OverlayGeometry.barWidth applies the 200 pt
    /// minimum). nil keeps the classic 264 pt bar, so 1.3.0 settings look the same.
    var chargeBarLength: Double? = nil
    var language: AppLanguage = .system
    var hudScale: Double = 1
    /// Screen-relative offset from the default HUD center; positive Y is down.
    var hudOffsetX: Double = 0
    var hudOffsetY: Double = 0
    var parallaxIntensity: Double = 1
    var perspectiveIntensity: Double = 1
    /// Stored as dimming opacity for compatibility; settings expose its inverse.
    var backgroundDarkness: Double = 0.63
    var blurAmount: Double = 0.75
    var backgroundBrightness: Double {
        get { 1 - backgroundDarkness }
        set { backgroundDarkness = 1 - newValue }
    }
    var reduceMotion = false
    var ambientAnimation = true
    var closeOnFocusLost = true
    // A nil fixed target preserves the legacy pointer/primary display choice.
    var openOnActiveDisplay = true
    var hudDisplayUUID: String? = nil
    var hudDisplayName: String? = nil
    var launchAtLogin = true
    var batteryAlertsEnabled = true
    var devicePopupEnabled = true
    var lowPowerVisualMode = false
    var applicationIcon: HUDApplicationIcon = .endfield
    var clockFormat: HUDClockFormat = .twentyFourHour
    var clockStyle: HUDClockStyle = .digital
    var centerLogo: HUDCenterLogo = .endfield
    var centerLogoRevision: String? = nil
    var alertMetric: HUDChargeMetric = .battery
    var summonShortcut: SummonShortcut = .default
    /// Off: the summon shortcut no longer opens the HUD (checklist 6).
    var summonHotkeyEnabled = true
    /// Redacts private content while recording or streaming (spec §3.26).
    var recordingMode = false
    /// A short terminal sequence on the first open of the day (spec §3.28).
    var dailyBootLog = true
    /// Low disk, memory pressure and overheating warnings (spec §3.29).
    var facilityAlerts = true
    /// Display > Close hint, the row below Battery alert: the "ESC / hotkey /
    /// click outside to close" line at the bottom of the HUD. Off hides it;
    /// nothing else moves.
    var showsCloseHint = true
    /// Order and visibility of the right-hand list (Display > Modules).
    var moduleLayout = HUDModuleLayout()
    /// Display > HUD elements: the title, Sanity gauge and alert tags on the
    /// HUD. Also stores Display > Open on (the module the HUD opens on).
    var overlayLayout = HUDOverlayLayout()

    static let defaults = AppConfiguration(displayMode: .whenChargingStarts,
                                           displayDuration: 3, accentHex: "FAD41F")

    var normalized: AppConfiguration {
        let raw = accentHex.trimmingCharacters(in: .whitespacesAndNewlines)
            .replacingOccurrences(of: "#", with: "").uppercased()
        let validColor = raw.count == 6 && raw.unicodeScalars.allSatisfy {
            (48...57).contains($0.value) || (65...70).contains($0.value)
        }
        var result = self
        result.displayDuration = displayDuration.isFinite ? min(60, max(1, displayDuration)) : Self.defaults.displayDuration
        result.accentHex = validColor ? raw : Self.defaults.accentHex
        result.scale = Self.clamp(scale, to: OverlayGeometry.scaleRange, fallback: 1)
        result.customPosition = customPosition.normalized
        result.chargeBarLength = chargeBarLength.flatMap { $0.isFinite && $0 > 0 ? min(1, $0) : nil }
        result.hudScale = Self.clamp(hudScale, to: 0.2...2, fallback: 1)
        result.hudOffsetX = Self.clamp(hudOffsetX, to: -0.5...0.5, fallback: 0)
        result.hudOffsetY = Self.clamp(hudOffsetY, to: -0.5...0.5, fallback: 0)
        result.parallaxIntensity = Self.clamp(parallaxIntensity, to: 0...2, fallback: 1)
        result.perspectiveIntensity = Self.clamp(perspectiveIntensity, to: 0...2, fallback: 1)
        result.backgroundDarkness = Self.clamp(backgroundDarkness, to: 0...1, fallback: Self.defaults.backgroundDarkness)
        result.blurAmount = Self.clamp(blurAmount, to: 0...1, fallback: Self.defaults.blurAmount)
        result.hudDisplayUUID = hudDisplayUUID.flatMap { UUID(uuidString: $0)?.uuidString }
        result.hudDisplayName = result.hudDisplayUUID == nil ? nil : hudDisplayName.map {
            String($0.trimmingCharacters(in: .whitespacesAndNewlines).prefix(128))
        }
        if result.hudDisplayName?.isEmpty == true { result.hudDisplayName = nil }
        result.centerLogoRevision = centerLogoRevision.flatMap { UUID(uuidString: $0)?.uuidString }
        if summonShortcut.validationError != nil { result.summonShortcut = .default }
        result.overlayLayout = overlayLayout.normalized
        return result
    }

    /// The settings' clamp: `value` inside `range`, or `fallback` when it is not finite.
    static func clamp<Value: BinaryFloatingPoint>(_ value: Value, to range: ClosedRange<Value>, fallback: Value) -> Value {
        value.isFinite ? min(range.upperBound, max(range.lowerBound, value)) : fallback
    }

    var accentColor: NSColor {
        let value = UInt32(normalized.accentHex, radix: 16) ?? 0xFAD41F
        return NSColor(srgbRed: CGFloat((value >> 16) & 255) / 255,
                       green: CGFloat((value >> 8) & 255) / 255,
                       blue: CGFloat(value & 255) / 255, alpha: 1)
    }
}

/// The settings a page's Restore defaults resets. Each scope lists its fields
/// once, here; `AppConfiguration.restoring(_:)` copies only those from
/// `.defaults`, so a new setting joins a Restore by adding one line.
enum HUDSettingsScope: CaseIterable {
    /// The System page.
    case system
    /// The Display page and every page under it: Open on, Modules, HUD
    /// elements, App / menu bar icon and Battery alert (with the bar's
    /// position, size and length).
    case display
    /// Display > HUD elements only. Open on, a Display row since 1.3.1, is kept.
    case hudElements

    /// One saved setting: copies its value from `source` into `target`.
    struct Field {
        let copy: (_ target: inout AppConfiguration, _ source: AppConfiguration) -> Void
        init<Value>(_ key: WritableKeyPath<AppConfiguration, Value>) {
            copy = { $0[keyPath: key] = $1[keyPath: key] }
        }
        /// `key` from `source`, except its `kept` part, which stays as `target` had it.
        init<Value, Kept>(_ key: WritableKeyPath<AppConfiguration, Value>, keeping kept: WritableKeyPath<Value, Kept>) {
            copy = { target, source in
                let held = target[keyPath: key][keyPath: kept]
                target[keyPath: key] = source[keyPath: key]
                target[keyPath: key][keyPath: kept] = held
            }
        }
    }

    var fields: [Field] {
        switch self {
        case .system:
            return [Field(\.language), Field(\.launchAtLogin), Field(\.closeOnFocusLost), Field(\.openOnActiveDisplay),
                    Field(\.hudDisplayUUID), Field(\.hudDisplayName)]
        case .display:
            return [Field(\.hudScale), Field(\.hudOffsetX), Field(\.hudOffsetY), Field(\.parallaxIntensity),
                    Field(\.perspectiveIntensity), Field(\.backgroundDarkness), Field(\.blurAmount), Field(\.theme),
                    Field(\.clockFormat), Field(\.clockStyle), Field(\.centerLogo), Field(\.centerLogoRevision),
                    Field(\.accentHex), Field(\.ambientAnimation), Field(\.reduceMotion), Field(\.lowPowerVisualMode),
                    Field(\.recordingMode), Field(\.dailyBootLog), Field(\.showsCloseHint),
                    // App / menu bar icon
                    Field(\.applicationIcon),
                    // Battery alert
                    Field(\.alertMetric), Field(\.displayMode), Field(\.batteryAlertsEnabled), Field(\.displayDuration),
                    Field(\.placement), Field(\.customPosition), Field(\.scale), Field(\.chargeBarLength),
                    // Modules
                    Field(\.moduleLayout),
                    // Open on and HUD elements
                    Field(\.overlayLayout), Field(\.facilityAlerts)]
        case .hudElements:
            return [Field(\.overlayLayout, keeping: \.openModule), Field(\.facilityAlerts)]
        }
    }

    /// Kept by every scope: the hotkey has its own Restore on the Hotkeys page,
    /// and the device popup has no setting to show.
    static let unscopedFields = [Field(\.summonShortcut), Field(\.summonHotkeyEnabled), Field(\.devicePopupEnabled)]
}

extension AppConfiguration {
    /// This configuration with only `scope`'s settings back at their defaults.
    func restoring(_ scope: HUDSettingsScope) -> AppConfiguration {
        var result = self
        scope.fields.forEach { $0.copy(&result, .defaults) }
        return result
    }
}

/// Choices added after v1.2.0 that its decoders do not know ("Don't display",
/// the GPU reading). v1.2.0 shares these defaults, so the original key keeps the
/// value v1.2.0 falls back to anyway and the new choice lives under its own key.
/// It applies only while the original key still holds that fallback, so a
/// different choice made in v1.2.0 wins.
enum ReleaseFallback {
    static let displayModeKey = "displayModeExtension", alertMetricKey = "alertMetricExtension"
    static func displayMode(_ value: DisplayMode) -> DisplayMode { value == .never ? .whenChargingStarts : value }
    static func alertMetric(_ value: HUDChargeMetric) -> HUDChargeMetric { value == .gpu ? .battery : value }

    static func save(_ value: AppConfiguration, to defaults: UserDefaults) {
        let mode = displayMode(value.displayMode), metric = alertMetric(value.alertMetric)
        if mode == value.displayMode { defaults.removeObject(forKey: displayModeKey) }
        else { defaults.set(value.displayMode.rawValue, forKey: displayModeKey) }
        if metric == value.alertMetric { defaults.removeObject(forKey: alertMetricKey) }
        else { defaults.set(value.alertMetric.rawValue, forKey: alertMetricKey) }
    }

    static func restore(_ value: AppConfiguration, from defaults: UserDefaults) -> AppConfiguration {
        var result = value
        if let mode = defaults.string(forKey: displayModeKey).flatMap(DisplayMode.init(rawValue:)),
           displayMode(mode) == result.displayMode { result.displayMode = mode }
        if let metric = defaults.string(forKey: alertMetricKey).flatMap(HUDChargeMetric.init(rawValue:)),
           alertMetric(metric) == result.alertMetric { result.alertMetric = metric }
        return result
    }
}

final class ConfigurationStore {
    private let defaults: UserDefaults
    private(set) var configuration: AppConfiguration
    var onChange: ((AppConfiguration) -> Void)?
    private var observers: [UUID: (AppConfiguration) -> Void] = [:]

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        // The original HUD ignored its legacy lime default and drew this yellow.
        // Migrate only that old default; deliberately selected custom colors survive.
        if defaults.object(forKey: "hudSettingsSchemaVersion") == nil {
            if defaults.string(forKey: "accentHex")?.uppercased() == "D9F36B" {
                defaults.set(AppConfiguration.defaults.accentHex, forKey: "accentHex")
            }
            defaults.set(1, forKey: "hudSettingsSchemaVersion")
        }
        // Screen IDs are unsigned; ignore corrupt preferences instead of truncating.
        let storedScreenID = defaults.object(forKey: "customScreenID") as? NSNumber
        let screenID: UInt32?
        if let number = storedScreenID, number.doubleValue.isFinite,
           number.doubleValue >= 0, number.doubleValue <= Double(UInt32.max),
           number.doubleValue.rounded(.towardZero) == number.doubleValue {
            screenID = UInt32(number.doubleValue)
        } else {
            screenID = nil
        }
        configuration = AppConfiguration(
            displayMode: defaults.string(forKey: "displayMode").flatMap(DisplayMode.init(rawValue:)) ?? .whenChargingStarts,
            displayDuration: defaults.object(forKey: "displayDuration") == nil ? 3 : defaults.double(forKey: "displayDuration"),
            accentHex: defaults.string(forKey: "accentHex") ?? AppConfiguration.defaults.accentHex,
            theme: defaults.string(forKey: "theme").flatMap(OverlayTheme.init(rawValue:)) ?? .dark,
            scale: defaults.object(forKey: "scale") == nil ? 1 : defaults.double(forKey: "scale"),
            placement: defaults.string(forKey: "placement").flatMap(OverlayPlacement.init(rawValue:)) ?? .topCenter,
            customPosition: OverlayPosition(
                screenID: screenID,
                x: defaults.object(forKey: "customPositionX") == nil ? 0.5 : defaults.double(forKey: "customPositionX"),
                y: defaults.object(forKey: "customPositionY") == nil ? 0.9 : defaults.double(forKey: "customPositionY")
            ),
            chargeBarLength: defaults.object(forKey: "chargeBarLength") == nil ? nil : defaults.double(forKey: "chargeBarLength"),
            language: defaults.string(forKey: "language").flatMap(AppLanguage.init(rawValue:)) ?? .system,
            hudScale: Self.double(defaults, "hudScale", fallback: 1),
            hudOffsetX: Self.double(defaults, "hudOffsetX", fallback: 0),
            hudOffsetY: Self.double(defaults, "hudOffsetY", fallback: 0),
            parallaxIntensity: Self.double(defaults, "parallaxIntensity", fallback: 1),
            perspectiveIntensity: Self.double(defaults, "perspectiveIntensity", fallback: 1),
            backgroundDarkness: Self.double(defaults, "backgroundDarkness", fallback: AppConfiguration.defaults.backgroundDarkness),
            blurAmount: Self.double(defaults, "blurAmount", fallback: AppConfiguration.defaults.blurAmount),
            reduceMotion: Self.bool(defaults, "reduceMotion", fallback: false),
            ambientAnimation: Self.bool(defaults, "ambientAnimation", fallback: true),
            closeOnFocusLost: Self.bool(defaults, "closeOnFocusLost", fallback: true),
            openOnActiveDisplay: Self.bool(defaults, "openOnActiveDisplay", fallback: true),
            hudDisplayUUID: defaults.string(forKey: "hudDisplayUUID"),
            hudDisplayName: defaults.string(forKey: "hudDisplayName"),
            launchAtLogin: Self.bool(defaults, "launchAtLogin", fallback: true),
            batteryAlertsEnabled: Self.bool(defaults, "batteryAlertsEnabled", fallback: true),
            devicePopupEnabled: Self.bool(defaults, "devicePopupEnabled", fallback: true),
            lowPowerVisualMode: Self.bool(defaults, "lowPowerVisualMode", fallback: false),
            applicationIcon: defaults.string(forKey: "applicationIcon").flatMap(HUDApplicationIcon.init(savedValue:)) ?? .endfield,
            clockFormat: defaults.string(forKey: "clockFormat").flatMap(HUDClockFormat.init(rawValue:)) ?? .twentyFourHour,
            clockStyle: defaults.string(forKey: "clockStyle").flatMap(HUDClockStyle.init(rawValue:)) ?? .digital,
            centerLogo: defaults.string(forKey: "centerLogo").flatMap(HUDCenterLogo.init(rawValue:)) ?? .endfield,
            centerLogoRevision: defaults.string(forKey: "centerLogoRevision"),
            alertMetric: defaults.string(forKey: "alertMetric").flatMap(HUDChargeMetric.init(rawValue:)) ?? .battery,
            summonShortcut: defaults.data(forKey: "summonShortcut").flatMap {
                try? JSONDecoder().decode(SummonShortcut.self, from: $0)
            } ?? .default,
            summonHotkeyEnabled: Self.bool(defaults, "summonHotkeyEnabled", fallback: true),
            recordingMode: Self.bool(defaults, "recordingMode", fallback: false),
            dailyBootLog: Self.bool(defaults, "dailyBootLog", fallback: true),
            facilityAlerts: Self.bool(defaults, "facilityAlerts", fallback: true),
            showsCloseHint: Self.bool(defaults, "showsCloseHint", fallback: true)
        ).normalized
        configuration = ReleaseFallback.restore(configuration, from: defaults)
        configuration.moduleLayout = HUDModuleLayout(defaults: defaults)
        configuration.overlayLayout = HUDOverlayLayout(defaults: defaults)
        L10n.language = configuration.language
    }

    func update(_ config: AppConfiguration) {
        let next = config.normalized
        guard next != configuration else { return }
        configuration = next
        L10n.language = next.language
        defaults.set(ReleaseFallback.displayMode(next.displayMode).rawValue, forKey: "displayMode")
        defaults.set(next.displayDuration, forKey: "displayDuration")
        defaults.set(next.accentHex, forKey: "accentHex")
        defaults.set(next.theme.rawValue, forKey: "theme")
        defaults.set(next.scale, forKey: "scale")
        defaults.set(next.placement.rawValue, forKey: "placement")
        if let screenID = next.customPosition.screenID {
            defaults.set(NSNumber(value: screenID), forKey: "customScreenID")
        } else {
            defaults.removeObject(forKey: "customScreenID")
        }
        defaults.set(next.customPosition.x, forKey: "customPositionX")
        defaults.set(next.customPosition.y, forKey: "customPositionY")
        if let length = next.chargeBarLength { defaults.set(length, forKey: "chargeBarLength") }
        else { defaults.removeObject(forKey: "chargeBarLength") }
        defaults.set(next.language.rawValue, forKey: "language")
        defaults.set(next.hudScale, forKey: "hudScale")
        defaults.set(next.hudOffsetX, forKey: "hudOffsetX")
        defaults.set(next.hudOffsetY, forKey: "hudOffsetY")
        defaults.set(next.parallaxIntensity, forKey: "parallaxIntensity")
        defaults.set(next.perspectiveIntensity, forKey: "perspectiveIntensity")
        defaults.set(next.backgroundDarkness, forKey: "backgroundDarkness")
        defaults.set(next.blurAmount, forKey: "blurAmount")
        defaults.set(next.reduceMotion, forKey: "reduceMotion")
        defaults.set(next.ambientAnimation, forKey: "ambientAnimation")
        defaults.set(next.closeOnFocusLost, forKey: "closeOnFocusLost")
        defaults.set(next.openOnActiveDisplay, forKey: "openOnActiveDisplay")
        defaults.set(next.hudDisplayUUID, forKey: "hudDisplayUUID")
        defaults.set(next.hudDisplayName, forKey: "hudDisplayName")
        defaults.set(next.launchAtLogin, forKey: "launchAtLogin")
        defaults.set(next.batteryAlertsEnabled, forKey: "batteryAlertsEnabled")
        defaults.set(next.devicePopupEnabled, forKey: "devicePopupEnabled")
        defaults.set(next.lowPowerVisualMode, forKey: "lowPowerVisualMode")
        defaults.set(next.applicationIcon.rawValue, forKey: "applicationIcon")
        defaults.set(next.clockFormat.rawValue, forKey: "clockFormat")
        defaults.set(next.clockStyle.rawValue, forKey: "clockStyle")
        defaults.set(next.centerLogo.rawValue, forKey: "centerLogo")
        defaults.set(next.centerLogoRevision, forKey: "centerLogoRevision")
        defaults.set(ReleaseFallback.alertMetric(next.alertMetric).rawValue, forKey: "alertMetric")
        defaults.set(try? JSONEncoder().encode(next.summonShortcut), forKey: "summonShortcut")
        defaults.set(next.summonHotkeyEnabled, forKey: "summonHotkeyEnabled")
        defaults.set(next.recordingMode, forKey: "recordingMode")
        defaults.set(next.dailyBootLog, forKey: "dailyBootLog")
        defaults.set(next.facilityAlerts, forKey: "facilityAlerts")
        defaults.set(next.showsCloseHint, forKey: "showsCloseHint")
        ReleaseFallback.save(next, to: defaults)
        next.moduleLayout.save(to: defaults)
        next.overlayLayout.save(to: defaults)
        onChange?(next)
        Array(observers.values).forEach { $0(next) }
    }

    /// Set once the hidden-gauge migration below has run.
    static let sanityMigrationKey = "hudOverlaySanityMigrated"

    /// Runs once: a Sanity gauge hidden through Account > Header display
    /// (1.3.0 and earlier, `headerHidden`) starts 1.3.1 with its Show switch
    /// off. The account cache, which v1.2.0 also reads, is left as it is.
    func migrateHiddenSanity(headerHidden: () -> Bool) {
        guard defaults.object(forKey: Self.sanityMigrationKey) == nil else { return }
        defaults.set(true, forKey: Self.sanityMigrationKey)
        guard headerHidden() else { return }
        var next = configuration
        next.overlayLayout.showsSanity = false
        update(next)
    }

    @discardableResult
    func addObserver(_ observer: @escaping (AppConfiguration) -> Void) -> UUID {
        let token = UUID()
        observers[token] = observer
        return token
    }

    func removeObserver(_ token: UUID) { observers.removeValue(forKey: token) }

    private static func double(_ defaults: UserDefaults, _ key: String, fallback: Double) -> Double {
        defaults.object(forKey: key) == nil ? fallback : defaults.double(forKey: key)
    }

    private static func bool(_ defaults: UserDefaults, _ key: String, fallback: Bool) -> Bool {
        defaults.object(forKey: key) == nil ? fallback : defaults.bool(forKey: key)
    }
}
