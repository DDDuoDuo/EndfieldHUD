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
    /// fonts synchronously (a few milliseconds); normal launches prewarm it on a
    /// utility queue so the first HUD frame never waits for it.
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
    static func tag(for language: AppLanguage = L10n.resolvedLanguage,
                    preferredLanguages: [String] = Locale.preferredLanguages) -> String {
        switch language {
        case .simplifiedChinese: return simplifiedChinese
        case .traditionalChinese: return traditionalChinese
        case .japanese: return japanese
        case .korean: return korean
        case .english, .system:
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

/// One cached CTFont per (size, weight, region, figures). Fonts are built once
/// and reused for every layer and frame; the cache only grows with the type
/// scale's distinct sizes.
enum HUDFontResolver {
    private struct Key: Hashable {
        let size: CGFloat, weight: HUDFontWeight, language: String, tabular: Bool
    }
    private static var cache: [Key: CTFont] = [:]
    private static let lock = NSLock()

    static func font(size: CGFloat, weight: HUDFontWeight, language: String, tabular: Bool = false) -> CTFont {
        let key = Key(size: size, weight: weight.resolved, language: language, tabular: tabular)
        lock.lock(); defer { lock.unlock() }
        if let cached = cache[key] { return cached }
        let font = CTFontCreateWithFontDescriptor(descriptor(for: key), size, nil)
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
