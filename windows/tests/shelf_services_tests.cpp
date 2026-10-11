// Owner-level File Shelf services: the production tools::ShelfPreview on a
// NEW temporary data root with synthetic files, a hidden owner window and
// injected OLE data objects. Covers the Event Log records SystemHUDView wires
// to the canvas (for imports, drops, removal and clearing), stale-path
// relocation saved by refresh, the properties card that replaces Quick Look
// (read on the owner's private worker, then through a shared executor),
// refused virtual-file drops, localized platform text and error descriptions,
// the UI Automation peers and the import ledger API. No user file, clipboard
// or Explorer window of the user is touched; the real clipboard path
// (ShelfPreview::paste) is never called here.
#ifdef _WIN32
#include "tools/shelf_preview.hpp"
#include "app/utility_executor.hpp"
#include "native/file_shelf_files.hpp"
#include "native/module_scene.hpp"
#include "core/data/file_io.hpp"
#include "core/migration/mac_import_relink.hpp"
#include <ole2.h>
#include <uiautomation.h>
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
namespace mig = ehud::migration;
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
    check(tools::shelfText(T::previewPrefix, L::english) == "Properties: " && tools::shelfText(T::previewPrefix, L::korean) == "속성: ", "The properties card replaces the Mac Quick Look name");
    check(tools::shelfText(T::properties, L::japanese) == "プロパティ" && tools::shelfText(T::propertyModified, L::simplifiedChinese) == "修改时间" && tools::shelfText(T::propertyItems, L::traditionalChinese) == "{0} 個項目" &&
          tools::shelfText(T::open, L::korean) == "열기" && tools::shelfText(T::reveal, L::english) == "Reveal in File Explorer", "The properties card speaks every source language");
    check(tools::shelfText(T::revealFailed, L::english) == "Unable to show the file in File Explorer" && tools::shelfText(T::revealFailed, L::simplifiedChinese) == "无法在文件资源管理器中显示文件",
          "The reveal failure keeps the host's English and Chinese wording");
    for (auto language : {L::english, L::simplifiedChinese, L::traditionalChinese, L::japanese, L::korean})
        for (unsigned n = 0; n <= unsigned(T::openFailed); ++n) check(!tools::shelfText(T(n), language).empty() && data::Json::validUtf8(tools::shelfText(T(n), language)), "Every Windows-only Shelf text exists in every language");
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
    auto shelf = std::make_unique<tools::ShelfPreview>(hwnd, raster, assets, std::move(options));
    shelf->resize({1280, 800, 96, 1, 1280, 800}); shelf->setLanguage(core::Language::english);
    double time{};
    const auto pump = [&] { MSG m{}; for (unsigned n = 0; n < 32 && PeekMessageW(&m, hwnd, WM_APP + 185, tools::ShelfPreview::workMessage, PM_REMOVE); ++n) shelf->message({hwnd, m.message, m.wParam, m.lParam}, ++time); };
    // Waits (bounded) for the owner's private utility worker, delivering its notices.
    const auto settle = [&](auto&& done) { for (unsigned n = 0; n < 500 && !done(); ++n) { Sleep(10); pump(); } return done(); };
    const auto item = [&](std::string_view name) -> const mod::FileShelfItem* {
        const auto items = shelf->state().items(); const auto found = std::find_if(items.begin(), items.end(), [&](const auto& i) { return i.name == name; });
        return found == items.end() ? nullptr : &*found;
    };
    check(shelf->state().strings().revealPrefix == "Reveal in File Explorer: " && shelf->state().strings().previewPrefix == "Properties: ", "Card actions use the Windows names");

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

    // The properties card of a folder (eye button, double-click or Space):
    // without a shared executor its read runs on the owner's private worker.
    shelf->requestPreview(item("Folder")->id);
    auto panels = shelf->panels();
    check(panels.properties && panels.propertiesItem == item("Folder")->id && panels.propertyReads == 1 && !panels.propertiesLoaded, "The folder's card opens at once and reads its properties off the HUD thread");
    check(settle([&] { return shelf->panels().propertiesLoaded; }), "The private utility worker delivers the properties");
    auto card = shelf->properties();
    check(card && card->heading == "Properties" && card->open == "Open" && card->reveal == "Reveal in File Explorer" && card->rows.size() == 6, "The card has its caption, actions and six rows for a folder");
    check(card->rows[0].label == "Name" && card->rows[0].value == "Folder" && card->rows[1].label == "Type" && card->rows[1].value == "Folder" &&
          card->rows[2].label == "Contains" && card->rows[2].value == "0 items", "A folder shows its name, type and item count");
    check(card->rows[3].label == "Location" && card->rows[3].value.ends_with("\\Moved") && (card->rows[3].value == utf8(movedFolder) || (card->rows[3].value.starts_with(utf8(movedFolder).substr(0, 3) + "…"))),
          "The location is the containing folder, shortened in the middle to fit");
    check(card->rows[4].label == "Created" && card->rows[5].label == "Modified" && card->rows[4].value.size() > 4 && card->rows[5].value != "…" && card->rows[5].value != "—", "The times arrive from the worker");
    shelf->requestPreview(item("Folder")->id);
    check(!shelf->panels().properties && !shelf->properties(), "Requesting the same item again closes the card (Space toggles)");
    // A file: its exact size and the Shell's type name.
    shelf->requestPreview(item("Renamed report.txt")->id);
    check(settle([&] { return shelf->panels().propertiesLoaded; }), "The file's properties arrive");
    card = shelf->properties();
    check(card && card->rows.size() == 6 && card->rows[0].value == "Renamed report.txt" && !card->rows[1].value.empty() && card->rows[2].label == "Size" && card->rows[2].value == "16 bytes",
          "A file shows its size in bytes");
    shelf->cancelPanels(); check(!shelf->panels().properties, "Closing the HUD closes the card at once");

    // The card of a deleted original: it closes with the localized reason.
    std::filesystem::remove(renamed); const auto deletedID = item("Renamed report.txt")->id;
    shelf->requestPreview(deletedID);
    check(settle([&] { return !shelf->panels().properties; }), "The card closes when its original is gone");
    check(shelf->state().error() == std::optional<std::string>("This item is unavailable: the original may have been deleted, or it is not accessible."),
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
    check(shelf->state().strings().revealPrefix == "エクスプローラーで表示：" && shelf->state().strings().previewPrefix == "プロパティ：", "Language changes rename the platform actions");
    shelf->cancelPanels(); pump();
    shelf.reset();
    { MSG m{}; while (PeekMessageW(&m, hwnd, WM_APP + 185, tools::ShelfPreview::workMessage, PM_REMOVE)) {} }
    check(RevokeDragDrop(hwnd) == DRAGDROP_E_NOTREGISTERED && !IsWindowVisible(hwnd), "Teardown revokes the drop target; nothing was shown");
}
// Presents the Shelf on the fixture window (the drop test's projection).
struct Presented {
    core::ModulePresentation modules{core::Module::fileShelf};
    core::source::DesktopChromeSettings settings{{0, 0, 1280, 800}, 1, {}, core::Module::fileShelf, true};
    core::Matrix4 center = core::source::DesktopChromeLayout::make(settings, {}, {}).designToScreen;
    void show(tools::ShelfPreview& shelf, double& time) { const auto sample = modules.sample(++time).presentation; shelf.update(center, settings, sample, 1, time); }
};
std::wstring property(IRawElementProviderFragment* fragment, PROPERTYID id) {
    ComPtr<IRawElementProviderSimple> simple; ok(fragment->QueryInterface(IID_PPV_ARGS(&simple)), "Peer is a simple provider");
    VARIANT value; VariantInit(&value); ok(simple->GetPropertyValue(id, &value), "Peer property");
    std::wstring out = value.vt == VT_BSTR && value.bstrVal ? std::wstring(value.bstrVal) : std::wstring{}; VariantClear(&value); return out;
}
std::wstring wide(std::string_view text) { const auto n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0); std::wstring out(std::size_t(n), L'\0'); MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n); return out; }
ComPtr<IRawElementProviderFragment> peer(tools::ShelfPreview& shelf, std::wstring_view id) {
    for (std::size_t n = 0; n < shelf.automationCount(); ++n) {
        ComPtr<IRawElementProviderFragment> fragment; ok(shelf.automationChild(n, &fragment), "Peer child");
        if (property(fragment.Get(), UIA_AutomationIdPropertyId) == id) return fragment;
    }
    return nullptr;
}
std::wstring value(IRawElementProviderFragment* fragment) {
    ComPtr<IRawElementProviderSimple> simple; ok(fragment->QueryInterface(IID_PPV_ARGS(&simple)), "Peer is a simple provider");
    ComPtr<IUnknown> pattern; ok(simple->GetPatternProvider(UIA_ValuePatternId, &pattern), "Value pattern query");
    if (!pattern) return L"<none>"; ComPtr<IValueProvider> provider; ok(pattern.As(&provider), "Value provider");
    BSTR text{}; ok(provider->get_Value(&text), "Value read"); std::wstring out(text ? text : L""); SysFreeString(text); return out;
}
HRESULT invoke(IRawElementProviderFragment* fragment) {
    check(fragment != nullptr, "The peer exists");
    ComPtr<IRawElementProviderSimple> simple; ok(fragment->QueryInterface(IID_PPV_ARGS(&simple)), "Peer is a simple provider");
    ComPtr<IUnknown> pattern; if (const auto hr = simple->GetPatternProvider(UIA_InvokePatternId, &pattern); FAILED(hr)) return hr;
    check(pattern != nullptr, "Every Shelf peer is invokable");
    ComPtr<IInvokeProvider> provider; ok(pattern.As(&provider), "Invoke provider"); return provider->Invoke();
}

