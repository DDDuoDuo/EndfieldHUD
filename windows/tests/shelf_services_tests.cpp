// Owner-level File Shelf services: the production tools::ShelfService and its
// DesignModuleOwner over the FileShelfCanvas, on a NEW temporary data root with
// synthetic files, a hidden window, WARP and injected OLE data objects. Covers
// the Event Log records SystemHUDView wires to the canvas (imports, drops,
// removal, clearing), drops over the projected canvas and the external drop
// routes of the other modules, refused virtual-file drops, stale-path renewal
// saved by refresh, the properties reads on the utility worker, Open and Reveal
// through the host's action message, the copy-only drag payload, the UI
// Automation tree, the import ledger relink, localized platform text and error
// descriptions, and what the Shelf keeps resident (the Shell icon worker only
// while shown, no artwork once the HUD closed on another module). No user
// file, clipboard, Explorer window or application of the user is touched; the
// real clipboard path (ShelfService::paste) and the OLE drag loop never run.
#ifdef _WIN32
#include "tools/shelf_owner.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#include "app/utility_executor.hpp"
#include "native/file_shelf_files.hpp"
#include "core/data/file_io.hpp"
#include "core/migration/mac_import_relink.hpp"
#include <ole2.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>

namespace gpu = endfield::native;
namespace core = endfield::core;
namespace app = endfield::app;
namespace mod = endfield::modules;
namespace d = endfield::modules::design;
namespace data = ehud::data;
namespace tools = endfield::tools;
namespace mig = ehud::migration;
using Microsoft::WRL::ComPtr;
using Failure = mod::FileShelfServices::Failure;
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
        hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, c.lpszClassName, L"Hidden Shelf services fixture", WS_POPUP, 0, 0, 1280, 800, nullptr, nullptr, c.hInstance, nullptr);
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
std::wstring property(IRawElementProviderFragment* fragment, PROPERTYID id) {
    ComPtr<IRawElementProviderSimple> simple; ok(fragment->QueryInterface(IID_PPV_ARGS(&simple)), "Element is a simple provider");
    VARIANT value; VariantInit(&value); ok(simple->GetPropertyValue(id, &value), "Element property");
    std::wstring out = value.vt == VT_BSTR && value.bstrVal ? std::wstring(value.bstrVal) : std::wstring{}; VariantClear(&value); return out;
}
std::wstring wide(std::string_view text) { const auto n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0); std::wstring out(std::size_t(n), L'\0'); MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n); return out; }
mig::MacRelinkItem imported(std::string record, std::string name, bool folder, double created) {
    mig::MacRelinkItem item; item.id = data::makeUUID(); item.store = std::string(mig::relinkShelfItem); item.recordID = std::move(record);
    item.displayName = name; item.lastKnownPath = "/Users/doctor/Desktop/" + name; item.kind = folder ? "folder" : "file";
    item.macReference = data::Json::Object{{"id", item.recordID}, {"name", name}, {"typeDescription", folder ? "Folder" : "PDF document"}, {"lastKnownPath", item.lastKnownPath},
                                           {"isDirectory", folder}, {"createdAt", created}, {"bookmark", "Ym9va21hcms="}, {"isSecurityScoped", true}};
    if (!folder) item.macReference["byteCount"] = 1234;
    return item;
}
core::ModulePresentationSample sample(core::Module m, core::Module requested) {
    core::ModulePresentationSample s;
    s.selected = s.requested = requested;
    s.current.module = m; s.current.contentFrame = core::moduleLocalContentFrame(m);
    s.current.designScreen = true; s.current.hostClip = false; s.current.attached = true; s.current.opacity = 1;
    s.acceptsModuleInput = true;
    return s;
}

