import Foundation

/// The call-site English and Simplified Chinese strings remain the source keys.
/// Other languages live in LocalizationCatalog. Interpolated values are retained
/// separately, so user names, versions and counts never become lookup keys.
/// UI callers use the current preference; workers pass a captured language.
enum L10n {
    static var language: AppLanguage = .system

    static var resolvedLanguage: AppLanguage {
        resolve(language)
    }

    static var isChinese: Bool {
        resolvedLanguage == .simplifiedChinese || resolvedLanguage == .traditionalChinese
    }

    static var isCJK: Bool {
        resolvedLanguage != .english
    }

    static func resolveLanguage(preferredLanguages: [String]) -> AppLanguage {
        for identifier in preferredLanguages {
            let components = identifier.lowercased().replacingOccurrences(of: "_", with: "-").split(separator: "-")
            switch components.first {
            case "en": return .english
            case "ja": return .japanese
            case "ko": return .korean
            case "zh":
                // An explicit script wins over region (for example zh-Hans-TW).
                if components.contains("hant") { return .traditionalChinese }
                if components.contains("hans") { return .simplifiedChinese }
                if components.contains(where: { ["tw", "hk", "mo"].contains($0) }) { return .traditionalChinese }
                return .simplifiedChinese
            default: continue
            }
        }
        return .english
    }

    private static func resolve(_ selection: AppLanguage) -> AppLanguage {
        selection == .system ? resolveLanguage(preferredLanguages: Locale.preferredLanguages) : selection
    }

    static func text(_ english: Text, _ simplifiedChinese: Text, language selection: AppLanguage? = nil) -> String {
        let selected = resolve(selection ?? language)
        switch selected {
        case .system, .english: return english.value
        case .simplifiedChinese: return simplifiedChinese.value
        case .traditionalChinese, .japanese, .korean:
            guard let entry = LocalizationCatalog.entry(english: english.template, simplifiedChinese: simplifiedChinese.template) else {
                // New uncatalogued strings stay readable while their translations
                // are added; tests check every shipped literal against the catalog.
                if selected == .traditionalChinese {
                    return simplifiedChinese.value.applyingTransform(StringTransform("Hans-Hant"), reverse: false)
                        ?? simplifiedChinese.value
                }
                return english.value
            }
            let template: String
            switch selected {
            case .traditionalChinese: template = entry.traditionalChinese
            case .japanese: template = entry.japanese
            case .korean: template = entry.korean
            default: template = english.template
            }
            return render(template, arguments: english.arguments) ?? english.value
        }
    }

    /// Render each placeholder once. Inserted text is never rescanned or passed
    /// through a printf formatter, even if it contains braces or percent signs.
    static func render(_ template: String, arguments: [String]) -> String? {
        let matches = placeholderPattern.matches(in: template, range: NSRange(template.startIndex..., in: template))
        var result = "", position = template.startIndex
        var used = Set<Int>()
        for match in matches {
            guard let range = Range(match.range, in: template), let indexRange = Range(match.range(at: 1), in: template),
                  let index = Int(template[indexRange]), arguments.indices.contains(index) else { return nil }
            result += template[position..<range.lowerBound]
            result += arguments[index]
            used.insert(index)
            position = range.upperBound
        }
        guard used == Set(arguments.indices) else { return nil }
        result += template[position...]
        return result
    }

    private static let placeholderPattern = try! NSRegularExpression(pattern: #"\{([0-9]+)\}"#)

    struct Text: ExpressibleByStringLiteral, ExpressibleByStringInterpolation {
        let template: String
        let arguments: [String]
        let value: String

        /// For strings that are already materialized. Prefer literals/interpolation
        /// at call sites so the translation key remains independent of values.
        init(_ value: String) {
            self.template = value; self.arguments = []; self.value = value
        }

        init(stringLiteral value: String) { self.init(value) }
        init(stringInterpolation: StringInterpolation) {
            template = stringInterpolation.template
            arguments = stringInterpolation.arguments
            value = stringInterpolation.value
        }

        struct StringInterpolation: StringInterpolationProtocol {
            var template = ""
            var arguments: [String] = []
            var value = ""

            init(literalCapacity: Int, interpolationCount: Int) {
                template.reserveCapacity(literalCapacity)
                value.reserveCapacity(literalCapacity)
                arguments.reserveCapacity(interpolationCount)
            }
            mutating func appendLiteral(_ literal: String) {
                template += literal; value += literal
            }
            mutating func appendInterpolation<T>(_ argument: T) {
                let rendered = String(describing: argument)
                template += "{\(arguments.count)}"
                arguments.append(rendered)
                value += rendered
            }
        }
    }
}