// UI Automation peers of the presented Shelf (HUDFileShelfInteraction.
// layoutAccessibility): one Button per accessible action, help and values,
// Invoke through ShelfPreview::perform, hit testing and disconnection.
void automation(HWND hwnd, const gpu::NativeShelfAssets& assets) {
    Temp temp; gpu::LayerRasterizer raster;
    tools::ShelfPreviewOptions options{temp.root / "metadata", false, false, {}};
    auto shelf = std::make_unique<tools::ShelfPreview>(hwnd, raster, assets, std::move(options));
    shelf->resize({1280, 800, 96, 1, 1280, 800}); shelf->setLanguage(core::Language::english);
    double time{};
    const auto a = temp.root / "Alpha.txt", b = temp.root / "Beta.txt"; write(a, "alpha"); write(b, "beta");
    check(shelf->importFiles(std::vector<std::string>{utf8(a), utf8(b)}, ++time), "Two references are added");
    check(!shelf->automationRefresh(nullptr) && shelf->automationCount() == 0, "A Shelf that is not presented lists no peers");
    Presented presented; presented.show(*shelf, time);
    check(shelf->automationRefresh(nullptr), "Presenting the Shelf lists its peers");
    const auto actions = shelf->state().accessibleActions();
    check(shelf->automationCount() == actions.size() && actions.size() == 10, "One peer per accessible action: Add, Clear and four per card");
    const auto idA = wide(shelf->state().items()[0].id), idB = wide(shelf->state().items()[1].id);
    auto add = peer(*shelf, L"shelf:add"); check(add && property(add.Get(), UIA_NamePropertyId) == L"Add files", "The Add files button is named by its label");
    check(property(add.Get(), UIA_HelpTextPropertyId) == wide(tools::shelfText(tools::ShelfText::accessibilityHelp, core::Language::english)), "Help is the Windows keyboard help");
    auto plateA = peer(*shelf, L"shelf:" + idA + L":select"), plateB = peer(*shelf, L"shelf:" + idB + L":select");
    check(plateA && plateB && property(plateA.Get(), UIA_NamePropertyId).starts_with(L"Alpha.txt, "), "A card plate is named by its details");
    check(value(plateA.Get()) == L"Selected" && value(plateB.Get()) == L"Selected" && value(add.Get()) == L"<none>", "Selected plates carry the Selected value; buttons have none");
    auto preview = peer(*shelf, L"shelf:" + idA + L":preview"); check(preview && property(preview.Get(), UIA_NamePropertyId) == L"Properties: Alpha.txt", "Card buttons use the Windows action names");
    UiaRect bounds{}; ok(plateA->get_BoundingRectangle(&bounds), "Plate bounds"); check(bounds.width > 20 && bounds.height > 10 && bounds.left >= 0 && bounds.top >= 0, "Plates are projected to screen pixels");
    // The hit test finds the plate at its centre (card buttons sit on top of it elsewhere).
    ComPtr<IRawElementProviderFragment> hit; ok(shelf->automationFromPoint(bounds.left + bounds.width * .3, bounds.top + bounds.height * .3, &hit), "Hit test");
    check(hit && property(hit.Get(), UIA_AutomationIdPropertyId) == L"shelf:" + idA + L":select", "The hit test returns the card under the point");
    // Invoke selects exactly that card and asks the host to refresh.
    check(invoke(plateA.Get()) == S_OK && shelf->state().selectedIDs().size() == 1 && shelf->state().selectedIDs().contains(shelf->state().items()[0].id), "Invoking a plate selects that card");
    MSG notice{}; check(PeekMessageW(&notice, hwnd, WM_APP + 185, WM_APP + 185, PM_REMOVE) && notice.lParam == 1 && shelf->message({hwnd, notice.message, notice.wParam, notice.lParam}, ++time),
                        "The invocation posts one content notice that the owner consumes");
    check(!shelf->automationRefresh(nullptr) && value(plateA.Get()) == L"Selected" && value(plateB.Get()).empty(), "Refreshing keeps identities and updates the values");
    // Clear asks for confirmation: the toolbar peers change.
    check(invoke(peer(*shelf, L"shelf:clear").Get()) == S_OK && shelf->state().confirmingClear(), "Invoking Clear asks for confirmation");
    { MSG m{}; while (PeekMessageW(&m, hwnd, WM_APP + 185, WM_APP + 189, PM_REMOVE)) shelf->message({hwnd, m.message, m.wParam, m.lParam}, ++time); }
    check(shelf->automationRefresh(nullptr) && peer(*shelf, L"shelf:cancelClear") && peer(*shelf, L"shelf:confirmClear") && !peer(*shelf, L"shelf:add"), "The confirmation buttons replace the toolbar");
    check(invoke(peer(*shelf, L"shelf:cancelClear").Get()) == S_OK && !shelf->state().confirmingClear(), "Cancel keeps every reference");
    check(invoke(add.Get()) == UIA_E_ELEMENTNOTAVAILABLE, "A withdrawn peer is disconnected");
    { MSG m{}; while (PeekMessageW(&m, hwnd, WM_APP + 185, WM_APP + 189, PM_REMOVE)) shelf->message({hwnd, m.message, m.wParam, m.lParam}, ++time); }
    // Add files goes through the host's choose action, as a click does.
    check(shelf->automationRefresh(nullptr), "The toolbar returns");
    check(invoke(peer(*shelf, L"shelf:add").Get()) == S_OK && shelf->takeAction() && true, "Invoking Add files queues the host's choose action");
    { MSG m{}; while (PeekMessageW(&m, hwnd, WM_APP + 185, WM_APP + 189, PM_REMOVE)) shelf->message({hwnd, m.message, m.wParam, m.lParam}, ++time); }
    // The properties card is modal for UI Automation: the card (its rows as
    // the value) and its close/open/reveal plates replace the Shelf's peers.
    check(invoke(peer(*shelf, L"shelf:" + idA + L":preview").Get()) == S_OK && shelf->panels().properties, "The eye button opens the properties card");
    presented.show(*shelf, time);
    check(shelf->automationRefresh(nullptr) && shelf->automationCount() == 4, "The open card lists only its own peers");
    auto cardPeer = peer(*shelf, L"shelf.properties:card");
    check(cardPeer && property(cardPeer.Get(), UIA_NamePropertyId) == L"Properties: Alpha.txt" && value(cardPeer.Get()).starts_with(L"Name: Alpha.txt; Type: ") &&
          property(cardPeer.Get(), UIA_HelpTextPropertyId) == wide(tools::shelfText(tools::ShelfText::propertiesHelp, core::Language::english)), "The card peer reads the item's properties and the card keys");
    check(property(peer(*shelf, L"shelf.properties:close").Get(), UIA_NamePropertyId) == L"Close" && property(peer(*shelf, L"shelf.properties:open").Get(), UIA_NamePropertyId) == L"Open",
          "The card's plates are named");
    check(invoke(peer(*shelf, L"shelf.properties:reveal").Get()) == S_OK && !shelf->panels().properties, "Reveal closes the card");
    const auto reveal = shelf->takeAction();
    check(reveal && reveal->kind == tools::ShelfPreviewAction::Kind::reveal && reveal->itemID == shelf->state().items()[0].id, "Reveal is handed to the host with the card's item");
    { MSG m{}; while (PeekMessageW(&m, hwnd, WM_APP + 185, tools::ShelfPreview::workMessage, PM_REMOVE)) shelf->message({hwnd, m.message, m.wParam, m.lParam}, ++time); }
    // (Not presented again here: without a renderer nothing publishes or
    // collects the replaced artwork; the fading card takes no input already.)
    check(shelf->automationRefresh(nullptr) && shelf->automationCount() == actions.size(), "The closed card gives the Shelf its peers back");
    // Disconnection (HUD hidden) and an unpresented Shelf.
    auto kept = peer(*shelf, L"shelf:" + idB + L":select");
    shelf->automationDisconnect(); check(shelf->automationCount() == 0 && kept->get_BoundingRectangle(&bounds) == UIA_E_ELEMENTNOTAVAILABLE, "Disconnected peers answer ELEMENTNOTAVAILABLE");
    core::ModulePresentation other(core::Module::clipboard); const auto hidden = other.sample(++time).presentation; shelf->update(presented.center, presented.settings, hidden, 1, time);
    shelf->automationRefresh(nullptr); check(shelf->automationCount() == 0, "Another module lists no Shelf peers");
    shelf.reset();
    { MSG m{}; while (PeekMessageW(&m, hwnd, WM_APP + 185, tools::ShelfPreview::workMessage, PM_REMOVE)) {} }
}

