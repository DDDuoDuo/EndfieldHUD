#include "core/migration/mac_import_settings.hpp"
#include "core/data/file_io.hpp"
#include "modules/settings.hpp"
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>

namespace m = ehud::migration;
namespace d = ehud::data;
using J = d::Json;
namespace {
unsigned checks{};
void check(bool value, const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }
std::string hex(double value) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 0; i < 16; ++i) out[static_cast<std::size_t>(i)] = digits[(bits >> (60 - i * 4)) & 15];
    return out;
}
struct Temporary {
    std::filesystem::path root = std::filesystem::canonical(std::filesystem::temp_directory_path()) / ("Endfield-mac-settings-" + d::makeUUID());
    ~Temporary() { std::error_code e; std::filesystem::remove_all(root, e); }
};
// Fixture-only grapheme rule: the oracle's display names are ASCII. Shipping
// code injects the installed ICU rule (see mac_import_native_tests).
m::MacImportTextRules scalarRules() {
    return {[](std::string_view text, std::size_t count) {
        std::size_t i = 0, n = 0;
        while (i < text.size() && n < count) { const auto b = static_cast<unsigned char>(text[i]); i += b < 0x80 ? 1 : b < 0xe0 ? 2 : b < 0xf0 ? 3 : 4; ++n; }
        return std::string(text.substr(0, i));
    }};
}
const m::MacSettingReport* find(const m::MacSettingsImport& result, std::string_view key, m::MacSettingOutcome outcome) {
    for (const auto& row : result.report) if (row.key == key && row.outcome == outcome) return &row;
    return nullptr;
}
void compare(const std::string& name, const m::MacSettingsImport& result, const J& expected) {
    static const std::set<std::string, std::less<>> doubles{"displayDuration", "scale", "customPositionX", "customPositionY", "hudScale", "hudOffsetX",
        "hudOffsetY", "parallaxIntensity", "perspectiveIntensity", "backgroundDarkness", "blurAmount"};
    const auto& f = result.settings.fields;
    for (const auto& [key, value] : expected.object()) {
        const auto& actual = f[key];
        if (key == "summonShortcut") {
            check(actual["keyCode"].integer() == value["keyCode"].integer() && actual["modifiers"].integer() == value["modifiers"].integer(), name + ": summonShortcut matches ConfigurationStore");
        } else if (value.isNull()) check(actual.isNull(), name + ": " + key + " is nil like ConfigurationStore");
        else if (value.isBool()) check(actual.isBool() && actual.boolean() == value.boolean(), name + ": " + key + " Boolean matches");
        else if (key == "customScreenID") check(actual.isNumber() && actual.integer() == value.integer(), name + ": customScreenID matches");
        else if (doubles.contains(key))
            check(actual.isNumber() && hex(actual.number()) == value.string(), name + ": " + key + " is bit-identical (" + hex(actual.number()) + " vs " + value.string() + ")");
        else check(actual.isString() && actual.string() == value.string(), name + ": " + key + " string matches (" + (actual.isString() ? actual.string() : std::string("?")) + ")");
    }
}
void oracle(const std::filesystem::path& path) {
    const auto text = d::detail::readFile(path, 8 * 1024 * 1024);
    check(text.has_value(), "Read the ConfigurationStore oracle fixture");
    const auto fixture = J::parse(*text, 8 * 1024 * 1024);
    const auto rules = scalarRules();
    unsigned cases{};
    for (const auto& row : fixture["cases"].array()) {
        const auto name = row["name"].string();
        std::optional<m::MacSettingsImport> first;
        for (const char* format : {"binary", "xml"}) {
            const auto bytes = m::base64Decode(row[format].string());
            check(bytes.has_value(), name + ": fixture bytes");
            const auto document = m::decodePlist(*bytes);
            const auto result = m::mapMacSettings(document.root, rules);
            // Apple's own XML writer drops the sign of -0.0, so each format is
            // compared with ConfigurationStore run over that format's decode.
            const auto& expected = row[std::string(format) + "Result"];
            compare(name + "/" + format, result, expected["configuration"]);
            const auto* bestKey = document.root.find("orbipom.bestScore.v1");
            if (bestKey) check(result.settings.fields["orbipom.bestScore.v1"].integer() == expected["bestScore"].integer(), name + ": OrbiPom best score coerced like integerForKey:");
            else check(!result.settings.fields.contains("orbipom.bestScore.v1") && expected["bestScore"].integer() == 0, name + ": absent best score is not invented");
            if (document.root.find("hasLaunched")) check(result.hasLaunched == std::optional<bool>(expected["hasLaunched"].boolean()), name + ": first-launch flag coerced like boolForKey:");
            else check(!result.hasLaunched, name + ": absent first-launch flag is not invented");
            check(!result.settings.fields.contains("hasLaunched"), name + ": first-launch flag stays outside the preference record");
            check(find(result, "hudSettingsSchemaVersion", expected["markerAbsent"].boolean() ? m::MacSettingOutcome::defaulted : m::MacSettingOutcome::imported) != nullptr, name + ": schema marker reported");
            // Each probe compares NSUserDefaults coercions directly.
            const m::MacDefaultsDomain domain(document.root);
            for (const auto& [key, probe] : expected["probes"].object()) {
                const auto actualString = domain.string(key);
                check(probe["string"].isNull() ? !actualString : (actualString && *actualString == probe["string"].string()), name + "/" + key + ": stringForKey: (" + actualString.value_or("nil") + ")");
                check(hex(domain.doubleValue(key)) == probe["double"].string(), name + "/" + key + ": doubleForKey: " + hex(domain.doubleValue(key)) + " vs " + probe["double"].string());
                check(domain.boolValue(key) == probe["bool"].boolean(), name + "/" + key + ": boolForKey:");
                check(std::to_string(domain.integerValue(key)) == probe["integer"].string(), name + "/" + key + ": integerForKey: " + std::to_string(domain.integerValue(key)) + " vs " + probe["integer"].string());
            }
            // The record is accepted by the unchanged Windows store and controller.
            Temporary t;
            d::SettingsStore store(t.root);
            check(store.update(result.settings), name + ": Windows SettingsStore persists the imported record");
            check(d::SettingsStore(t.root).value() == result.settings, name + ": imported record reloads exactly");
            const endfield::modules::SettingsController controller(result.settings);
            check(controller.committed().fields["windowsSummonShortcut"] == result.settings.fields["windowsSummonShortcut"], name + ": Windows controller accepts the hotkey intent");
            if (first && expected["configuration"] == row["binaryResult"]["configuration"]) check(first->settings == result.settings, name + ": binary and XML exports map identically");
            if (!first) first = result;
        }
        ++cases;
    }
    check(cases >= 50, "Oracle covers the settings matrix");
}
void outcomes() {
    const auto rules = scalarRules();
    using P = m::PlistValue;
    auto map = [&](P::Dictionary entries) { return m::mapMacSettings(P::dictionary(std::move(entries)), rules); };
    {
        const auto result = map({{"accentHex", P::string("d9f36b")}});
        check(result.settings.fields["accentHex"].string() == "FAD41F" && find(result, "accentHex", m::MacSettingOutcome::migrated), "Legacy accent migration is reported");
        check(result.settings.fields["windowsSummonShortcut"]["virtualKey"].integer() == 0xc0 && result.settings.fields["windowsSummonShortcut"]["modifiers"].integer() == 2 &&
              find(result, "windowsSummonShortcut", m::MacSettingOutcome::remapped) && !result.customShortcutUntranslated, "Default Ctrl+` maps by intent to VK_OEM_3 + MOD_CONTROL");
        check(result.launchAtLogin && find(result, "launchAtLogin", m::MacSettingOutcome::requiresAction), "Startup intent defaults on and needs the Windows provider");
    }
    {
        const auto custom = map({{"hudSettingsSchemaVersion", P::integer(1)}, {"summonShortcut", P::data(R"({"keyCode":4,"modifiers":3})")}, {"launchAtLogin", P::boolean(false)},
                                 {"HUDUpdateAutomaticallyInstall", P::boolean(true)}, {"SUFeedURL", P::string("https://example.invalid")}, {"futureKey", P::integer(3)},
                                 {"centerLogo", P::string("custom")}, {"centerLogoRevision", P::string("9b2d1c3a-1111-4222-8333-444455556666")}});
        check(custom.customShortcutUntranslated && !custom.settings.fields.contains("windowsSummonShortcut") && find(custom, "summonShortcut", m::MacSettingOutcome::untranslated),
              "Custom Mac key codes are never copied as Windows virtual keys");
        check(custom.settings.fields["summonShortcut"]["keyCode"].integer() == 4 && custom.settings.fields["summonShortcut"]["modifiers"].integer() == 3, "The Mac shortcut stays verbatim for reference");
        check(!custom.launchAtLogin && find(custom, "HUDUpdateAutomaticallyInstall", m::MacSettingOutcome::excluded) && find(custom, "SUFeedURL", m::MacSettingOutcome::excluded) &&
              find(custom, "futureKey", m::MacSettingOutcome::notImported), "Updater keys are excluded and unknown keys are only archived");
        check(!custom.settings.fields.contains("HUDUpdateAutomaticallyInstall") && !custom.settings.fields.contains("futureKey"), "Excluded and unknown keys never enter the Windows record");
        check(custom.centerLogoRevision == std::optional<std::string>("9B2D1C3A-1111-4222-8333-444455556666"), "Custom center-logo revision is canonical and handed to the logo step");
    }
    {   // Swift.max/min keep the second argument's sign for equal zeros.
        const auto zeros = map({{"customPositionX", P::real(-0.0)}, {"hudOffsetX", P::real(-0.0)}, {"perspectiveIntensity", P::real(-0.0)}});
        check(std::signbit(zeros.settings.fields["customPositionX"].number()) && std::signbit(zeros.settings.fields["hudOffsetX"].number()) &&
              std::signbit(zeros.settings.fields["perspectiveIntensity"].number()), "Negative zero survives AppConfiguration.normalized clamps");
    }
    bool threw{};
    try { (void)m::mapMacSettings(P::array({}), rules); } catch (const std::exception&) { threw = true; }
    check(threw, "A non-dictionary export is rejected");
    threw = false;
    try { (void)m::mapMacSettings(P::dictionary({}), {}); } catch (const std::exception&) { threw = true; }
    check(threw, "Missing Unicode rules are an explicit error");
    check(m::foundationTrimmed("\xE3\x80\x80 a b\xC2\xA0\n") == "a b" && m::foundationTrimmed("\xE2\x80\x8B" "x") == "\xE2\x80\x8B" "x", "Foundation whitespace set is pinned (ZWSP is not whitespace)");
    check(m::foundationUUID("3f2504e0-4f89-41d3-9a0c-0305e82c3301") == std::optional<std::string>("3F2504E0-4F89-41D3-9A0C-0305E82C3301") && !m::foundationUUID("3f2504e04f8941d39a0c0305e82c3301"), "UUID canonicalization");
}
}
int main(int argc, char** argv) {
    try {
        outcomes();
        if (argc >= 2) oracle(std::filesystem::absolute(argv[1]));
        std::cout << "PASS " << checks << " Mac settings import checks; synthetic preference suites only\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
