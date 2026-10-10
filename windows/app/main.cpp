// EndfieldHUD.exe: production entry point (Mac AppDelegate launch path).
//
//   1. A second launch forwards its arguments to the running copy and exits
//      without touching data (single instance; also the activation path when
//      Windows hides the tray icon).
//   2. The versioned %LOCALAPPDATA%\EndfieldHUD root is opened; a newer or
//      damaged root is reported and preserved, never erased.
//   3. Packaged resources are verified once against resources/resources.json
//      (app/application_package.hpp); a missing or damaged module resource
//      disables only the modules that read it, named in one localized notice.
//   4. The single Application owner runs with real providers and per-module
//      failure isolation until Quit, logoff or a Restart Manager shutdown.
//
// --verify-package checks only this executable's own resource package and
// exits; it creates no window, mutex or data root (used by hidden tests).
#include "app/application.hpp"
#include "app/application_modules.hpp"
#include "app/application_package.hpp"
#include "app/single_instance.hpp"
#include "core/application_arguments.hpp"
#include "core/data/application_root.hpp"
#include "core/localization.hpp"
#include "core/resource_locator.hpp"
#include <array>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <winrt/base.h>

namespace app = endfield::app;
namespace core = endfield::core;
namespace fs = std::filesystem;
namespace {
std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), count, nullptr, nullptr);
    return out;
}
std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count);
    return out;
}
core::Language interfaceLanguage() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (!GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH)) return core::Language::english;
    const auto text = narrow(name);
    const std::array<std::string_view, 1> preferred{text};
    return core::resolveLanguage(preferred);
}
void report(std::string_view english, std::string_view chinese, UINT icon = MB_ICONERROR) {
    const auto language = interfaceLanguage();
    MessageBoxW(nullptr, wide(core::localized(english, chinese, language)).c_str(), L"EndfieldHUD", MB_OK | icon | MB_SETFOREGROUND);
}
fs::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) throw std::runtime_error("Cannot locate EndfieldHUD.exe");
    buffer.resize(length);
    return fs::path(buffer).parent_path();
}

app::ApplicationOptions productionOptions(const app::PackagedResources& package, const fs::path& data, const core::ApplicationArguments& arguments) {
    app::ApplicationOptions a;
    a.mode = app::ApplicationMode::production;
    a.runtimeInput = true;
    a.packet = package.packet; a.cache = package.cache; a.cachePin = package.cachePin;
    a.shader = package.shader; a.watchBlur = package.watchBlur; a.cursor = package.cursor;
    a.notesAssets = package.notesAssets; a.notesAssetsSHA = package.notesAssetsSHA; a.notesFormatAssets = package.notesFormatAssets;
    a.shelfAssets = package.shelfAssets; a.shelfMask = package.shelfMask; a.clipboardAssets = package.clipboardAssets;
    a.residentIcons = package.residentIcons; a.settingsAssets = package.settingsAssets; a.archiveAssets = package.archiveAssets;
    a.storageAssets = package.storageAssets; a.activityAssets = package.activityAssets;
    a.mapGeography = package.mapGeography; a.mapPlayerAssets = package.mapPlayerAssets; a.orbipomAssets = package.orbipomAssets;
    a.dataRoot = a.shelfDataRoot = data;
    // Notes hosts module selection and the shared text services; without it
    // only the source shell (and the tray with Quit) can run.
    const bool modules = package.modules;
    a.reader = a.calendar = a.projection = modules;
    a.nativeClipboard = modules && !a.clipboardAssets.empty();
    a.nativeActivity = modules && !a.activityAssets.empty();
    a.nativeVolume = a.nativeBattery = a.nativeEventLog = modules;
    a.startup = arguments.startup;
    a.isolateModuleFailures = true;
    a.windowTitle = L"EndfieldHUD";
    return a;
}

