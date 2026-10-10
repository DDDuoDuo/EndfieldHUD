#pragma once
// EHUDZ01 payloads of the macOS Watch runtime packager
// (scripts/package-watch-resources.py compressed()/decoded(), read on the Mac
// by HUDSourceScene). Portable; the vendored zlib inflate only.
//
//   "EHUDZ01\0" | uint64 little-endian decoded length | raw DEFLATE (wbits -15)
//
// Bytes without the magic are identity resources and are returned unchanged.
// A packed resource must declare 1...128 MiB, inflate to exactly that many
// bytes, end its DEFLATE stream and leave no trailing input — the checks of
// the Mac packager's own decoded(). Anything else is rejected before more
// than the declared length (plus one detection byte) is allocated.
//
// Decision (WINDOWS-MIGRATION.md section 4): EndfieldHUD.exe ships only the
// compiled runtime catalog (resources/shell: catalog.ehscene and
// runtime-input), compiled from the unchanged Mac resources; no EHUDZ01
// payload is packaged or decoded at runtime. This decoder and the staged
// inventory verifier below prove Mac-staged WatchSource trees byte-exact
// (windows/tools/package_watch_resources.cpp) and give any future runtime
// reader one bounded implementation.
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace endfield::packaging {
inline constexpr std::array<std::uint8_t, 8> ehudz01Magic{'E', 'H', 'U', 'D', 'Z', '0', '1', '\0'};
inline constexpr std::uint64_t ehudz01MaximumBytes = 128ull * 1024ull * 1024ull;
class Ehudz01Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
bool ehudz01Encoded(std::span<const std::uint8_t>) noexcept;
std::vector<std::uint8_t> decodeEhudz01(std::span<const std::uint8_t>);

// package-watch-resources.py verify, natively: the staged folder holds exactly
// the files runtime-inventory.json names (plus runtime-selection.json for the
// desktop profile), every stored file matches its size and SHA-256, and every
// record decodes to exactly its source size and source SHA-256.
struct WatchInventoryReport {
    std::size_t files{}, encoded{}, derived{};
    std::uint64_t storedBytes{}, decodedBytes{};
};
WatchInventoryReport verifyWatchRuntimeInventory(const std::filesystem::path& staged);
} // namespace endfield::packaging
