// Properties of Temporary File Shelf references (native/shelf_properties.hpp),
// read as the properties card reads them on the utility worker: synthetic
// files, folders, a PNG made with WIC and a PCM WAV, all in a NEW temporary
// directory. No user file or setting is touched.
#ifdef _WIN32
#include "native/shelf_properties.hpp"
#include "native/file_shelf_files.hpp"
#include "core/data/data_store.hpp"
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace gpu = endfield::native;
namespace data = ehud::data;
using Microsoft::WRL::ComPtr;
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
// A 3 x 2 PNG encoded by WIC.
void png(const std::filesystem::path& file) {
    ComPtr<IWICImagingFactory> factory; check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))), "WIC factory");
    ComPtr<IWICStream> stream; check(SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)), "PNG stream");
    ComPtr<IWICBitmapEncoder> encoder; check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) && SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)), "PNG encoder");
    ComPtr<IWICBitmapFrameEncode> frame; check(SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)), "PNG frame");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA; check(SUCCEEDED(frame->SetSize(3, 2)) && SUCCEEDED(frame->SetPixelFormat(&format)), "PNG size");
    std::array<BYTE, 3 * 2 * 4> pixels{}; pixels.fill(0x80);
    check(SUCCEEDED(frame->WritePixels(2, 3 * 4, UINT(pixels.size()), pixels.data())) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()), "PNG written");
}
// 1.5 s of 8 kHz, 8-bit mono PCM.
void wav(const std::filesystem::path& file) {
    constexpr std::uint32_t rate = 8000, samples = 12000;
    std::string bytes;
    const auto u32 = [&](std::uint32_t v) { for (unsigned n = 0; n < 4; ++n) bytes.push_back(char((v >> (8 * n)) & 0xFF)); };
    const auto u16 = [&](std::uint16_t v) { bytes.push_back(char(v & 0xFF)); bytes.push_back(char(v >> 8)); };
    bytes += "RIFF"; u32(36 + samples); bytes += "WAVEfmt "; u32(16); u16(1); u16(1); u32(rate); u32(rate); u16(1); u16(8);
    bytes += "data"; u32(samples); bytes.append(samples, '\x80');
    write(file, bytes);
}

void pure() {
    check(gpu::shelfDurationText(0) == "0:00" && gpu::shelfDurationText(70'000'000) == "0:07" && gpu::shelfDurationText(2'050'000'000) == "3:25" &&
          gpu::shelfDurationText(37'239'999'999) == "1:02:03", "Durations read m:ss or h:mm:ss, seconds rounded down");
    check(gpu::shelfGroupedCount(0) == "0" && gpu::shelfGroupedCount(999) == "999" && gpu::shelfGroupedCount(1000) == "1,000" && gpu::shelfGroupedCount(12345678) == "12,345,678", "Counts are grouped by three");
}

void native() {
    Temp temp; const gpu::NativeFileShelfFiles files;
    // A text file: its size and times; no dimensions or duration.
    const auto notes = temp.root / "Notes.txt"; write(notes, std::string(1234, 'x'));
    auto lease = files.acquire(utf8(notes));
    const auto text = gpu::readShelfProperties(lease);
    check(text.windowsPath == utf8(notes) && !text.directory && !text.symbolicLink && !text.cloudPlaceholder && !text.remote, "A local file is described as itself");
    check(text.byteCount == std::optional<std::int64_t>(1234) && !text.folderItems && !text.dimensions && !text.duration, "A text file has its size only");
    FILETIME now{}; GetSystemTimeAsFileTime(&now); const auto ticks = std::int64_t(ULARGE_INTEGER{{now.dwLowDateTime, now.dwHighDateTime}}.QuadPart);
    check(text.created && text.modified && *text.created > ticks - 36'000'000'000ll && *text.modified <= ticks + 36'000'000'000ll && *text.created <= *text.modified + 10'000'000,
          "Creation and modification times are the file's own");

    // Folders: direct children only, within the bound.
    const auto three = temp.root / "Three", empty = temp.root / "Empty";
    check(std::filesystem::create_directory(three) && std::filesystem::create_directory(three / "Inner") && std::filesystem::create_directory(empty), "Synthetic folders");
    write(three / "a.txt", "a"); write(three / "b.txt", "b"); write(three / "Inner" / "c.txt", "c");
    auto folderLease = files.acquire(utf8(three));
    const auto folder = gpu::readShelfProperties(folderLease);
    check(folder.directory && folder.folderItems == std::optional<std::uint64_t>(3) && !folder.byteCount && !folder.dimensions, "A folder counts its direct children only");
    gpu::ShelfPropertyOptions bounded; bounded.folderItemBound = 2;
    check(!gpu::readShelfProperties(folderLease, bounded).folderItems, "A folder larger than the bound shows no count");
    auto emptyLease = files.acquire(utf8(empty));
    check(gpu::readShelfProperties(emptyLease).folderItems == std::optional<std::uint64_t>(0), "An empty folder has no items");

    // The property system: image dimensions and media duration.
    const auto picture = temp.root / "Picture.png"; png(picture);
    auto pictureLease = files.acquire(utf8(picture));
    const auto image = gpu::readShelfProperties(pictureLease);
    check(image.dimensions && (*image.dimensions)[0] == 3 && (*image.dimensions)[1] == 2 && !image.duration, "A PNG reports its pixel dimensions");
    gpu::ShelfPropertyOptions noHandlers; noHandlers.propertyHandlers = false;
    check(!gpu::readShelfProperties(pictureLease, noHandlers).dimensions, "Without property handlers nothing beyond the file system is read");
    const auto tone = temp.root / "Tone.wav"; wav(tone);
    auto toneLease = files.acquire(utf8(tone));
    const auto audio = gpu::readShelfProperties(toneLease);
    check(audio.duration && *audio.duration >= 14'990'000 && *audio.duration <= 15'010'000 && !audio.dimensions, "A WAV reports its 1.5 s duration");

    // Identity: a lease for another object is refused, never described.
    auto other = files.acquire(utf8(notes));
    auto metadata = other.metadata(); metadata.identity.objectID[0] ^= 0xFF;
    const data::ShelfFileAccess forged(std::move(metadata), [] {});
    rejects([&] { (void)gpu::readShelfProperties(forged); }, "A changed identity is refused");
    rejects([&] { (void)gpu::readShelfProperties(data::ShelfFileAccess{}); }, "A closed lease is refused");
    // A worker thread without COM: the reader initializes it for the call.
    std::optional<gpu::ShelfProperties> fromWorker; std::thread worker([&] { try { fromWorker = gpu::readShelfProperties(pictureLease); } catch (...) {} }); worker.join();
    check(fromWorker && fromWorker->dimensions == image.dimensions, "A thread without COM reads the same properties");
}
} // namespace

int wmain() {
    try {
        check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "Test apartment");
        pure(); native();
        CoUninitialize();
        std::cout << "Shelf properties: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "Shelf properties failed after " << checks << " checks: " << e.what() << '\n'; return 1; }
}
#else
#include <iostream>
int main() { std::cout << "SKIP Shelf properties: requires Windows\n"; }
#endif
