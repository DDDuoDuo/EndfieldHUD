// Owner-level File Shelf services: the production tools::ShelfPreview on a
// NEW temporary data root with synthetic files, a hidden owner window, a
// hidden preview popup and injected OLE data objects. Covers the Event Log
// records SystemHUDView wires to the canvas (for imports, drops, removal and
// clearing), stale-path relocation saved by refresh, Quick Look's generic
// preview, refused virtual-file drops, localized platform text and error
// descriptions. No user file, clipboard, Explorer window or preview handler of
// the user is touched; the real clipboard path (ShelfPreview::paste) is never
// called here.
#ifdef _WIN32
#include "tools/shelf_preview.hpp"
#include "native/file_shelf_files.hpp"
#include "native/module_scene.hpp"
#include "core/data/file_io.hpp"
#include <ole2.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

namespace gpu = endfield::native;
namespace core = endfield::core;
namespace app = endfield::app;
namespace mod = endfield::modules;
namespace data = ehud::data;
namespace tools = endfield::tools;
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
void ok(HRESULT value, const char* why) { check(SUCCEEDED(value), why); }
std::string utf8(const std::filesystem::path& p) { const auto s = p.u8string(); return {reinterpret_cast<const char*>(s.data()), s.size()}; }
struct Ole { Ole() { ok(OleInitialize(nullptr), "Owned OLE STA"); } ~Ole() { OleUninitialize(); } };
struct Window {
    HWND hwnd{};
    Window() {
        WNDCLASSW c{}; c.lpfnWndProc = DefWindowProcW; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"EndfieldShelfServicesFixture";
        check(RegisterClassW(&c) != 0, "Register fixture class");
        hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, c.lpszClassName, L"Hidden Shelf services fixture", WS_POPUP, 0, 0, 1280, 800, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd && !IsWindowVisible(hwnd), "Fixture remains hidden");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"EndfieldShelfServicesFixture", GetModuleHandleW(nullptr)); }
};
struct Temp {
    std::filesystem::path root = std::filesystem::canonical(std::filesystem::temp_directory_path()) / ("ehud-shelf-services-" + data::makeUUID());
    Temp() { check(std::filesystem::create_directory(root), "Create owned test directory"); }
    ~Temp() { std::error_code e; std::filesystem::remove_all(root, e); }
};
void write(const std::filesystem::path& path, std::string_view bytes) { std::ofstream out(path, std::ios::binary); out.write(bytes.data(), std::streamsize(bytes.size())); check(bool(out), "Write synthetic file"); }
// CF_HDROP or virtual-file-only synthetic sources; each answers only its format.
struct Source final : IDataObject {
    std::atomic<ULONG> refs{1}; std::vector<std::uint8_t> paths; bool virtualOnly{};
    CLIPFORMAT descriptor = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"FileGroupDescriptorW"));
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override { if (!out) return E_POINTER; *out = nullptr; if (id != IID_IUnknown && id != IID_IDataObject) return E_NOINTERFACE; *out = static_cast<IDataObject*>(this); AddRef(); return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f) override {
        if (!f || !(f->tymed & TYMED_HGLOBAL)) return DV_E_FORMATETC;
        return (virtualOnly ? f->cfFormat == descriptor : f->cfFormat == CF_HDROP) ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f, STGMEDIUM* out) override {
        if (!out) return E_POINTER; *out = {};
        if (virtualOnly || !f || f->cfFormat != CF_HDROP) return DV_E_FORMATETC;
        auto h = GlobalAlloc(GMEM_MOVEABLE, paths.size()); if (!h) return E_OUTOFMEMORY;
        auto* p = GlobalLock(h); std::memcpy(p, paths.data(), paths.size()); GlobalUnlock(h);
        out->tymed = TYMED_HGLOBAL; out->hGlobal = h; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};
struct Recorded { mod::EventKind kind; mod::EventMetadata metadata; };
bool event(const std::vector<Recorded>& events, std::size_t index, mod::EventKind kind, std::string_view key, std::string_view value) {
    if (index >= events.size() || events[index].kind != kind) return false;
    const auto found = events[index].metadata.find(key);
    return found != events[index].metadata.end() && found->second == value;
}

