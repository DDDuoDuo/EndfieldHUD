import Foundation

enum HUDBatchTwoConfigurationTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let domain = "EndfieldHUD-BatchTwoConfigurationTests-" + UUID().uuidString
        let defaults = UserDefaults(suiteName: domain)!
        let previousLanguage = L10n.language
        defaults.removePersistentDomain(forName: domain)
        defer {
            defaults.removePersistentDomain(forName: domain)
            L10n.language = previousLanguage
        }

        // A user's pre-batch preferences have none of the four new keys.
        defaults.set(1, forKey: "hudSettingsSchemaVersion")
        defaults.set(7.25, forKey: "displayDuration")
        defaults.set("123ABC", forKey: "accentHex")
        defaults.set(OverlayTheme.light.rawValue, forKey: "theme")
        defaults.set(HUDClockFormat.twelveHour.rawValue, forKey: "clockFormat")
        let store = ConfigurationStore(defaults: defaults)
        check(store.configuration.clockStyle == .digital && store.configuration.centerLogo == .endfield
              && store.configuration.centerLogoRevision == nil && store.configuration.alertMetric == .battery,
              "Existing installations receive compatible defaults for all four new settings")
        check(store.configuration.displayDuration == 7.25 && store.configuration.accentHex == "123ABC"
              && store.configuration.theme == .light && store.configuration.clockFormat == .twelveHour,
              "Loading new options preserves existing alert, color, appearance and time-format preferences")
        check(defaults.object(forKey: "clockStyle") == nil && defaults.object(forKey: "centerLogo") == nil
              && defaults.object(forKey: "centerLogoRevision") == nil && defaults.object(forKey: "alertMetric") == nil,
              "Reading legacy preferences does not eagerly migrate or write the new defaults")

        var notifications = 0
        let observer = store.addObserver { _ in notifications += 1 }
        for index in 0..<5 {
            var next = store.configuration
            next.clockStyle = HUDClockStyle.allCases[index]
            next.centerLogo = HUDCenterLogo.allCases[index]
            next.alertMetric = HUDChargeMetric.allCases[index]
            let revision = UUID()
            next.centerLogoRevision = revision.uuidString.lowercased()
            store.update(next)
            let loaded = ConfigurationStore(defaults: defaults).configuration
            check(loaded.clockStyle == next.clockStyle && loaded.centerLogo == next.centerLogo
                  && loaded.alertMetric == next.alertMetric && loaded.centerLogoRevision == revision.uuidString,
                  "Every saved clock style, logo choice and metric reloads with a canonical custom-logo revision")
            check(defaults.string(forKey: "clockStyle") == next.clockStyle.rawValue
                  && defaults.string(forKey: "centerLogo") == next.centerLogo.rawValue
                  && defaults.string(forKey: "alertMetric") == next.alertMetric.rawValue,
                  "Preferences use the stable enum identifiers rather than localized labels or indices")
            check(loaded.displayDuration == 7.25 && loaded.accentHex == "123ABC"
                  && loaded.theme == .light && loaded.clockFormat == .twelveHour,
                  "Saving new options preserves unrelated settings")
        }
        check(notifications == 5, "Each coherent new-settings update emits one observer notification")
        store.update(store.configuration)
        check(notifications == 5, "Saving identical normalized settings is a no-op")
        var removed = store.configuration
        removed.centerLogoRevision = nil
        store.update(removed)
        check(defaults.object(forKey: "centerLogoRevision") == nil
              && ConfigurationStore(defaults: defaults).configuration.centerLogoRevision == nil,
              "Clearing a custom revision removes the saved reference instead of reviving an older revision")
        store.removeObserver(observer)

        for malformed in ["", "not-a-uuid", "../../outside.png", "00000000-0000-0000-0000-00000000000Z"] {
            defaults.set(malformed, forKey: "centerLogoRevision")
            let restored = ConfigurationStore(defaults: defaults).configuration
            check(restored.centerLogoRevision == nil,
                  "Malformed saved revisions resolve to the backward-compatible nil default")
            var candidate = AppConfiguration.defaults
            candidate.centerLogoRevision = malformed
            check(candidate.normalized.centerLogoRevision == nil,
                  "Direct settings updates reject malformed custom revision identifiers before persistence")
        }
        defaults.set("unknown-future-style", forKey: "clockStyle")
        defaults.set("unknown-future-logo", forKey: "centerLogo")
        defaults.set("unknown-future-metric", forKey: "alertMetric")
        let unknown = ConfigurationStore(defaults: defaults).configuration
        check(unknown.clockStyle == .digital && unknown.centerLogo == .endfield && unknown.alertMetric == .battery,
              "Unknown saved enum values fall back without crashing or selecting an unrelated option")
        return count
    }
}