// Mac-imported Shelf items (Migration/relink.json written by the import area):
// unavailable cards until the user picks the file on this PC.
mig::MacRelinkItem imported(std::string record, std::string name, bool folder, double created) {
    mig::MacRelinkItem item; item.id = data::makeUUID(); item.store = std::string(mig::relinkShelfItem); item.recordID = std::move(record);
    item.displayName = name; item.lastKnownPath = "/Users/doctor/Desktop/" + name; item.kind = folder ? "folder" : "file";
    item.macReference = data::Json::Object{{"id", item.recordID}, {"name", name}, {"typeDescription", folder ? "Folder" : "PDF document"}, {"lastKnownPath", item.lastKnownPath},
                                           {"isDirectory", folder}, {"createdAt", created}, {"bookmark", "Ym9va21hcms="}, {"isSecurityScoped", true}};
    if (!folder) item.macReference["byteCount"] = 1234;
    return item;
}
void relink(HWND hwnd, const gpu::NativeShelfAssets& assets) {
    Temp temp; gpu::LayerRasterizer raster; std::vector<Recorded> events;
    const auto root = temp.root / "data";
    tools::ShelfPreviewOptions options{root, false, false, {}};
    options.recordEvent = [&](mod::EventKind kind, mod::EventMetadata metadata) { events.push_back({kind, std::move(metadata)}); };
    // The host's shared executor (EndfieldHUD.exe passes its own): the ledger
    // is read and written on its worker; drain() runs the completions here.
    constexpr UINT utilityNotice = WM_APP + 300;
    app::UtilityExecutor utility([hwnd] { PostMessageW(hwnd, utilityNotice, 0, 0); });
    auto shelf = std::make_unique<tools::ShelfPreview>(hwnd, raster, assets, std::move(options));
    shelf->setUtility(&utility);
    shelf->resize({1280, 800, 96, 1, 1280, 800}); shelf->setLanguage(core::Language::english);
    double time{};
    const auto settle = [&] {
        utility.waitIdle(); utility.drain();
        MSG m{}; while (PeekMessageW(&m, hwnd, WM_APP + 185, utilityNotice, PM_REMOVE)) if (m.message != utilityNotice) shelf->message({hwnd, m.message, m.wParam, m.lParam}, ++time);
    };
    const auto kept = temp.root / "Kept.txt"; write(kept, "kept");
    check(shelf->importFiles(std::vector<std::string>{utf8(kept)}, ++time) && shelf->pendingRelinks() == 0, "A Windows reference; nothing imported yet");
    // The import area's ledger: two Shelf items and one Notes item.
    const auto reportID = std::string("AAAAAAAA-0000-4000-8000-000000000001"), folderID = std::string("AAAAAAAA-0000-4000-8000-000000000002");
    auto report = imported(reportID, "Report.pdf", false, 700000000.5), folder = imported(folderID, "Mac folder", true, 700000100.25);
    mig::MacRelinkItem note; note.id = data::makeUUID(); note.store = std::string(mig::relinkNotesMedia); note.recordID = data::makeUUID(); note.displayName = "photo.png"; note.kind = "image";
    std::filesystem::create_directories(root / "Migration"); write(root / "Migration" / "relink.json", mig::MacRelinkLedger::encode({report, folder, note}));
    shelf->refresh(++time);
    check(shelf->pendingRelinks() == 0, "The ledger is read on the utility worker, not during refresh");
    settle();
    const auto items = shelf->state().items();
    check(shelf->pendingRelinks() == 2 && items.size() == 3 && items[0].name == "Kept.txt" && items[1].id == reportID && items[2].id == folderID, "Imported Shelf items follow the Windows references; other modules' entries are not listed");
    check(items[1].name == "Report.pdf" && items[1].availabilityError && items[1].byteCount == 1234 && !items[1].isDirectory && items[2].isDirectory && items[2].availabilityError,
          "An imported item is an unavailable card with its Mac metadata");
    check(shelf->state().dragSelection(reportID).empty(), "An imported item is never dragged");
    check((!shelf->perform("shelf:" + reportID + ":preview", ++time) || !shelf->takeAction()) && !shelf->panels().properties, "An imported item has no properties card and is never revealed");
    // Its relink action (UI Automation peer next to the plate).
    Presented presented; presented.show(*shelf, time); shelf->automationRefresh(nullptr);
    auto action = peer(*shelf, wide("shelf:" + reportID + ":relink"));
    check(action && property(action.Get(), UIA_NamePropertyId) == L"Relink: Report.pdf" && property(action.Get(), UIA_HelpTextPropertyId) == wide(tools::shelfText(tools::ShelfText::relinkHelp, core::Language::english)),
          "An imported card has a Relink peer with its own help");
    check(invoke(action.Get()) == S_OK, "Invoking Relink is accepted");
    const auto request = shelf->takeAction(); check(request && request->kind == tools::ShelfPreviewAction::Kind::choose && shelf->state().selectedID() == std::optional<std::string>(reportID),
          "Relink selects the card and asks the host for the picker (choose action)");
    { MSG m{}; while (PeekMessageW(&m, hwnd, WM_APP + 185, WM_APP + 189, PM_REMOVE)) shelf->message({hwnd, m.message, m.wParam, m.lParam}, ++time); }
    // The picker's completion: exactly one item.
    const auto chosen = temp.root / "Report on this PC.pdf"; write(chosen, "pdf");
    check(!shelf->relink(reportID, std::vector<std::string>{utf8(chosen), utf8(kept)}, ++time) && shelf->state().error() == std::optional<std::string>(tools::shelfText(tools::ShelfText::relinkChooseOne, core::Language::english)) &&
          shelf->pendingRelinks() == 2, "Two chosen items relink nothing and explain why");
    events.clear();
    check(shelf->relink(reportID, std::vector<std::string>{utf8(chosen)}, ++time) && shelf->pendingRelinks() == 1, "The chosen file relinks the imported item");
    const auto* relinked = shelf->state().item(reportID);
    check(relinked && !relinked->availabilityError && relinked->name == "Report on this PC.pdf" && relinked->lastKnownPath == utf8(chosen) && shelf->state().selectedID() == std::optional<std::string>(reportID),
          "The reference keeps the imported identifier, is available and selected");
    check(events.size() == 1 && event(events, 0, mod::EventKind::shelfAdded, "filename", "Report on this PC.pdf"), "Relinking records shelfAdded for the chosen file");
    { mig::MacRelinkLedger ledger(root); check(ledger.find(report.id)->state == mig::RelinkState::unresolved, "The record is written first; the ledger follows on the worker"); }
    settle();
    {
        gpu::NativeFileShelfFiles files; data::FileShelfStore saved(root, files.platform());
        const auto row = std::find_if(saved.items().begin(), saved.items().end(), [&](const auto& r) { return r.id == reportID; });
        check(row != saved.items().end() && row->createdAt == 700000000.5 && row->windowsPath == utf8(chosen), "The saved reference keeps the Mac identifier and date");
        mig::MacRelinkLedger ledger(root); const auto* entry = ledger.find(report.id);
        check(entry && entry->state == mig::RelinkState::resolved && entry->windowsPath == utf8(chosen) && ledger.find(note.id)->state == mig::RelinkState::unresolved, "The ledger entry is resolved with the chosen path; others are untouched");
    }
    // Removing an imported card dismisses its entry (references only).
    events.clear();
    check(shelf->perform("shelf:" + folderID + ":remove", ++time) && shelf->pendingRelinks() == 0 && !shelf->state().item(folderID), "Removing an imported card dismisses it");
    check(events.size() == 1 && event(events, 0, mod::EventKind::shelfRemoved, "filename", "Mac folder"), "Removal records shelfRemoved");
    settle();
    { mig::MacRelinkLedger ledger(root); check(ledger.find(folder.id)->state == mig::RelinkState::dismissed, "The folder entry is dismissed in the ledger"); }
    // A record written before its ledger entry (an interrupted relink) is
    // resolved on the next load, never shown twice.
    auto interrupted = imported(reportID, "Report.pdf", false, 700000000.5);
    { mig::MacRelinkLedger ledger(root); auto rows = ledger.items(); rows.push_back(interrupted); write(root / "Migration" / "relink.json", mig::MacRelinkLedger::encode(rows)); }
    shelf->refresh(++time); settle();
    { mig::MacRelinkLedger ledger(root); check(shelf->pendingRelinks() == 0 && ledger.find(interrupted.id)->state == mig::RelinkState::resolved && ledger.find(interrupted.id)->windowsPath == utf8(chosen),
          "An entry whose record exists is reconciled to that record's path"); }
    // Clearing the Shelf dismisses imported items that are still pending.
    auto later = imported("AAAAAAAA-0000-4000-8000-000000000003", "Later.key", false, 700000200.0);
    { mig::MacRelinkLedger ledger(root); auto rows = ledger.items(); rows.push_back(later); write(root / "Migration" / "relink.json", mig::MacRelinkLedger::encode(rows)); }
    shelf->refresh(++time); settle(); check(shelf->pendingRelinks() == 1, "A later imported item is listed");
    check(shelf->perform("shelf:clear", ++time) && shelf->perform("shelf:confirmClear", ++time) && shelf->state().items().empty() && shelf->pendingRelinks() == 0, "Clear removes references and imported items");
    settle();
    { mig::MacRelinkLedger ledger(root); check(ledger.find(later.id)->state == mig::RelinkState::dismissed && ledger.find(note.id)->state == mig::RelinkState::unresolved, "Clear dismisses only Shelf entries"); }
    check(std::filesystem::exists(chosen) && std::filesystem::exists(kept), "Relinking and clearing never touch the files");
    shelf.reset(); utility.drain();
    { MSG m{}; while (PeekMessageW(&m, hwnd, WM_APP + 185, utilityNotice, PM_REMOVE)) {} }
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 2, "Usage: shelf_services_tests shelf-assets");
        Ole ole; text();
        Window window; gpu::NativeShelfAssets assets(std::filesystem::absolute(argv[1]));
        owner(window.hwnd, assets);
        automation(window.hwnd, assets);
        relink(window.hwnd, assets);
        std::cout << "Shelf services: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "Shelf services failed after " << checks << " checks: " << e.what() << '\n'; return 1; }
}
#else
#include <iostream>
int main() { std::cout << "SKIP Shelf services: requires Windows owned OLE fixture\n"; }
#endif
