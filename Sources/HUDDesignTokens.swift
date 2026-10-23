import AppKit
import QuartzCore

/// Design tokens of the centre redesign (spec §1.6). Colours are sRGB and
/// created once per theme / theme colour change; components never build a
/// colour per frame. The accent family follows the theme colour
/// (`HUDAccentPalette`) and the surface family follows Theme (`HUDTheme`):
/// read them when layers are built, never cache them outside this type.
enum HUDColor {
    static func rgb(_ hex: UInt32, _ alpha: CGFloat = 1) -> CGColor {
        CGColor(srgbRed: CGFloat((hex >> 16) & 255) / 255, green: CGFloat((hex >> 8) & 255) / 255,
                blue: CGFloat(hex & 255) / 255, alpha: alpha)
    }
    static func white(_ alpha: CGFloat) -> CGColor { CGColor(srgbRed: 1, green: 1, blue: 1, alpha: alpha) }
    static func black(_ alpha: CGFloat) -> CGColor { CGColor(srgbRed: 0, green: 0, blue: 0, alpha: alpha) }
    static let clear = black(0)

    // Signature palette: the accent family, then fixed colours.
    /// The accent family in use; the host applies the theme colour with `apply`.
    private(set) static var palette = HUDAccentPalette.standard
    /// The surface family in use (Theme, System resolved by the host).
    private(set) static var theme = HUDTheme.dark
    /// Increments on every accent or theme change; canvases compare it to rebuild lazily.
    private(set) static var paletteGeneration = 0
    static var yellow: CGColor { palette.yellow }
    static var yellowHigh: CGColor { palette.yellowHigh }
    static var yellowLow: CGColor { palette.yellowLow }
    /// Between `yellow` and `yellowLow` (the done side of a task card).
    static var yellowDeep: CGColor { palette.yellowDeep }
    /// The primary button gradient, top to bottom (high, yellow, low).
    static var primaryButton: [CGColor] { palette.primaryButton }
    /// The accent as text or a thin line on a theme surface: `yellow` in
    /// dark, a deep variant that stays legible on light surfaces in light.
    /// Fills behind dark text (buttons, chips, active tabs) keep `yellow`.
    static var accentInk: CGColor { palette.ink }
    static var isLight: Bool { theme.isLight }

    /// Applies the theme colour (`AppConfiguration.accentHex`). Returns true
    /// when the family changed: cached rasters are dropped and the host then
    /// rebuilds the redesigned pages.
    @discardableResult
    static func applyAccent(hex: String) -> Bool { apply(accentHex: hex, light: theme.isLight) }

    /// Applies the theme colour and Theme together (see `applyAccent`).
    @discardableResult
    static func apply(accentHex: String, light: Bool) -> Bool {
        let next = HUDAccentPalette(accentHex: accentHex, light: light)
        guard next.accent != palette.accent || light != theme.isLight else { return false }
        palette = next
        if light != theme.isLight { theme = light ? .light : .dark }
        paletteGeneration &+= 1
        HUDArtwork.releaseCachedImages()
        return true
    }

    /// A neutral literal that follows Theme: `hex` itself in dark, its light
    /// counterpart (`HUDTheme.lightTone`) in light. Use it for greys drawn on
    /// theme surfaces; plates, photos and fixed dark tags keep `rgb`.
    static func tone(_ hex: UInt32, _ alpha: CGFloat = 1) -> CGColor {
        rgb(theme.isLight ? HUDTheme.lightTone(hex) : hex, alpha)
    }
    /// Text and hairlines on theme surfaces: white in dark, ink in light.
    static func foreground(_ alpha: CGFloat) -> CGColor { theme.isLight ? theme.foreground.copy(alpha: alpha) ?? clear : white(alpha) }
    /// A status or accent colour as text or a thin line on a theme surface
    /// (light variants keep their meaning with enough contrast on light).
    static func legible(_ color: CGColor) -> CGColor {
        guard theme.isLight else { return color }
        if color == palette.yellow { return palette.ink }
        for (base, ink) in theme.statusInks where base == color { return ink }
        // Neutrals are already theme tokens; other hues deepen.
        guard let parts = color.converted(to: CGColorSpace(name: CGColorSpace.sRGB)!, intent: .defaultIntent, options: nil)?.components,
              parts.count >= 3, (parts[0...2].max() ?? 0) - (parts[0...2].min() ?? 0) > 0.1 else { return color }
        return HUDTheme.darkened(color, by: 0.4)
    }

