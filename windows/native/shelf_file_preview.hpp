#pragma once
// NOT USED by the Temporary File Shelf any more: its preview is the in-HUD
// properties card (tools/shelf_preview.hpp, native/shelf_properties.hpp), and
// the preview thread that contained these handlers was removed. This file and
// its suite stay only while windows/CMakeLists.txt still lists
// native/shelf_file_preview.cpp in hud_shelf_icons and the
// shelf_file_preview_tests target (a host change removes all three).
#include "core/data/file_shelf_store.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace endfield::native {
// Pure validation; no filesystem or association lookup. Preview never follows
// a stored symbolic-link reference or treats a folder as an ordinary stream.
bool validShelfPreviewReference(std::string_view itemID,
    const ehud::data::ShelfFileMetadata&)noexcept;
// The generic (fallback) preview also describes folders and selected links:
// they are named and shown with their type icon, never opened or followed.
bool validShelfPreviewFallbackReference(std::string_view itemID,
    const ehud::data::ShelfFileMetadata&)noexcept;
// Caller-localized text of the generic preview (title plus at most four lines).
struct ShelfPreviewDescription {
    std::string title;std::vector<std::string> details;
};
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
struct IStream;
struct IPreviewHandler;
namespace endfield::native {
struct ShelfPreviewRoute {HWND owner{};UINT message{};UINT_PTR generation{};};
struct ShelfPreviewOptions {
    // Native Quick Look equivalent, outside the HUD projection. A false value
    // is only for owned hidden fixtures; production shows after DoPreview.
    bool visible{true};int clientWidth{800},clientHeight{600};
    // Quick Look's generic preview: when no stream preview handler can show the
    // item (folders, selected links, unregistered types, a handler that fails
    // to initialize or render) the same owned popup shows the Shell type icon
    // (system image list for the name only: no file access or per-file handler
    // code), the name and ShelfPreviewServices::describe lines. Off: such items
    // report `failed` as before.
    bool fallback{};
    // Contained hosting (shelf_preview_thread.hpp): the popup is an unowned
    // top-level tool window of the preview thread, centred on this window of
    // the HUD's thread, which gets the foreground back before a focused
    // preview hides. Cross-thread owner/parent links would attach the two
    // threads' input queues, so a hung handler could stall the HUD. Null: the
    // popup is owned by route.owner on the same thread.
    HWND anchor{};
};
enum class ShelfPreviewNotice:LPARAM {work=1,events=2};
enum class ShelfPreviewEventKind {opened,closed,failed,revealRequested};
struct ShelfPreviewEvent {
    ShelfPreviewEventKind kind{};HRESULT result{S_OK};std::string itemID;
    bool fallback{}; // opened: the generic preview is shown (result: why no handler showed it)
};
struct ShelfPreviewStats {
    bool queued{},active{},visible{},fallback{};std::uint64_t requests{},opens{},closes{},failures{},notices{},fallbacks{};
};
// Modifier keys held for a popup key (unmodified Escape/Space close, Ctrl-R
// reveals; source Quick Look panel keys).
struct ShelfPreviewModifiers {bool shift{},control{},alt{};};
struct ShelfPreviewServices {
    // Independent provider callbacks; never capture a Store or UI object. The
    // controller calls these only after its queued request leaves store/state
    // busy callbacks. Defaults use NativeFileShelfFiles, Shell associations and
    // a read-only native IStream over a separately identity-checked handle.
    std::function<ehud::data::ShelfFileAccess(const ehud::data::ShelfRecord&)>resolve;
    std::function<HRESULT(const ehud::data::ShelfFileMetadata&,IPreviewHandler**)>createHandler;
    std::function<HRESULT(const ehud::data::ShelfFileAccess&,IStream**)>openStream;
    // Generic preview text for the current metadata (owner language). Default:
    // name, the stored type description and the byte count.
    std::function<ShelfPreviewDescription(const ehud::data::ShelfFileMetadata&)>describe;
    // Containment hooks (preview thread). watch(true)/watch(false) bracket every
    // operation that can block in third-party code or on a slow filesystem:
    // resolve, describe, handler activation, stream opening, every
    // IPreviewHandler/IInitializeWithStream/IObjectWithSite call, proxy
    // releases and the popup teardown. Brackets nest; a hook may close or
    // abandon this controller from watch(false). activity() runs whenever the
    // handler reads, seeks or clones the preview stream: it is making progress
    // (for example while a cloud placeholder hydrates), not hung.
    std::function<void(bool)>watch;
    std::function<void()>activity;
    // Modifier state of the popup key being translated, read on the popup's
    // thread. Default: GetKeyState, which Win32 synchronizes with keyboard
    // input that thread retrieves from its queue. A posted key message (tests,
    // synthetic input) carries no modifier state and does not synchronize it:
    // that thread's GetKeyState then follows the interactive session's live
    // keyboard, so such callers inject the state explicitly.
    std::function<ShelfPreviewModifiers()>modifiers;
};
// The default Shell activation (registered out-of-process stream preview
// handler for the name's extension), for services that delegate to it.
HRESULT createShelfPreviewHandler(const ehud::data::ShelfFileMetadata&,IPreviewHandler**)noexcept;
// Read-only, seekable stream for an explicitly selected ordinary file. Opens a
// no-recall/no-final-reparse handle, compares FILE_ID_INFO to the lease and
// retains it until the last stream/clone releases. Does not load a whole file.
// A cloud placeholder (classifyShelfObject) is the one exception: this explicit
// preview reopens the same identity with recall, so reads hydrate it.
// Individual Read requests are bounded to 16 MiB; oversized requests fail
// explicitly rather than truncating. File contents may change while leased.
HRESULT openShelfPreviewStream(const ehud::data::ShelfFileAccess&,IStream**)noexcept;

class NativeShelfFilePreview final {
public:
    // Caller owns STA COM initialization and the existing UI message pump.
    // No worker, timer, polling, renderer or default-app execution is added.
    explicit NativeShelfFilePreview(ShelfPreviewRoute,ShelfPreviewOptions={},ShelfPreviewServices={});
    ~NativeShelfFilePreview(); // creating-thread lifetime, before owner HWND
    NativeShelfFilePreview(const NativeShelfFilePreview&)=delete;
    NativeShelfFilePreview&operator=(const NativeShelfFilePreview&)=delete;
    // Queues only. Same active item toggles closed on the queued owner turn.
    // The moved store.access() lease remains held through handler Unload.
    bool request(std::string itemID,ehud::data::ShelfFileAccess);
    bool handleMessage(UINT_PTR routeGeneration,LPARAM notice);
    // Events are bounded/coalesced notices, never direct state/store callbacks.
    std::vector<ShelfPreviewEvent>drain(UINT_PTR routeGeneration);
    void close(bool restoreOwnerFocus=true);
    // The handler stopped responding (containment watchdog): forget the
    // current preview without calling into the handler again. Its leases and
    // stream file handle close now, this side's COM objects are disconnected,
    // the popup hides and the handler proxies are deliberately leaked (a
    // release could block on the hung server). Nothing is reported.
    void abandon()noexcept;
    void setRoute(ShelfPreviewRoute); // close/cancel before owner replacement
    // Call before TranslateMessage. Handles only this owned preview window or
    // its child windows: unmodified Escape/Space closes, Ctrl-R queues reveal.
    bool preTranslate(const MSG&);
    ShelfPreviewStats stats()const;
    HWND window()const;
private:
    struct State;std::shared_ptr<State>state_;
};
// Handler support is explicit: registered out-of-process IPreviewHandler plus
// IInitializeWithStream and IObjectWithSite are required. Missing/file-only
// handlers, folders and links show the generic preview when
// ShelfPreviewOptions::fallback is set, and otherwise report failure; no
// unrelated app is launched either way.
// Third-party handlers can block synchronous COM calls: production hosts this
// controller on a dedicated preview thread (ContainedShelfFilePreview) whose
// watchdog cancels and abandons a hung handler. Native visual/handler coverage
// requires actual Windows testing and is not inferred from injected fixtures.
} // namespace endfield::native
#endif
