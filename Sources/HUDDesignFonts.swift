import AppKit
import CoreText

/// The shared font folder, Resources/Fonts (EndfieldHUD.app/Contents/Resources/Fonts).
/// Its manifest.json is the only list of fonts: every family whose files exist
/// is registered for this process alone (nothing is installed into macOS), so
/// adding a font is a file drop plus a manifest entry.
enum HUDFontRegistry {
    enum Role: String { case latin, cjk }

    struct Family: Equatable {
        let name: String
        let role: Role
        /// BCP 47 tags of the text this family draws, e.g. "zh-Hant" or "ja".
        let languages: [String]
        let fonts: [URL]
    }

    static let directory = "Fonts"

    /// Registered families in manifest order. The first access registers the
    /// fonts synchronously (tens of milliseconds); the overlay controller
    /// prewarms it on a utility queue at launch so no opening waits for it.
    static let families: [Family] = register(manifestFamilies())

    static func prewarm() {
        DispatchQueue.global(qos: .utility).async { _ = families }
    }

    static func family(role: Role, language: String? = nil) -> Family? {
        families.first { $0.role == role && (language.map($0.languages.contains) ?? true) }
    }

    /// Families whose files are present. Missing files (a font not yet dropped
    /// into the folder) leave their family out instead of failing the HUD.
    static func manifestFamilies(directory url: URL? = HUDResources.url(for: directory)) -> [Family] {
        guard let url, let data = try? Data(contentsOf: url.appendingPathComponent("manifest.json")),
              let manifest = try? JSONDecoder().decode(Manifest.self, from: data), manifest.schemaVersion == 2 else { return [] }
        return manifest.families.compactMap { family in
            guard let role = Role(rawValue: family.role), family.platforms.contains("macos") else { return nil }
            let fonts = family.files.map(\.file).filter { $0.lowercased().hasSuffix(".ttf") || $0.lowercased().hasSuffix(".otf") }
                .map { url.appendingPathComponent($0) }
            guard !fonts.isEmpty, fonts.allSatisfy({ FileManager.default.fileExists(atPath: $0.path) }) else { return nil }
            return Family(name: family.family, role: role, languages: family.languages ?? [], fonts: fonts)
        }
    }

    private static func register(_ families: [Family]) -> [Family] {
        var phase = HUDStartupTrace.begin()
        defer { HUDStartupTrace.end("fonts.register", since: &phase) }
        let urls = families.flatMap(\.fonts)
        guard !urls.isEmpty else { return [] }
        var failed = Set<URL>()
        // Process scope: the handler runs before this call returns.
        CTFontManagerRegisterFontURLs(urls as CFArray, .process, true) { errors, _ in
            for error in errors as? [NSError] ?? [] {
                // A second registration in the same process (tests) is harmless.
                guard CTFontManagerError(rawValue: error.code) != .alreadyRegistered else { continue }
                if let url = (error.userInfo[kCTFontManagerErrorFontURLsKey as String] as? [URL])?.first { failed.insert(url) }
            }
            return true
        }
        return families.filter { $0.fonts.allSatisfy { !failed.contains($0) } }
    }

    private struct Manifest: Decodable {
        struct Family: Decodable {
            struct File: Decodable { let file: String }
            let family: String
            let role: String
            let languages: [String]?
            let platforms: [String]
            let files: [File]
        }
        let schemaVersion: Int
        let families: [Family]
    }
}

/// The five embedded HarmonyOS Sans weights. CSS 600 has no file, so the
/// design's 600 requests render Bold, as browsers do with the demo (spec §1.7).
enum HUDFontWeight: Int, CaseIterable {
    case light = 300, regular = 400, medium = 500, semibold = 600, bold = 700, black = 900

    var resolved: HUDFontWeight { self == .semibold ? .bold : self }
    /// CoreText weight traits of the embedded faces; CJK families match the
    /// nearest face (Noto variable instances, or a system family's weights).
    var trait: CGFloat {
        switch resolved {
        case .light: return -0.23
        case .regular: return 0
        case .medium: return 0.2
        case .semibold, .bold: return 0.4
        case .black: return 0.8
        }
    }
}

/// The CJK region of text. Latin glyphs always come from HarmonyOS Sans; Han,
/// Kana and Hangul come from the Noto family of this region, and the tag is
/// attached to the string (kCTLanguageAttributeName) so shapes follow it.
enum HUDTextLanguage {
    static let simplifiedChinese = "zh-Hans", traditionalChinese = "zh-Hant", japanese = "ja", korean = "ko"
    static let regions = [simplifiedChinese, traditionalChinese, japanese, korean]