    static let ink = rgb(0x121212)
    static let charcoal = rgb(0x262626)
    static var steel: CGColor { theme.steel }
    static let steel2 = rgb(0x6C6D68)
    static let plate = rgb(0xD3D4CF)
    static let plateHigh = rgb(0xE4E5E0)
    static let plateLow = rgb(0xB3B4AF)
    static let cyan = rgb(0x22A6F2)
    static let orange = rgb(0xF97501)
    static let red = rgb(0xE5383B)
    static let green = rgb(0x8CE03C)
    static let triStrip = [rgb(0xFF1AAC), rgb(0x00FFA2), rgb(0xFFFA00)]

    // Literal colours the spec repeats (§1.6 "frequently used"). Plates, the
    // ink on them and the small dark tags are the same in both themes.
    static let nearBlack = rgb(0x111111)
    static var titleText: CGColor { theme.titleText }
    static var crumbCurrent: CGColor { theme.crumbCurrent }
    static let counterBackground = rgb(0x151515)
    static let microTagBlack = rgb(0x161616)
    static let microTagText = rgb(0xE9E9E9)
    /// Light text on a surface that stays dark in both themes (dark chips, tags).
    static let onDarkTag = rgb(0xEEEEEE)
    static let notch = rgb(0x2A2A2A)
    static let rowPlate = [rgb(0xE4E5E0), rgb(0xD3D4CF), rgb(0xC6C7C2)]
    static let lightButton = [rgb(0xECEDE8), rgb(0xCDCEC9)]
    static let heroPlate = [rgb(0xE7E8E3), rgb(0xD1D2CD)]
    static var field: CGColor { theme.field }
    static var control: CGColor { theme.control }
    static var toggleTrack: CGColor { theme.toggleTrack }
    static var tabBar: CGColor { theme.tabBar }
    static var iconButton: CGColor { theme.iconButton }
    static var secondaryButton: CGColor { theme.secondaryButton }
    static var secondaryButtonHover: CGColor { theme.secondaryButtonHover }
    static var panel: CGColor { theme.panel }
    static var ringField: CGColor { theme.ringField }
    static var ringTab: CGColor { theme.ringTab }
    static var ringToggle: CGColor { theme.ringToggle }
    static var ringIcon: CGColor { theme.ringIcon }
    static var ringMenu: CGColor { theme.ringMenu }
    static var separator: CGColor { theme.separator }
    static var textOnDark: CGColor { theme.textOnDark }
    static var tabText: CGColor { theme.tabText }
    static var menuText: CGColor { theme.menuText }
    static var segmentedText: CGColor { theme.segmentedText }
    static var muted: CGColor { theme.muted }
    static var caption: CGColor { theme.caption }
    static var description: CGColor { theme.description }
    static let bracket = rgb(0x8A8B86)
    static let sliderTrack = rgb(0x252525)
    static let sliderTick = rgb(0x8D8E89)
    static let knobOff = rgb(0x8E8F8A)
    static var danger: CGColor { theme.danger }
}

/// The colours that change with Theme (Settings > Display > Theme). Dark is
/// the redesign as designed. Light keeps the light plates, the ink on them,
/// the small dark tags and the accent fills, and turns the dark family into
/// its light counterpart (`lightTone`): dark panels, fields, tabs and menus
/// become pale surfaces with darker rings, the light inks on them become dark
/// ink, and statuses get deeper variants where they are text or thin lines.
struct HUDTheme {
    let isLight: Bool
    let foreground: CGColor
    let panel, field, control, toggleTrack, tabBar, iconButton, secondaryButton, secondaryButtonHover: CGColor
    let ringField, ringTab, ringToggle, ringIcon, ringMenu, separator: CGColor
    let titleText, crumbCurrent, textOnDark, tabText, menuText, segmentedText, muted, caption, description, steel, danger: CGColor
    /// (status colour, its light-surface variant) pairs for `HUDColor.legible`.
    let statusInks: [(CGColor, CGColor)]

    static let dark = HUDTheme(light: false)
    static let light = HUDTheme(light: true)

