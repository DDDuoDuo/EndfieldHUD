#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
// Process identity reads shared by the Activity catalog and the audio
// session backend: one process snapshot (PIDs, parents, thread counts), an
// open process's creation time and executable path, and the folder rule by
// which a helper executable belongs to the app that launched it.

// `path` lies inside `folder` (at any depth; trailing separators on `folder`
// are ignored). ASCII letters compare caselessly and other bytes exactly, so
// lower-cased keys (Activity) and paths as Windows reports them (audio) use
// the same rule.
bool pathInsideFolder(std::string_view path,std::string_view folder)noexcept;
// `image` lies inside the folder holding the executable `parent` (at any
// depth), below a drive's root folder (pathInsideFolder).
bool executableInsideFolderOf(std::string_view image,std::string_view parent)noexcept;
// The file name of `path` is `name`, compared like pathInsideFolder.
bool executableNamed(std::string_view path,std::string_view name)noexcept;
// A process is an app in its own right, never another app's helper, when it
// owns an app window (readWindowsApplicationWindows) or has an
// AppUserModelID. Activity gives such a process its own card; the audio
// sessions never name it after a launcher.
constexpr bool processIsApplication(bool appWindow,std::string_view applicationID)noexcept{return appWindow||!applicationID.empty();}
#ifdef _WIN32
struct WindowsProcessEntry {std::uint32_t pid{},parent{},threads{};};
// Every process of one TH32CS_SNAPPROCESS snapshot, at most `limit` of them.
// false when the snapshot fails, holds more than `limit` or stops early; `out`
// keeps what was read.
bool readWindowsProcesses(std::vector<WindowsProcessEntry>&out,std::size_t limit=8192);
// The PIDs owning an app window: a top-level window with no owner that is not
// a tool window, of at most `limit` top-level windows. false when the
// enumeration fails or passes `limit`; `out` is then incomplete.
bool readWindowsApplicationWindows(std::set<std::uint32_t>&out,std::size_t limit=8192);
// Of a process handle opened with PROCESS_QUERY_LIMITED_INFORMATION: its
// creation time (FILETIME ticks, never 0) and its full Win32 image path.
std::optional<std::uint64_t>readWindowsProcessCreation(void*process)noexcept;
std::optional<std::wstring>readWindowsProcessPath(void*process);
#endif
}
