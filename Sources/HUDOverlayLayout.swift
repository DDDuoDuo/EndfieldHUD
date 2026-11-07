import AppKit

/// Display > HUD elements: where the HUD's own overlays sit on the core plane,
/// their size, whether they show and the title's text. The module the HUD
/// opens on is stored here too but shown as Display > Open on. The overlays
/// are the ENDFIELDHUD / SYSTEM INTERFACE title, the Sanity gauge and the
/// facility alert tags; the alerts' Show switch is the existing
/// `facilityAlerts` setting, which also runs their monitor.
///
/// Saved as one JSON object under `key`, removed while it holds the defaults,
/// so builds before 1.3.1 (which ignore the key) and untouched settings look
/// the same. Missing fields read as their defaults.
struct HUDOverlayLayout: Equatable {
    enum Element: String, CaseIterable {
        case title, sanity, alerts
        /// The editor strip's Size slider for the element.
        var sizeTitle: String {
            switch self {
            case .title: return L10n.text("Title size", "标题大小")
            case .sanity: return L10n.text("Sanity size", "理智大小")
            case .alerts: return L10n.text("Alerts size", "警报大小")
            }
        }
    }

    /// An offset in core-plane points (the 1,000 x 640 plane, positive y down)
    /// from the element's authored place, and its size (1 = as designed).
    struct Placement: Equatable, Codable {
        var x: Double = 0, y: Double = 0, size: Double = 1

        init(x: Double = 0, y: Double = 0, size: Double = 1) { self.x = x; self.y = y; self.size = size }

        init(from decoder: Decoder) throws {
            let values = try decoder.container(keyedBy: CodingKeys.self)
            x = try values.decodeIfPresent(Double.self, forKey: .x) ?? 0
            y = try values.decodeIfPresent(Double.self, forKey: .y) ?? 0
            size = try values.decodeIfPresent(Double.self, forKey: .size) ?? 1
        }

        var normalized: Placement {
            let offsets = -HUDOverlayLayout.offsetLimit...HUDOverlayLayout.offsetLimit
            return Placement(x: AppConfiguration.clamp(x, to: offsets, fallback: 0),
                             y: AppConfiguration.clamp(y, to: offsets, fallback: 0),
                             size: AppConfiguration.clamp(size, to: HUDOverlayLayout.sizeRange, fallback: 1))
        }
    }

    static let key = "hudOverlayLayout"
    static let sizeRange: ClosedRange<Double> = 0.5...2
    /// A coarse bound for stored offsets; the HUD keeps each element on the plane.
    static let offsetLimit: Double = 1_000
    static let textLimit = 24
    static let defaultTitle = "ENDFIELDHUD", defaultSubtitle = "SYSTEM INTERFACE"

    var title = Placement(), sanity = Placement(), alerts = Placement()
    var showsTitle = true, showsSanity = true
    /// One string for every language; empty shows the default text.
    var titleText = "", subtitleText = ""
    /// nil opens the HUD on the module last opened (the default).
    var openModule: HUDModule?

    init() {}

    subscript(element: Element) -> Placement {
        get {
            switch element { case .title: return title; case .sanity: return sanity; case .alerts: return alerts }
        }
        set {
            switch element { case .title: title = newValue; case .sanity: sanity = newValue; case .alerts: alerts = newValue }
        }
    }

    var shownTitle: String { titleText.isEmpty ? Self.defaultTitle : titleText }
    var shownSubtitle: String { subtitleText.isEmpty ? Self.defaultSubtitle : subtitleText }
    var isDefault: Bool { self == HUDOverlayLayout() }

    /// Custom text as it is kept: one line, trimmed, at most `textLimit` characters.
    static func cleanText(_ value: String) -> String {
        let line = value.components(separatedBy: .newlines).map { $0.trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty }.joined(separator: " ")
        return String(line.prefix(textLimit))
    }

    var normalized: HUDOverlayLayout {
        var result = self
        for element in Element.allCases { result[element] = self[element].normalized }
        result.titleText = Self.cleanText(titleText)
        result.subtitleText = Self.cleanText(subtitleText)
        if let module = openModule, !Self.openableModules.contains(module) { result.openModule = nil }
        return result
    }

    /// Every module the left and right navigation can open, in navigation
    /// order; Add App is an action, never a place to open on.
    static let openableModules: [HUDModule] = HUDDesktopWatchNavigation.fixedModules + HUDModuleLayout.configurable

    /// The module a new HUD opens on: an explicit request (a failed app launch
    /// reopening on Add App, a notification, the menu) wins, then the chosen
    /// module, then the module last opened. A module hidden in Display > Modules
    /// is replaced by the first visible one.
    static func initialModule(request: HUDModule?, configuration: AppConfiguration, last: HUDModule) -> HUDModule {
        if let request { return request }
        return configuration.moduleLayout.restorable(configuration.overlayLayout.openModule ?? last)
    }