    private init(light: Bool) {
        isLight = light
        func c(_ hex: UInt32, light lightHex: UInt32? = nil) -> CGColor {
            HUDColor.rgb(light ? lightHex ?? Self.lightTone(hex) : hex)
        }
        foreground = c(0xFFFFFF)
        panel = c(0x1A1A1A); field = c(0x1D1D1D); control = c(0x212121); toggleTrack = c(0x202020)
        tabBar = c(0x1E1E1E); iconButton = c(0x2B2B2B)
        secondaryButton = c(0x383838, light: 0xDEDEDA); secondaryButtonHover = c(0x484848, light: 0xCDCDC9)
        ringField = c(0x3A3A3A); ringTab = c(0x343434); ringToggle = c(0x393939); ringIcon = c(0x444444)
        ringMenu = c(0x3D3D3D); separator = c(0x333333)
        titleText = c(0xF4F4F4); crumbCurrent = c(0xE6E6E6); textOnDark = c(0xEEEEEE); tabText = c(0xD8D8D8)
        menuText = c(0xD9D9D9); segmentedText = c(0xCCCCCC); muted = c(0x777777); caption = c(0x9A9A9A)
        description = c(0xA9A9A9); steel = c(0x8A8A8A); danger = c(0xFF8A8C, light: 0xC62828)
        statusInks = light ? [(HUDColor.green, HUDColor.rgb(0x3F8F14)), (HUDColor.cyan, HUDColor.rgb(0x0B7DC0)),
                              (HUDColor.orange, HUDColor.rgb(0xD05A00)), (HUDColor.red, HUDColor.rgb(0xC8282C))] : []
    }

    /// The light counterpart of a neutral: below 0.32 luminance (surfaces
    /// and rings) it turns pale, the darker the more (1 - 4.5 v²); above it
    /// (inks) it turns dark, the lighter the darker. Tinted colours pass.
    static func lightTone(_ hex: UInt32) -> UInt32 {
        let channels = [CGFloat((hex >> 16) & 255), CGFloat((hex >> 8) & 255), CGFloat(hex & 255)].map { $0 / 255 }
        guard let high = channels.max(), let low = channels.min(), high - low <= 0.1 else { return hex }
        let v = channels.reduce(0, +) / 3
        let mapped = v < 0.32 ? 1 - 4.5 * v * v : 0.52 - (v - 0.32) * 0.68
        return channels.reduce(UInt32(0)) { result, channel in
            result << 8 | UInt32((min(1, max(0, channel + mapped - v)) * 255).rounded())
        }
    }

    /// `a` mixed toward `b` by `amount` (sRGB).
    static func mixed(_ a: CGColor, _ b: CGColor, _ amount: CGFloat) -> CGColor {
        let space = CGColorSpace(name: CGColorSpace.sRGB)!
        guard let x = a.converted(to: space, intent: .defaultIntent, options: nil)?.components,
              let y = b.converted(to: space, intent: .defaultIntent, options: nil)?.components, x.count >= 3, y.count >= 3 else { return a }
        return CGColor(srgbRed: x[0] + (y[0] - x[0]) * amount, green: x[1] + (y[1] - x[1]) * amount,
                       blue: x[2] + (y[2] - x[2]) * amount, alpha: 1)
    }

    /// `color` blended toward black by `amount` (sRGB).
    static func darkened(_ color: CGColor, by amount: CGFloat) -> CGColor {
        guard let rgb = color.converted(to: CGColorSpace(name: CGColorSpace.sRGB)!, intent: .defaultIntent, options: nil),
              let parts = rgb.components, parts.count >= 3 else { return color }
        return CGColor(srgbRed: parts[0] * (1 - amount), green: parts[1] * (1 - amount), blue: parts[2] * (1 - amount),
                       alpha: rgb.alpha)
    }
}

/// The redesign's accent family. The default theme colour (FAD41F) keeps the
/// signature yellow FFFA00 with its exact high/low stops; any other theme
/// colour is the accent itself, with high/low derived by the same relations
/// those stops have to FFFA00 (high: 42 % toward white; low and deep: a
/// slightly warmer hue, less saturation and brightness).
struct HUDAccentPalette {
    /// The normalised theme colour this family was made from.
    let accent: UInt32
    let yellow: CGColor, yellowHigh: CGColor, yellowLow: CGColor, yellowDeep: CGColor
    let primaryButton: [CGColor]
    /// The accent as text or a thin line on a theme surface (`HUDColor.accentInk`).
    let ink: CGColor

    static let standardAccent: UInt32 = 0xFAD41F
    static let standard = HUDAccentPalette(accentHex: "FAD41F")

