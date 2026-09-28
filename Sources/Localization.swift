import Foundation

/// A deliberately small bilingual catalog: call sites keep the two translations together.
/// Accessed on the main thread alongside the native user interface.
enum L10n {
    static var language: AppLanguage = .system

    static var isChinese: Bool {
        switch language {
        case .english: return false
        case .simplifiedChinese: return true
        case .system:
            // Prefer the first supported language, matching normal bundle fallback behavior.
            for identifier in Locale.preferredLanguages {
                let prefix = identifier.lowercased()
                if prefix.hasPrefix("zh") { return true }
                if prefix.hasPrefix("en") { return false }
            }
            return false
        }
    }

    static func text(_ english: String, _ simplifiedChinese: String) -> String {
        isChinese ? simplifiedChinese : english
    }
}
