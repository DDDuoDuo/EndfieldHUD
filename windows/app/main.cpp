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
//      Module areas registered by the CMake fragments (app/application_areas.hpp,
//      app/module_owner.hpp) join after the built-in owners.
//
// --verify-package checks only this executable's own resource package and
// exits; it creates no window, mutex or data root (used by hidden tests).
//
// A fatal error (an exception that left the run: only module owners are
// isolated) is still reported in one localized message box, and is also
// written to <data root>/v1/Diagnostics/last-fatal.txt: the exception type, a
// bounded message with path-like words redacted, the HRESULT and the host
// stage/module owner that was active (app/fatal_stage.hpp). No document text.
// Without the host backdrop (Windows before Windows 11 build 22000) the
// message says that Windows 11 is required.
#include "app/application.hpp"
#include "app/application_areas.hpp"
#include "app/application_modules.hpp"
#include "app/application_package.hpp"
#include "app/fatal_stage.hpp"
#include "app/module_failure_log.hpp"
#include "app/single_instance.hpp"
#include "core/application_arguments.hpp"
#include "core/data/application_root.hpp"
#include "core/data/file_io.hpp"
#include "core/localization.hpp"
#include "core/resource_locator.hpp"
#include "native/font_resources.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <functional>
#include <optional>
#include <iostream>
#include <string>
#include <typeinfo>
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
// The System UI language (the preferred UI-language list, the one reader
// native::preferredSystemLanguage): no settings are open yet.
core::Language interfaceLanguage() { return endfield::native::preferredSystemLanguage(); }
// Startup and fatal dialogs name Windows parts (data folder, packaged
// resources, launch arguments) and have no Mac source: Windows-only text in
// the five source languages, in the catalog's terms.
using Text = core::TranslationEntry;
constexpr Text newerData{"This EndfieldHUD data was created by a newer version. It was left unchanged; install the newer EndfieldHUD to use it",
    "此 EndfieldHUD 数据由更新版本创建，已保持原样。请安装更新版本的 EndfieldHUD 后使用",
    "此 EndfieldHUD 資料由較新版本建立，已保持原樣。請安裝較新版本的 EndfieldHUD 後使用",
    "この EndfieldHUD のデータは新しいバージョンで作成されたため、変更せずに残しました。使用するには新しいバージョンの EndfieldHUD をインストールしてください",
    "이 EndfieldHUD 데이터는 최신 버전에서 만들어져 변경하지 않고 그대로 두었습니다. 사용하려면 최신 버전의 EndfieldHUD를 설치하세요"};
constexpr Text dataFolderFailed{"EndfieldHUD could not open its data folder. Nothing was changed",
    "EndfieldHUD 无法打开数据文件夹，未作任何更改", "EndfieldHUD 無法開啟資料所在的資料夾，未作任何變更",
    "EndfieldHUD のデータフォルダーを開けませんでした。何も変更されていません", "EndfieldHUD 데이터 폴더를 열 수 없습니다. 변경된 내용은 없습니다"};
constexpr Text invalidArguments{"EndfieldHUD received invalid launch arguments", "EndfieldHUD 收到了无效的启动参数", "EndfieldHUD 收到了無效的啟動參數",
    "EndfieldHUD に無効な起動引数が渡されました", "EndfieldHUD에 잘못된 실행 인수가 전달되었습니다"};
constexpr Text displayResourcesDamaged{"The EndfieldHUD display resources are missing or damaged. Reinstall EndfieldHUD; your data was not changed",
    "EndfieldHUD 显示资源缺失或已损坏。请重新安装 EndfieldHUD；数据未被更改", "EndfieldHUD 顯示資源遺失或已損毀。請重新安裝 EndfieldHUD；資料未被變更",
    "EndfieldHUD の表示リソースが見つからないか、破損しています。EndfieldHUD を再インストールしてください。データは変更されていません",
    "EndfieldHUD 표시 리소스가 없거나 손상되었습니다. EndfieldHUD를 다시 설치하세요. 데이터는 변경되지 않았습니다"};
constexpr Text modulesDisabled{"Some EndfieldHUD resources are missing or damaged, so these modules are disabled:",
    "部分 EndfieldHUD 资源缺失或已损坏，以下模块已停用：", "部分 EndfieldHUD 資源遺失或已損毀，以下模組已停用：",
    "EndfieldHUD の一部のリソースが見つからないか破損しているため、次のモジュールを無効にしました：",
    "일부 EndfieldHUD 리소스가 없거나 손상되어 다음 모듈이 비활성화되었습니다:"};
