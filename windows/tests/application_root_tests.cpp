// Portable data-root, first-launch marker and production argument contracts.
// Every path is a new owned temporary directory; the real %LOCALAPPDATA%
// EndfieldHUD folder is only resolved (read-only), never created or opened.
#include "core/application_arguments.hpp"
#include "core/data/application_root.hpp"
#include "core/data/file_io.hpp"
#include <array>
#include <chrono>
#include <vector>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#else
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
namespace data = ehud::data;
namespace core = endfield::core;
namespace {
unsigned checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
template <class F> bool throwsStore(F&& f, data::StoreErrorCode code) {
    try { f(); } catch (const data::StoreError& e) { return e.code() == code; } catch (...) { return false; }
    return false;
}
struct Temporary {
    fs::path root;
    Temporary() {
        std::random_device random;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::weakly_canonical(fs::temp_directory_path()) / ("EndfieldHUD root contracts " + std::to_string(stamp) + "-" + std::to_string(random()));
        check(fs::create_directory(root), "Tests use a new owned temporary directory");
    }
    ~Temporary() { std::error_code ignored; fs::remove_all(root, ignored); }
};
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void write(const fs::path& path, std::string_view bytes) { std::ofstream out(path, std::ios::binary); out << bytes; }

void root() {
    Temporary temp;
    const auto base = temp.root / "EndfieldHUD";
    const auto first = data::openApplicationRoot(base);
    check(first.created && first.base == base && first.data == base / "v1", "First launch creates the versioned layout");
    check(fs::is_directory(first.data) && fs::is_regular_file(first.marker), "Data directory and schema marker exist");
    const auto marker = data::Json::parse(read(first.marker));
    check(marker["schema"].integer() == 1 && marker["data"].string() == "v1" && marker["format"].string() == "EndfieldHUD.Windows.DataRoot",
          "Marker records the schema and data directory");
    const auto markerBytes = read(first.marker);
    const auto second = data::openApplicationRoot(base);
    check(!second.created && second.data == first.data && read(first.marker) == markerBytes, "Reopening an existing root changes nothing");
    // The stores use the Mac relative layout inside the data directory.
    data::SettingsStore settings(second.data);
    check(settings.path() == second.data / "settings.json", "Settings use <root>/v1/settings.json");
#ifndef _WIN32
    struct stat info{};
    check(::stat(base.c_str(), &info) == 0 && (info.st_mode & 0777) == 0700, "POSIX base is private to the current user");
#else
    PACL dacl{};
    PSECURITY_DESCRIPTOR descriptor{};
    check(GetNamedSecurityInfoW(base.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor) == ERROR_SUCCESS,
          "Read the owned base DACL");
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision{};
    check(GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED), "Base DACL does not inherit broader access");
    ACL_SIZE_INFORMATION size{};
    check(dacl && GetAclInformation(dacl, &size, sizeof(size), AclSizeInformation) && size.AceCount == 2, "Base DACL grants exactly two principals");
    bool system{}, user{};
    for (DWORD n = 0; n < size.AceCount; ++n) {
        void* ace{};
        check(GetAce(dacl, n, &ace), "Read base ACE");
        const auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
        check(allowed->Header.AceType == ACCESS_ALLOWED_ACE_TYPE, "Only allow entries");
        PSID sid = const_cast<DWORD*>(&allowed->SidStart);
        if (IsWellKnownSid(sid, WinLocalSystemSid)) system = true;
        else {
            HANDLE token{};
            check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token), "Open own token");
            BYTE buffer[512]{};
            DWORD length{};
            const bool ok = GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &length);
            CloseHandle(token);
            check(ok && EqualSid(sid, reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid), "Second principal is the current user");
            user = true;
        }
    }
    LocalFree(descriptor);
    check(system && user, "Current user and SYSTEM only");
    const auto production = data::localApplicationDataBase();
    check(production.is_absolute() && production.filename() == "EndfieldHUD", "Production base is LocalAppData\\EndfieldHUD");
#endif
    // A newer schema is preserved byte-for-byte and rejected.
    const auto newer = temp.root / "newer";
    fs::create_directory(newer);
    const std::string future = R"({"data":"v2","format":"EndfieldHUD.Windows.DataRoot","schema":2,"future":true})";
    write(newer / "root.json", future);
    check(throwsStore([&] { data::openApplicationRoot(newer); }, data::StoreErrorCode::newerVersion), "Newer schema is rejected");
    check(read(newer / "root.json") == future && !fs::exists(newer / "v1") && !fs::exists(newer / "v2"), "Newer data root is untouched");
    // Malformed and foreign markers are preserved and rejected.
    const auto malformed = temp.root / "malformed";
    fs::create_directory(malformed);
    write(malformed / "root.json", "{not json");
    check(throwsStore([&] { data::openApplicationRoot(malformed); }, data::StoreErrorCode::invalid), "Malformed marker is rejected");
    check(read(malformed / "root.json") == "{not json" && !fs::exists(malformed / "v1"), "Malformed marker is untouched");
    const auto foreign = temp.root / "foreign";
    fs::create_directory(foreign);
    write(foreign / "root.json", R"({"format":"Other","schema":1,"data":"v1"})");
    check(throwsStore([&] { data::openApplicationRoot(foreign); }, data::StoreErrorCode::invalid), "Foreign marker is rejected");
    check(throwsStore([&] { data::openApplicationRoot(fs::path("relative") / "EndfieldHUD"); }, data::StoreErrorCode::invalid), "Relative root rejected");
    check(throwsStore([&] { data::openApplicationRoot(temp.root / "missing parent" / "EndfieldHUD"); }, data::StoreErrorCode::unavailable),
          "A missing parent is never created implicitly");
    // A file where the base belongs is rejected, never replaced.
    const auto file = temp.root / "file";
    write(file, "owned");
    check(throwsStore([&] { data::openApplicationRoot(file); }, data::StoreErrorCode::invalid), "Non-directory base rejected");
    check(read(file) == "owned", "Non-directory base untouched");
