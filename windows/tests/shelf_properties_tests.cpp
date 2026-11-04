// Properties of File Shelf references (native/shelf_properties.hpp), read as
// the properties card reads them on the utility worker (FileShelfCanvas.swift
// ShelfFileProperties.read): synthetic files and folders in a NEW temporary
// directory. Dates of the item itself; a folder's direct children without
// hidden entries, within the 10,000 bound; nothing else (the Mac card shows no
// dimensions or duration). No user file or setting is touched.
#ifdef _WIN32
#include "native/shelf_properties.hpp"
#include "native/file_shelf_files.hpp"
#include "modules/file_shelf_canvas.hpp"
#include "core/data/data_store.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace gpu = endfield::native;
namespace data = ehud::data;
namespace {
unsigned checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
template <class F> void rejects(F f, const char* why) { bool rejected{}; try { f(); } catch (const std::exception&) { rejected = true; } check(rejected, why); }
std::string utf8(const std::filesystem::path& p) { const auto s = p.u8string(); return {reinterpret_cast<const char*>(s.data()), s.size()}; }
struct Temp {
    std::filesystem::path root = std::filesystem::canonical(std::filesystem::temp_directory_path()) / ("ehud-shelf-properties-" + data::makeUUID());
    Temp() { check(std::filesystem::create_directory(root), "Create owned test directory"); }
    ~Temp() { std::error_code e; std::filesystem::remove_all(root, e); }
};
void write(const std::filesystem::path& path, std::string_view bytes) { std::ofstream out(path, std::ios::binary); out.write(bytes.data(), std::streamsize(bytes.size())); check(bool(out), "Write synthetic file"); }

void native() {
    Temp temp; const gpu::NativeFileShelfFiles files;
    // A file: its size and times.
    const auto notes = temp.root / "Notes.txt"; write(notes, std::string(1234, 'x'));
    auto lease = files.acquire(utf8(notes));
    const auto text = gpu::readShelfProperties(lease);
    check(text.windowsPath == utf8(notes) && !text.directory && !text.symbolicLink && !text.cloudPlaceholder && !text.remote, "A local file is described as itself");
    check(text.byteCount == std::optional<std::int64_t>(1234) && !text.folderItems, "A file has its size and no item count");
    FILETIME now{}; GetSystemTimeAsFileTime(&now); const auto ticks = std::int64_t(ULARGE_INTEGER{{now.dwLowDateTime, now.dwHighDateTime}}.QuadPart);
    check(text.created && text.modified && *text.created > ticks - 36'000'000'000ll && *text.modified <= ticks + 36'000'000'000ll && *text.created <= *text.modified + 10'000'000,
          "Creation and modification times are the file's own");

    // Folders: direct children only, hidden ones left out (.skipsHiddenFiles), within the bound.
    const auto three = temp.root / "Three", empty = temp.root / "Empty";
    check(std::filesystem::create_directory(three) && std::filesystem::create_directory(three / "Inner") && std::filesystem::create_directory(empty), "Synthetic folders");
    write(three / "a.txt", "a"); write(three / "b.txt", "b"); write(three / "Inner" / "c.txt", "c"); write(three / "hidden.txt", "h");
    check(SetFileAttributesW((three / "hidden.txt").c_str(), FILE_ATTRIBUTE_HIDDEN) != FALSE, "Hide one child");
    auto folderLease = files.acquire(utf8(three));
    const auto folder = gpu::readShelfProperties(folderLease);
    check(folder.directory && folder.folderItems == std::optional<std::uint64_t>(3) && !folder.byteCount, "A folder counts its visible direct children only");
    gpu::ShelfPropertyOptions bounded; bounded.folderItemBound = 2;
    check(!gpu::readShelfProperties(folderLease, bounded).folderItems, "A folder larger than the bound shows no count");
    bounded.folderItemBound = 3;
    check(gpu::readShelfProperties(folderLease, bounded).folderItems == std::optional<std::uint64_t>(3), "A folder at the bound keeps its count");
    check(gpu::ShelfPropertyOptions{}.folderItemBound == endfield::modules::ShelfFileProperties::itemBound, "The bound is the Mac's 10,000");
    auto emptyLease = files.acquire(utf8(empty));
    check(gpu::readShelfProperties(emptyLease).folderItems == std::optional<std::uint64_t>(0), "An empty folder has no items");

    // Identity: a lease for another object is refused, never described.
    auto other = files.acquire(utf8(notes));
    auto metadata = other.metadata(); metadata.identity.objectID[0] ^= 0xFF;
    const data::ShelfFileAccess forged(std::move(metadata), [] {});
    rejects([&] { (void)gpu::readShelfProperties(forged); }, "A changed identity is refused");
    rejects([&] { (void)gpu::readShelfProperties(data::ShelfFileAccess{}); }, "A closed lease is refused");
    // Any worker thread reads the same (no COM is needed).
    std::optional<gpu::ShelfProperties> fromWorker; std::thread worker([&] { try { fromWorker = gpu::readShelfProperties(folderLease); } catch (...) {} }); worker.join();
    check(fromWorker && fromWorker->folderItems == folder.folderItems, "A worker thread reads the same properties");
}
} // namespace

int wmain() {
    try {
        native();
        std::cout << "Shelf properties: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "Shelf properties failed after " << checks << " checks: " << e.what() << '\n'; return 1; }
}
#else
#include <iostream>
int main() { std::cout << "SKIP Shelf properties: requires Windows\n"; }
#endif