constexpr Text reinstallModules{"Reinstall EndfieldHUD to restore them", "请重新安装 EndfieldHUD 以恢复", "請重新安裝 EndfieldHUD 以還原",
    "復元するには EndfieldHUD を再インストールしてください", "복원하려면 EndfieldHUD를 다시 설치하세요"};
constexpr Text displayFailed{"EndfieldHUD could not start its display. Your data was not changed", "EndfieldHUD 无法启动显示界面，数据未被更改",
    "EndfieldHUD 無法啟動顯示介面，資料未被變更", "EndfieldHUD の表示を開始できませんでした。データは変更されていません",
    "EndfieldHUD 화면을 시작할 수 없습니다. 데이터는 변경되지 않았습니다"};
constexpr Text stoppedByError{"EndfieldHUD stopped because of an error. Your saved data was not changed", "EndfieldHUD 因错误而停止，已保存的数据未被更改",
    "EndfieldHUD 因錯誤而停止，已儲存的資料未被變更", "エラーのため EndfieldHUD が停止しました。保存されたデータは変更されていません",
    "오류로 인해 EndfieldHUD가 중지되었습니다. 저장된 데이터는 변경되지 않았습니다"};
// Windows has no host backdrop before Windows 11 (build 22000), and the HUD's
// blurred background needs it.
constexpr Text windows11Required{"EndfieldHUD requires Windows 11 (build 22000 or later): this version of Windows cannot draw the HUD's blurred background. Your data was not changed",
    "EndfieldHUD 需要 Windows 11（内部版本 22000 或更高版本）：此版本的 Windows 无法绘制 HUD 的模糊背景。数据未被更改",
    "EndfieldHUD 需要 Windows 11（組建 22000 或更新版本）：此版本的 Windows 無法繪製 HUD 的模糊背景。資料未被變更",
    "EndfieldHUD には Windows 11（ビルド 22000 以降）が必要です。このバージョンの Windows では HUD のぼかし背景を描画できません。データは変更されていません",
    "EndfieldHUD를 사용하려면 Windows 11(빌드 22000 이상)이 필요합니다. 이 버전의 Windows에서는 HUD의 흐림 배경을 그릴 수 없습니다. 데이터는 변경되지 않았습니다"};
