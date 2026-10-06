import Foundation
import AppKit
import QuartzCore

enum LocalizationTests {
    static func run() -> Int {
        var assertions = 0
        func check(_ condition: @autoclosure () -> Bool, _ message: String) {
            assertions += 1
            precondition(condition(), message)
        }
        let previous = L10n.language
        defer { L10n.language = previous }

        let localeCases: [([String], AppLanguage)] = [
            (["zh-Hant"], .traditionalChinese), (["zh-Hant-TW"], .traditionalChinese),
            (["zh-TW"], .traditionalChinese), (["zh_HK"], .traditionalChinese),
            (["zh-MO"], .traditionalChinese), (["zh-Hant-CN"], .traditionalChinese),
            (["zh-Hans-TW"], .simplifiedChinese), (["zh-Hans"], .simplifiedChinese),
            (["zh-CN"], .simplifiedChinese), (["zh-SG"], .simplifiedChinese), (["zh"], .simplifiedChinese),
            (["ja-JP"], .japanese), (["JA_jp"], .japanese), (["en-GB"], .english),
            (["ko"], .korean), (["ko-KR"], .korean), (["KO_kr"], .korean), (["ko-Kore-KR"], .korean),
            (["fr-FR", "ja-JP", "en-US"], .japanese), (["en-US", "zh-TW"], .english),
            (["fr-FR", "ko-KR", "en-US"], .korean), (["ko-KR", "ja-JP"], .korean),
            (["en-US", "ko-KR"], .english), (["kok-IN", "ko-KR"], .korean), (["kok-IN"], .english),
            (["de-DE", "zh-HK", "ja-JP"], .traditionalChinese), (["fr-FR"], .english), ([], .english)
        ]
        for (identifiers, expected) in localeCases {
            check(L10n.resolveLanguage(preferredLanguages: identifiers) == expected,
                  "Preferred languages resolve scripts, regions and fallback order: \(identifiers)")
        }
        check(AppLanguage.system.rawValue == "system" && AppLanguage.english.rawValue == "english"
              && AppLanguage.simplifiedChinese.rawValue == "simplifiedChinese"
              && AppLanguage.traditionalChinese.rawValue == "traditionalChinese"
              && AppLanguage.japanese.rawValue == "japanese" && AppLanguage.korean.rawValue == "korean",
              "Existing stored language values remain compatible and Korean has a stable value")
        L10n.language = .system
        check(L10n.resolvedLanguage == L10n.resolveLanguage(preferredLanguages: Locale.preferredLanguages),
              "System mode resolves the current ordered system language preferences")

        L10n.language = .traditionalChinese
        check(L10n.resolvedLanguage == .traditionalChinese && L10n.isChinese && L10n.isCJK, "Traditional Chinese has a distinct identity and Chinese layout")
        check(HUDModule.clipboard.title == "剪貼簿" && HUDModule.storage.title == "儲存", "Traditional Chinese module labels use regional terminology")
        check(L10n.text("System", "跟随系统") == "跟隨系統", "Context distinguishes following the system from the System module")
        check(L10n.text("Choose output device", "选择输出设备") == "選擇輸出裝置", "Audio controls are translated")
        L10n.language = .japanese
        check(L10n.resolvedLanguage == .japanese && !L10n.isChinese && L10n.isCJK, "Japanese has a distinct identity and CJK layout")
        check(HUDModule.notes.title == "メモ" && HUDModule.workMode.title == "作業モード", "Japanese module titles do not fall back to English")
        check(L10n.text("Save shortcut", "保存快捷方式") == "ショートカットを保存", "Editor actions have Japanese translations")
        check(L10n.text("System", "跟随系统") == "システムに合わせる"
              && L10n.text("System", "系统") == "システム", "Context is part of the catalog key")
        check(L10n.text("Display", "显示器") == "ディスプレイ"
              && L10n.text("Display", "显示") == "表示", "Display device and display settings retain different meanings")
        check(L10n.text("The audio control value is invalid.", "音频控制值无效。") == "オーディオ設定値が無効です。", "Operational errors are translated")
        check(L10n.text("Storage capacity is unavailable.", "无法读取存储容量。") == "ストレージ容量を取得できません。", "Storage errors are translated")
        L10n.language = .korean
        check(L10n.resolvedLanguage == .korean && !L10n.isChinese && L10n.isCJK,
              "Korean manual selection has a distinct identity and CJK layout")
        check(HUDModule.notes.title == "메모" && HUDModule.workMode.title == "작업 모드"
              && HUDModule.fileShelf.title == "임시 파일 보관함", "Korean module titles do not fall back to English")
        check(L10n.text("Save shortcut", "保存快捷方式") == "바로가기 저장", "Editor actions have Korean translations")
        check(L10n.text("System", "跟随系统") == "시스템 설정 따르기"
              && L10n.text("System", "系统") == "시스템", "Korean translations preserve System label context")
        check(L10n.text("Display", "显示器") == "디스플레이"
              && L10n.text("Display", "显示") == "화면", "Korean device and display settings retain different meanings")
        check(L10n.text("The audio control value is invalid.", "音频控制值无效。") == "오디오 설정값이 올바르지 않습니다."
              && L10n.text("Storage capacity is unavailable.", "无法读取存储容量。") == "저장 공간 용량을 확인할 수 없습니다.",
              "Korean operational errors are translated")
        for selection in AppLanguage.allCases {
            L10n.language = selection
            check(HUDModule.notes.englishTitle == "Notes" && HUDModule.eventLog.englishTitle == "Event Log",
                  "Intentionally English HUD headings remain English in \(selection)")
            check(L10n.text("RAM", "RAM") == "RAM" && L10n.text("RAM ", "RAM ") == "RAM ",
                  "The RAM metric keeps its requested label in \(selection)")
        }

        L10n.language = .japanese
        let navigation = HUDNavigation()
        navigation.update(dark: true, accent: .systemYellow, contentsScale: 2)
        func textLayers(_ layer: CALayer) -> [CATextLayer] {
            (layer as? CATextLayer).map { [$0] } ?? (layer.sublayers ?? []).flatMap(textLayers)
        }
        for entry in navigation.entries where entry.group == .right || entry.module == .activityMonitor {
            guard let module = entry.module else { preconditionFailure("Navigation entry needs a module") }
            let caption = module == .fileShelf ? "一時ファイル\nシェルフ" : module.title
            guard let title = textLayers(entry.layer).first(where: { ($0.string as? String) == caption }) else {
                preconditionFailure("Japanese navigation must retain its localized title")
            }
            let text = caption as NSString
            let attributes: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: title.fontSize, weight: .bold)]
            if module == .fileShelf {
                check(title.isWrapped && title.fontSize >= 9.5, "Japanese shelf caption wraps at a readable font size")
            }
            if title.isWrapped {
                let bounds = text.boundingRect(with: CGSize(width: title.bounds.width, height: 100),
                                               options: [.usesLineFragmentOrigin, .usesFontLeading], attributes: attributes)
                check(bounds.height <= title.bounds.height + 1, "Japanese navigation title fits vertically: \(module.title)")
            } else {
                check(text.size(withAttributes: attributes).width <= title.bounds.width + 0.5,
                      "Japanese navigation title fits horizontally: \(module.title)")
            }
        }
        L10n.language = .traditionalChinese
        navigation.update(dark: true, accent: .systemYellow, contentsScale: 2)
        let shelf = navigation.entries.first { $0.module == .fileShelf }!
        check(textLayers(shelf.layer).contains { ($0.string as? String) == "檔案暫存架" && !$0.isWrapped },
              "Switching back to Chinese restores its compact single-line layout")

        // The supplied official OrbiPom locales use SP / 技力 / 스킬 게이지.
        let gameSkillTerms: [(AppLanguage, String)] = [(.english, "SP"), (.simplifiedChinese, "技力"),
            (.traditionalChinese, "技力"), (.japanese, "SP"), (.korean, "스킬 게이지")]
        for (language, expected) in gameSkillTerms {
            check(L10n.text("SP", "技力", language: language) == expected,
                  "OrbiPom uses the original game's skill resource term in \(language)")
            check(!L10n.text("Rules", "游戏规则", language: language).isEmpty,
                  "OrbiPom rules header is localized in \(language)")
        }

        let count = 37, total = 128
        check(L10n.text("\(count) / \(total) items · Click to copy", "\(count) / \(total) 项 · 点击复制", language: .japanese)
              == "37 / 128件 · クリックでコピー", "Multiple interpolated values do not change the translation key")
        check(L10n.text("Keep this UI scale? Reverts in \(count)s", "保留此缩放？\(count) 秒后自动恢复", language: .traditionalChinese)
              == "保留此縮放？37 秒後自動恢復", "Traditional Chinese interpolations preserve dynamic countdowns")
        check(L10n.text("\(count) / \(total) items · Click to copy", "\(count) / \(total) 项 · 点击复制", language: .korean)
              == "37 / 128개 항목 · 클릭하여 복사", "Korean item counts preserve every interpolated argument")
        check(L10n.text("Keep this UI scale? Reverts in \(count)s", "保留此缩放？\(count) 秒后自动恢复", language: .korean)
              == "이 UI 배율을 유지할까요? 37초 후 복원됩니다", "Korean countdowns retain their dynamic value")
        let value = "v1.0% %@ {0} {1} 日本語\n✓"
        check(L10n.text("\(value) is available. Open About to see the release.", "\(value) 已发布。打开关于页面查看。", language: .japanese)
              == value + "が利用可能です。「このアプリについて」でリリースを確認できます。", "Inserted user values are never interpreted as placeholders or format strings")
        check(L10n.text("\(value) is available. Open About to see the release.", "\(value) 已发布。打开关于页面查看。", language: .korean)
              == value + " 버전을 사용할 수 있습니다. 정보에서 릴리스를 확인하세요.",
              "Korean translations preserve user values containing placeholders and percent signs")
        check(L10n.render("{1} / {0} / {1}", arguments: ["first", "second"]) == "second / first / second", "Translations can reorder and repeat arguments")
        check(L10n.render("{0}", arguments: ["a", "b"]) == nil, "Missing arguments reject an incomplete translation")
        check(L10n.render("{1}", arguments: ["a"]) == nil, "Invalid indices reject an unsafe translation")
        L10n.language = .english
        check(L10n.text("Notes", "便笺", language: .japanese) == "メモ" && L10n.language == .english,
              "Captured worker languages do not read or mutate the current UI preference")
        check(L10n.text("Notes", "便笺", language: .korean) == "메모" && L10n.language == .english,
              "Captured Korean worker languages do not change the current UI preference")
        check(L10n.text("Uncatalogued \(value)", "未收录 \(value)", language: .japanese) == "Uncatalogued " + value,
              "Unknown strings retain their original interpolated content")
        check(L10n.text("Uncatalogued \(value)", "未收录 \(value)", language: .korean) == "Uncatalogued " + value,
              "Unknown Korean strings fall back to English without altering interpolated content")

        let suiteName = "EndfieldCharge.LocalizationTests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suiteName)!
        defer { defaults.removePersistentDomain(forName: suiteName) }
        let store = ConfigurationStore(defaults: defaults)
        for selected in [AppLanguage.traditionalChinese, .japanese, .korean] {
            var config = store.configuration; config.language = selected; store.update(config)
            check(defaults.string(forKey: "language") == selected.rawValue, "New language preferences persist using stable raw values")
            check(ConfigurationStore(defaults: defaults).configuration.language == selected && L10n.resolvedLanguage == selected,
                  "New language preferences survive relaunch and apply immediately")
        }

        let placeholder = try! NSRegularExpression(pattern: #"\{([0-9]+)\}"#)
        var keys = Set<String>()
        for entry in LocalizationCatalog.entries {
            check(keys.insert(entry.english + "\u{1F}" + entry.simplifiedChinese).inserted, "Catalog source pairs are unique: \(entry.english)")
            let matches = placeholder.matches(in: entry.english, range: NSRange(entry.english.startIndex..., in: entry.english))
            let indices = matches.compactMap { Range($0.range(at: 1), in: entry.english) }.compactMap { Int(entry.english[$0]) }
            let arguments = (0..<(indices.max().map { $0 + 1 } ?? 0)).map { "value-\($0)-%@-{0}-雪" }
            check(!entry.traditionalChinese.isEmpty && !entry.japanese.isEmpty && !entry.korean.isEmpty,
                  "Every catalog entry has Traditional Chinese, Japanese and Korean translations")
            check(L10n.render(entry.traditionalChinese, arguments: arguments) != nil
                  && L10n.render(entry.japanese, arguments: arguments) != nil
                  && L10n.render(entry.korean, arguments: arguments) != nil,
                  "Every translation preserves all dynamic arguments: \(entry.english)")
        }
        check(Set(LocalizationCatalog.koreanTranslations.keys) == keys,
              "Korean translations cover every complete source pair exactly once without unused keys")

        // Keep the catalog complete as UI literals and audio-worker messages change.
        // Checking shipped call sites also catches same-English/different-context additions.
        let sourceDirectory = URL(fileURLWithPath: #file).deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent("Sources")
        let files = try! FileManager.default.contentsOfDirectory(at: sourceDirectory, includingPropertiesForKeys: nil)
        let calls = try! NSRegularExpression(pattern: #"\b(?:L10n\.text|localized|message)\s*\(\s*"#)
        var checkedSources = 0
        for file in files where file.pathExtension == "swift" && !file.lastPathComponent.hasPrefix("Localization") {
            let source = try! String(contentsOf: file, encoding: .utf8), characters = Array(source)
            for match in calls.matches(in: source, range: NSRange(source.startIndex..., in: source)) {
                guard let range = Range(match.range, in: source) else { continue }
                var position = source.distance(from: source.startIndex, to: range.upperBound)
                guard let english = parseLiteral(characters, position: &position) else { continue }
                while position < characters.count && characters[position].isWhitespace { position += 1 }
                guard position < characters.count && characters[position] == "," else { continue }
                position += 1
                while position < characters.count && characters[position].isWhitespace { position += 1 }
                guard let simplified = parseLiteral(characters, position: &position) else { continue }
                checkedSources += 1
                check(LocalizationCatalog.entry(english: english, simplifiedChinese: simplified) != nil,
                      "Untranslated UI string in \(file.lastPathComponent): \(english)")
            }
        }
        check(checkedSources > 500, "Coverage checks the shipped UI and worker messages")
        return assertions
    }

    /// Read a Swift string literal as a catalog template, skipping the balanced
    /// expression (including nested strings) inside each interpolation.
    private static func parseLiteral(_ source: [Character], position: inout Int) -> String? {
        guard position < source.count && source[position] == "\"" else { return nil }
        position += 1
        var value = "", arguments = 0
        while position < source.count {
            let character = source[position]; position += 1
            if character == "\"" { return value }
            guard character == "\\" else { value.append(character); continue }
            guard position < source.count else { return nil }
            let escaped = source[position]; position += 1
            if escaped == "(" {
                var depth = 1
                while position < source.count && depth > 0 {
                    if source[position] == "\"" {
                        guard parseLiteral(source, position: &position) != nil else { return nil }
                        continue
                    }
                    if source[position] == "(" { depth += 1 }
                    if source[position] == ")" { depth -= 1 }
                    position += 1
                }
                guard depth == 0 else { return nil }
                value += "{\(arguments)}"; arguments += 1
            } else {
                switch escaped {
                case "n": value += "\n"
                case "t": value += "\t"
                case "r": value += "\r"
                default: value.append(escaped)
                }
            }
        }
        return nil
    }
}
