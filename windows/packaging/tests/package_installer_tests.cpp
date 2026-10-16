// EndfieldHUD-Setup.exe / uninstall.exe end to end, through the real programs
// in --quiet isolated mode: synthetic payloads (a stand-in EndfieldHUD.exe),
// a temporary %TEMP%\ehud-<id> folder (removed at the end) and a private
// HKCU\Software\EndfieldHUD-Test-<guid> subtree. Never the real Start menu, Run/Classes keys, notification
// platform, installed EndfieldHUD or user data; every process this test
// starts or stops lives in its own temporary folder.
//
//   packaging_installer_tests <setup stub> <uninstall.exe> <fixture app>
//   packaging_installer_tests --release <staged setup.exe>
#include "core/data/application_root.hpp"
#include "core/data/data_store.hpp"
#include "core/data/json.hpp"
#include "native/app_notification_identity.hpp"
#include "packaging/installer.hpp"
#include "packaging/package_io.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

namespace p = endfield::packaging;
namespace fs = std::filesystem;
using ehud::data::Json;

namespace {
unsigned checks{};
void check(bool value, const std::string& what) {
    ++checks;
    if (!value) throw std::runtime_error(what);
}
std::string guidText() {
    GUID g{};
    check(SUCCEEDED(CoCreateGuid(&g)), "Fresh GUID");
    wchar_t text[64]{};
    StringFromGUID2(g, text, 64);
    std::wstring value(text);
    return p::narrow(value.substr(1, value.size() - 2));
}
std::string utf8(const fs::path& path) { return p::narrow(path.native()); }
std::wstring commandArgument(std::wstring_view value) {
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++backslashes; out += c; continue; }
        if (c == L'"') out.append(backslashes + 1, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes, L'\\');
    return out + L"\"";
}
std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}
void writeFile(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(out), "Writing a test file");
}
// Inside the temporary work folder (never anything else is started or stopped).
bool inside(const fs::path& path, const fs::path& folder) {
    const auto a = path.lexically_normal().native(), b = folder.lexically_normal().native();
    return a.size() > b.size() && CompareStringOrdinal(a.c_str(), static_cast<int>(b.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
std::optional<std::wstring> imageOf(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return std::nullopt;
    std::wstring image(32768, L'\0');
    DWORD size = static_cast<DWORD>(image.size());
    const bool ok = QueryFullProcessImageNameW(h, 0, image.data(), &size);
    CloseHandle(h);
    if (!ok) return std::nullopt;
    image.resize(size);
    return image;
}

struct Run {
    DWORD exit{};
    Json result;
    std::string output;
};
Run runProgram(const fs::path& exe, const std::vector<std::wstring>& args, const fs::path& result, const fs::path& folder) {
    std::error_code ignored;
    fs::remove(result, ignored);
    std::wstring command = commandArgument(exe.native());
    for (const auto& a : args) command += L" " + commandArgument(a);
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE read{}, write{};
    check(CreatePipe(&read, &write, &inherit, 0) && SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0), "Output pipe");
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write;
    startup.hStdError = write;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, folder.c_str(), &startup, &process),
          "Start " + utf8(exe.filename()));
    CloseHandle(write);
    CloseHandle(process.hThread);
    Run run;
    std::thread reader([&] {
        char buffer[4096];
        DWORD n{};
        while (ReadFile(read, buffer, sizeof(buffer), &n, nullptr) && n) run.output.append(buffer, n);
    });
    const auto waited = WaitForSingleObject(process.hProcess, 300000);
    if (waited != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 99);
    reader.join();
    CloseHandle(read);
    GetExitCodeProcess(process.hProcess, &run.exit);
    CloseHandle(process.hProcess);
    check(waited == WAIT_OBJECT_0, utf8(exe.filename()) + " finished in time");
    if (fs::exists(result)) run.result = Json::parse(readFile(result));
    return run;
}

