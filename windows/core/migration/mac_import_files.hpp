#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ehud::migration::files {
// Filesystem primitives for the one-time import. Nothing here follows a
// symbolic link, junction or other reparse point; such entries are refused.
// New files are created exclusively and flushed before they are trusted.
void requireOrdinaryDirectory(const std::filesystem::path&);
// Every directory between `base` and `base/relative` must be ordinary.
void requireOrdinaryParents(const std::filesystem::path& base, const std::filesystem::path& relative);
bool ordinaryDirectory(const std::filesystem::path&);        // exists, real directory
std::optional<std::uint64_t> ordinaryFileSize(const std::filesystem::path&); // nullopt when absent; throws when not ordinary
// Streams `from` into a new file `to`, checking size and SHA-256 while copying.
void copyVerified(const std::filesystem::path& from, const std::filesystem::path& to,
                  std::uint64_t expectedBytes, std::string_view expectedSha256);
// Moves a file this import already verified into another folder of the same
// private work tree (no second copy of large databases or images).
void moveFile(const std::filesystem::path& from, const std::filesystem::path& to);
// Plain exclusive copy of an ordinary file (carrying existing Windows data).
void copyFile(const std::filesystem::path& from, const std::filesystem::path& to, std::uint64_t maximumBytes);
std::string readFile(const std::filesystem::path&, std::size_t maximumBytes); // throws when absent
std::string readPrefix(const std::filesystem::path&, std::size_t count);
void writeNewFile(const std::filesystem::path&, std::string_view bytes);
// Resumable, bounded copy (or verification) of one ordinary file, so that no
// single job on the shared utility worker streams an unbounded amount of data.
// The source is opened without following links and stays open (read-shared)
// between advance() calls; the target is created exclusively and flushed.
// With `expected`, the bytes are checked against the manifest size and SHA-256
// (a mismatch is MacImportErrorCode::corrupt); without it, more than
// `maximumBytes` is tooLarge. An empty `to` verifies without writing anything.
class StreamCopy final {
public:
    struct Expected { std::uint64_t bytes{}; std::string sha256; };
    StreamCopy(std::filesystem::path from, std::filesystem::path to, std::uint64_t maximumBytes, std::optional<Expected> expected = {});
    ~StreamCopy();                                // an unfinished target is removed
    StreamCopy(const StreamCopy&) = delete;
    StreamCopy& operator=(const StreamCopy&) = delete;
    std::uint64_t advance(std::uint64_t budget);  // bytes processed by this call (at most budget)
    bool done() const noexcept;
    std::uint64_t processed() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// One ordinary file of a planned tree copy.
struct PlannedFile { std::filesystem::path from, to; std::uint64_t bytes{}; };
// Lists every ordinary file below `from` in a stable order, refusing links and
// reparse points anywhere in the tree. When `to` is not empty, the matching
// directories (including empty ones) are created there. More than
// `maximumBytes` in total is tooLarge.
std::vector<PlannedFile> planTree(const std::filesystem::path& from, const std::filesystem::path& to, std::uint64_t maximumBytes);
// Same-volume rename that never replaces an existing target. Flushes on Windows.
void renameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to);
// Removes a tree this import created. Never descends into a link/reparse point.
bool removeTree(const std::filesystem::path&) noexcept;
}
