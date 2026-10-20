import AppKit
import QuartzCore

/// Design tokens of the centre redesign (spec §1.6). Colours are sRGB and
/// created once; components never build a colour per frame.
enum HUDColor {
    static func rgb(_ hex: UInt32, _ alpha: CGFloat = 1) -> CGColor {
        CGColor(srgbRed: CGFloat((hex >> 16) & 255) / 255, green: CGFloat((hex >> 8) & 255) / 255,
                blue: CGFloat(hex & 255) / 255, alpha: alpha)
    }
    static func white(_ alpha: CGFloat) -> CGColor { CGColor(srgbRed: 1, green: 1, blue: 1, alpha: alpha) }
    static func black(_ alpha: CGFloat) -> CGColor { CGColor(srgbRed: 0, green: 0, blue: 0, alpha: alpha) }
    static let clear = black(0)

    // Signature palette.
    static let yellow = rgb(0xFFFA00)
    static let yellowHigh = rgb(0xFFFC6B)
    static let yellowLow = rgb(0xE9CF12)
    static let ink = rgb(0x121212)
    static let charcoal = rgb(0x262626)
    static let steel = rgb(0x8A8A8A)
    static let steel2 = rgb(0x6C6D68)
    static let plate = rgb(0xD3D4CF)
    static let plateHigh = rgb(0xE4E5E0)
    static let plateLow = rgb(0xB3B4AF)
    static let cyan = rgb(0x22A6F2)
    static let orange = rgb(0xF97501)
    static let red = rgb(0xE5383B)
    static let green = rgb(0x8CE03C)
    static let triStrip = [rgb(0xFF1AAC), rgb(0x00FFA2), rgb(0xFFFA00)]