    /// In light, `ink` is the low stop 42 % toward black (the default reads as
    /// a deep gold on pale surfaces); in dark it is the accent itself.
    init(accentHex: String, light: Bool = false) {
        let raw = accentHex.trimmingCharacters(in: .whitespacesAndNewlines).replacingOccurrences(of: "#", with: "")
        let value = raw.count == 6 ? UInt32(raw, radix: 16) ?? Self.standardAccent : Self.standardAccent
        accent = value
        if value == Self.standardAccent {
            yellow = HUDColor.rgb(0xFFFA00); yellowHigh = HUDColor.rgb(0xFFFC6B)
            yellowLow = HUDColor.rgb(0xE9CF12); yellowDeep = HUDColor.rgb(0xF1DD10)
        } else {
            let r = CGFloat((value >> 16) & 255) / 255, g = CGFloat((value >> 8) & 255) / 255, b = CGFloat(value & 255) / 255
            let base = NSColor(srgbRed: r, green: g, blue: b, alpha: 1)
            yellow = CGColor(srgbRed: r, green: g, blue: b, alpha: 1)
            yellowHigh = CGColor(srgbRed: r + (1 - r) * 0.42, green: g + (1 - g) * 0.42, blue: b + (1 - b) * 0.42, alpha: 1)
            yellowLow = Self.shifted(base, hue: -6.08, saturation: 0.923, brightness: 0.914)
            yellowDeep = Self.shifted(base, hue: -4.16, saturation: 0.934, brightness: 0.945)
        }
        primaryButton = [yellowHigh, yellow, yellowLow]
        ink = light ? HUDTheme.darkened(yellowLow, by: 0.42) : yellow
    }

    /// `color` (sRGB) with its hue turned by `hue` degrees and its HSB
    /// saturation / brightness scaled.
    private static func shifted(_ color: NSColor, hue: CGFloat, saturation: CGFloat, brightness: CGFloat) -> CGColor {
        let h = (color.hueComponent * 360 + hue + 360).truncatingRemainder(dividingBy: 360) / 60
        let s = min(1, color.saturationComponent * saturation), v = min(1, color.brightnessComponent * brightness)
        let c = v * s, x = c * (1 - abs(h.truncatingRemainder(dividingBy: 2) - 1)), m = v - c
        let (r, g, b): (CGFloat, CGFloat, CGFloat)
        switch Int(h) {
        case 0: (r, g, b) = (c, x, 0)
        case 1: (r, g, b) = (x, c, 0)
        case 2: (r, g, b) = (0, c, x)
        case 3: (r, g, b) = (0, x, c)
        case 4: (r, g, b) = (x, 0, c)
        default: (r, g, b) = (c, 0, x)
        }
        return CGColor(srgbRed: r + m, green: g + m, blue: b + m, alpha: 1)
    }
}

/// The type scale (spec §1.7). Sizes are content-box px, tracking is in em and
/// line height is a multiple of the size, as in the demo's CSS.
struct HUDTextStyle: Equatable {
    var size: CGFloat
    var weight: HUDFontWeight
    var color: CGColor
    var lineHeight: CGFloat = 1
    var tracking: CGFloat = 0
    var uppercase = false
    var tabular = false

    func sized(_ value: CGFloat) -> HUDTextStyle { var copy = self; copy.size = value; return copy }
    func colored(_ value: CGColor) -> HUDTextStyle { var copy = self; copy.color = value; return copy }
    func weighted(_ value: HUDFontWeight) -> HUDTextStyle { var copy = self; copy.weight = value; return copy }
}

