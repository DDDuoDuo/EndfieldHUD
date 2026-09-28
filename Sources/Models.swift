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
    var summonShortcut: SummonShortcut = .default

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
        result.scale = scale.isFinite ? min(1.6, max(0.65, scale)) : 1
        result.customPosition = customPosition.normalized
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
        if summonShortcut.validationError != nil { result.summonShortcut = .default }
        return result
    }

    private static func clamp(_ value: Double, to range: ClosedRange<Double>, fallback: Double) -> Double {
        value.isFinite ? min(range.upperBound, max(range.lowerBound, value)) : fallback
    }

    var accentColor: NSColor {
        let value = UInt32(normalized.accentHex, radix: 16) ?? 0xFAD41F
        return NSColor(srgbRed: CGFloat((value >> 16) & 255) / 255,
                       green: CGFloat((value >> 8) & 255) / 255,
                       blue: CGFloat(value & 255) / 255, alpha: 1)
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
            applicationIcon: defaults.string(forKey: "applicationIcon").flatMap(HUDApplicationIcon.init(rawValue:)) ?? .endfield,
            clockFormat: defaults.string(forKey: "clockFormat").flatMap(HUDClockFormat.init(rawValue:)) ?? .twentyFourHour,
            summonShortcut: defaults.data(forKey: "summonShortcut").flatMap {
                try? JSONDecoder().decode(SummonShortcut.self, from: $0)
            } ?? .default
        ).normalized
        L10n.language = configuration.language
    }

    func update(_ config: AppConfiguration) {
        let next = config.normalized
        guard next != configuration else { return }
        configuration = next
        L10n.language = next.language
        defaults.set(next.displayMode.rawValue, forKey: "displayMode")
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
        defaults.set(try? JSONEncoder().encode(next.summonShortcut), forKey: "summonShortcut")
        onChange?(next)
        Array(observers.values).forEach { $0(next) }
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