#ifndef _WIN32
    const auto link = temp.root / "link";
    fs::create_directory_symlink(base, link);
    check(throwsStore([&] { data::openApplicationRoot(link); }, data::StoreErrorCode::invalid), "Linked base rejected");
#endif
}

void launchMarker() {
    Temporary temp;
    const auto root = data::openApplicationRoot(temp.root / "EndfieldHUD").data;
    {
        data::SettingsStore store(root);
        check(!store.hasLaunched(), "A new root has not launched");
        auto value = store.value();
        value.set("theme", "light");
        check(store.update(value), "Save a preference");
        check(store.markLaunched() && !store.markLaunched(), "Marker writes once");
        check(store.value() == value, "Marker preserves the preference record");
    }
    {
        data::SettingsStore store(root);
        check(store.hasLaunched() && store.value().string("theme") == "light", "Marker and preferences survive restart");
        check(store.update(data::Settings::defaults()) && store.hasLaunched(), "Restore defaults keeps the launch marker");
    }
    data::SettingsStore reread(root);
    check(reread.hasLaunched() && reread.value() == data::Settings::defaults(), "Reset record keeps the marker on disk");
    const auto record = data::Json::parse(read(root / "settings.json"));
    check(record["hasLaunched"].boolean() && !record["settings"].contains("hasLaunched"), "Marker lives in the envelope, not the preferences");
    // Invalid marker types preserve the file and reject.
    const auto other = data::openApplicationRoot(temp.root / "Other").data;
    const std::string bad = R"({"version":1,"settings":{},"hasLaunched":"yes"})";
    write(other / "settings.json", bad);
    check(throwsStore([&] { data::SettingsStore store(other); }, data::StoreErrorCode::invalid), "Invalid marker rejected");
    check(read(other / "settings.json") == bad, "Invalid marker file preserved");
}

void arguments() {
    const std::array<std::string_view, 7> values{"--login", "--settings", "--power", "--preview", "--no-onboarding", "--notification-activation", "unknown"};
    const auto parsed = core::parseApplicationArguments(values);
    check(parsed.startup.login && parsed.startup.settings && parsed.startup.power && parsed.startup.preview && parsed.startup.noOnboarding &&
          parsed.notificationActivation && parsed.ignored == 1, "Mac launch arguments keep their names");
    check(core::forwardedActivation(core::parseApplicationArguments({})) == core::ForwardedActivation::reopen, "Plain relaunch reopens");
    const std::array<std::string_view, 1> settings{"--settings"};
    check(core::forwardedActivation(core::parseApplicationArguments(settings)) == core::ForwardedActivation::settings, "Relaunch --settings opens System");
    const std::array<std::string_view, 1> toast{"--notification-activation"};
    check(core::forwardedActivation(core::parseApplicationArguments(toast)) == core::ForwardedActivation::none, "Notification server launch waits for COM");
    const std::array<std::string_view, 3> forward{"--settings", "云终末地 한국어", ""};
    const auto payload = core::encodeForwardedArguments(forward);
    const auto decoded = core::decodeForwardedArguments(payload);
    check(decoded.size() == 3 && decoded[0] == "--settings" && decoded[1] == "云终末地 한국어" && decoded[2].empty(), "Forwarded payload round-trips UTF-8");
    bool rejected{};
    try { (void)core::decodeForwardedArguments(payload.substr(0, payload.size() - 3)); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Unterminated payload rejected");
    rejected = false;
    try { (void)core::decodeForwardedArguments("EHUD-ACTIVATE-2" + std::string(1, '\0')); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Foreign magic rejected");
    rejected = false;
    const std::string invalid("\xff", 1);
    const std::array<std::string_view, 1> bad{invalid};
    try { (void)core::parseApplicationArguments(bad); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Invalid UTF-8 rejected");
    rejected = false;
    std::vector<std::string_view> many(core::applicationArgumentLimit + 1, "--login");
    try { (void)core::parseApplicationArguments(many); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Argument count is bounded");
    rejected = false;
    const std::string huge(core::applicationArgumentBytes + 1, 'a');
    const std::array<std::string_view, 1> large{huge};
    try { (void)core::encodeForwardedArguments(large); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Argument size is bounded");
}
} // namespace

int main() {
    try {
        root();
        launchMarker();
        arguments();
        std::cout << "Application root/arguments: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Application root/arguments failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
