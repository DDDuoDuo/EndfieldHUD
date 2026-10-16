#include "packaging/setup_arguments.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>

namespace endfield::packaging {
namespace {
constexpr std::array<std::string_view, static_cast<std::size_t>(InstallStep::count)> stepNames{
    "extract", "stop", "swap", "uninstall-entry", "identity", "login-item", "settings", "commit"};
[[noreturn]] void invalid(const std::string& what) { throw std::invalid_argument(what); }
bool validUtf8(std::string_view text) noexcept {
    std::size_t at = 0;
    while (at < text.size()) {
        const auto c = static_cast<unsigned char>(text[at]);
        const std::size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 0;
        if (!length || at + length > text.size()) return false;
        for (std::size_t n = 1; n < length; ++n)
            if ((static_cast<unsigned char>(text[at + n]) & 0xc0) != 0x80) return false;
        at += length;
    }
    return true;
}
} // namespace

std::string_view installStepName(InstallStep step) noexcept {
    const auto index = static_cast<std::size_t>(step);
    return index < stepNames.size() ? stepNames[index] : std::string_view{};
}
std::optional<InstallStep> installStepFromName(std::string_view name) noexcept {
    for (std::size_t n = 0; n < stepNames.size(); ++n)
        if (stepNames[n] == name) return static_cast<InstallStep>(n);
    return std::nullopt;
}

bool validTestRegistryRoot(std::string_view value) noexcept {
    constexpr std::string_view prefix = "Software\\EndfieldHUD-Test-";
    if (!value.starts_with(prefix)) return false;
    const auto id = value.substr(prefix.size());
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '{' || c == '}';
    });
}
bool absoluteWindowsPath(std::string_view p) noexcept {
    if (p.size() < 3 || p.size() > 32767 || p.find('\0') != std::string_view::npos || !validUtf8(p)) return false;
    if (p.find('/') != std::string_view::npos) return false;
    const bool drive = ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':' && p[2] == '\\';
    const bool unc = p.starts_with("\\\\") && p.size() > 4 && p[2] != '\\' && p[2] != '?' && p[2] != '.';
    if (!drive && !unc) return false;
    // No relative components, no trailing separator (except a drive root) and no wildcards.
    if (p.find("\\..\\") != std::string_view::npos || p.ends_with("\\..") || p.find("\\.\\") != std::string_view::npos || p.ends_with("\\."))
        return false;
    if (p.size() > 3 && p.back() == '\\') return false;
    return p.find_first_of("*?\"<>|", 0) == std::string_view::npos && p.find(':', 2) == std::string_view::npos;
}

std::string_view setupUsage(SetupRole role) noexcept {
    return role == SetupRole::install
        ? "usage: EndfieldHUD-Setup.exe [--quiet] [--launch-at-login on|off] [--open] [--allow-downgrade] [--verify-payload] [--result <file>]"
        : "usage: uninstall.exe [--quiet] [--remove-user-data] [--result <file>]";
}