/// Styles whose colour follows Theme are computed (cheap value types).
enum HUDType {
    static var moduleTitle: HUDTextStyle { HUDTextStyle(size: 26, weight: .bold, color: HUDColor.titleText, tracking: 0.08, uppercase: true) }
    static var titleSubtitle: HUDTextStyle { HUDTextStyle(size: 13, weight: .medium, color: HUDColor.steel, tracking: 0.2) }
    static var breadcrumb: HUDTextStyle { HUDTextStyle(size: 10.5, weight: .regular, color: HUDColor.steel, tracking: 0.06, uppercase: true) }
    static var ruleCounter: HUDTextStyle { HUDTextStyle(size: 11, weight: .bold, color: HUDColor.yellow, tabular: true) }
    static let rowLabel = HUDTextStyle(size: 15.5, weight: .medium, color: HUDColor.ink, lineHeight: 1.2)
    static let rowSublabel = HUDTextStyle(size: 11, weight: .regular, color: HUDColor.steel2, lineHeight: 1.2)
    static let button = HUDTextStyle(size: 12, weight: .bold, color: HUDColor.nearBlack, tracking: 0.08, uppercase: true)
    static let pill = HUDTextStyle(size: 14, weight: .medium, color: HUDColor.nearBlack, tracking: 0.06)
    static var toggleLabel: HUDTextStyle { HUDTextStyle(size: 10, weight: .bold, color: HUDColor.muted, tracking: 0.12) }
    static var dropdownValue: HUDTextStyle { HUDTextStyle(size: 12.5, weight: .medium, color: HUDColor.textOnDark) }
    static var menuItem: HUDTextStyle { HUDTextStyle(size: 12.5, weight: .regular, color: HUDColor.menuText) }
    static var segmented: HUDTextStyle { HUDTextStyle(size: 12.5, weight: .medium, color: HUDColor.segmentedText) }
    static var tab: HUDTextStyle { HUDTextStyle(size: 13, weight: .medium, color: HUDColor.tabText, tracking: 0.04) }
    static var tabKeycap: HUDTextStyle { HUDTextStyle(size: 9, weight: .bold, color: HUDColor.tone(0x8A8A8A)) }
    static let chip = HUDTextStyle(size: 9.5, weight: .bold, color: HUDColor.nearBlack, tracking: 0.1, uppercase: true)
    static let newBadge = HUDTextStyle(size: 8, weight: .black, color: HUDColor.nearBlack, tracking: 0.06)
    static var stamp: HUDTextStyle { HUDTextStyle(size: 10, weight: .black, color: HUDColor.accentInk, tracking: 0.16, uppercase: true) }
    static var numeral: HUDTextStyle { HUDTextStyle(size: 64, weight: .light, color: HUDColor.titleText, lineHeight: 0.9, tracking: -0.02, tabular: true) }
    static var numeralUnit: HUDTextStyle { HUDTextStyle(size: 64 * 0.34, weight: .medium, color: HUDColor.accentInk) }
    static var label: HUDTextStyle { HUDTextStyle(size: 10, weight: .regular, color: HUDColor.steel, tracking: 0.1, uppercase: true) }
    static var labelCJK: HUDTextStyle { HUDTextStyle(size: 11.5, weight: .regular, color: HUDColor.steel, tracking: 0.12) }
    static var statValue: HUDTextStyle { HUDTextStyle(size: 18, weight: .regular, color: HUDColor.tone(0xF2F2F2), tabular: true) }
    static var statUnit: HUDTextStyle { HUDTextStyle(size: 11, weight: .regular, color: HUDColor.steel) }
    static let readout = HUDTextStyle(size: 12, weight: .bold, color: HUDColor.counterBackground, tabular: true)
    static let readoutBracket = readout.colored(HUDColor.bracket)
    static let sliderBubble = HUDTextStyle(size: 11, weight: .bold, color: HUDColor.nearBlack, tabular: true)
    static var field: HUDTextStyle { HUDTextStyle(size: 13, weight: .regular, color: HUDColor.textOnDark) }
    static var emptyTitle: HUDTextStyle { HUDTextStyle(size: 20, weight: .bold, color: HUDColor.tone(0xE9E9E9), tracking: 0.24) }
    static let keycap = HUDTextStyle(size: 11, weight: .bold, color: HUDColor.nearBlack)
    static var toastTitle: HUDTextStyle { HUDTextStyle(size: 14, weight: .bold, color: HUDColor.foreground(1), tracking: 0.22, uppercase: true) }
    static var toastCaption: HUDTextStyle { HUDTextStyle(size: 11.5, weight: .regular, color: HUDColor.caption, tracking: 0.3) }
    static var confirmTitle: HUDTextStyle { HUDTextStyle(size: 17, weight: .medium, color: HUDColor.titleText, lineHeight: 1.25) }
    static var confirmDescription: HUDTextStyle { HUDTextStyle(size: 12, weight: .regular, color: HUDColor.description, lineHeight: 1.5) }
    static var confirmCountdown: HUDTextStyle { HUDTextStyle(size: 22, weight: .bold, color: HUDColor.accentInk, tabular: true) }
    static let microTag = HUDTextStyle(size: 5.5, weight: .bold, color: HUDColor.microTagText, lineHeight: 7 / 5.5, tracking: 0.08, uppercase: true)
}