// The HUD's parts for one Shelf: the shared overlay (the design Context),
// the service and its DesignModuleOwner, published like the host.
struct Hud {
    HWND hwnd; gpu::Renderer& renderer;
    gpu::LayerRasterizer raster;
    gpu::LayerComposition composition;
    std::unique_ptr<tools::DesignOverlayOwner> overlay;
    std::unique_ptr<tools::ShelfService> shelf;
    std::unique_ptr<tools::DesignModuleOwner> owner;
    core::source::DesktopChromeSettings chrome;
    double time{};
    bool released{};
    Hud(HWND h, gpu::Renderer& r, tools::ShelfServiceOptions options, core::Language language = core::Language::english) : hwnd(h), renderer(r) {
        tools::DesignOverlayOptions o; o.raster.pixelsPerPoint = 1; o.window = hwnd;
        overlay = std::make_unique<tools::DesignOverlayOwner>(raster, o);
        shelf = std::make_unique<tools::ShelfService>(hwnd, std::move(options));
        tools::DesignOwnerOptions ownerOptions; ownerOptions.name = "fileShelf"; ownerOptions.routing = tools::ShelfService::routing(); ownerOptions.raster.pixelsPerPoint = 1;
        ownerOptions.now = [this] { return time; };
        owner = shelf->makeOwner(raster, overlay->context(), std::move(ownerOptions));
        const app::ClientMetrics metrics{1280, 800, 96, 1, 1280, 800};
        owner->resize(metrics); overlay->resize(metrics);
        app::ModuleAppearance appearance; appearance.language = language; appearance.reduceMotion = true; appearance.accentSRGB = {250. / 255, 212. / 255, 31. / 255};
        overlay->setAppearance(appearance, 0); owner->setAppearance(appearance, 0);
        overlay->setOverlayVisible(true, 0); owner->setOverlayVisible(true, 0);
        chrome.viewport = {0, 0, 1280, 800};
    }
    ~Hud() { if (!released) try { release(); } catch (...) { renderer.reset(); } }
    void release() { released = true; composition.detach(renderer); owner->release(renderer); overlay->release(renderer); owner.reset(); }
    std::size_t frame(core::Module shown = core::Module::fileShelf, std::optional<core::Module> requested = {}) {
        time += .02;
        chrome.module = requested.value_or(shown);
        const core::Matrix4 center{};
        const auto s = sample(shown, requested.value_or(shown));
        const app::ModuleFrame f{center, chrome, s, 1, time, true};
        overlay->update(f); owner->update(f);
        overlay->upload(renderer); owner->upload(renderer);
        std::vector<gpu::LayerCompositionEntry> entries;
        for (const auto& e : owner->entries(shown)) entries.push_back(e);
        for (const auto& e : overlay->modalEntries()) entries.push_back(e);
        composition.setEntries(renderer, entries); composition.present(renderer);
        owner->collected(renderer); overlay->collected(renderer);
        return entries.size();
    }
    // The service's own notices (drops, utility, icons) as the host's window procedure delivers them.
    void pump() {
        MSG m{};
        for (unsigned n = 0; n < 256 && PeekMessageW(&m, hwnd, WM_APP + 185, WM_APP + 192, PM_REMOVE); ++n) (void)owner->message({hwnd, m.message, m.wParam, m.lParam}, time += .001);
    }
    mod::FileShelfCanvas& canvas() { return *shelf->canvas(); }
    std::string toast() { const auto& t = overlay->context().overlay().toastState().current(); return t ? t->title + "|" + t->caption : std::string(); }
    // A box point as a screen point of the hidden window.
    POINTL screen(core::Point box) {
        const auto rect = owner->core().clientRect({box.x, box.y, 0, 0});
        check(rect.has_value(), "A Shelf point projects");
        POINT p{LONG(std::lround(rect->x)), LONG(std::lround(rect->y))}; check(ClientToScreen(hwnd, &p) != FALSE, "Client to screen");
        return {p.x, p.y};
    }
    const mod::FileShelfItem* item(std::string_view name) {
        const auto& items = canvas().items();
        const auto it = std::find_if(items.begin(), items.end(), [&](const auto& i) { return i.name == name; });
        return it == items.end() ? nullptr : &*it;
    }
};

