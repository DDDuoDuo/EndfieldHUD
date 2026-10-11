#pragma once
#include "native/shelf_file_preview.hpp"
#ifdef _WIN32
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace endfield::native {
// Bounds of the dedicated preview thread (ContainedShelfFilePreview).
struct ShelfPreviewContainment {
    // Longest single blocking operation of a preview before its handler counts
    // as hung: one handler call or activation, the reference resolution, the
    // stream opening, a proxy release or the popup teardown. Every access of
    // the handler to the preview stream restarts it (progress, for example a
    // cloud placeholder hydrating), so only a stalled operation expires.
    unsigned timeoutMilliseconds{10000};
    // Retired preview threads that are still blocked (a hang outside COM, which
    // cannot be cancelled). While this many remain, a new preview is refused
    // (reported `failed` with ERROR_BUSY) instead of starting another thread.
    unsigned maximumBlocked{2};
};
struct ShelfPreviewContainmentStats {
    std::uint64_t threads{},timeouts{},cancellations{},refusals{};
    unsigned blocked{}; // retired preview threads that have not ended
    DWORD thread{};     // the current preview thread (0 before the first preview)
    bool busy{};        // a watched operation is running on it
    HRESULT lastCancel{S_OK}; // result of the latest CoCancelCall (diagnostics)
    int retiredStage{};       // how far the latest retired thread got (diagnostics):
                              // 1 call returned, 2 handler abandoned, 3 pump left, 4 controller gone, 5 ended
};
// The Shell preview (Quick Look equivalent, NativeShelfFilePreview) contained
// on a dedicated STA thread with its own message pump, so a third-party
// preview handler that hangs cannot freeze the HUD:
//  - The facade lives on the HUD thread (the route's owner thread) and never
//    waits for the preview thread while it works: request() and close() only
//    queue commands; events come back as the route's private notice
//    (ShelfPreviewNotice::events) and are drained exactly like the
//    single-thread controller's.
//  - The preview thread owns the controller, its message-only notice window
//    and the popup. The popup is an unowned top-level tool window centred on
//    the route's owner (ShelfPreviewOptions::anchor): a cross-thread owner or
//    parent would attach the two threads' input queues. Its keys (Escape,
//    Space, Ctrl-R) are translated on the preview thread; preTranslate() on
//    the HUD thread therefore never claims a message.
//  - Watchdog: every blocking operation (ShelfPreviewServices::watch) arms one
//    one-shot thread-pool timer for timeoutMilliseconds; it is never periodic
//    and is disarmed when the operation returns. On expiry the preview thread
//    is retired: the pending COM call is cancelled (CoEnableCallCancellation on
//    the preview thread, CoCancelCall from the timer), the controller abandons
//    the handler (never calls it again, leaks its proxies, closes the leases
//    and the stream's file handle), the thread ends on its own, and `failed`
//    is reported with HRESULT_FROM_WIN32(ERROR_TIMEOUT). The next request
//    starts a fresh preview thread. A thread blocked outside COM is left
//    behind (bounded by maximumBlocked) and never awaited.
//  - A request or close while an operation runs cancels that COM call at once
//    (the user moved on); a repeated request for the item being opened or
//    shown toggles it closed, as the single-thread controller does.
//  - Destruction asks the preview thread to close and end, cancels a running
//    call and waits at most 3 seconds; a thread that does not end is left.
// Services run on the preview thread: resolve/describe/createHandler/
// openStream must be thread-safe and independent of the HUD's objects (the
// defaults are). Leases move to the preview thread with their request.
class ContainedShelfFilePreview final {
public:
    ContainedShelfFilePreview(ShelfPreviewRoute,ShelfPreviewOptions={},ShelfPreviewServices={},ShelfPreviewContainment={});
    ~ContainedShelfFilePreview();
    ContainedShelfFilePreview(const ContainedShelfFilePreview&)=delete;
    ContainedShelfFilePreview&operator=(const ContainedShelfFilePreview&)=delete;
    // Validates on the calling (HUD) thread exactly like NativeShelfFilePreview
    // and queues; starts the preview thread on first use.
    bool request(std::string itemID,ehud::data::ShelfFileAccess);
    bool handleMessage(UINT_PTR routeGeneration,LPARAM notice);
    std::vector<ShelfPreviewEvent>drain(UINT_PTR routeGeneration);
    void close(bool restoreOwnerFocus=true);
    bool preTranslate(const MSG&)const noexcept{return false;}
    ShelfPreviewStats stats()const;
    ShelfPreviewContainmentStats containment()const;
    HWND window()const; // the popup (owned by the preview thread), if any
    // Tests and hidden coverage: waits (bounded) until the preview thread has
    // taken every command, finished its work and posted its notices; true when
    // it is idle. The caller then delivers the route's notices.
    bool settle(DWORD milliseconds);
    // Tests: waits (bounded) until every retired preview thread has ended.
    bool settleRetired(DWORD milliseconds);
    struct Shared;struct Worker;
private:
    std::shared_ptr<Shared>shared_;
    HRESULT start();
};
} // namespace endfield::native
#endif