/// Shared geometry (spec §1.4, §2.1, §2.3). Component-specific dimensions stay
/// with their component; these are the ones layouts across modules agree on.
enum HUDMetric {
    /// The centre content box, in content-box px (the spec's stage px).
    static let box = CGSize(width: 590, height: 510)
    /// Module header: breadcrumb 14 + 6 + title row 30 + 10 + rule 9 + 12.
    static let headerHeight: CGFloat = 81
    static var bodyHeight: CGFloat { box.height - headerHeight }
    static let rowHeight: CGFloat = 50
    static let compactRowHeight: CGFloat = 44
    static let rowGap: CGFloat = 6
    static let rowChamfer: CGFloat = 11
    static let buttonChamfer: CGFloat = 9
    static let sliderLabelColumn: CGFloat = 170
}

/// Stacking inside the tilted plane (spec §1.5).
enum HUDLayerOrder {
    static let content: CGFloat = 0
    static let dim: CGFloat = 15
    static let confirmBar: CGFloat = 20
    static let toast: CGFloat = 25
    static let bootLog: CGFloat = 30
    /// Inside the content box: menus and pop-overs, then the slider bubble.
    static let menu: CGFloat = 30
    static let bubble: CGFloat = 40
}

/// The spec's easings mapped to Core Animation (spec §0, §1.9).
enum HUDEasing {
    /// `E`, cubic-bezier(.2,.8,.2,1): the default motion.
    static let standard = CAMediaTimingFunction(controlPoints: 0.2, 0.8, 0.2, 1)
    /// `SNAP`, cubic-bezier(.2,.9,.1,1): toggle knob, confirm band, pop.
    static let snap = CAMediaTimingFunction(controlPoints: 0.2, 0.9, 0.1, 1)
    static let ease = CAMediaTimingFunction(controlPoints: 0.25, 0.1, 0.25, 1)
    static let easeIn = CAMediaTimingFunction(controlPoints: 0.42, 0, 1, 1)
    static let easeOut = CAMediaTimingFunction(controlPoints: 0, 0, 0.58, 1)
    static let easeInOut = CAMediaTimingFunction(controlPoints: 0.42, 0, 0.58, 1)
    static let linear = CAMediaTimingFunction(name: .linear)
}

/// Every duration is finite and short; Reduce Motion collapses all of them.
enum HUDDuration {
    static let hover: CFTimeInterval = 0.22
    static let press: CFTimeInterval = 0.10
    static let sheen: CFTimeInterval = 0.70
    static let entrance: CFTimeInterval = 0.38
    static let staggerStep: CFTimeInterval = 0.036
    static let staggerBase: CFTimeInterval = 0.06
    static let staggerCap = 14
    static let leave: CFTimeInterval = 0.13
    static let titleWipe: CFTimeInterval = 0.45
    static let ruleWipe: CFTimeInterval = 0.42
    static let ruleDelay: CFTimeInterval = 0.05
    static let menuWipe: CFTimeInterval = 0.18
    static let tabOn: CFTimeInterval = 0.20
    static let knob: CFTimeInterval = 0.18
    static let toggleLabel: CFTimeInterval = 0.14
    static let caret: CFTimeInterval = 0.16
    static let accentBar: CFTimeInterval = 0.14
    static let thumb: CFTimeInterval = 0.12
    static let bubbleFade: CFTimeInterval = 0.14
    static let bubbleScale: CFTimeInterval = 0.18
    static let bandIn: CFTimeInterval = 0.30
    static let bandOut: CFTimeInterval = 0.18
    static let confirmText: CFTimeInterval = 0.30
    static let confirmTextDelay: CFTimeInterval = 0.12
    static let pillDelay: CFTimeInterval = 0.16
    static let pillStep: CFTimeInterval = 0.06
    static let confirmCallback: CFTimeInterval = 0.12
    static let dim: CFTimeInterval = 0.22
    static let dimFilter: CFTimeInterval = 0.25
    static let toastVisible: CFTimeInterval = 1.5
    static let toastOut: CFTimeInterval = 0.25
    static let pop: CFTimeInterval = 0.32
    static let taskFlip: CFTimeInterval = 0.25
    static let perspective: CFTimeInterval = 0.5
    /// "Keep this UI scale?" reverts after this many seconds (spec §2.18).
    static let scaleConfirmation: TimeInterval = 15
}