struct RegistryEntry {
    DWORD type{};
    std::vector<BYTE> data;
    bool operator==(const RegistryEntry&) const = default;
};
void enumerateKey(HKEY root, const std::wstring& path, std::map<std::wstring, RegistryEntry>& out) {
    HKEY key{};
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    out[path + L"\\"] = {};
    wchar_t name[16384];
    std::vector<BYTE> data(65536);
    for (DWORD n = 0;; ++n) {
        DWORD nameLength = 16384, type{}, dataLength = static_cast<DWORD>(data.size());
        if (RegEnumValueW(key, n, name, &nameLength, nullptr, &type, data.data(), &dataLength) != ERROR_SUCCESS) break;
        out[path + L"\\:" + name] = {type, std::vector<BYTE>(data.begin(), data.begin() + dataLength)};
    }
    std::vector<std::wstring> subkeys;
    for (DWORD n = 0;; ++n) {
        DWORD nameLength = 16384;
        if (RegEnumKeyExW(key, n, name, &nameLength, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        subkeys.emplace_back(name, nameLength);
    }
    RegCloseKey(key);
    for (const auto& s : subkeys) enumerateKey(root, path + L"\\" + s, out);
}
std::map<std::string, std::string> treeHashes(const fs::path& root) {
    std::map<std::string, std::string> out;
    if (!fs::exists(root)) return out;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const auto relative = p::narrow(entry.path().lexically_relative(root).generic_wstring());
        out[relative] = entry.is_directory() ? "<dir>" : p::fileSha256Hex(entry.path());
    }
    return out;
}

struct Snapshot {
    std::map<std::string, std::string> programs, startMenu, data;
    std::map<std::wstring, RegistryEntry> registry;
    bool operator==(const Snapshot&) const = default;
};

class Lab {
public:
    Lab(fs::path stub, fs::path uninstaller, fs::path app, const fs::path& work)
        : stub_(std::move(stub)), uninstaller_(std::move(uninstaller)), app_(std::move(app)) {
        const auto id = guidText();
        // Short folder names: Setup refuses install paths that could exceed MAX_PATH.
        root_ = work / (L"ehud-" + p::widen(id.substr(0, 8)));
        registry_ = L"Software\\EndfieldHUD-Test-" + p::widen(id);
        fs::create_directories(startMenu());
        fs::create_directories(dataBase().parent_path());
        fs::create_directories(workFolder());
    }
    ~Lab() {
        stopStrays();
        RegDeleteTreeW(HKEY_CURRENT_USER, registry_.c_str());
        for (int attempt = 0; attempt < 20 && fs::exists(root_); ++attempt) {
            (void)p::removeTree(root_);
            if (fs::exists(root_)) Sleep(250);
        }
    }
    Lab(const Lab&) = delete;
    Lab& operator=(const Lab&) = delete;

    fs::path work() const { return root_; }
    fs::path programs() const { return root_ / L"Programs"; }
    fs::path installRoot() const { return programs() / L"EndfieldHUD"; }
    fs::path startMenu() const { return root_ / L"StartMenu"; }
    fs::path dataBase() const { return root_ / L"Data" / L"EndfieldHUD"; }
    fs::path workFolder() const { return root_ / L"Work"; }
    fs::path resultFile() const { return root_ / L"result.json"; }
    const std::wstring& registry() const { return registry_; }
    std::wstring key(std::wstring_view leaf) const { return registry_ + L"\\" + std::wstring(leaf); }

    std::vector<std::wstring> isolation(const fs::path& installRoot = {}) const {
        return {L"--test-registry", registry_, L"--install-root", (installRoot.empty() ? this->installRoot() : installRoot).native(), L"--programs-folder",
                startMenu().native(), L"--data-base", dataBase().native(), L"--work-folder", workFolder().native()};
    }
    Run setup(const fs::path& setupFile, std::vector<std::wstring> extra = {}, const fs::path& installRoot = {}) {
        auto args = std::vector<std::wstring>{L"--quiet"};
        for (auto& a : isolation(installRoot)) args.push_back(std::move(a));
        for (auto& a : extra) args.push_back(std::move(a));
        args.push_back(L"--result");
        args.push_back(resultFile().native());
        return runProgram(setupFile, args, resultFile(), root_);
    }
    Run uninstall(std::vector<std::wstring> extra = {}, const fs::path& program = {}) {
        auto args = std::vector<std::wstring>{L"--quiet"};
        for (auto& a : isolation()) args.push_back(std::move(a));
        for (auto& a : extra) args.push_back(std::move(a));
        args.push_back(L"--result");
        args.push_back(resultFile().native());
        return runProgram(program.empty() ? installRoot() / L"uninstall.exe" : program, args, resultFile(), root_);
    }
    Snapshot snapshot() const {
        Snapshot s;
        s.programs = treeHashes(programs());
        s.startMenu = treeHashes(startMenu());
        s.data = treeHashes(dataBase());
        enumerateKey(HKEY_CURRENT_USER, registry_, s.registry);
        return s;
    }

    // Synthetic payloads: a stand-in EndfieldHUD.exe, the real uninstaller and
    // a few resource files (compressible, incompressible and empty).
    struct Payload {
        fs::path setup;
        p::PayloadManifest manifest;
    };
    Payload payload(const std::string& name, const std::string& release, int build, const std::map<std::string, std::string>& files, bool complete = true) {
        const auto tree = root_ / L"trees" / p::widen(name);
        fs::create_directories(tree);
        fs::copy_file(app_, tree / L"EndfieldHUD.exe", fs::copy_options::overwrite_existing);
        for (const auto& [path, bytes] : files) writeFile(tree / p::widen(path), bytes);
        auto sources = p::collectTree(tree);
        sources.push_back({"uninstall.exe", uninstaller_});
        p::PackOptions options;
        options.version = *p::parseProductVersion(release, build);
        options.commit = "ca04f142185c7de40acd8523bdb563195d90a1d1";
        options.complete = complete;
        fs::create_directories(root_ / L"setups");
        Payload out;
        out.setup = root_ / L"setups" / p::widen(name + "-setup.exe");
        out.manifest = p::writeSetup(stub_, sources, options, out.setup);
        return out;
    }

    // Fixture processes started by this test (stopped at the end even if a
    // check failed; only images inside the work folder are ever terminated).
    DWORD startApp(const std::vector<std::wstring>& args, const fs::path& marker) {
        const auto exe = installRoot() / L"EndfieldHUD.exe";
        std::wstring command = commandArgument(exe.native());
        for (const auto& a : args) command += L" " + commandArgument(a);
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, installRoot().c_str(), &startup, &process), "Start the stand-in app");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        markers_.push_back(marker);
        check(waitFor(marker, "started " + std::to_string(process.dwProcessId)), "The stand-in app started");
        return process.dwProcessId;
    }
    static bool waitFor(const fs::path& marker, const std::string& line, int milliseconds = 20000) {
        for (int waited = 0; waited < milliseconds; waited += 50) {
            if (readFile(marker).find(line) != std::string::npos) return true;
            Sleep(50);
        }
        return false;
    }
    static std::vector<DWORD> started(const fs::path& marker) {
        std::vector<DWORD> out;
        std::istringstream in(readFile(marker));
        std::string word;
        DWORD pid{};
        while (in >> word >> pid) if (word == "started") out.push_back(pid);
        return out;
    }
    bool alive(DWORD pid) const {
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (!h) return false;
        const bool running = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
        CloseHandle(h);
        return running && imageOf(pid) && inside(*imageOf(pid), root_);
    }
    void terminate(DWORD pid) const {
        const auto image = imageOf(pid);
        if (!image || !inside(*image, root_)) return; // never anything outside the work folder
        if (HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid)) {
            TerminateProcess(h, 0);
            WaitForSingleObject(h, 5000);
            CloseHandle(h);
        }
    }
    void stopStrays() const {
        for (const auto& marker : markers_) for (const auto pid : started(marker)) terminate(pid);
    }