void report(const Text& text) {
    MessageBoxW(nullptr, wide(core::translated(text, interfaceLanguage())).c_str(), L"EndfieldHUD", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
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
    a.notesFormatAssets = package.notesFormatAssets;
    a.residentIcons = package.residentIcons; a.settingsAssets = package.settingsAssets;
    a.mapGeography = package.mapGeography; a.mapPlayerAssets = package.mapPlayerAssets; a.orbipomAssets = package.orbipomAssets;
    a.dataRoot = a.shelfDataRoot = data;
    // Notes hosts module selection and the shared text services; without it
    // only the source shell (and the tray with Quit) can run.
    const bool modules = package.modules;
    a.reader = a.calendar = a.projection = modules;
    a.nativeClipboard = a.fileShelf = a.archive = modules;
    a.nativeActivity = a.nativeStorage = modules;
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
    const auto text = std::string(core::translated(modulesDisabled, language)) + "\n" + names + "\n\n" + std::string(core::translated(reinstallModules, language));
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

// The versioned data folder of the current run (<base>/v1), once opened; the
// fatal record goes to its Diagnostics folder. Empty before the root opens.
fs::path fatalDataFolder;

// Best effort: never throws, never replaces the message box below.
void writeFatal(std::string_view type, std::string_view message, std::optional<std::uint32_t> hresult) noexcept {
    try {
        std::string text = "EndfieldHUD fatal error\n";
        char stamp[32]{};
        const auto now = std::time(nullptr);
        std::tm utc{};
        if (!gmtime_s(&utc, &now) && std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &utc)) text += "utc: " + std::string(stamp) + "\n";
        text += "exception: " + app::redacted(type.substr(0, 160)) + "\n";
        text += "message: " + app::redacted(message) + "\n";
        if (hresult) {
            char code[16]{};
            std::snprintf(code, sizeof code, "%08X", static_cast<unsigned>(*hresult));
            text += "hresult: 0x" + std::string(code) + "\n";
        }
        const auto stage = app::FatalStage::last();
        text += "stage: " + (stage.empty() ? std::string("(not recorded)") : std::string(stage)) + "\n";
        std::fputs(text.c_str(), stderr);
        OutputDebugStringA(text.c_str());
        if (fatalDataFolder.empty()) return;
        constexpr std::size_t maximum = 4096;
        const auto path = fatalDataFolder / "Diagnostics" / "last-fatal.txt";
        ehud::data::detail::replaceFile(path, ehud::data::detail::readFile(path, maximum), text, maximum);
    } catch (...) {}
}

// Opens the data root, reporting failures as at startup; nullopt: reported.
std::optional<ehud::data::ApplicationRoot> openRoot(const fs::path& base) {
    try { return ehud::data::openApplicationRoot(base); }
    catch (const ehud::data::StoreError& e) {
        if (e.code() == ehud::data::StoreErrorCode::newerVersion)
            report(newerData);
        else report(dataFolderFailed);
        return std::nullopt;
    }
}

int run(const std::vector<std::string>& values) {
    std::vector<std::string_view> views(values.begin(), values.end());
    if (views.size() == 1 && views[0] == "--verify-package") return verifyPackage();
    const app::FatalStage launch("launch");
    core::ApplicationArguments arguments;
    try { arguments = core::parseApplicationArguments(views); }
    catch (const std::exception&) { report(invalidArguments); return 1; }

    // --data-root (section 9 gate): an explicit absolute isolated root with
    // its own single instance; never the user's root, running copy or restart.
    fs::path isolated;
    if (!arguments.dataRoot.empty()) {
        isolated = fs::path(std::u8string(reinterpret_cast<const char8_t*>(arguments.dataRoot.data()), arguments.dataRoot.size())).lexically_normal();
        if (!isolated.is_absolute()) { report(invalidArguments); return 1; }
    }
    app::SingleInstanceGuard instance(isolated.empty() ? app::productionSingleInstanceNames() : app::isolatedSingleInstanceNames(isolated));
    if (!instance.primary()) {
        // AppDelegate: a second copy activates the existing one and exits.
        (void)instance.forward(views);
        return 0;
    }
    instance.listen();
    // Restart after Restart Manager shutdowns (installer/updater) and, when
    // the user allows it, after an update reboot, like a login item.
    if (isolated.empty()) RegisterApplicationRestart(L"--login", RESTART_NO_CRASH | RESTART_NO_HANG);

    auto locator = core::ResourceLocator::open(executableDirectory() / "resources");
    const auto package = app::resolvePackagedResources(locator);
    if (!package.shell) {
        report(displayResourcesDamaged);
        return 1;
    }
    reportDisabled(package);
    // Each run opens the root again: a GPU device loss ends a run with every
    // store closed, then a fresh owner runs under the same instance guard.
    auto startup = arguments.startup;
    std::optional<core::Module> reopen;
    // Bounded device recovery: a GPU that keeps failing ends the run with the
    // usual error instead of restarting forever.
    std::vector<std::chrono::steady_clock::time_point> recoveries;
    for (;;) {
        const auto base = isolated.empty() ? ehud::data::localApplicationDataBase() : isolated;
        // An earlier version's Mac import killed between its two renames left
        // no v1: its backup moves back before anything opens the root.
        (void)ehud::data::restoreInterruptedImportRoot(base);
        const auto root = openRoot(base);
        if (!root) return 1;
        fatalDataFolder = root->data;
        core::ApplicationArguments runArguments = arguments;
        runArguments.startup = startup;
        auto options = productionOptions(package, root->data, runArguments);
        options.isolatedDataRoot = !isolated.empty();
        // Section 9 gate only: ENDFIELDHUD_CLOSED_GRAPHICS_GRACE=<seconds>
        // (1...3600) shortens the closed HUD's graphics grace (ten minutes,
        // app/application.hpp closedGraphicsGraceSeconds), so the gate can
        // measure the closed HUD after its device was released without
        // waiting ten minutes. Honoured only with --data-root (an isolated
        // temporary root); every other run keeps the production grace.
        if (!isolated.empty()) {
            wchar_t text[32]{};
            const auto length = GetEnvironmentVariableW(L"ENDFIELDHUD_CLOSED_GRAPHICS_GRACE", text, 32);
            if (length) {
                wchar_t* end{};
                const double seconds = length < 32 ? std::wcstod(text, &end) : 0.0;
                if (length >= 32 || end != text + length || !std::isfinite(seconds) || seconds < 1 || seconds > 3600) {
                    report(invalidArguments);
                    return 1;
                }
                options.closedGraphicsGrace = seconds;
            }
        }
        options.reopenModule = reopen;
        // Opt-in timing trace (numbers only): ENDFIELDHUD_STARTUP_TRACE=1 writes
        // Diagnostics/startup-trace.json in the data root once both traced
        // openings completed (or at quit).
        if (wchar_t flag[8]{}; GetEnvironmentVariableW(L"ENDFIELDHUD_STARTUP_TRACE", flag, 8) == 1 && flag[0] == L'1')
            options.startupTrace = root->data / "Diagnostics" / "startup-trace.json";
        // Opt-in memory and thread census (numbers, owner names and system
        // module file names only): ENDFIELDHUD_MEMORY_TRACE=1 rewrites
        // Diagnostics/memory-trace.json after construction, the launch
        // prewarm, each of the first eight openings, closings and close
        // requests, an expired transition deadline and a quit; every row
        // carries the frame scheduling state.
        if (wchar_t flag[8]{}; GetEnvironmentVariableW(L"ENDFIELDHUD_MEMORY_TRACE", flag, 8) == 1 && flag[0] == L'1')
            options.memoryTrace = root->data / "Diagnostics" / "memory-trace.json";
        // Always: a disabled module owner leaves one bounded record
        // (app/module_failure_log.hpp) beside last-fatal.txt.
        options.moduleFailureLog = root->data / "Diagnostics" / "module-failures.txt";
        // Factory-added module areas resolve their own verified resources (the
        // locator outlives the Application and is used on its UI thread only).
        options.resources = [&locator](std::string_view id) -> fs::path { return locator.available(id) ? locator.resolve(id).path : fs::path{}; };
        // Areas join the module owner only with Notes (module selection, TSF).
        if (package.modules) app::appendRegisteredAreas(options.modules, false);
        int code{};
        std::optional<std::optional<core::Module>> recovered;
        {
            // Declared before the owner: the teardown stage covers its destructor.
            std::optional<app::FatalStage> stage(std::in_place, "application start");
            app::Application application(std::move(options));
            instance.setHandler([&application](std::vector<std::string> forwarded) { application.activate(forwarded); });
            stage.emplace("application run");
            code = application.run();
            instance.setHandler({});
            recovered = application.takeDeviceRecovery();
            stage.emplace("application teardown");
        }
        if (recovered) {
            // A lost GPU device: a fresh owner creates a new device and
            // reopens the HUD on its last module (no onboarding).
            const auto now = std::chrono::steady_clock::now();
            std::erase_if(recoveries, [&](const auto& at) { return now - at > std::chrono::minutes(1); });
            recoveries.push_back(now);
            if (recoveries.size() > 3) {
                report(displayFailed);
                return 1;
            }
            startup = {}; startup.login = true; reopen = *recovered;
            continue;
        }
        return code;
    }
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count{};
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> values;
    for (int n = 1; argv && n < count; ++n) values.push_back(narrow(argv[n]));
    if (argv) LocalFree(argv);
    try { return run(values); }
    catch (const app::SystemBackdropUnavailable& e) {
        std::fprintf(stderr, "EndfieldHUD failed: %s (HRESULT %08x)\n", e.what(), static_cast<unsigned>(e.result()));
        writeFatal("SystemBackdropUnavailable", e.what(), static_cast<std::uint32_t>(e.result()));
        report(windows11Required);
        return 1;
    } catch (const winrt::hresult_error& e) {
        std::fprintf(stderr, "EndfieldHUD failed: HRESULT %08x\n", static_cast<unsigned>(e.code().value));
        writeFatal("winrt::hresult_error", narrow(e.message()), static_cast<std::uint32_t>(e.code().value));
        report(displayFailed);
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "EndfieldHUD failed: %s\n", e.what());
        // RendererError and std::system_error carry their HRESULT/Win32 code in the message.
        writeFatal(typeid(e).name(), e.what(), std::nullopt);
        report(stoppedByError);
        return 1;
    }
}
