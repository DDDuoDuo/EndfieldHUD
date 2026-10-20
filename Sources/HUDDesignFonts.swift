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
