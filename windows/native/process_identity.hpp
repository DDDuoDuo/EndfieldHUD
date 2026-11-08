#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
// Process identity reads shared by the Activity catalog and the audio
// session backend: one process snapshot (PIDs, parents, thread counts), an
// open process's creation time and executable path, and the folder rule by
// which a helper executable belongs to the app that launched it.

// `image` lies inside the folder holding the executable `parent` (at any
// depth), below a drive's root folder. ASCII letters compare caselessly and
// other bytes exactly, so lower-cased keys (Activity) and paths as Windows
// reports them (audio) use the same rule.
bool executableInsideFolderOf(std::string_view image,std::string_view parent)noexcept;
#ifdef _WIN32
struct WindowsProcessEntry {std::uint32_t pid{},parent{},threads{};};
// Every process of one TH32CS_SNAPPROCESS snapshot, at most `limit` of them.
// false when the snapshot fails, holds more than `limit` or stops early; `out`
// keeps what was read.
bool readWindowsProcesses(std::vector<WindowsProcessEntry>&out,std::size_t limit=8192);
// Of a process handle opened with PROCESS_QUERY_LIMITED_INFORMATION: its
// creation time (FILETIME ticks, never 0) and its full Win32 image path.
std::optional<std::uint64_t>readWindowsProcessCreation(void*process)noexcept;
std::optional<std::wstring>readWindowsProcessPath(void*process);
#endif
}