    // MARK: Storage

    private struct Stored: Codable {
        var title: Placement?, sanity: Placement?, alerts: Placement?
        var showsTitle: Bool?, showsSanity: Bool?
        var titleText: String?, subtitleText: String?
        var openModule: String?
    }

    init(defaults: UserDefaults) {
        guard let data = defaults.data(forKey: Self.key), let stored = try? JSONDecoder().decode(Stored.self, from: data) else { return }
        title = stored.title ?? Placement(); sanity = stored.sanity ?? Placement(); alerts = stored.alerts ?? Placement()
        showsTitle = stored.showsTitle ?? true; showsSanity = stored.showsSanity ?? true
        titleText = stored.titleText ?? ""; subtitleText = stored.subtitleText ?? ""
        openModule = stored.openModule.flatMap(HUDModule.init(rawValue:))
        self = normalized
    }

    /// The default layout leaves no key behind.
    func save(to defaults: UserDefaults) {
        let value = normalized
        guard !value.isDefault else { defaults.removeObject(forKey: Self.key); return }
        let stored = Stored(title: value.title, sanity: value.sanity, alerts: value.alerts,
                            showsTitle: value.showsTitle, showsSanity: value.showsSanity,
                            titleText: value.titleText, subtitleText: value.subtitleText,
                            openModule: value.openModule?.rawValue)
        if let data = try? JSONEncoder().encode(stored) { defaults.set(data, forKey: Self.key) }
    }

    // MARK: Last opened module

    /// "Last opened" survives a relaunch (1.3.1); before, the first open showed Map.
    static let lastModuleKey = "hudLastOpenedModule"
    static let firstModule: HUDModule = .map

    static func lastModule(in defaults: UserDefaults?) -> HUDModule {
        defaults?.string(forKey: lastModuleKey).flatMap(HUDModule.init(rawValue:)) ?? firstModule
    }

    static func saveLastModule(_ module: HUDModule, to defaults: UserDefaults?) {
        guard let defaults, defaults.string(forKey: lastModuleKey) != module.rawValue else { return }
        defaults.set(module.rawValue, forKey: lastModuleKey)
    }
}

/// Where each HUD overlay sits on the core plane (the 1,000 x 640 design
/// plane): its authored anchor, its frame for a placement and the placement
/// pulled back so the element stays on the plane.
enum HUDOverlayGeometry {
    static let plane = CGRect(x: 0, y: 0, width: 1_000, height: 640)
    /// The header group holding the title, the Sanity gauge and the legacy clock.
    static let header = CGRect(x: 270, y: 2, width: 600, height: 74)
    /// The title's and the gauge's top-left corners, and the alert column's
    /// top-right corner (it grows down and to the left from it).
    static func anchor(_ element: HUDOverlayLayout.Element) -> CGPoint {
        switch element {
        case .title: return header.origin
        case .sanity: return CGPoint(x: header.minX + HUDAccountGauge.headerPosition.x, y: header.minY + HUDAccountGauge.headerPosition.y)
        case .alerts: return CGPoint(x: header.minX - 8, y: header.minY + 50)
        }
    }

    /// The element's frame for `placement`, where `natural` is its size at 100 %.
    static func frame(_ element: HUDOverlayLayout.Element, _ placement: HUDOverlayLayout.Placement, natural: CGSize) -> CGRect {
        let anchor = anchor(element), size = CGFloat(placement.size)
        let width = natural.width * size, height = natural.height * size
        let x = anchor.x + CGFloat(placement.x), y = anchor.y + CGFloat(placement.y)
        return CGRect(x: element == .alerts ? x - width : x, y: y, width: width, height: height)
    }

    /// `placement` with its offset pulled in so the frame stays on the plane
    /// (centred on it when the element is larger than the plane).
    static func clamped(_ element: HUDOverlayLayout.Element, _ placement: HUDOverlayLayout.Placement,
                        natural: CGSize) -> HUDOverlayLayout.Placement {
        var result = placement.normalized
        let frame = frame(element, result, natural: natural)
        func shift(_ minimum: CGFloat, _ length: CGFloat, _ lower: CGFloat, _ upper: CGFloat) -> CGFloat {
            if length > upper - lower { return (lower + upper - length) / 2 - minimum }
            return max(lower, min(minimum, upper - length)) - minimum
        }
        result.x += Double(shift(frame.minX, frame.width, plane.minX, plane.maxX))
        result.y += Double(shift(frame.minY, frame.height, plane.minY, plane.maxY))
        return result
    }
}