    // Literal colours the spec repeats (§1.6 "frequently used").
    static let nearBlack = rgb(0x111111)
    static let titleText = rgb(0xF4F4F4)
    static let crumbCurrent = rgb(0xE6E6E6)
    static let counterBackground = rgb(0x151515)
    static let microTagBlack = rgb(0x161616)
    static let microTagText = rgb(0xE9E9E9)
    static let notch = rgb(0x2A2A2A)
    static let rowPlate = [rgb(0xE4E5E0), rgb(0xD3D4CF), rgb(0xC6C7C2)]
    static let lightButton = [rgb(0xECEDE8), rgb(0xCDCEC9)]
    static let primaryButton = [rgb(0xFFFC6B), rgb(0xFFFA00), rgb(0xE9CF12)]
    static let heroPlate = [rgb(0xE7E8E3), rgb(0xD1D2CD)]
    static let field = rgb(0x1D1D1D)
    static let control = rgb(0x212121)
    static let toggleTrack = rgb(0x202020)
    static let tabBar = rgb(0x1E1E1E)
    static let iconButton = rgb(0x2B2B2B)
    static let secondaryButton = rgb(0x383838)
    static let secondaryButtonHover = rgb(0x484848)
    static let panel = rgb(0x1A1A1A)
    static let ringField = rgb(0x3A3A3A)
    static let ringTab = rgb(0x343434)
    static let ringToggle = rgb(0x393939)
    static let ringIcon = rgb(0x444444)
    static let ringMenu = rgb(0x3D3D3D)
    static let separator = rgb(0x333333)
    static let textOnDark = rgb(0xEEEEEE)
    static let tabText = rgb(0xD8D8D8)
    static let menuText = rgb(0xD9D9D9)
    static let segmentedText = rgb(0xCCCCCC)
    static let muted = rgb(0x777777)
    static let caption = rgb(0x9A9A9A)
    static let description = rgb(0xA9A9A9)
    static let bracket = rgb(0x8A8B86)
    static let sliderTrack = rgb(0x252525)
    static let sliderTick = rgb(0x8D8E89)
    static let knobOff = rgb(0x8E8F8A)
    static let danger = rgb(0xFF8A8C)
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

enum HUDType {
    static let moduleTitle = HUDTextStyle(size: 26, weight: .bold, color: HUDColor.titleText, tracking: 0.08, uppercase: true)
    static let titleSubtitle = HUDTextStyle(size: 13, weight: .medium, color: HUDColor.steel, tracking: 0.2)
    static let breadcrumb = HUDTextStyle(size: 10.5, weight: .regular, color: HUDColor.steel, tracking: 0.06, uppercase: true)
    static let ruleCounter = HUDTextStyle(size: 11, weight: .bold, color: HUDColor.yellow, tabular: true)
    static let rowLabel = HUDTextStyle(size: 15.5, weight: .medium, color: HUDColor.ink, lineHeight: 1.2)
    static let rowSublabel = HUDTextStyle(size: 11, weight: .regular, color: HUDColor.steel2, lineHeight: 1.2)
    static let button = HUDTextStyle(size: 12, weight: .bold, color: HUDColor.nearBlack, tracking: 0.08, uppercase: true)
    static let pill = HUDTextStyle(size: 14, weight: .medium, color: HUDColor.nearBlack, tracking: 0.06)
    static let toggleLabel = HUDTextStyle(size: 10, weight: .bold, color: HUDColor.muted, tracking: 0.12)
    static let dropdownValue = HUDTextStyle(size: 12.5, weight: .medium, color: HUDColor.textOnDark)
    static let menuItem = HUDTextStyle(size: 12.5, weight: .regular, color: HUDColor.menuText)
    static let segmented = HUDTextStyle(size: 12.5, weight: .medium, color: HUDColor.segmentedText)
    static let tab = HUDTextStyle(size: 13, weight: .medium, color: HUDColor.tabText, tracking: 0.04)
    static let tabKeycap = HUDTextStyle(size: 9, weight: .bold, color: HUDColor.rgb(0x8A8A8A))
    static let chip = HUDTextStyle(size: 9.5, weight: .bold, color: HUDColor.nearBlack, tracking: 0.1, uppercase: true)
    static let newBadge = HUDTextStyle(size: 8, weight: .black, color: HUDColor.nearBlack, tracking: 0.06)
    static let stamp = HUDTextStyle(size: 10, weight: .black, color: HUDColor.yellow, tracking: 0.16, uppercase: true)
    static let numeral = HUDTextStyle(size: 64, weight: .light, color: HUDColor.titleText, lineHeight: 0.9, tracking: -0.02, tabular: true)
    static let numeralUnit = HUDTextStyle(size: 64 * 0.34, weight: .medium, color: HUDColor.yellow)
    static let label = HUDTextStyle(size: 10, weight: .regular, color: HUDColor.steel, tracking: 0.1, uppercase: true)
    static let labelCJK = HUDTextStyle(size: 11.5, weight: .regular, color: HUDColor.steel, tracking: 0.12)
    static let statValue = HUDTextStyle(size: 18, weight: .regular, color: HUDColor.rgb(0xF2F2F2), tabular: true)
    static let statUnit = HUDTextStyle(size: 11, weight: .regular, color: HUDColor.steel)
    static let readout = HUDTextStyle(size: 12, weight: .bold, color: HUDColor.counterBackground, tabular: true)
    static let readoutBracket = readout.colored(HUDColor.bracket)
    static let sliderBubble = HUDTextStyle(size: 11, weight: .bold, color: HUDColor.nearBlack, tabular: true)
    static let field = HUDTextStyle(size: 13, weight: .regular, color: HUDColor.textOnDark)
    static let emptyTitle = HUDTextStyle(size: 20, weight: .bold, color: HUDColor.microTagText, tracking: 0.24)
    static let keycap = HUDTextStyle(size: 11, weight: .bold, color: HUDColor.nearBlack)
    static let toastTitle = HUDTextStyle(size: 14, weight: .bold, color: HUDColor.white(1), tracking: 0.22, uppercase: true)
    static let toastCaption = HUDTextStyle(size: 11.5, weight: .regular, color: HUDColor.caption, tracking: 0.3)
    static let confirmTitle = HUDTextStyle(size: 17, weight: .medium, color: HUDColor.titleText, lineHeight: 1.25)
    static let confirmDescription = HUDTextStyle(size: 12, weight: .regular, color: HUDColor.description, lineHeight: 1.5)
    static let confirmCountdown = HUDTextStyle(size: 22, weight: .bold, color: HUDColor.yellow, tabular: true)
    static let microTag = HUDTextStyle(size: 5.5, weight: .bold, color: HUDColor.microTagText, lineHeight: 7 / 5.5, tracking: 0.08, uppercase: true)
}