    /// The UI language decides; English and System use the first CJK language
    /// the user prefers (Simplified Chinese when there is none).
    static func tag(for language: AppLanguage = L10n.resolvedLanguage, preferredLanguages: [String]? = nil) -> String {
        switch language {
        case .simplifiedChinese: return simplifiedChinese
        case .traditionalChinese: return traditionalChinese
        case .japanese: return japanese
        case .korean: return korean
        case .english, .system:
            guard let preferredLanguages else { return systemTag }
            return firstRegion(in: preferredLanguages)
        }
    }

    private static func firstRegion(in preferredLanguages: [String]) -> String {
        for identifier in preferredLanguages {
            switch L10n.resolveLanguage(preferredLanguages: [identifier]) {
            case .simplifiedChinese: return simplifiedChinese
            case .traditionalChinese: return traditionalChinese
            case .japanese: return japanese
            case .korean: return korean
            default: continue
            }
        }
        return simplifiedChinese
    }

    /// The region of the user's preferred languages, kept with its reading
    /// (`SystemLanguages`): fonts resolve it for every text layer.
    private static let systemTagLock = NSLock()
    private static var systemTagCache: (identifiers: [String], tag: String)?
    private static var systemTag: String {
        let identifiers = SystemLanguages.current.identifiers
        systemTagLock.lock(); defer { systemTagLock.unlock() }
        if let cached = systemTagCache, cached.identifiers == identifiers { return cached.tag }
        let tag = firstRegion(in: identifiers)
        systemTagCache = (identifiers, tag)
        return tag
    }

    /// A language's own name (e.g. 日本語 in the Language menu) is set in its own face.
    static func tag(naming language: AppLanguage) -> String? {
        switch language {
        case .simplifiedChinese: return simplifiedChinese
        case .traditionalChinese: return traditionalChinese
        case .japanese: return japanese
        case .korean: return korean
        case .english, .system: return nil
        }
    }

    /// Used only when a region's Noto file is absent from Resources/Fonts. The
    /// shared folder ships SC, TC, JP and KR, so this is a safety net for a
    /// development tree or a future region, never the normal path.
    static func systemFamily(for tag: String) -> String {
        switch tag {
        case traditionalChinese: return "PingFang TC"
        case japanese: return "Hiragino Sans"
        case korean: return "Apple SD Gothic Neo"
        default: return "PingFang SC"
        }
    }
}

/// One cached CTFont per (size, weight, region, figures, slant). Fonts are
/// built once and reused for every layer and frame; the cache only grows with
/// the type scale's distinct sizes.
enum HUDFontResolver {
    private struct Key: Hashable {
        let size: CGFloat, weight: HUDFontWeight, language: String, tabular: Bool, oblique: Bool
    }
    private static var cache: [Key: CTFont] = [:]
    private static let lock = NSLock()

    /// `oblique` slants the upright faces as browsers synthesize italic
    /// (HarmonyOS Sans and Noto ship no italic).
    static func font(size: CGFloat, weight: HUDFontWeight, language: String, tabular: Bool = false, oblique: Bool = false) -> CTFont {
        let key = Key(size: size, weight: weight.resolved, language: language, tabular: tabular, oblique: oblique)
        lock.lock(); defer { lock.unlock() }
        if let cached = cache[key] { return cached }
        var slant = CGAffineTransform(a: 1, b: 0, c: 0.2, d: 1, tx: 0, ty: 0)
        let font = oblique ? CTFontCreateWithFontDescriptor(descriptor(for: key), size, &slant)
            : CTFontCreateWithFontDescriptor(descriptor(for: key), size, nil)
        cache[key] = font
        return font
    }

    /// Family names in fallback order after HarmonyOS Sans, for tests and diagnostics.
    static func cascadeFamilies(language: String) -> [String] {
        let regional = HUDFontRegistry.family(role: .cjk, language: language)?.name ?? HUDTextLanguage.systemFamily(for: language)
        let others = HUDTextLanguage.regions.filter { $0 != language }.compactMap { HUDFontRegistry.family(role: .cjk, language: $0)?.name }
        return [regional] + others.filter { $0 != regional }
    }

    static var latinFamily: String? { HUDFontRegistry.family(role: .latin)?.name }

    private static func descriptor(for key: Key) -> CTFontDescriptor {
        let weight = [kCTFontWeightTrait: key.weight.trait] as CFDictionary
        let cascade = cascadeFamilies(language: key.language).map {
            CTFontDescriptorCreateWithAttributes([kCTFontFamilyNameAttribute: $0, kCTFontTraitsAttribute: weight] as CFDictionary)
        }
        var attributes: [CFString: Any] = [kCTFontCascadeListAttribute: cascade]
        if key.tabular {
            attributes[kCTFontFeatureSettingsAttribute] = [[kCTFontFeatureTypeIdentifierKey: kNumberSpacingType,
                                                            kCTFontFeatureSelectorIdentifierKey: kMonospacedNumbersSelector]]
        }
        let base: CTFontDescriptor
        if let latinFamily {
            base = CTFontDescriptorCreateWithAttributes([kCTFontFamilyNameAttribute: latinFamily, kCTFontTraitsAttribute: weight] as CFDictionary)
        } else {
            // Without the font folder (a stripped test host) the system UI face stands in.
            base = CTFontCopyFontDescriptor(NSFont.systemFont(ofSize: key.size, weight: NSFont.Weight(key.weight.trait)))
        }
        return CTFontDescriptorCreateCopyWithAttributes(base, attributes as CFDictionary)
    }
}