void text() {
    using T = mod::ShelfText; using L = core::Language;
    const auto error = [](data::StoreErrorCode code, const char* what, Failure failure, L language) { return tools::shelfErrorText(data::StoreError(code, what), failure, language); };
    check(error(data::StoreErrorCode::unavailable, "Item is no longer on the shelf", Failure::action, L::english) == "This item is no longer on the shelf.", "Missing items use the source text");
    check(error(data::StoreErrorCode::unavailable, "Original shelf item is unavailable; a different item occupies its former location", Failure::action, L::simplifiedChinese) == "原始项目不可用，其他项目已占用了原来的位置。",
          "A replaced original uses the source text");
    check(error(data::StoreErrorCode::unavailable, "Cannot acquire shelf file metadata (Windows error 5)", Failure::importing, L::english) == "Choose a file or folder that is available on this PC.", "Import failures ask for an available item");
    check(error(data::StoreErrorCode::unavailable, "Cannot acquire shelf file metadata (Windows error 2)", Failure::action, L::english) == "This item is unavailable: the original may have been deleted, or it is not accessible.",
          "Action failures explain the missing original");
    check(error(data::StoreErrorCode::changedOnDisk, "Saved data changed in another instance", Failure::changing, L::japanese) == mod::shelfText(T::changedOnDisk, L::japanese), "External changes use the Windows wording");
    check(error(data::StoreErrorCode::tooLarge, "Shelf metadata exceeds the explicit archive byte bound", Failure::importing, L::korean) == mod::shelfText(T::tooManyReferences, L::korean), "An oversized archive is explained");
    check(tools::shelfErrorText(std::runtime_error("disk full"), Failure::changing, L::english) == "The file shelf could not be saved or opened: disk full", "Other persistence failures keep the source prefix");
    check(tools::shelfKnownMessage("Drop source did not provide filesystem paths", L::english) == std::optional<std::string>(""), "A clipboard without files stays silent, as on the Mac");
    check(!tools::shelfKnownMessage("Unable to show the file in File Explorer", L::english), "Host messages pass through unchanged");
    check(mod::shelfText(T::revealFailed, L::simplifiedChinese) == "无法在文件资源管理器中显示文件", "Reveal failures name File Explorer");
}