private:
    fs::path stub_, uninstaller_, app_, root_;
    std::wstring registry_;
    std::vector<fs::path> markers_;
};

std::optional<std::wstring> regString(const std::wstring& key, const wchar_t* name) {
    wchar_t value[4096]{};
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, value, &bytes) != ERROR_SUCCESS) return std::nullopt;
    return std::wstring(value);
}
std::optional<DWORD> regNumber(const std::wstring& key, const wchar_t* name) {
    DWORD value{}, bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_DWORD, nullptr, &value, &bytes) != ERROR_SUCCESS) return std::nullopt;
    return value;
}
bool keyExists(const std::wstring& key) {
    HKEY k{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    RegCloseKey(k);
    return true;
}
std::string outcome(const Run& r) { return r.result.isObject() && r.result["outcome"].isString() ? r.result["outcome"].string() : "<none>"; }
std::string describe(const Run& r) { return "exit " + std::to_string(r.exit) + ", outcome " + outcome(r) + "\n" + r.output; }
bool warned(const Run& r, std::string_view text) {
    if (!r.result.isObject() || !r.result["warnings"].isArray()) return false;
    for (const auto& w : r.result["warnings"].array()) if (w.isString() && w.string().find(text) != std::string::npos) return true;
    return false;
}

// The installed folder holds exactly the payload, the install manifest and `extra`.
void expectInstalled(const Lab& lab, const p::PayloadManifest& m, const std::set<std::string>& extra = {}) {
    const auto tree = treeHashes(lab.installRoot());
    std::set<std::string> files;
    for (const auto& [path, hash] : tree) if (hash != "<dir>") files.insert(path);
    std::set<std::string> expected(extra);
    expected.insert(std::string(p::installManifestName));
    for (const auto& f : m.files) {
        expected.insert(f.path);
        const auto it = tree.find(f.path);
        check(it != tree.end() && it->second == f.sha256, "Installed bytes match the payload: " + f.path);
    }
    check(files == expected, "The install folder holds exactly the payload");
    const auto installed = p::readInstallManifest(lab.installRoot());
    check(installed && installed->version == m.version && installed->files.size() == m.files.size(), "The install manifest describes the payload");
    for (std::size_t n = 0; n < m.files.size(); ++n)
        check(installed->files[n].path == m.files[n].path && installed->files[n].sha256 == m.files[n].sha256, "Install manifest entries");
    // Nothing is left beside the install folder.
    std::set<std::string> siblings;
    for (const auto& e : fs::directory_iterator(lab.programs())) siblings.insert(p::narrow(e.path().filename().native()));
    check(siblings == std::set<std::string>{"EndfieldHUD"}, "No staging or backup folder remains beside the install folder");
}
void expectRegistration(const Lab& lab, const p::PayloadManifest& m, bool loginItem) {
    const auto uninstall = lab.key(L"Uninstall\\EndfieldHUD");
    const auto root = lab.installRoot().native();
    check(regString(uninstall, L"DisplayName") == L"EndfieldHUD" && regString(uninstall, L"Publisher") == L"DDDuoDuo", "Uninstall entry names EndfieldHUD");
    check(regString(uninstall, L"DisplayVersion") == p::widen(m.version.dotted()), "Uninstall entry version");
    check(regString(uninstall, L"InstallLocation") == root, "Uninstall entry location");
    check(regString(uninstall, L"DisplayIcon") == root + L"\\EndfieldHUD.exe,0", "Uninstall entry icon");
    const auto command = regString(uninstall, L"UninstallString");
    check(command && command->starts_with(L"\"" + root + L"\\uninstall.exe\"") && command->find(lab.registry()) != std::wstring::npos,
          "The private entry's uninstall command stays isolated");
    check(regString(uninstall, L"QuietUninstallString")->find(L"--quiet") != std::wstring::npos, "Quiet uninstall command");
    check(regNumber(uninstall, L"NoModify") == 1u && regNumber(uninstall, L"NoRepair") == 1u &&
          regNumber(uninstall, L"EstimatedSize") == static_cast<DWORD>((m.installedBytes() + 1023) / 1024), "Uninstall entry sizes and flags");
    const auto run = regString(lab.key(L"Run"), L"EndfieldHUD");
    if (loginItem) check(run == L"\"" + root + L"\\EndfieldHUD.exe\" --login", "Launch at login runs the installed EndfieldHUD.exe --login");
    else check(!run, "No launch-at-login entry");
    HKEY classes{};
    check(RegOpenKeyExW(HKEY_CURRENT_USER, lab.key(L"Classes").c_str(), 0, KEY_READ, &classes) == ERROR_SUCCESS, "Private Classes key");
    endfield::native::AppNotificationIdentity identity{std::string(p::installedAppUserModelID), std::string(p::installedActivatorCLSID),
                                                       utf8(lab.installRoot() / L"EndfieldHUD.exe"), utf8(lab.installRoot()), utf8(lab.startMenu() / L"EndfieldHUD.lnk")};
    auto state = endfield::native::AppNotificationRegistration::missing;
    check(SUCCEEDED(endfield::native::inspectAppNotificationRegistration(classes, identity, &state)) &&
          state == endfield::native::AppNotificationRegistration::matching, "Start menu shortcut with the AppUserModelID and toast activator");
    RegCloseKey(classes);
}
std::optional<bool> savedLaunchAtLogin(const Lab& lab) {
    if (!fs::exists(lab.dataBase() / "root.json")) return std::nullopt;
    const auto root = ehud::data::openApplicationRoot(lab.dataBase());
    return ehud::data::SettingsStore(root.data).value().boolean("launchAtLogin");
}

// --- Hidden task-dialog flows -------------------------------------------------
// The real Setup/uninstall windows driven with task-dialog messages, only when
// this test runs outside the interactive console session (CI runners, the
// laptop's SSH session), so no window ever appears on someone's desktop.
bool hiddenSession() {
    DWORD session{};
    return ProcessIdToSessionId(GetCurrentProcessId(), &session) && session != WTSGetActiveConsoleSessionId();
}
HWND findDialog(const fs::path& folder, DWORD timeout) {
    struct Search { const fs::path* folder; HWND found{}; } search{&folder};
    for (DWORD waited = 0; waited < timeout && !search.found; waited += 100) {
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            auto* s = reinterpret_cast<Search*>(data);
            wchar_t name[16]{};
            if (!GetClassNameW(window, name, 16) || std::wstring_view(name) != L"#32770") return TRUE;
            DWORD pid{};
            GetWindowThreadProcessId(window, &pid);
            const auto image = imageOf(pid);
            if (image && inside(*image, *s->folder)) { s->found = window; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        if (!search.found) Sleep(100);
    }
    return search.found;
}
// Starts `exe`, lets `drive` act on its first page, then presses Close until
// the program exits (Close is ignored while the progress page runs).
Run runWindow(const Lab& lab, const fs::path& exe, std::vector<std::wstring> args, const std::function<void(HWND)>& drive) {
    std::error_code ignored;
    fs::remove(lab.resultFile(), ignored);
    args.push_back(L"--result");
    args.push_back(lab.resultFile().native());
    std::wstring command = commandArgument(exe.native());
    for (const auto& a : args) command += L" " + commandArgument(a);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, lab.work().c_str(), &startup, &process), "Start a Setup window");
    CloseHandle(process.hThread);
    const auto dialog = findDialog(lab.work(), 30000);
    if (!dialog) TerminateProcess(process.hProcess, 99);
    check(dialog != nullptr, "The Setup window appeared (hidden session)");
    drive(dialog);
    for (int attempt = 0; attempt < 600 && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT; ++attempt) {
        DWORD_PTR ignoredResult{};
        SendMessageTimeoutW(dialog, TDM_CLICK_BUTTON, IDCLOSE, 0, SMTO_ABORTIFHUNG, 1000, &ignoredResult);
        Sleep(200);
    }
    Run run;
    if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 99);
    GetExitCodeProcess(process.hProcess, &run.exit);
    CloseHandle(process.hProcess);
    if (fs::exists(lab.resultFile())) run.result = Json::parse(readFile(lab.resultFile()));
    return run;
}
void press(HWND dialog, int button) {
    DWORD_PTR result{};
    check(SendMessageTimeoutW(dialog, TDM_CLICK_BUTTON, static_cast<WPARAM>(button), 0, SMTO_ABORTIFHUNG, 10000, &result) != 0, "Press a task-dialog button");
    Sleep(300);
}
void tick(HWND dialog, bool checked) {
    DWORD_PTR result{};
    check(SendMessageTimeoutW(dialog, TDM_CLICK_VERIFICATION, checked, FALSE, SMTO_ABORTIFHUNG, 10000, &result) != 0, "Set the task-dialog checkbox");
}
void windowFlows(Lab& lab, const Lab::Payload& v1) {
    if (!hiddenSession()) {
        std::cout << "Setup window flows skipped: interactive console session\n";
        return;
    }
    std::error_code ignored;
    fs::remove_all(lab.dataBase(), ignored);
    // Install with the launch-at-login box cleared.
    auto run = runWindow(lab, v1.setup, lab.isolation(), [&](HWND dialog) {
        tick(dialog, false);
        press(dialog, 100); // Install
    });
    check(run.exit == 0 && outcome(run) == "installed" && !run.result["launchAtLogin"].boolean(), "Setup window install: " + describe(run));
    expectInstalled(lab, v1.manifest);
    expectRegistration(lab, v1.manifest, false);
    check(savedLaunchAtLogin(lab) == false, "The window's choice is the app's saved preference");
    // Uninstall from the window: data removal needs the box and a second confirmation.
    run = runWindow(lab, lab.installRoot() / L"uninstall.exe", lab.isolation(), [&](HWND dialog) {
        tick(dialog, true);
        press(dialog, 100); // Uninstall
        press(dialog, 102); // Delete data and uninstall
    });
    check(run.exit == 0 && outcome(run) == "uninstalled" && run.result["dataRemoved"].boolean() && !fs::exists(lab.installRoot()) &&
          !fs::exists(lab.dataBase()), "Setup window uninstall with confirmed data removal: " + describe(run));
    // Keep-data path: the box is ticked but "Keep my data" wins.
    run = lab.setup(v1.setup, {L"--launch-at-login", L"off"});
    check(run.exit == 0, "Reinstall for the keep-data window: " + describe(run));
    run = runWindow(lab, lab.installRoot() / L"uninstall.exe", lab.isolation(), [&](HWND dialog) {
        tick(dialog, true);
        press(dialog, 100);
        press(dialog, 103); // Keep my data
    });
    check(run.exit == 0 && outcome(run) == "uninstalled" && !run.result["dataRemoved"].boolean() && fs::exists(lab.dataBase() / "root.json"),
          "Keep my data keeps the data: " + describe(run));
    // Cancel on the first page changes nothing.
    run = lab.setup(v1.setup, {L"--launch-at-login", L"off"});
    const auto before = lab.snapshot();
    run = runWindow(lab, lab.installRoot() / L"uninstall.exe", lab.isolation(), [&](HWND dialog) { press(dialog, IDCANCEL); });
    check(run.exit == 4 && lab.snapshot() == before, "Cancelling the uninstall window changes nothing: " + describe(run));
}