// MARK: Text outside the redesigned centre

extension HUDFontWeight {
    /// The embedded face nearest an AppKit weight: semibold renders Bold, as
    /// the design's 600 does, and heavy renders Black.
    init(_ weight: NSFont.Weight) {
        switch weight.rawValue {
        case ..<(-0.2): self = .light
        case ..<0.15: self = .regular
        case ..<0.27: self = .medium
        case ..<0.5: self = .bold
        default: self = .black
        }
    }
}

extension HUDFontResolver {
    /// The standard faces as an NSFont (the same cached CTFont, toll-free
    /// bridged) for AppKit controls, attributed strings, CG drawing and text
    /// measurement outside the centre. CATextLayers use HUDStandardTextLayer.
    static func nsFont(size: CGFloat, weight: HUDFontWeight = .regular, tabular: Bool = false, oblique: Bool = false,
                       language: String = HUDTextLanguage.tag()) -> NSFont {
        font(size: size, weight: weight, language: language, tabular: tabular, oblique: oblique) as NSFont
    }
}

/// AppKit rich text whose runs name no family (floating notes store such runs
/// in the system face) is shown and edited in the standard faces, then
/// converted back before it is captured, so saved runs keep "no family". A
/// slanted standard face stands for italic: HarmonyOS Sans has no italic.
enum HUDStandardRichText {
    static func isStandard(_ font: NSFont) -> Bool {
        !font.fontName.hasPrefix(".") && font.familyName == HUDFontResolver.latinFamily
    }
    static func isBold(_ font: NSFont) -> Bool {
        NSFontManager.shared.traits(of: font).contains(.boldFontMask)
            || ((CTFontCopyTraits(font) as? [CFString: Any])?[kCTFontWeightTrait] as? CGFloat ?? 0) >= 0.3
    }
    static func isItalic(_ font: NSFont) -> Bool {
        NSFontManager.shared.traits(of: font).contains(.italicFontMask) || CTFontGetMatrix(font).c != 0
    }
    static func font(size: CGFloat, bold: Bool, italic: Bool) -> NSFont {
        HUDFontResolver.nsFont(size: size, weight: bold ? .bold : .regular, oblique: italic)
    }

    /// System-face runs in the standard faces, for display and editing.
    static func displayed(_ text: NSAttributedString) -> NSAttributedString {
        replacingFonts(in: text) { font, _ in
            font.fontName.hasPrefix(".") ? self.font(size: font.pointSize, bold: isBold(font), italic: isItalic(font)) : nil
        }
    }

    /// Standard-face runs back in the system face with the same traits,
    /// including the CJK face TextKit writes into runs the Latin face lacks
    /// (as it wrote the system face's own private fallback before).
    static func stored(_ text: NSAttributedString) -> NSAttributedString {
        replacingFonts(in: text) { font, characters in
            guard isStandard(font) || isFallback(font, for: characters) else { return nil }
            let system = NSFont.systemFont(ofSize: font.pointSize, weight: isBold(font) ? .bold : .regular)
            return isItalic(font) ? NSFontManager.shared.convert(system, toHaveTrait: .italicFontMask) : system
        }
    }

    /// A bundled CJK face on characters the Latin face cannot draw.
    private static func isFallback(_ font: NSFont, for characters: String) -> Bool {
        guard let family = font.familyName, let covered = latinCoverage,
              HUDFontRegistry.families.contains(where: { $0.role == .cjk && $0.name == family }) else { return false }
        return !characters.unicodeScalars.contains { !$0.properties.isWhitespace && covered.contains($0) }
    }
    private static let latinCoverage = HUDFontResolver.latinFamily.map {
        (CTFontCreateWithFontDescriptor(CTFontDescriptorCreateWithAttributes([kCTFontFamilyNameAttribute: $0] as CFDictionary), 12, nil) as NSFont)
            .coveredCharacterSet
    }

    private static func replacingFonts(in text: NSAttributedString, _ replacement: (NSFont, String) -> NSFont?) -> NSAttributedString {
        let result = NSMutableAttributedString(attributedString: text), string = text.string as NSString
        text.enumerateAttribute(.font, in: NSRange(location: 0, length: text.length)) { value, range, _ in
            if let font = value as? NSFont, let next = replacement(font, string.substring(with: range)) {
                result.addAttribute(.font, value: next, range: range)
            }
        }
        return result
    }
}
