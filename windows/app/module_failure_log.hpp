#pragma once
// Module failure diagnostics. An isolated owner that throws is disabled
// (ModuleRegistry, app/module_owner.hpp): its HUD modules stop presenting.
// The failure is reported once per owner, on stderr and OutputDebugString,
// and so is a module whose isolated production startup threw (Application),
// and production appends one bounded record per owner and run to
// <data root>/v1/Diagnostics/module-failures.txt (app/application.cpp),
// beside main.cpp's last-fatal.txt and through the same writer.
//
// Exception messages are never recorded whole: only their static prefix (up
// to the first ": ", where need() literals end and dynamic suffixes such as
// language tags, layer ids or file names begin), at most 160 bytes, with
// path-like words redacted and non-ASCII bytes replaced. Operations, stages
// and owner names are code literals; the host adds numbers only.
#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::app {
// Failure report delivered once per disabled owner. `what` is the exception's
// dynamic type name; `message` its sanitized static prefix (failureMessage);
// `stage` the FatalStage path recorded inside the failing call, or
// "<operation> [<owner>]" when none was.
struct ModuleFailure { std::string owner, what, operation, message, stage; };

// Exception text can carry file paths (std::filesystem_error, Windows API
// wrappers): words with a path separator are redacted, non-ASCII bytes and
// control characters replaced, and the result bounded to 512 bytes.
std::string redacted(std::string_view message);
// The static prefix of an exception message (up to the first ": "), cut to
// 160 bytes, then redacted.
std::string failureMessage(std::string_view message);
// A module whose isolated production startup threw (Application's start
// sites, each inside FatalStage("start", owner)): reported like a disabled
// owner, with operation "start". Called inside the catch.
ModuleFailure startFailure(std::string_view owner, const std::exception&);
// The stderr / OutputDebugString line: "EndfieldHUD module <owner> could not
// start (<type>)" or "... was disabled after a <type> during <operation>",
// then ": <message>" and " (<stage>)".
std::string failureLine(const ModuleFailure&);
// The record's budget lines (Application::recordModuleFailure): the shared
// raster cache's entries, the pixel bytes it holds against its budget
// (LayerRasterizer::maximumResourceBytes bounds resourceBytes less the pixels
// given back to GPU textures) and those given back; the renderer's resident
// bytes and its parked copies against Renderer::maximumParkedBytes. Numbers
// as "N of M": a slash would be redacted.
struct FailureBudgets {
    std::size_t rasterEntries{}, maximumRasterEntries{}, heldBytes{}, maximumHeldBytes{}, releasedBytes{};
    std::size_t rendererBytes{}, maximumRendererBytes{}, parkedBytes{}, maximumParkedBytes{};
};
std::string budgetLines(const FailureBudgets&);
// Appends `record` to the file at `path` (read and replaced with
// ehud::data::detail::readFile / replaceFile, as last-fatal.txt), dropping
// the oldest records (each starts with `header`) so the file stays within
// `maximum` bytes. A missing folder is created. Never throws: false when the
// record could not be written.
bool appendBoundedRecord(const std::filesystem::path& path, std::string_view record, std::string_view header, std::size_t maximum) noexcept;

// <data root>/v1/Diagnostics/module-failures.txt: one record per owner per
// run (UTC time, owner, exception type, message, operation, stage and the
// host's `context` lines), written synchronously on the failure path.
class ModuleFailureLog final {
public:
    static constexpr std::size_t maximumBytes = 16 * 1024;
    static constexpr std::string_view header = "EndfieldHUD module failure\n";
    explicit ModuleFailureLog(std::filesystem::path path) : path_(std::move(path)) {}
    const std::filesystem::path& path() const noexcept { return path_; }
    // `context`: "key: value\n" lines (numbers and code literals only).
    // False when this owner was already recorded in this run, or the write failed.
    bool record(const ModuleFailure&, std::string_view context = {}) noexcept;
private:
    std::filesystem::path path_;
    std::vector<std::string> owners_;
};
} // namespace endfield::app
