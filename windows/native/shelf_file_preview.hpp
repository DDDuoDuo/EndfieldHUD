#pragma once
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
};
enum class ShelfPreviewNotice:LPARAM {work=1,events=2};
enum class ShelfPreviewEventKind {opened,closed,failed,revealRequested};
struct ShelfPreviewEvent {
    ShelfPreviewEventKind kind{};HRESULT result{S_OK};std::string itemID;
};
struct ShelfPreviewStats {
    bool queued{},active{},visible{};std::uint64_t requests{},opens{},closes{},failures{},notices{};
};
struct ShelfPreviewServices {
    // Independent provider callbacks; never capture a Store or UI object. The
    // controller calls these only after its queued request leaves store/state
    // busy callbacks. Defaults use NativeFileShelfFiles, Shell associations and
    // a read-only native IStream over a separately identity-checked handle.
    std::function<ehud::data::ShelfFileAccess(const ehud::data::ShelfRecord&)>resolve;
    std::function<HRESULT(const ehud::data::ShelfFileMetadata&,IPreviewHandler**)>createHandler;
    std::function<HRESULT(const ehud::data::ShelfFileAccess&,IStream**)>openStream;
};
// Read-only, seekable stream for an explicitly selected ordinary file. Opens a
// no-recall/no-final-reparse handle, compares FILE_ID_INFO to the lease and
// retains it until the last stream/clone releases. Does not load a whole file.
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
// handlers, folders and links report failure; no unrelated app is launched.
// Third-party handlers can block synchronous COM calls; no forced abort or
// unbounded replacement service is created. Native visual/handler coverage
// requires actual Windows testing and is not inferred from injected fixtures.
} // namespace endfield::native
#endif