SetupArguments parseSetupArguments(SetupRole role, std::span<const std::string_view> values) {
    if (values.size() > 64) invalid("Too many arguments");
    SetupArguments a;
    a.role = role;
    bool sawInstallRoot{}, sawProgramsFolder{}, sawDataBase{}, sawWorkFolder{};
    for (std::size_t n = 0; n < values.size(); ++n) {
        std::string_view name = values[n];
        if (name.size() > 32768 || !validUtf8(name)) invalid("Invalid argument");
        std::optional<std::string_view> inlineValue;
        if (name.starts_with("--")) {
            if (const auto equals = name.find('='); equals != std::string_view::npos) {
                inlineValue = name.substr(equals + 1);
                name = name.substr(0, equals);
            }
        }
        const auto value = [&]() -> std::string_view {
            if (inlineValue) return *inlineValue;
            if (n + 1 >= values.size()) invalid(std::string(name) + " needs a value");
            return values[++n];
        };
        const auto flag = [&](bool& target) {
            if (inlineValue) invalid(std::string(name) + " takes no value");
            if (target) invalid(std::string(name) + " is repeated");
            target = true;
        };
        const auto text = [&](std::string& target, bool& seen) {
            if (seen) invalid(std::string(name) + " is repeated");
            seen = true;
            target = std::string(value());
            if (target.empty()) invalid(std::string(name) + " needs a value");
        };
        if (name == "--quiet" || name == "/quiet" || name == "/S" || name == "/s") { flag(a.quiet); }
        else if (name == "--result") { bool seen = !a.resultFile.empty(); text(a.resultFile, seen); }
        else if (name == "--test-registry") { bool seen = !a.testRegistry.empty(); text(a.testRegistry, seen); }
        else if (name == "--install-root") text(a.installRoot, sawInstallRoot);
        else if (name == "--programs-folder") text(a.programsFolder, sawProgramsFolder);
        else if (name == "--data-base") text(a.dataBase, sawDataBase);
        else if (name == "--work-folder") text(a.workFolder, sawWorkFolder);
        else if (name == "--fail-after") {
            if (a.failAfter) invalid("--fail-after is repeated");
            a.failAfter = installStepFromName(value());
            if (!a.failAfter) invalid("--fail-after names an unknown step");
        } else if (role == SetupRole::install && name == "--launch-at-login") {
            if (a.launchAtLogin) invalid("--launch-at-login is repeated");
            const auto v = value();
            if (v == "on") a.launchAtLogin = true;
            else if (v == "off") a.launchAtLogin = false;
            else invalid("--launch-at-login must be on or off");
        } else if (role == SetupRole::install && name == "--test-windows-build") {
            if (a.testWindowsBuild) invalid("--test-windows-build is repeated");
            const auto v = value();
            std::uint32_t build{};
            const auto [end, error] = std::from_chars(v.data(), v.data() + v.size(), build);
            if (error != std::errc{} || end != v.data() + v.size()) invalid("--test-windows-build needs a Windows build number");
            a.testWindowsBuild = build;
        } else if (role == SetupRole::install && name == "--open") flag(a.open);
        else if (role == SetupRole::install && name == "--allow-downgrade") flag(a.allowDowngrade);
        else if (role == SetupRole::install && name == "--verify-payload") flag(a.verifyPayload);
        else if (role == SetupRole::uninstall && name == "--remove-user-data") flag(a.removeUserData);
        else if (role == SetupRole::uninstall && name == "--worker") flag(a.worker);
        else if (role == SetupRole::uninstall && name == "--parent") {
            if (a.parentProcess) invalid("--parent is repeated");
            const auto v = value();
            std::uint32_t pid{};
            const auto [end, error] = std::from_chars(v.data(), v.data() + v.size(), pid);
            if (error != std::errc{} || end != v.data() + v.size() || pid == 0) invalid("--parent needs a process id");
            a.parentProcess = pid;
        } else invalid("Unknown argument: " + std::string(name));
    }
    for (const auto* path : {&a.installRoot, &a.programsFolder, &a.dataBase, &a.workFolder, &a.resultFile})
        if (!path->empty() && !absoluteWindowsPath(*path)) invalid("Paths must be absolute Windows paths");
    if (a.isolated()) {
        if (!validTestRegistryRoot(a.testRegistry)) invalid("--test-registry must be Software\\EndfieldHUD-Test-<id>");
        if (a.installRoot.empty() || a.programsFolder.empty() || a.dataBase.empty())
            invalid("--test-registry requires --install-root, --programs-folder and --data-base");
    } else {
        if (a.failAfter || a.testWindowsBuild || !a.workFolder.empty() || !a.programsFolder.empty() || !a.dataBase.empty())
            invalid("Test options require --test-registry");
        if (!a.installRoot.empty() && !a.worker) invalid("--install-root requires --test-registry");
    }
    if (a.worker && (!a.parentProcess || a.installRoot.empty())) invalid("--worker requires --parent and --install-root");
    if (!a.worker && a.parentProcess) invalid("--parent requires --worker");
    if (a.verifyPayload && (a.quiet || a.open || a.launchAtLogin || a.allowDowngrade || a.isolated()))
        invalid("--verify-payload takes no other option");
    return a;
}

std::vector<std::string> uninstallWorkerArguments(const SetupArguments& a, std::uint32_t parent, std::string_view installRoot) {
    std::vector<std::string> out{"--worker", "--parent", std::to_string(parent), "--install-root", std::string(installRoot)};
    if (a.quiet) out.emplace_back("--quiet");
    if (a.removeUserData) out.emplace_back("--remove-user-data");
    if (!a.resultFile.empty()) { out.emplace_back("--result"); out.push_back(a.resultFile); }
    if (a.isolated()) {
        out.emplace_back("--test-registry"); out.push_back(a.testRegistry);
        out.emplace_back("--programs-folder"); out.push_back(a.programsFolder);
        out.emplace_back("--data-base"); out.push_back(a.dataBase);
        if (!a.workFolder.empty()) { out.emplace_back("--work-folder"); out.push_back(a.workFolder); }
        if (a.failAfter) { out.emplace_back("--fail-after"); out.emplace_back(installStepName(*a.failAfter)); }
    }
    return out;
}
} // namespace endfield::packaging
