#pragma once
// Five-language text of EndfieldHUD-Setup.exe and uninstall.exe (portable).
// Shared terms follow the app catalog (core/localization_catalog.inc), e.g.
// "Launch at login". Placeholders are {0}, {1}, ... rendered once each by
// core::renderTranslation; every language uses exactly the English ones.
#include "core/localization.hpp"
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace endfield::packaging {
enum class SetupText : std::uint8_t {
    setupTitle, uninstallTitle,
    installHeading, installBody, upgradeHeading, upgradeBody, repairHeading, repairBody,
    launchAtLogin, installButton, updateButton, repairButton, continueButton, openButton, closeButton,
    runningHeading, runningInstallBody, runningUninstallBody,
    installingHeading, stepExtract, stepStop, stepRegister, stepFinish,
    installedHeading, updatedHeading, repairedHeading, installedBody,
    failedHeading, failedBody, refusedHeading, refusedNewer, refusedForeign, refusedIncomplete, refusedPath, refusedWindows,
    damagedPayload, busy, closeFailed, details,
    uninstallHeading, uninstallBody, deleteData, uninstallButton,
    confirmDataHeading, confirmDataBody, deleteDataButton, keepDataButton,
    uninstallingHeading, uninstalledHeading, dataKept, dataDeleted, uninstallFailedHeading, uninstallFailedBody,
    notInstalled, keptFiles,
    count
};
// The localized text for a resolved language (system resolves to English).
std::string_view setupText(SetupText, core::Language) noexcept;
// Rendered with its placeholders; throws std::invalid_argument when the
// argument count does not match.
std::string setupText(SetupText, core::Language, std::initializer_list<std::string_view> arguments);
// Number of {n} placeholders in the English text.
std::size_t setupTextArguments(SetupText) noexcept;
} // namespace endfield::packaging