// The production layout is only resolved here (known folders, no creation,
// no registry access): its keys, paths and launch command.
void productionLayout() {
    const auto l = p::InstallLayout::production();
    check(!l.isolated() && l.installRoot.filename() == L"EndfieldHUD" && l.installRoot.parent_path().filename() == L"Programs" &&
          l.installRoot.is_absolute(), "Per-user install folder %LOCALAPPDATA%\\Programs\\EndfieldHUD");
    check(l.uninstallKey() == L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\EndfieldHUD" &&
          l.runKey() == L"Software\\Microsoft\\Windows\\CurrentVersion\\Run" &&
          l.approvedKey() == L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run" && l.classesKey() == L"Software\\Classes",
          "Production registry keys (HKCU only)");
    check(l.loginCommand() == L"\"" + l.executable().native() + L"\" --login" && l.shortcut().filename() == L"EndfieldHUD.lnk" &&
          l.dataBase.filename() == L"EndfieldHUD" && l.dataBase != l.installRoot && l.workFolder.is_absolute(),
          "Launch command, Start menu entry and the separate data root");
}

void contracts(const fs::path& stub, const fs::path& uninstaller, const fs::path& app, const fs::path& work) {
    productionLayout();
    Lab lab(stub, uninstaller, app, work);
    std::string compressible;
    for (int n = 0; n < 20000; ++n) compressible += "EndfieldHUD resource line " + std::to_string(n % 97) + "\n";
    std::string noise(60000, '\0');
    std::uint32_t x = 2463534242u;
    for (auto& c : noise) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; c = static_cast<char>(x); }
    const auto v1 = lab.payload("v1", "v1.2.0", 18, {{"resources/shell/scene.json", compressible}, {"fonts/Noise.ttf", noise},
                                                     {"resources/empty.dat", ""}, {"resources/old/legacy.bin", "legacy"}});
    const auto v2 = lab.payload("v2", "v1.2.1", 19, {{"resources/shell/scene.json", compressible + "v2"}, {"fonts/Noise.ttf", noise},
                                                     {"resources/empty.dat", ""}, {"resources/new.json", "{\"new\":true}"}});
    const auto v3 = lab.payload("v3", "v1.3.0", 20, {{"resources/shell/scene.json", compressible + "v3"}, {"fonts/Noise.ttf", noise},
                                                     {"resources/empty.dat", ""}, {"resources/new.json", "{\"new\":3}"}});
    const auto incomplete = lab.payload("dev", "v1.3.0", 21, {{"resources/empty.dat", ""}}, false);
    check(std::any_of(v1.manifest.files.begin(), v1.manifest.files.end(), [](const auto& f) { return f.encoding == p::Encoding::lzms; }) &&
          v1.manifest.find("fonts/Noise.ttf")->encoding == p::Encoding::identity && v1.manifest.find("resources/empty.dat")->stored == 0,
          "LZMS where it shrinks a file, identity otherwise");

    // Verification only.
    auto run = runProgram(v1.setup, {L"--verify-payload"}, lab.resultFile(), lab.work());
    check(run.exit == 0 && run.output.find("1.2.0 (18)") != std::string::npos && run.output.find("unsigned") != std::string::npos, "Payload verification: " + run.output);

    // 1. Fresh install: Mac default launch at login, nothing written to the data folder.
    run = lab.setup(v1.setup);
    check(run.exit == 0 && outcome(run) == "installed" && run.result["launchAtLogin"].boolean(), "Fresh install: " + describe(run));
    expectInstalled(lab, v1.manifest);
    expectRegistration(lab, v1.manifest, true);
    check(!fs::exists(lab.dataBase()), "A fresh install writes no user data");

    // 2. Repair keeps files Setup did not install.
    writeFile(lab.installRoot() / L"notes-by-user.txt", "mine");
    run = lab.setup(v1.setup);
    check(run.exit == 0 && outcome(run) == "repaired", "Repair: " + describe(run));
    expectInstalled(lab, v1.manifest, {"notes-by-user.txt"});
    check(readFile(lab.installRoot() / L"notes-by-user.txt") == "mine", "A foreign file survives repair");

    // 3. Upgrade with an explicit launch-at-login choice: saved to the app's setting.
    run = lab.setup(v2.setup, {L"--launch-at-login", L"off"});
    check(run.exit == 0 && outcome(run) == "upgraded" && run.result["previous"].string() == "1.2.0.18" && run.result["current"].string() == "1.2.1.19",
          "Upgrade: " + describe(run));
    expectInstalled(lab, v2.manifest, {"notes-by-user.txt"});
    expectRegistration(lab, v2.manifest, false);
    check(savedLaunchAtLogin(lab) == false, "The off choice is the app's saved preference");
    run = lab.setup(v2.setup, {L"--launch-at-login", L"on"});
    check(run.exit == 0 && outcome(run) == "repaired", "Login on: " + describe(run));
    expectRegistration(lab, v2.manifest, true);
    check(savedLaunchAtLogin(lab) == true, "The on choice is saved");
    // Quiet Setup without a choice follows the saved preference.
    {
        const auto root = ehud::data::openApplicationRoot(lab.dataBase());
        ehud::data::SettingsStore store(root.data);
        auto value = store.value();
        value.set("launchAtLogin", false);
        store.update(value);
    }
    run = lab.setup(v2.setup);
    check(run.exit == 0 && !run.result["launchAtLogin"].boolean(), "The saved preference is kept: " + describe(run));
    expectRegistration(lab, v2.manifest, false);
    // Windows Settings -> Startup turned off: reported, never overridden.
    {
        HKEY approved{};
        check(RegCreateKeyExW(HKEY_CURRENT_USER, lab.key(L"StartupApproved\\Run").c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &approved, nullptr) == ERROR_SUCCESS,
              "Private StartupApproved key");
        BYTE off[12]{0x03};
        RegSetValueExW(approved, L"EndfieldHUD", 0, REG_BINARY, off, sizeof(off));
        RegCloseKey(approved);
    }
    run = lab.setup(v2.setup, {L"--launch-at-login", L"on"});
    // (The warning is the app's own localized Startup text.)
    check(run.exit == 0 && !run.result["warnings"].array().empty() && !regString(lab.key(L"Run"), L"EndfieldHUD"),
          "The user's Startup choice is not overridden: " + describe(run));
    RegDeleteKeyValueW(HKEY_CURRENT_USER, lab.key(L"StartupApproved\\Run").c_str(), L"EndfieldHUD");
    run = lab.setup(v2.setup, {L"--launch-at-login", L"on"});
    check(run.exit == 0, "Login on again: " + describe(run));
    expectRegistration(lab, v2.manifest, true);

    // 4. Refusals change nothing.
    auto before = lab.snapshot();
    run = lab.setup(v1.setup);
    check(run.exit == 3 && outcome(run) == "refused" && run.result["refusal"].string() == "newer-installed", "Downgrade refused: " + describe(run));
    check(lab.snapshot() == before, "A refused downgrade changes nothing");
    // Below Windows 11 (build 22000) Setup refuses before reading or writing anything.
    for (const auto* build : {L"19045", L"21999"}) {
        run = lab.setup(v2.setup, {L"--test-windows-build", build});
        check(run.exit == 3 && outcome(run) == "refused" && run.result["refusal"].string() == "unsupported-windows" &&
              run.result["error"].string().find("Windows 11") != std::string::npos && run.result["error"].string().find(p::narrow(build)) != std::string::npos,
              "Windows 10 is refused: " + describe(run));
        check(lab.snapshot() == before, "A refusal below Windows 11 changes nothing");
    }
    run = lab.setup(v2.setup, {L"--test-windows-build", L"22000"});
    check(run.exit == 0 && outcome(run) == "repaired", "Windows 11 build 22000 is supported: " + describe(run));
    run = lab.setup(incomplete.setup);
    check(run.exit == 0 && outcome(run) == "upgraded", "Isolated layouts accept developer payloads: " + describe(run));
    run = lab.setup(v2.setup, {L"--allow-downgrade"});
    check(run.exit == 0 && outcome(run) == "upgraded", "Explicit downgrade: " + describe(run));
    expectInstalled(lab, v2.manifest, {"notes-by-user.txt"});
    {
        auto corrupt = lab.work() / L"setups" / L"corrupt-setup.exe";
        fs::copy_file(v2.setup, corrupt);
        std::fstream f(corrupt, std::ios::in | std::ios::out | std::ios::binary);
        f.seekg(-200, std::ios::end);
        char c{};
        f.get(c);
        f.seekp(-200, std::ios::end);
        f.put(static_cast<char>(c ^ 0x5a));
        f.close();
        before = lab.snapshot();
        run = lab.setup(corrupt);
        check(run.exit == 6 && lab.snapshot() == before, "A damaged payload is refused before any change: " + describe(run));
    }
    {
        auto id = lab.registry();
        for (auto& c : id) if (c == L'\\') c = L'.';
        HANDLE busy = CreateMutexW(nullptr, TRUE, (L"Local\\EndfieldHUD.Setup.{3B0F9C61-7A4E-4D1B-9E25-5C8A2F14D7B3}." + id).c_str());
        run = lab.setup(v2.setup);
        CloseHandle(busy);
        check(run.exit == 5, "A second Setup waits for the first: " + describe(run));
    }
    {
        const auto other = lab.work() / L"Other" / L"EndfieldHUD";
        writeFile(other / L"someone-else.txt", "not ours");
        before = lab.snapshot();
        run = lab.setup(v2.setup, {}, other);
        check(run.exit == 3 && run.result["refusal"].string() == "foreign-folder" && readFile(other / L"someone-else.txt") == "not ours" &&
              lab.snapshot() == before, "A folder Setup did not create is refused: " + describe(run));
    }

    // 5. Every step rolls back completely.
    for (std::size_t n = 0; n < static_cast<std::size_t>(p::InstallStep::count); ++n) {
        const auto step = std::string(p::installStepName(static_cast<p::InstallStep>(n)));
        before = lab.snapshot();
        run = lab.setup(v3.setup, {L"--launch-at-login", L"off", L"--fail-after", p::widen(step)});
        check(run.exit == 1 && outcome(run) == "failed" && run.result["error"].string().find("Injected failure") != std::string::npos,
              "Injected failure after " + step + ": " + describe(run));
        check(lab.snapshot() == before, "Complete rollback after " + step);
    }

    // 6. An interrupted swap is repaired by the next run.
    check(MoveFileExW(lab.installRoot().c_str(), (lab.programs() / L".EndfieldHUD.backup-0badf00d").c_str(), 0), "Simulate an interrupted swap");
    writeFile(lab.programs() / L".EndfieldHUD.staging-12345678" / L"partial.bin", "partial");
    run = lab.setup(v2.setup);
    check(run.exit == 0 && outcome(run) == "repaired" && warned(run, "interrupted update"), "Interrupted update restored: " + describe(run));
    expectInstalled(lab, v2.manifest, {"notes-by-user.txt"});

    // 7. A running EndfieldHUD that refuses to close is left alone.
    const auto refuseMarker = lab.work() / L"refuse-marker.txt";
    const auto refusing = lab.startApp({L"--marker", refuseMarker.native(), L"--refuse-shutdown"}, refuseMarker);
    before = lab.snapshot();
    run = lab.setup(v3.setup);
    check(run.exit == 3 && run.result["refusal"].string() == "close-failed" && lab.alive(refusing), "An app that refuses to close stops the update: " + describe(run));
    check(lab.snapshot() == before, "Nothing changed while it ran");
    lab.terminate(refusing);

    // 8. A running EndfieldHUD is closed through the session-end protocol,
    //    updated and restarted.
    const auto marker = lab.work() / L"marker.txt";
    const auto first = lab.startApp({L"--marker", marker.native(), L"--register-restart"}, marker);
    run = lab.setup(v3.setup);
    check(run.exit == 0 && outcome(run) == "upgraded" && run.result["stoppedApplications"].integer() == 1 && run.result["restarted"].boolean(),
          "Update of a running app: " + describe(run));
    check(Lab::waitFor(marker, "query " + std::to_string(first)) && Lab::waitFor(marker, "ended " + std::to_string(first)) && !lab.alive(first),
          "The running app received WM_QUERYENDSESSION/WM_ENDSESSION and exited");
    bool restarted{};
    for (int waited = 0; waited < 20000 && !restarted; waited += 100) {
        for (const auto pid : Lab::started(marker)) restarted = restarted || (pid != first && lab.alive(pid));
        if (!restarted) Sleep(100);
    }
    check(restarted, "The updated app was restarted from the install folder");
    expectInstalled(lab, v3.manifest, {"notes-by-user.txt"});

    // 9. Uninstall: closes the app, removes exactly what Setup installed and
    //    every registration, keeps foreign files and all user data.
    writeFile(lab.dataBase() / L"v1" / L"Notes" / L"note.json", "{\"text\":\"keep\"}");
    run = lab.uninstall();
    check(run.exit == 0 && outcome(run) == "uninstalled" && run.result["stoppedApplications"].integer() == 1 && !run.result["dataRemoved"].boolean(),
          "Uninstall: " + describe(run));
    for (const auto pid : Lab::started(marker)) check(!lab.alive(pid), "Uninstall closed the app");
    std::set<std::string> left;
    for (const auto& e : fs::recursive_directory_iterator(lab.installRoot())) left.insert(p::narrow(e.path().lexically_relative(lab.installRoot()).generic_wstring()));
    check(left == std::set<std::string>{"notes-by-user.txt"}, "Only the file Setup did not create remains");
    check(run.result["keptFiles"].array().size() == 1 && run.result["keptFiles"].array()[0].string() == "notes-by-user.txt", "Kept files are reported");
    check(!keyExists(lab.key(L"Uninstall\\EndfieldHUD")) && !regString(lab.key(L"Run"), L"EndfieldHUD") &&
          !keyExists(lab.key(L"Classes\\CLSID\\" + p::widen(p::installedActivatorCLSID))) && !fs::exists(lab.startMenu() / L"EndfieldHUD.lnk"),
          "Every registration is removed");
    check(readFile(lab.dataBase() / L"v1" / L"Notes" / L"note.json") == "{\"text\":\"keep\"}" && savedLaunchAtLogin(lab) == true, "User data is kept");
    bool relocated{};
    for (const auto& e : fs::directory_iterator(lab.workFolder()))
        if (e.path().filename().native().starts_with(L"EndfieldHUD-Uninstall-")) relocated = fs::exists(e.path() / L"uninstall.exe");
    check(relocated, "The uninstaller ran from a relocated copy");
    (void)p::removeTree(lab.installRoot());

    // 10. Nothing installed: refused.
    run = lab.uninstall({}, uninstaller);
    check(run.exit == 3 && run.result["refusal"].string() == "not-installed", "Uninstall without an install: " + describe(run));

    // 11. Explicit data removal removes only a recognized data root.
    run = lab.setup(v1.setup);
    check(run.exit == 0, "Reinstall: " + describe(run));
    run = lab.uninstall({L"--remove-user-data"});
    check(run.exit == 0 && run.result["dataRemoved"].boolean(), "Uninstall with data removal: " + describe(run));
    check(!fs::exists(lab.dataBase()), "The data root was deleted");
    {
        std::string left;
        if (fs::exists(lab.installRoot()))
            for (const auto& e : fs::recursive_directory_iterator(lab.installRoot())) left += " " + p::narrow(e.path().lexically_relative(lab.installRoot()).native());
        check(!fs::exists(lab.installRoot()), "The install folder was removed; left:" + (left.empty() ? std::string(" (empty folder)") : left));
    }
    for (const auto& [name, value] : lab.snapshot().registry)
        check(name.find(L"\\:") == std::wstring::npos, "No private registry value remains: " + p::narrow(name));
    run = lab.setup(v1.setup);
    check(run.exit == 0, "Reinstall: " + describe(run));
    writeFile(lab.dataBase() / L"unrelated.txt", "not EndfieldHUD data");
    run = lab.uninstall({L"--remove-user-data"});
    check(run.exit == 0 && !run.result["dataRemoved"].boolean() && warned(run, "not recognized") && fs::exists(lab.dataBase() / L"unrelated.txt"),
          "A folder that is not EndfieldHUD data is kept: " + describe(run));

    // 12. A signed setup (certificate table after the trailer) still verifies.
    {
        const auto signedSetup = lab.work() / L"setups" / L"signed-setup.exe";
        fs::copy_file(v1.setup, signedSetup);
        auto bytes = readFile(signedSetup);
        check(bytes.size() % 8 == 0, "The setup ends 8-byte aligned");
        const auto table = static_cast<std::uint32_t>(bytes.size());
        const std::uint32_t length = 24;
        std::string certificate(length, '\0');
        certificate[0] = static_cast<char>(length);
        certificate[4] = 0x00; certificate[5] = 0x02; // WIN_CERT_REVISION_2_0
        certificate[6] = 0x02;                       // WIN_CERT_TYPE_PKCS_SIGNED_DATA
        bytes += certificate;
        const auto pe = *reinterpret_cast<const std::uint32_t*>(bytes.data() + 0x3c);
        const auto directory = pe + 24 + 144;
        std::memcpy(bytes.data() + directory, &table, 4);
        std::memcpy(bytes.data() + directory + 4, &length, 4);
        writeFile(signedSetup, bytes);
        run = runProgram(signedSetup, {L"--verify-payload"}, lab.resultFile(), lab.work());
        check(run.exit == 0 && run.output.find(" signed") != std::string::npos, "A certificate table after the payload is honoured: " + run.output);
    }
    windowFlows(lab, v1);
    std::cout << "Installer contracts: " << checks << " checks passed\n";
}