void text() {
    using T = tools::ShelfText; using L = core::Language;
    check(tools::shelfText(T::revealPrefix, L::english) == "Reveal in File Explorer: " && tools::shelfText(T::revealPrefix, L::simplifiedChinese) == "在文件资源管理器中显示：" &&
          tools::shelfText(T::revealPrefix, L::traditionalChinese) == "在檔案總管中顯示：" && tools::shelfText(T::revealPrefix, L::japanese) == "エクスプローラーで表示：" &&
          tools::shelfText(T::revealPrefix, L::korean) == "파일 탐색기에서 보기: ", "Reveal names File Explorer in every source language");
    check(tools::shelfText(T::previewPrefix, L::english) == "Preview: " && tools::shelfText(T::previewPrefix, L::korean) == "미리 보기: ", "Preview replaces the Mac Quick Look name");
    check(tools::shelfText(T::revealFailed, L::english) == "Unable to show the file in File Explorer" && tools::shelfText(T::revealFailed, L::simplifiedChinese) == "无法在文件资源管理器中显示文件",
          "The reveal failure keeps the host's English and Chinese wording");
    for (auto language : {L::english, L::simplifiedChinese, L::traditionalChinese, L::japanese, L::korean})
        for (unsigned n = 0; n <= unsigned(T::revealFailed); ++n) check(!tools::shelfText(T(n), language).empty() && data::Json::validUtf8(tools::shelfText(T(n), language)), "Every Windows-only Shelf text exists in every language");
    using F = mod::FileShelfState::Failure;
    const auto error = [](data::StoreErrorCode code, const char* message, F failure, L language) { return tools::shelfErrorText(data::StoreError(code, message), failure, language); };
    check(error(data::StoreErrorCode::unavailable, "Item is no longer on the shelf", F::action, L::english) == "This item is no longer on the shelf.", "Missing items use the source text");
    check(error(data::StoreErrorCode::unavailable, "Original shelf item is unavailable; a different item occupies its former location", F::action, L::simplifiedChinese) == "原始项目不可用，其他项目已占用了原来的位置。",
          "Replaced originals use the source text");
    check(error(data::StoreErrorCode::unavailable, "Cannot acquire shelf file metadata (Windows error 5)", F::importing, L::english) == "Choose a file or folder that is available on this PC.", "Import failures ask for an available item");
    check(error(data::StoreErrorCode::unavailable, "Cannot acquire shelf file metadata (Windows error 2)", F::action, L::english) == "This item is unavailable: the original may have been deleted, or it is not accessible.",
          "Access failures explain the unavailable item");
    check(error(data::StoreErrorCode::changedOnDisk, "Saved data changed in another instance", F::changing, L::japanese) == tools::shelfText(T::changedOnDisk, L::japanese), "External changes use the source wording");
    check(error(data::StoreErrorCode::tooLarge, "Shelf metadata exceeds the explicit archive byte bound", F::importing, L::korean) == tools::shelfText(T::tooManyReferences, L::korean), "An oversized archive is explained");
    check(tools::shelfErrorText(std::runtime_error("disk full"), F::changing, L::english) == "The file shelf could not be saved or opened: disk full", "Other persistence failures keep the source prefix");
    check(tools::shelfKnownMessage("Drop source did not provide filesystem paths", L::english) == std::optional<std::string>(""), "A clipboard without files stays silent, as on the Mac");
    check(!tools::shelfKnownMessage("Unable to show the file in File Explorer", L::english), "Host messages pass through unchanged");
}