// Names the disabled modules in the interface language (no paths or hashes).
void reportDisabled(const app::PackagedResources& package) {
    if (package.disabled.empty()) return;
    const auto language = interfaceLanguage();
    std::string names;
    for (const auto module : package.disabled) {
        const auto id = core::moduleIdentifier(module);
        if (!names.empty()) names += core::isCJK(language) ? "\u3001" : ", ";
        names += app::moduleCaption(id, language, id);
    }
    const auto text = core::localized("Some EndfieldHUD resources are missing or damaged, so these modules are disabled:",
                                      "部分 EndfieldHUD 资源缺失或已损坏，以下模块已停用：", language) + "\n" + names + "\n\n" +
                      core::localized("Reinstall EndfieldHUD to restore them.", "请重新安装 EndfieldHUD 以恢复。", language);
    MessageBoxW(nullptr, wide(text).c_str(), L"EndfieldHUD", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}

int verifyPackage() {
    // Hidden package check: no window, mutex, data root or user data. Every
    // declared resource must verify, every required one must be packaged and
    // nothing unlisted may sit in the package.
    auto locator = core::ResourceLocator::open(executableDirectory() / "resources");
    int failures = 0;
    for (const auto id : app::packagedResourceIDs())
        if (!locator.declared(id)) {
            const bool required = app::packagedResourceRequired(id);
            std::cout << id << ": not packaged" << (required ? "" : " (optional build input)") << '\n';
            if (required) ++failures;
        }
    for (const auto& id : locator.ids()) {
        const auto& result = locator.resolve(id);
        std::cout << id << ": " << (result.status == core::ResourceLocator::Status::verified ? "verified" : result.reason) << '\n';
        if (result.status != core::ResourceLocator::Status::verified) ++failures;
    }
    for (const auto& path : locator.unlistedFiles()) { std::cout << "unlisted: " << path << '\n'; ++failures; }
    const auto package = app::resolvePackagedResources(locator);
    std::cout << "shell: " << (package.shell ? "ready" : "unavailable") << ", modules disabled: " << package.disabled.size() << '\n';
    return failures || !package.shell ? 2 : 0;
}

int run(const std::vector<std::string>& values) {
    std::vector<std::string_view> views(values.begin(), values.end());
    if (views.size() == 1 && views[0] == "--verify-package") return verifyPackage();
    core::ApplicationArguments arguments;
    try { arguments = core::parseApplicationArguments(views); }
    catch (const std::exception&) { report("EndfieldHUD received invalid launch arguments.", "EndfieldHUD 收到了无效的启动参数。"); return 1; }

    app::SingleInstanceGuard instance(app::productionSingleInstanceNames());
    if (!instance.primary()) {
        // AppDelegate: a second copy activates the existing one and exits.
        (void)instance.forward(views);
        return 0;
    }
    instance.listen();
    // Restart after Restart Manager shutdowns (installer/updater) and, when
    // the user allows it, after an update reboot, like a login item.
    RegisterApplicationRestart(L"--login", RESTART_NO_CRASH | RESTART_NO_HANG);

    ehud::data::ApplicationRoot root;
    try { root = ehud::data::openApplicationRoot(ehud::data::localApplicationDataBase()); }
    catch (const ehud::data::StoreError& e) {
        if (e.code() == ehud::data::StoreErrorCode::newerVersion)
            report("This EndfieldHUD data was created by a newer version. It was left unchanged; install the newer EndfieldHUD to use it.",
                   "此 EndfieldHUD 数据由更新版本创建，已保持原样。请安装更新版本的 EndfieldHUD 后使用。");
        else report("EndfieldHUD could not open its data folder. Nothing was changed.", "EndfieldHUD 无法打开数据文件夹，未作任何更改。");
        return 1;
    }

    auto locator = core::ResourceLocator::open(executableDirectory() / "resources");
    const auto package = app::resolvePackagedResources(locator);
    if (!package.shell) {
        report("The EndfieldHUD display resources are missing or damaged. Reinstall EndfieldHUD; your data was not changed.",
               "EndfieldHUD 显示资源缺失或已损坏。请重新安装 EndfieldHUD；数据未被更改。");
        return 1;
    }
    reportDisabled(package);
    auto options = productionOptions(package, root.data, arguments);
    // Factory-added module areas resolve their own verified resources (the
    // locator outlives the Application and is used on its UI thread only).
    options.resources = [&locator](std::string_view id) -> fs::path { return locator.available(id) ? locator.resolve(id).path : fs::path{}; };
    app::Application application(std::move(options));
    instance.setHandler([&application](std::vector<std::string> forwarded) { application.activate(forwarded); });
    return application.run();
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count{};
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> values;
    for (int n = 1; argv && n < count; ++n) values.push_back(narrow(argv[n]));
    if (argv) LocalFree(argv);
    try { return run(values); }
    catch (const winrt::hresult_error& e) {
        std::fprintf(stderr, "EndfieldHUD failed: HRESULT %08x\n", static_cast<unsigned>(e.code().value));
        report("EndfieldHUD could not start its display. Your data was not changed.", "EndfieldHUD 无法启动显示界面，数据未被更改。");
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "EndfieldHUD failed: %s\n", e.what());
        report("EndfieldHUD stopped because of an error. Your saved data was not changed.", "EndfieldHUD 因错误而停止，已保存的数据未被更改。");
        return 1;
    }
}