// Imports, drops, stale paths, the properties reads, the UIA tree, Open /
// Reveal / drag, removal and clearing: the private utility executor.
void services(HWND hwnd, gpu::Renderer& renderer) {
    Temp temp; std::vector<Recorded> events; std::vector<std::string> opened;
    tools::ShelfServiceOptions options{temp.root / "metadata", false, true};
    options.recordEvent = [&](mod::EventKind kind, mod::EventMetadata metadata) { events.push_back({kind, std::move(metadata)}); };
    options.openOperation = [&](const data::ShelfFileAccess& lease) { opened.push_back(lease.metadata().windowsPath); return S_OK; };
    options.homeFolder = utf8(temp.root);
    Hud hud(hwnd, renderer, std::move(options));
    auto& shelf = *hud.shelf;
    check(hud.frame() > 0 && hud.canvas().isActive() && hud.canvas().itemCount() == 0, "The empty Shelf composes and takes input");
    const auto report = temp.root / "Report.txt", folder = temp.root / "Folder";
    write(report, "Synthetic report"); check(std::filesystem::create_directory(folder), "Synthetic folder");
    write(folder / "a.txt", "a"); write(folder / "b.txt", "b"); write(folder / "hidden.txt", "h"); check(SetFileAttributesW((folder / "hidden.txt").c_str(), FILE_ATTRIBUTE_HIDDEN) != FALSE, "Hide a child");
    const std::vector<std::string> paths{utf8(report), utf8(folder)};
    check(shelf.importFiles(paths, hud.time) && events.size() == 2 && event(events, 0, mod::EventKind::shelfAdded, "filename", "Report.txt") && event(events, 1, mod::EventKind::shelfAdded, "filename", "Folder"),
          "Every new reference records shelfAdded with its filename");
    check(shelf.importFiles(paths, hud.time) && events.size() == 2, "Duplicates record nothing");
    check(hud.canvas().selectedIDs().empty(), "Adding files selects nothing");
    hud.frame();

    // A drop over the projected canvas (HUDFileShelfInteraction.importPasteboard).
    const auto dropped = temp.root / "Dropped photo.png"; write(dropped, "png");
    ComPtr<IDropTarget> target = shelf.fileDropTarget(); check(target != nullptr, "The service's drop target exists");
    ComPtr<Source> source; source.Attach(new Source); source->paths = gpu::encodeShelfDropPaths(std::vector<std::string>{utf8(dropped)});
    const auto point = hud.screen({300, 300});
    DWORD effect = DROPEFFECT_COPY; ok(target->DragEnter(source.Get(), MK_LBUTTON, point, &effect), "Drag enters the HUD");
    check(effect == DROPEFFECT_COPY, "Files over the projected Shelf are accepted"); hud.pump();
    check(hud.canvas().inputLocked() && shelf.preservesFocusOnLoss(), "An incoming drag locks the Shelf (drop feedback) and keeps the HUD up");
    effect = DROPEFFECT_COPY; ok(target->Drop(source.Get(), MK_LBUTTON, point, &effect), "Drop");
    check(effect == DROPEFFECT_COPY, "The drop commits a copy-only reference"); hud.pump();
    check(hud.item("Dropped photo.png") && events.size() == 3 && event(events, 2, mod::EventKind::shelfAdded, "filename", "Dropped photo.png"), "A drop records shelfAdded");
    check(hud.canvas().selectedIDs().empty() && !hud.canvas().inputLocked(), "The dropped reference is not selected; the feedback ends");
    const auto outside = hud.screen({300, 40});
    effect = DROPEFFECT_COPY; ok(target->DragEnter(source.Get(), MK_LBUTTON, {outside.x - 2000, outside.y}, &effect), "Drag enters off the canvas");
    check(effect == DROPEFFECT_NONE, "Outside the canvas nothing is accepted"); ok(target->DragLeave(), "Drag leaves");
    ComPtr<Source> virtualSource; virtualSource.Attach(new Source); virtualSource->virtualOnly = true;
    effect = DROPEFFECT_COPY; ok(target->DragEnter(virtualSource.Get(), MK_LBUTTON, point, &effect), "Virtual drag enters");
    effect = DROPEFFECT_COPY; ok(target->Drop(virtualSource.Get(), MK_LBUTTON, point, &effect), "Virtual drop");
    check(effect == DROPEFFECT_NONE, "Virtual files are refused"); hud.pump();
    check(hud.toast() == "Notice|This item isn't a file on disk. Save it to a folder first, then add it.", "The refusal is a Notice toast");
    check(hud.canvas().itemCount() == 3, "No reference was added for virtual files");
    // An external route (another module's drop) takes the target while it is set.
    std::vector<std::string> routed;
    shelf.setExternalDropCallbacks(gpu::ShelfDropTarget::Callbacks{[](POINTL) { return true; }, [&](std::span<const std::string> p) { routed.assign(p.begin(), p.end()); return true; }}, hud.time);
    effect = DROPEFFECT_COPY; ok(target->Drop(source.Get(), MK_LBUTTON, point, &effect), "External drop"); hud.pump();
    check(effect == DROPEFFECT_COPY && routed.size() == 1 && hud.canvas().itemCount() == 3 && events.size() == 3, "An external route's commit never imports into the Shelf");
    shelf.setExternalDropCallbacks(std::nullopt, hud.time);

    // Stale paths: a renamed file and a moved folder are found again by identity.
    const auto renamed = temp.root / "Renamed report.txt", moved = temp.root / "Moved" / "Folder";
    check(std::filesystem::create_directory(temp.root / "Moved"), "Synthetic destination");
    std::filesystem::rename(report, renamed); std::filesystem::rename(folder, moved);
    shelf.refresh(hud.time); hud.pump();
    check(hud.item("Renamed report.txt") && hud.item("Renamed report.txt")->lastKnownPath == utf8(renamed) && !hud.item("Renamed report.txt")->availabilityError, "A renamed original is followed by identity");
    check(hud.item("Folder") && hud.item("Folder")->lastKnownPath == utf8(moved), "A moved folder is followed by identity");
    { gpu::NativeFileShelfFiles files; data::FileShelfStore saved(temp.root / "metadata", files.platform());
      check(std::any_of(saved.items().begin(), saved.items().end(), [&](const auto& r) { return r.windowsPath == utf8(renamed); }), "The renewed path is saved"); }

    // The properties card: its reads run on the utility worker, never the HUD thread.
    const auto folderID = hud.item("Folder")->id;
    hud.canvas().click(folderID); hud.frame();
    check(hud.canvas().hasPropertiesCardForTesting() && shelf.panels().propertyReads == 1 && shelf.panels().pendingPropertyReads == 1, "The card reads its properties on the worker");
    for (unsigned n = 0; n < 200 && shelf.panels().pendingPropertyReads; ++n) { Sleep(5); hud.pump(); }
    const auto& properties = hud.canvas().propertiesForTesting();
    check(properties.contains(folderID) && properties.at(folderID).itemCount == std::optional<std::int64_t>(2) && properties.at(folderID).created && properties.at(folderID).modified,
          "A folder counts its visible children; its dates are read");
    hud.frame();

    // UI Automation: the header actions, one tab per tile, the card's buttons.
    check(hud.owner->automationRefresh(nullptr), "The presented Shelf lists its tree");
    std::vector<std::wstring> ids;
    for (std::size_t n = 0; n < hud.owner->automationCount(); ++n) { ComPtr<IRawElementProviderFragment> e; ok(hud.owner->automationChild(n, &e), "Element"); ids.push_back(property(e.Get(), UIA_AutomationIdPropertyId)); }
    const auto has = [&](std::wstring_view id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
    check(has(L"shelf.add") && has(L"shelf.clear") && has(L"file:1") && has(L"file:3") && has(L"props.open") && has(L"props.reveal") && has(L"props.remove"),
          "Add, Clear, the tiles and the card's actions are listed");
    for (std::size_t n = 0; n < hud.owner->automationCount(); ++n) {
        ComPtr<IRawElementProviderFragment> e; ok(hud.owner->automationChild(n, &e), "Element");
        if (property(e.Get(), UIA_AutomationIdPropertyId) != L"file:1") continue;
        check(property(e.Get(), UIA_NamePropertyId) == L"Renamed report.txt, TXT file, 16 bytes", "A tile is named by its details");
        ComPtr<IRawElementProviderSimple> simple; e.As(&simple); ComPtr<IUnknown> pattern; ok(simple->GetPatternProvider(UIA_SelectionItemPatternId, &pattern), "Tab selection pattern");
        ComPtr<ISelectionItemProvider> select; check(pattern && SUCCEEDED(pattern.As(&select)) && SUCCEEDED(select->Select()), "Selecting a tile through UIA");
    }
    const auto reportID = hud.item("Renamed report.txt")->id;
    check(hud.canvas().selectedIDs().size() == 1 && hud.canvas().selectedIDs().contains(reportID), "UIA selects the tile like a click");

    // Open: from the host's action message; the HUD stays and the toast follows.
    check(shelf.perform("shelf:" + reportID + ":open", hud.time), "Open is accepted");
    auto action = shelf.takeAction();
    check(action && action->kind == tools::ShelfServiceAction::Kind::open && action->itemID == reportID && opened.empty(), "Open waits for the host's action message");
    shelf.open(action->itemID, hud.time);
    check(opened == std::vector<std::string>{utf8(renamed)} && hud.toast() == "Opened|Renamed report.txt", "The Shell opens the revalidated item; Opened toasts its name");
    // Reveal: the lease is taken now and handed to the host, which closes the HUD first.
    check(shelf.perform("shelf:" + reportID + ":reveal", hud.time) && hud.toast() == "Revealed in File Explorer|~", "Reveal toasts the location");
    action = shelf.takeAction();
    check(action && action->kind == tools::ShelfServiceAction::Kind::reveal, "Reveal is handed to the host");
    { auto lease = shelf.access(action->itemID); check(lease.open() && lease.metadata().windowsPath == utf8(renamed), "The host receives the held lease"); }
    // The drag payload of the selection: copy-only, prepared without the OLE loop.
    { auto transfer = shelf.prepareDrag(reportID); check(transfer && transfer->dataObject() != nullptr, "A selected reference creates the copy-only OLE payload"); }

    // Removal and clearing: references only, with their Event Log records.
    events.clear();
    check(shelf.perform("shelf:" + reportID + ":remove", hud.time) && !hud.item("Renamed report.txt") && events.size() == 1 && event(events, 0, mod::EventKind::shelfRemoved, "filename", "Renamed report.txt"),
          "Removing a reference records shelfRemoved");
    check(hud.toast() == "Removed from shelf|", "One removal toast");
    check(shelf.perform("shelf:clear", hud.time) && hud.overlay->context().overlay().isConfirming(), "Clear asks through the confirm bar");
    hud.overlay->context().overlay().decide(d::ConfirmState::Decision::confirm);
    check(hud.canvas().itemCount() == 0 && events.size() == 2 && event(events, 1, mod::EventKind::shelfCleared, "count", "2"), "Clearing records shelfCleared with the count");
    check(std::filesystem::exists(renamed) && std::filesystem::exists(moved) && std::filesystem::exists(dropped), "References never move or delete originals");
    hud.frame();
    hud.release();
    hud.shelf.reset();
    check(RevokeDragDrop(hwnd) == DRAGDROP_E_NOTREGISTERED && !IsWindowVisible(hwnd), "Teardown revokes the drop target; nothing was shown");
}

// Mac-imported Shelf items (Migration/relink.json written by the import area):
// unavailable tiles until the user picks the file on this PC.
void relink(HWND hwnd, gpu::Renderer& renderer) {
    Temp temp; std::vector<Recorded> events;
    const auto root = temp.root / "data";
    tools::ShelfServiceOptions options{root, false, false};
    options.recordEvent = [&](mod::EventKind kind, mod::EventMetadata metadata) { events.push_back({kind, std::move(metadata)}); };
    constexpr UINT utilityNotice = WM_APP + 300;
    app::UtilityExecutor utility([hwnd] { PostMessageW(hwnd, utilityNotice, 0, 0); });
    Hud hud(hwnd, renderer, std::move(options));
    auto& shelf = *hud.shelf; shelf.setUtility(&utility);
    const auto settle = [&] { utility.waitIdle(); utility.drain(); MSG m{}; while (PeekMessageW(&m, hwnd, utilityNotice, utilityNotice, PM_REMOVE)) {} hud.pump(); };
    hud.frame();
    const auto kept = temp.root / "Kept.txt"; write(kept, "kept");
    check(shelf.importFiles(std::vector<std::string>{utf8(kept)}, hud.time) && shelf.pendingRelinks() == 0, "A Windows reference; nothing imported yet");
    const auto reportID = std::string("AAAAAAAA-0000-4000-8000-000000000001"), folderID = std::string("AAAAAAAA-0000-4000-8000-000000000002");
    auto report = imported(reportID, "Report.pdf", false, 700000000.5), folder = imported(folderID, "Mac folder", true, 700000100.25);
    mig::MacRelinkItem note; note.id = data::makeUUID(); note.store = std::string(mig::relinkNotesMedia); note.recordID = data::makeUUID(); note.displayName = "photo.png"; note.kind = "image";
    std::filesystem::create_directories(root / "Migration"); write(root / "Migration" / "relink.json", mig::MacRelinkLedger::encode({report, folder, note}));
    shelf.refresh(hud.time);
    check(shelf.pendingRelinks() == 0, "The ledger is read on the utility worker, not during refresh");
    settle();
    const auto items = hud.canvas().items();
    check(shelf.pendingRelinks() == 2 && items.size() == 3 && items[0].name == "Kept.txt" && items[1].id == reportID && items[2].id == folderID, "Imported Shelf items follow the Windows references; other modules' entries are not listed");
    check(items[1].awaitingRelink && items[1].availabilityError && items[1].byteCount == 1234 && items[2].isDirectory, "An imported item is an unavailable tile with its Mac metadata");
    check(hud.canvas().dragSelection(reportID).empty(), "An imported item is never dragged");
    hud.canvas().click(reportID); hud.frame();
    check(shelf.panels().propertyReads == 0, "An imported item has no properties to read");
    check(shelf.perform("shelf:" + reportID + ":relink", hud.time), "Relink is offered for an imported item");
    auto request = shelf.takeAction();
    check(request && request->kind == tools::ShelfServiceAction::Kind::choose, "Relink asks the host for the file picker");
    const auto chosen = temp.root / "Report on this PC.pdf"; write(chosen, "pdf");
    check(!shelf.relink(reportID, std::vector<std::string>{utf8(chosen), utf8(kept)}, hud.time) && hud.toast() == "Notice|" + mod::shelfText(mod::ShelfText::relinkChooseOne, core::Language::english),
          "Exactly one item relinks");
    check(shelf.relink(reportID, std::vector<std::string>{utf8(chosen)}, hud.time) && shelf.pendingRelinks() == 1, "The chosen file relinks the imported item");
    const auto* relinked = hud.item("Report on this PC.pdf");
    check(relinked && relinked->id == reportID && !relinked->availabilityError && !relinked->awaitingRelink && hud.canvas().selectedIDs().contains(reportID),
          "The relinked reference keeps the imported identifier and its selection");
    check(events.size() == 2 && event(events, 1, mod::EventKind::shelfAdded, "filename", "Report on this PC.pdf"), "Relinking records shelfAdded for the chosen file");
    settle();
    { gpu::NativeFileShelfFiles files; data::FileShelfStore saved(root, files.platform());
      const auto row = std::find_if(saved.items().begin(), saved.items().end(), [&](const auto& r) { return r.id == reportID; });
      check(row != saved.items().end() && row->createdAt == 700000000.5 && row->windowsPath == utf8(chosen), "The saved reference keeps the Mac identifier and date");
      mig::MacRelinkLedger ledger(root); const auto* entry = ledger.find(report.id);
      check(entry && entry->state == mig::RelinkState::resolved && entry->windowsPath == utf8(chosen) && ledger.find(note.id)->state == mig::RelinkState::unresolved, "The ledger entry is resolved; others are untouched"); }
    events.clear();
    check(shelf.perform("shelf:" + folderID + ":remove", hud.time) && shelf.pendingRelinks() == 0 && events.size() == 1 && event(events, 0, mod::EventKind::shelfRemoved, "filename", "Mac folder"),
          "Removing an imported tile dismisses it and records shelfRemoved");
    settle();
    { mig::MacRelinkLedger ledger(root); check(ledger.find(folder.id)->state == mig::RelinkState::dismissed, "The folder entry is dismissed in the ledger"); }
    auto later = imported("AAAAAAAA-0000-4000-8000-000000000003", "Later.pdf", false, 700000200);
    { auto rows = mig::MacRelinkLedger(root).items(); rows.push_back(later); write(root / "Migration" / "relink.json", mig::MacRelinkLedger::encode(rows)); }
    shelf.refresh(hud.time); settle(); check(shelf.pendingRelinks() == 1, "A later imported item is listed");
    check(shelf.perform("shelf:clear", hud.time), "Clear asks");
    hud.overlay->context().overlay().decide(d::ConfirmState::Decision::confirm); settle();
    check(hud.canvas().itemCount() == 0 && shelf.pendingRelinks() == 0, "Clear removes references and imported items");
    { mig::MacRelinkLedger ledger(root); check(ledger.find(later.id)->state == mig::RelinkState::dismissed && ledger.find(note.id)->state == mig::RelinkState::unresolved, "Clear dismisses only Shelf entries"); }
    check(std::filesystem::exists(chosen) && std::filesystem::exists(kept), "Relinking and clearing never touch the files");
    hud.frame();
}

// What the Shelf keeps resident: no icon worker before the first shown Shelf,
// the worker parked while another module shows, no artwork once the HUD
// closed on another module, a rebuild when shown again.
void resident(HWND hwnd, gpu::Renderer& renderer) {
    Temp temp;
    Hud hud(hwnd, renderer, tools::ShelfServiceOptions{temp.root / "metadata", true, false});
    auto& shelf = *hud.shelf;
    check(!shelf.resources().iconProvider && shelf.resources().sceneParts == 0, "At launch the Shelf has no icon worker and no artwork");
    std::vector<std::string> paths;
    for (int n = 0; n < 6; ++n) { const auto file = temp.root / ("Sample " + std::to_string(n) + ".txt"); write(file, "x"); paths.push_back(utf8(file)); }
    check(shelf.importFiles(paths, hud.time), "Six references");
    hud.frame(core::Module::clipboard);
    check(!shelf.resources().iconProvider, "A Shelf that never showed starts no icon worker");
    // Switch-in, idle and pointer-move cost on WARP (printed for the port report).
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    auto started = Clock::now(); hud.frame(); const double switchIn = ms(Clock::now() - started);
    const auto shown = shelf.resources();
    check(shown.iconProvider && shown.iconRequests >= 1 && shown.sceneParts > 0 && shown.scenePixelBytes > 0, "The shown Shelf asks for its tiles' Shell icons (drag images) and holds its artwork");
    for (unsigned n = 0; n < 400 && shelf.resources().cachedIcons < 6; ++n) { Sleep(5); hud.pump(); }
    const auto rasterized = hud.raster.stats().rasterizations;
    started = Clock::now(); for (unsigned n = 0; n < 30; ++n) hud.frame(); const double idle = ms(Clock::now() - started) / 30;
    const auto tile = hud.owner->core().clientRect({5, 86, 135, 124}); check(tile.has_value(), "A tile projects");
    started = Clock::now();
    for (unsigned n = 0; n < 30; ++n) { (void)hud.owner->pointer({app::PointerKind::move, app::PointerButton::none, tile->x + tile->width * (n % 2 ? .25 : 3.2), tile->y + tile->height / 2, 0}, hud.time); hud.frame(); }
    const double moves = ms(Clock::now() - started) / 30;
    check(hud.raster.stats().rasterizations == rasterized, "Idle frames and hover moves rasterize nothing");
    const auto stats = hud.owner->core().sceneStats(core::Module::fileShelf);
    std::cout << "cost: switch-in " << switchIn << " ms, idle frame " << idle << " ms, hover move frame " << moves << " ms, raster entries " << (stats ? stats->rasterEntries : 0)
              << ", surfaces " << (stats ? stats->surfaces : 0) << '\n';
    hud.frame(core::Module::fileShelf, core::Module::clipboard);
    hud.frame(core::Module::clipboard);
    check(shelf.resources().iconParks >= 1, "Switching away parks the icon worker");
    // A switch back inside the open HUD keeps its rasters (only textures were parked).
    const auto beforeBack = hud.raster.stats().rasterizations;
    started = Clock::now(); hud.frame(); const double back = ms(Clock::now() - started);
    check(hud.raster.stats().rasterizations == beforeBack, "Switching back inside the open HUD rasterizes nothing");
    std::cout << "cost: switching back to the Shelf inside the open HUD " << back << " ms\n";
    hud.frame(core::Module::fileShelf, core::Module::clipboard);
    hud.frame(core::Module::clipboard);
    hud.owner->overlayClosing(hud.time); hud.owner->setOverlayVisible(false, hud.time);
    (void)hud.owner->hostEvent({app::HostEvent::Kind::overlayConcealed, 0}, hud.time);
    hud.frame(core::Module::clipboard);
    const auto closed = shelf.resources();
    check(closed.scenePixelBytes == 0 && closed.sceneParks >= 1, "Once the HUD closed on another module the Shelf keeps no raster artwork");
    hud.owner->setOverlayVisible(true, hud.time);
    started = Clock::now(); hud.frame(); const double reopened = ms(Clock::now() - started);
    check(shelf.resources().scenePixelBytes > 0, "Showing it again rebuilds the artwork");
    std::cout << "cost: reopening on the Shelf after its artwork was released " << reopened << " ms\n";
    std::cout << "resident: icons cached " << shelf.resources().cachedIcons << " (" << shelf.resources().cachedIconBytes << " bytes), worker starts " << shelf.resources().iconWorkerStarts
              << ", scene parts " << shelf.resources().sceneParts << ", raster bytes " << shelf.resources().scenePixelBytes << '\n';
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 2, "Usage: shelf_services_tests hud.hlsl");
        Ole ole; Window window;
        gpu::Renderer renderer; renderer.initialize(window.hwnd, 1280, 800, {gpu::Driver::warpForTests, argv[1], gpu::RenderTarget::offscreenForTests});
        text();
        services(window.hwnd, renderer);
        relink(window.hwnd, renderer);
        resident(window.hwnd, renderer);
        std::cout << "File Shelf services: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "File Shelf services failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
#else
#include <iostream>
int main() { std::cout << "SKIP File Shelf services: requires Windows\n"; }
#endif