void owner(HWND hwnd, const gpu::NativeShelfAssets& assets) {
    Temp temp; gpu::LayerRasterizer raster; std::vector<Recorded> events;
    tools::ShelfPreviewOptions options{temp.root / "metadata", false, true, {}};
    options.recordEvent = [&](mod::EventKind kind, mod::EventMetadata metadata) { events.push_back({kind, std::move(metadata)}); };
    options.visiblePanels = false;
    auto shelf = std::make_unique<tools::ShelfPreview>(hwnd, raster, assets, std::move(options));
    shelf->resize({1280, 800, 96, 1, 1280, 800}); shelf->setLanguage(core::Language::english);
    double time{};
    const auto pump = [&] { MSG m{}; for (unsigned n = 0; n < 32 && PeekMessageW(&m, hwnd, WM_APP + 185, WM_APP + 189, PM_REMOVE); ++n) shelf->message({hwnd, m.message, m.wParam, m.lParam}, ++time); };
    const auto item = [&](std::string_view name) -> const mod::FileShelfItem* {
        const auto items = shelf->state().items(); const auto found = std::find_if(items.begin(), items.end(), [&](const auto& i) { return i.name == name; });
        return found == items.end() ? nullptr : &*found;
    };
    check(shelf->state().strings().revealPrefix == "Reveal in File Explorer: " && shelf->state().strings().previewPrefix == "Preview: ", "Card actions use the Windows names");

    // Import: one shelfAdded per new item, none for duplicates.
    const auto report = temp.root / "Report.txt", folder = temp.root / "Folder";
    write(report, "synthetic report"); std::filesystem::create_directory(folder);
    const std::vector<std::string> paths{utf8(report), utf8(folder)};
    check(shelf->importFiles(paths, ++time) && events.size() == 2 && event(events, 0, mod::EventKind::shelfAdded, "filename", "Report.txt") &&
          event(events, 1, mod::EventKind::shelfAdded, "filename", "Folder"), "Picker/paste imports record shelfAdded per new item");
    check(shelf->importFiles(paths, ++time) && events.size() == 2, "Duplicates record nothing");

    // Drop route: the same records after the deferred owner notice.
    core::ModulePresentation modules(core::Module::fileShelf);
    core::source::DesktopChromeSettings settings{{0, 0, 1280, 800}, 1, {}, core::Module::fileShelf, true};
    const auto center = core::source::DesktopChromeLayout::make(settings, {}, {}).designToScreen;
    const auto sample = modules.sample(++time).presentation;
    shelf->update(center, settings, sample, 1, time);
    gpu::LayerScene geometry(raster);
    geometry.load(data::Json::Object{{"bounds", data::Json::Array{0, 0, 400, 334}}, {"position", data::Json::Array{0, 0}}, {"anchorPoint", data::Json::Array{0, 0}}, {"children", data::Json::Array{}}}, {});
    gpu::NativeModuleSurface placement(geometry, core::Module::fileShelf); placement.update(center, settings, sample.current);
    const auto projected = core::Projection::viewport(gpu::layerViewportProjection(1280, 800) * placement.pose().contentWorld, 1280, 800).project({80, 80});
    check(projected.has_value(), "A Shelf point projects");
    POINT screen{LONG(std::lround(projected->x)), LONG(std::lround(projected->y))}; check(ClientToScreen(hwnd, &screen) != FALSE, "Client to screen");
    ComPtr<IDropTarget> target = shelf->fileDropTarget(); check(target != nullptr, "The owner's drop target exists");
    const auto dropped = temp.root / "Dropped photo.png"; write(dropped, "synthetic png bytes");
    ComPtr<Source> source; source.Attach(new Source); source->paths = gpu::encodeShelfDropPaths(std::vector<std::string>{utf8(dropped)});
    DWORD effect = DROPEFFECT_COPY; ok(target->DragEnter(source.Get(), 0, {screen.x, screen.y}, &effect), "Files enter the Shelf");
    check(effect == DROPEFFECT_COPY, "Files over the projected Shelf are accepted"); pump();
    check(shelf->state().dropTarget(), "The Shelf shows its drop highlight");
    effect = DROPEFFECT_COPY; ok(target->Drop(source.Get(), 0, {screen.x, screen.y}, &effect), "Files drop on the Shelf");
    check(effect == DROPEFFECT_COPY, "The drop commits a copy-only reference"); pump();
    check(item("Dropped photo.png") && events.size() == 3 && event(events, 2, mod::EventKind::shelfAdded, "filename", "Dropped photo.png"),
          "A dropped file records shelfAdded like the Mac canvas import");
    check(shelf->state().selectedIDs().size() == 1 && shelf->state().selectedIDs().contains(item("Dropped photo.png")->id), "The dropped reference is revealed and selected");

    // Virtual files over the Shelf: refused with the localized explanation.
    ComPtr<Source> attachment; attachment.Attach(new Source); attachment->virtualOnly = true;
    effect = DROPEFFECT_COPY; ok(target->DragEnter(attachment.Get(), 0, {screen.x, screen.y}, &effect), "A virtual-file drag enters");
    check(effect == DROPEFFECT_NONE, "Virtual files are refused"); pump();
    check(shelf->state().error() == std::optional<std::string>("This item isn't a file on disk. Save it to a folder first, then add it."), "The refusal is explained, not silent");
    ok(target->DragLeave(), "The virtual drag leaves"); pump();
    check(item("Dropped photo.png") && shelf->state().items().size() == 3, "No reference was added for virtual files");

    // Stale paths: rename and move are found again and saved; deletion is unavailable.
    const auto renamed = temp.root / "Renamed report.txt", movedFolder = temp.root / "Moved", moved = movedFolder / "Folder";
    std::filesystem::rename(report, renamed); std::filesystem::create_directory(movedFolder); std::filesystem::rename(folder, moved);
    shelf->refresh(++time);
    check(item("Renamed report.txt") && item("Renamed report.txt")->lastKnownPath == utf8(renamed) && !item("Renamed report.txt")->availabilityError, "A renamed original is followed by identity");
    check(item("Folder") && item("Folder")->lastKnownPath == utf8(moved) && !item("Folder")->availabilityError, "A moved folder is followed by identity");
    {
        gpu::NativeFileShelfFiles files; data::FileShelfStore saved(temp.root / "metadata", files.platform());
        check(std::any_of(saved.items().begin(), saved.items().end(), [&](const auto& r) { return r.windowsPath == utf8(renamed); }), "The renewed path is saved");
    }

    // Generic preview of a folder through the owner (hidden popup).
    shelf->requestPreview(item("Folder")->id); pump();
    auto panels = shelf->panels();
    check(panels.previewActive && panels.previewFallback && !panels.previewVisible && panels.previewFallbacks == 1, "A folder opens the generic preview");
    shelf->requestPreview(item("Folder")->id); pump();
    check(!shelf->panels().previewActive, "Requesting the same item again toggles the preview closed (Space)");

    // Preview of a deleted original: a localized error, never an exception.
    std::filesystem::remove(renamed); const auto deletedID = item("Renamed report.txt")->id;
    shelf->requestPreview(deletedID); pump();
    check(shelf->state().error() == std::optional<std::string>("This item is unavailable: the original may have been deleted, or it is not accessible.") && !shelf->panels().previewActive,
          "An unavailable original reports the localized reason");
    shelf->showError("Drop source did not provide filesystem paths", ++time);
    check(shelf->state().error() == std::optional<std::string>("This item is unavailable: the original may have been deleted, or it is not accessible."), "A silent diagnostic does not replace the message");
    shelf->refresh(++time); check(item("Renamed report.txt")->availabilityError.has_value(), "Refresh marks the deleted original unavailable");

    // Remove and clear record their events; originals stay in place.
    events.clear();
    check(shelf->perform("shelf:" + deletedID + ":remove", ++time) && events.size() == 1 && event(events, 0, mod::EventKind::shelfRemoved, "filename", "Renamed report.txt"), "Removal records shelfRemoved");
    check(shelf->perform("shelf:clear", ++time) && shelf->perform("shelf:confirmClear", ++time) && shelf->state().items().empty(), "Clear removes every reference");
    check(events.size() == 2 && event(events, 1, mod::EventKind::shelfCleared, "count", "2"), "Clear records shelfCleared with its count");
    check(std::filesystem::exists(moved) && std::filesystem::exists(dropped), "References never move or delete originals");

    // Recorder can be replaced (host wiring) and language changes update text.
    unsigned later{}; shelf->setEventRecorder([&](mod::EventKind, mod::EventMetadata) { ++later; });
    check(shelf->importFiles(std::vector<std::string>{utf8(dropped)}, ++time) && later == 1 && events.size() == 2, "A replaced recorder receives later records only");
    shelf->setLanguage(core::Language::japanese);
    check(shelf->state().strings().revealPrefix == "エクスプローラーで表示：" && shelf->state().strings().previewPrefix == "プレビュー：", "Language changes rename the platform actions");
    shelf->cancelPanels(); pump();
    shelf.reset();
    { MSG m{}; while (PeekMessageW(&m, hwnd, WM_APP + 185, WM_APP + 189, PM_REMOVE)) {} }
    check(RevokeDragDrop(hwnd) == DRAGDROP_E_NOTREGISTERED && !IsWindowVisible(hwnd), "Teardown revokes the drop target; nothing was shown");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 2, "Usage: shelf_services_tests shelf-assets");
        Ole ole; text();
        Window window; gpu::NativeShelfAssets assets(std::filesystem::absolute(argv[1]));
        owner(window.hwnd, assets);
        std::cout << "Shelf services: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "Shelf services failed after " << checks << " checks: " << e.what() << '\n'; return 1; }
}
#else
#include <iostream>
int main() { std::cout << "SKIP Shelf services: requires Windows owned OLE fixture\n"; }
#endif
