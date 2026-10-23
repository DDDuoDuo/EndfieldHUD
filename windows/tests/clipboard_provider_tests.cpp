// The Clipboard canvas's view of the shared history (native/clipboard_provider):
// ClipboardStore records (the 90-character preview and ClipboardDetails, Mac
// expression oracle tools/clipboard_provider_reference.swift ->
// tests/fixtures/clipboard-provider-preview.json), the store's operations,
// statusMessage, the copied-kind report and the thumbnails published for the
// canvas's owner. Synthetic history only; the Windows part reads attributes of
// files it creates in its own temporary folder.
#include "native/clipboard_provider.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <filesystem>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace n = endfield::native;
namespace m = endfield::modules;
using Json = ehud::data::Json;
namespace {
unsigned checks{};
void check(bool b, const char* message) { ++checks; if (!b) throw std::runtime_error(message); }
std::u16string utf16(std::string_view value) {
    check(Json::validUtf8(value), "Owned oracle contains valid UTF-8");
    std::u16string out;
    for (std::size_t at = 0; at < value.size();) {
        auto c = std::uint32_t(static_cast<unsigned char>(value[at++]));
        if (c >= 128) { const auto width = c < 0xe0 ? 2u : c < 0xf0 ? 3u : 4u; c &= (1u << (7 - width)) - 1; for (unsigned k = 1; k < width; ++k) c = (c << 6) | (static_cast<unsigned char>(value[at++]) & 63); }
        if (c < 65536) out.push_back(char16_t(c)); else { c -= 65536; out.push_back(char16_t(0xd800 + (c >> 10))); out.push_back(char16_t(0xdc00 + (c & 1023))); }
    }
    return out;
}
struct Fixture {
    n::ClipboardHistory history;
    unsigned reads{}, copies{}, pins{}, removes{}, clears{};
    std::uint64_t copied{}; bool failCopy{}; std::optional<std::string> status;
    explicit Fixture(std::size_t capacity = 10) : history(capacity) {}
    n::ClipboardProviderSource source() {
        return {[this]() -> const n::ClipboardHistory& { ++reads; return history; },
            [this](auto id) { ++copies; copied = id; return !failCopy; },
            [this](auto id) { ++removes; return history.erase(id); },
            [this](auto id, bool pinned) { ++pins; return history.pin(id, pinned); },
            [this] { ++clears; const auto count = history.items().size(); history.clear(true); return count != history.items().size(); },
            [this] { return status; }, {}};
    }
    std::uint64_t text(std::u16string value, n::ClipboardKind kind = n::ClipboardKind::text, n::ClipboardOrigin origin = {}) {
        n::ClipboardPayload p; p.kind = kind; p.text = std::move(value);
        check(history.ingest(std::move(p), false, std::move(origin)) == n::ClipboardInsertResult::inserted, "Owned text accepted once");
        return history.items().front().id;
    }
    // A validated 1x1 DIB with its decoded thumbnail, as the capture stores it.
    std::uint64_t image(unsigned value, unsigned width = 1, unsigned height = 1) {
        auto bytes = std::make_shared<std::vector<std::uint8_t>>(44);
        auto put = [&](unsigned at, std::uint32_t v) { for (unsigned k = 0; k < 4; ++k) (*bytes)[at + k] = std::uint8_t(v >> (8 * k)); };
        put(0, 40); put(4, 1); put(8, 1); (*bytes)[12] = 1; (*bytes)[14] = 32; (*bytes)[40] = std::uint8_t(value); (*bytes)[43] = 255;
        auto t = std::make_shared<n::ClipboardImageThumbnail>();
        t->encoded = {n::ClipboardEncodedFormat::dib, bytes}; t->sourceWidth = width; t->sourceHeight = height; t->width = t->height = 1;
        t->rgba = {std::uint8_t(value), 30, 20, 255};
        n::ClipboardPayload p; p.kind = n::ClipboardKind::image; p.image_format = n::ClipboardImageFormat::dib; p.decodedImage = std::move(t);
        check(history.ingest(std::move(p)) == n::ClipboardInsertResult::inserted, "Injected validated image accepted");
        return history.items().front().id;
    }
};
const m::ClipboardRecord* record(const m::ClipboardSnapshot& snapshot, std::uint64_t id) {
    const auto found = std::find_if(snapshot.items.begin(), snapshot.items.end(), [&](const auto& r) { return r.id == id; });
    return found == snapshot.items.end() ? nullptr : &*found;
}
// ClipboardStore.read / make as the canvas reads them, and the store's operations.
void records(const char* oracle) {
    Fixture f(64);
    const auto first = f.text(u"  hello\tworld \r\n", n::ClipboardKind::text, {1790000000.5, u"Notepad", {}});
    n::NativeClipboardProvider provider(f.source());
    check(f.reads == 0, "Constructing the provider never reads the history");
    auto actions = provider.actions();
    auto snapshot = actions.snapshot();
    check(snapshot.capacity == 64 && snapshot.items.size() == 1, "The snapshot carries the history capacity");
    const auto& text = snapshot.items[0];
    check(text.id == first && text.kind == m::ClipboardRecordKind::text && text.preview == "hello world" && !text.pinned && !text.thumbnail,
        "A text record carries the flattened preview");
    check(text.createdAt == 1790000000.5 && text.details.source == "Notepad" && text.details.characters == 15 && !text.details.format && !text.details.fileCount,
        "A text record carries its capture time, source app and character count");
    for (unsigned k = 0; k < 10; ++k) actions.snapshot();
    check(provider.stats().previewBuilds == 1 && provider.stats().snapshots == 11, "Unchanged history reuses the built record");
    // Store operations route to the same history item.
    check(actions.copy(first) && f.copies == 1 && f.copied == first, "Copy targets the shared history item");
    f.failCopy = true; check(!actions.copy(first) && f.copies == 2, "A failed copy reports failure"); f.failCopy = false;
    check(actions.togglePin(first) && f.pins == 1 && f.history.find(first)->pinned && actions.snapshot().items[0].pinned, "Pin toggles the history item and the next snapshot");
    check(actions.togglePin(first) && !f.history.find(first)->pinned && !actions.snapshot().items[0].pinned && provider.stats().previewBuilds == 1,
        "Unpinning flips the cached record without rebuilding it");
    f.history.pin(first, true);
    // Files: the first three names and the remaining count; size only from the capture.
    n::ClipboardPayload files; files.kind = n::ClipboardKind::files; files.files = {u"C:\\one\\first.txt", u"D:\\two\\한국어.png", u"C:/owned/third/", u"C:\\four"};
    check(f.history.ingest(std::move(files), false, {5, {}, 4096}) == n::ClipboardInsertResult::inserted, "Owned file metadata accepted without file access");
    snapshot = actions.snapshot();
    const auto& listed = snapshot.items[0];
    check(listed.kind == m::ClipboardRecordKind::files && listed.preview == "first.txt, 한국어.png, third (+1)", "The first three file names and the remaining count");
    check(listed.details.fileCount == 4 && listed.details.byteCount == 4096 && !listed.details.source && !listed.details.characters, "File details carry the count and the captured size");
    n::ClipboardPayload unsized; unsized.kind = n::ClipboardKind::files; unsized.files = {u"C:\\folder\\"};
    check(f.history.ingest(std::move(unsized)) == n::ClipboardInsertResult::inserted && !actions.snapshot().items[0].details.byteCount, "Files without a captured size have none");
    check(actions.clearUnpinned() && f.clears == 1 && f.history.items().size() == 1 && f.history.items()[0].pinned, "Clear keeps the pinned item");
    f.status = "injected capture status";
    check(actions.snapshot().status == f.status, "The capture status reaches the snapshot"); f.status.reset();
    f.history.pin(first, false);
    check(actions.remove(first) && f.removes == 1 && f.history.items().empty() && actions.snapshot().items.empty(), "Delete routes to the history");
    check(!actions.remove(first) && f.removes == 1 && !actions.togglePin(first) && !actions.copy(first), "Unknown items are neither removed, pinned nor copied");
    check(provider.stats().metadataRows == 0, "Removed items leave no cached record");
    // The Mac expression oracle: grapheme/whitespace compaction and String.count.
    std::ifstream file(oracle, std::ios::binary); check(bool(file), "Open the isolated ClipboardStore preview oracle");
    const std::string bytes((std::istreambuf_iterator<char>(file)), {});
    const auto document = Json::parse(bytes, 1024 * 1024);
    check(document["sourceSHA256"].string() == "c871a86bb4b4268d7529fc685b28a1a4867fec55ce6fed5070834fb2c147d90a", "The oracle pins the Mac 1.3.0 ClipboardStore");
    unsigned compared{};
    for (const auto& row : document["rows"].array()) {
        f.history.clear(false);
        const auto input = utf16(row["input"].string());
        if (input.empty()) continue; // the Mac never records an empty string
        f.text(input);
        const auto built = actions.snapshot().items.front();
        check(built.preview == row["preview"].string(), "Grapheme/whitespace compaction matches ClipboardStore.make");
        check(built.details.characters == std::int64_t(row["characters"].number()), "The character count matches String.count");
        ++compared;
    }
    check(compared == 15, "Every non-empty oracle row compared");
    provider.close();
    check(!actions.copy(first) && !actions.togglePin(first) && !actions.remove(first) && !actions.clearUnpinned() && actions.snapshot().items.empty(),
        "Callbacks of a closed provider cannot touch the borrowed history");
    m::ClipboardActions expired;
    { n::NativeClipboardProvider temporary(f.source()); expired = temporary.actions(); }
    const auto reads = f.reads;
    check(expired.snapshot().items.empty() && !expired.copy(1) && !expired.togglePin(1) && !expired.clearUnpinned() && f.reads == reads,
        "Copied callbacks stay harmless after the provider is destroyed");
}
// SystemHUDView clipboardCanvas.onCopy: kind read before the copy, recorded
// only after it succeeds; the owner turns it into Event Log clipboardCopied.
void copiedEvents() {
    Fixture f; const auto link = f.text(u"https://example.com/a", n::ClipboardKind::url); const auto plain = f.text(u"synthetic body");
    std::vector<n::ClipboardKind> kinds; auto source = f.source(); source.copied = [&](n::ClipboardKind kind) { kinds.push_back(kind); };
    n::NativeClipboardProvider provider(std::move(source)); auto actions = provider.actions();
    check(actions.copy(link) && kinds.size() == 1 && kinds[0] == n::ClipboardKind::url, "A successful copy reports the copied item's kind once");
    f.failCopy = true; check(!actions.copy(plain) && kinds.size() == 1 && f.copies == 2, "A failed copy records nothing"); f.failCopy = false;
    check(!actions.copy(999999) && kinds.size() == 1 && f.copies == 2, "An unknown item is neither copied nor recorded");
    check(actions.copy(plain) && kinds.size() == 2 && kinds[1] == n::ClipboardKind::text, "Each successful copy reports again");
    check(n::clipboardEventKind(n::ClipboardKind::text) == "text" && n::clipboardEventKind(n::ClipboardKind::url) == "url" && n::clipboardEventKind(n::ClipboardKind::image) == "image" && n::clipboardEventKind(n::ClipboardKind::files) == "files",
        "Event kinds are the source ClipboardKind raw values");
    provider.close(); check(!actions.copy(plain) && kinds.size() == 2, "A closed provider records nothing");
}
// ClipboardStore link rows: the preview is the ORIGINAL string (an explicit
// URL type may come with any text), the kind is url, no character count.
void links() {
    Fixture f; n::ClipboardPayload p; p.kind = n::ClipboardKind::url; p.text = u"  Open the\tEndfield app "; p.url = u"x-endfield:open";
    check(f.history.ingest(std::move(p)) == n::ClipboardInsertResult::inserted, "An explicit link with its original string is accepted");
    n::NativeClipboardProvider provider(f.source()); auto actions = provider.actions();
    const auto snapshot = actions.snapshot();
    check(snapshot.items.size() == 1 && snapshot.items[0].kind == m::ClipboardRecordKind::url && snapshot.items[0].preview == "Open the Endfield app" && !snapshot.items[0].details.characters,
        "A link record previews its compacted original string, not the URL");
    const auto derived = f.text(u"https://example.com/路径", n::ClipboardKind::url);
    check(f.history.find(derived)->payload.url == u"https://example.com/%E8%B7%AF%E5%BE%84", "A synthetic link string gets its URL from the Mac string heuristic");
    provider.close();
}
// ClipboardStore.statusMessage: CaptureError and pinned-cache messages in the
// five languages; no status while capturing works or an image validates.
void statuses() {
    namespace s = n::clipboard_status; using L = endfield::core::Language;
    check(!n::clipboardStatusText(0, L::english) && !n::clipboardStatusText(1, L::english) && !n::clipboardStatusText(s::pending, L::korean), "Success and a pending image validation show no status");
    check(n::clipboardStatusText(s::cacheFull, L::english) == "The cache is full of pinned items. Unpin or remove an item to capture more.", "A cache full of pinned items uses the source message");
    check(n::clipboardStatusText(s::textTooLarge, L::simplifiedChinese) == "已跳过超过 1 MB 的文本。", "Oversized text uses the source message");
    check(n::clipboardStatusText(s::imageTooLarge, L::japanese) == "64 MBを超える画像はスキップしました。", "An image over 64 MB uses the catalog translation");
    check(n::clipboardStatusText(s::imageDimensionsTooLarge, L::korean) == "1억 픽셀을 초과하는 이미지를 건너뛰었습니다.", "An image over 100 megapixels uses the catalog translation");
    check(n::clipboardStatusText(s::tooManyFiles, L::traditionalChinese) == "此檔案選擇過大，無法快取。", "A file selection too large to cache uses the catalog translation");
    const auto busy = std::int32_t(0x80070005u);
    check(n::clipboardStatusText(busy, L::english) == "Clipboard unavailable" && n::clipboardStatusText(busy, L::simplifiedChinese) == "剪贴板不可用" && n::clipboardStatusText(busy, L::korean) == "클립보드를 사용할 수 없습니다",
        "Other failures keep the Windows-only notice in every language");
}
// Image records: the 96 px thumbnail published once per record at {key, 1}
// while the record lives (the Mac keeps ClipboardItem.thumbnail with it).
void thumbnails() {
    Fixture f; std::vector<std::uint64_t> ids; for (unsigned k = 0; k < 10; ++k) ids.push_back(f.image(k, 640 + k, 480));
    n::NativeClipboardProvider provider(f.source()); auto actions = provider.actions();
    check(provider.stats().images.cachedImages == 0, "Constructing the provider prepares nothing");
    const auto snapshot = actions.snapshot();
    check(snapshot.items.size() == 10 && provider.stats().images.cachedImages == 10 && provider.stats().imagePreparations == 10, "Each image record publishes its thumbnail once");
    const auto* last = record(snapshot, ids[0]);
    check(last && last->kind == m::ClipboardRecordKind::image && last->preview.empty() && last->thumbnail && last->thumbnail->kind == m::design::ImageSource::Kind::memory && last->thumbnail->revision == 1,
        "An image record names its memory thumbnail");
    check(last->details.format == "BMP" && last->details.pixelWidth == 640 && last->details.pixelHeight == 480 && !last->details.characters, "A DIB reports BMP and its source pixel size");
    auto borrowed = provider.images().acquire(last->thumbnail->key, 1);
    check(borrowed && borrowed->premultipliedBGRA().size() == 4 && borrowed->premultipliedBGRA()[0] == 20 && borrowed->premultipliedBGRA()[2] == 0, "The thumbnail is the capture's RGBA as premultiplied BGRA");
    const auto encoded = f.history.find(ids[0])->payload.decodedImage->encoded.bytes;
    for (unsigned k = 0; k < 5; ++k) actions.snapshot();
    check(provider.stats().imagePreparations == 10 && provider.stats().previewBuilds == 10 && f.history.find(ids[0])->payload.decodedImage->encoded.bytes == encoded,
        "Later snapshots neither republish nor copy the encoded image");
    const auto key = last->thumbnail->key;
    f.history.erase(ids[0]); actions.snapshot();
    check(!provider.images().acquire(key, 1) && provider.stats().images.cachedImages == 9 && borrowed->premultipliedBGRA()[0] == 20,
        "A removed record retires its thumbnail; a borrowed snapshot stays valid");
    provider.clearImages(); check(provider.stats().images.cachedImages == 0, "Explicit cleanup drops the cached thumbnails");
    provider.close(); check(actions.snapshot().items.empty() && provider.stats().images.cachedImages == 0, "Close disconnects the history");
}
#ifdef _WIN32
// ClipboardStore.totalSize: up to 64 regular files, one attribute read each.
void origin() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("ehud-clipboard-origin-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(root); fs::create_directories(root / "folder");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); } } cleanup{root};
    const auto write = [&](const char* name, std::size_t size) { std::ofstream(root / name, std::ios::binary) << std::string(size, 'x'); return (root / name).u16string(); };
    n::ClipboardPayload files; files.kind = n::ClipboardKind::files; files.files = {write("a.bin", 1000), write("b.bin", 2500)};
    const auto sized = n::windowsClipboardOrigin(files);
    check(sized.fileBytes == 3500u && sized.created == 0, "Regular files report their total size; the history stamps the time");
    check(!sized.source || (!sized.source->empty() && *sized.source != u"clipboard_provider_tests"), "The source app is never this process");
    files.files.push_back((root / "folder").u16string());
    check(!n::windowsClipboardOrigin(files).fileBytes, "A folder in the selection leaves the size unknown");
    files.files.assign(65, (root / "a.bin").u16string());
    check(!n::windowsClipboardOrigin(files).fileBytes, "More than 64 files leave the size unknown");
    files.files = {(root / "missing.bin").u16string()};
    check(!n::windowsClipboardOrigin(files).fileBytes, "A missing file leaves the size unknown");
    n::ClipboardPayload text; text.text = u"synthetic";
    check(!n::windowsClipboardOrigin(text).fileBytes, "Text has no file size");
}
#endif
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass the ClipboardStore preview oracle (clipboard-provider-preview.json)");
        records(argv[1]); copiedEvents(); links(); statuses(); thumbnails();
#ifdef _WIN32
        origin();
#endif
        std::cout << "PASS " << checks << " Clipboard provider checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