// The staged release setup: verified, installed, checked with the installed
// EndfieldHUD.exe --verify-package (no window, mutex or data root) and removed.
void release(const fs::path& setup, const fs::path& work) {
    const auto payload = p::SetupPayload::open(setup);
    const auto& m = payload.manifest();
    check(m.find(p::thirdPartyNoticesName) != nullptr && m.find(p::applicationExecutable) != nullptr,
          "The setup's file list includes THIRD-PARTY-NOTICES.txt beside EndfieldHUD.exe");
    Lab lab({}, {}, {}, work);
    auto run = runProgram(setup, {L"--verify-payload"}, lab.resultFile(), lab.work());
    check(run.exit == 0, "Release payload verification: " + run.output);
    run = lab.setup(setup, {L"--launch-at-login", L"off"});
    check(run.exit == 0 && outcome(run) == "installed", "Release install: " + describe(run));
    expectInstalled(lab, m);
    expectRegistration(lab, m, false);
    const auto installed = p::readInstallManifest(lab.installRoot());
    check(installed && std::any_of(installed->files.begin(), installed->files.end(), [](const p::InstalledFile& f) { return f.path == p::thirdPartyNoticesName; }),
          "The uninstall list (install manifest) includes THIRD-PARTY-NOTICES.txt");
    run = runProgram(lab.installRoot() / L"EndfieldHUD.exe", {L"--verify-package"}, lab.resultFile(), lab.work());
    check(m.complete ? run.exit == 0 : run.exit != 0, "The installed package verifies: " + run.output);
    run = lab.uninstall();
    check(run.exit == 0 && outcome(run) == "uninstalled" && !fs::exists(lab.installRoot()), "Release uninstall: " + describe(run));
    check(!keyExists(lab.key(L"Uninstall\\EndfieldHUD")) && !fs::exists(lab.startMenu() / L"EndfieldHUD.lnk"), "Release registrations removed");
    std::cout << "Release install check (" << m.files.size() << " files, " << m.installedBytes() << " bytes, " << (m.complete ? "complete" : "incomplete")
              << "): " << checks << " checks passed\n";
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    try {
        // CTest passes '/'-separated paths; Setup accepts only native absolute
        // paths. Work happens in a short folder below %TEMP%, in its long form:
        // %TEMP% can be an 8.3 alias (GitHub's runners use C:\Users\RUNNER~1),
        // and the Shell stores a shortcut's target with long names, so the
        // installed shortcut would not compare equal to an 8.3 install root.
        const auto native = [](const wchar_t* value) { return fs::path(value).make_preferred().lexically_normal(); };
        std::wstring temp(MAX_PATH + 1, L'\0');
        temp.resize(GetTempPathW(static_cast<DWORD>(temp.size()), temp.data()));
        std::wstring longTemp(32768, L'\0');
        const auto longLength = GetLongPathNameW(temp.c_str(), longTemp.data(), static_cast<DWORD>(longTemp.size()));
        if (longLength && longLength < longTemp.size()) temp.assign(longTemp.data(), longLength);
        const auto work = fs::path(temp).lexically_normal().parent_path();
        if (argc == 3 && std::wstring_view(argv[1]) == L"--release") release(native(argv[2]), work);
        else if (argc == 4) contracts(native(argv[1]), native(argv[2]), native(argv[3]), work);
        else { std::cerr << "usage: packaging_installer_tests <stub> <uninstall.exe> <fixture app> | --release <setup>\n"; return 2; }
        CoUninitialize();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Installer contracts failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
