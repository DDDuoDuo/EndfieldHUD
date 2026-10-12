#pragma once
#include "core/data/file_shelf_store.hpp"
#include "native/layer_image_source.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace endfield::native {
// Trimmed immutable request; unknown persisted JSON never crosses to the worker.
// The owner advances revision when path/identity/unavailable/type changes.
struct ShelfIconRequest {
    std::string imageKey;std::uint64_t revision{};
    std::string itemID,path;ehud::data::ShelfFileIdentity identity;
    bool directory{},unavailable{};
    bool operator==(const ShelfIconRequest&)const=default;
};
// Pure bounded validation, also available to import/presentation tests on other
// platforms. Optional volume UUID follows the same UUID contract as the store.
bool validShelfIconRequest(const ShelfIconRequest&)noexcept;
#ifdef _WIN32
struct ShelfIconRoute {HWND owner{};UINT message{};UINT_PTR generation{};};
struct ShelfIconPixels {
    unsigned width{},height{};std::vector<std::uint8_t>straightRGBA;
    HRESULT result{E_FAIL};bool typeFallback{};
};
struct ShelfIconCompletion {
    std::string imageKey,itemID;std::uint64_t revision{};
    std::shared_ptr<const LayerMemoryImage>image;
    HRESULT result{E_FAIL};bool typeFallback{};
};
struct ShelfIconProviderStats {
    std::size_t queued{},completed{};bool inFlight{},stopped{};
    std::uint64_t requests{},extractions{},wakeups{},notices{},discarded{};
    // Worker threads not yet finished (0 while parked), threads started over
    // the provider's lifetime and park() calls.
    std::size_t workers{};std::uint64_t workerStarts{},parks{};
};
// One STA worker, reused across HUD hide/show. No application window, timer,
// polling, thumbnail request, renderer, extra pixel cache or user-file
// enumeration. Work/results are bounded to eight visible requests; owner-
// thread drain publishes into the supplied source-capacity24 image cache.
// park() (the Shelf hidden or the HUD closed) lets an idle worker end its
// thread, which releases its COM apartment, WIC factory and the Shell icon
// handlers it loaded; the next setVisible that needs an extraction starts a
// new worker on the same provider, cache and route (the cached icons are
// kept, as FileShelfCanvas keeps its 24 recent icons). A worker parked while
// it extracts finishes that call first; a setVisible before it decided to
// end keeps it serving, so at most one worker ever runs handler code.
// The resolver MUST independently own its dependencies and be worker-safe;
// NativeFileShelfFiles::platform().resolve satisfies that contract. Never bind
// UI-thread FileShelfStore::access or capture HUD/cache/store pointers in it.
//
// Shell extraction may load third-party in-process icon handlers, which can
// inspect file contents or hang. ICONONLY excludes thumbnails, not handler
// content access. Neither hide nor stop forcibly aborts such code. Stop clears
// the route/queues synchronously and returns without waiting. The worker owns
// its resolver, lease and shared state until it returns; at most one worker is
// allowed process-wide, including a stopped but still blocked worker. A new
// provider fails while that worker remains alive rather than accumulating
// detached threads. No TerminateThread, forced COM unload or hang timeout.
// Thus a hung handler can retain one worker/lease until process exit; hard
// cancellation/crash isolation requires a separate helper-process design.
class NativeShelfIconProvider final {
public:
    static constexpr std::size_t maximumVisible=8;
    using Resolver=std::function<ehud::data::ShelfFileAccess(const ehud::data::ShelfRecord&)>;
    // Optional injected worker-only extractor, for bounded synthetic tests.
    // A valid lease pointer remains open throughout this callback. nullptr
    // means unavailable/failed resolve and permits type-only artwork only.
    using Extractor=std::function<ShelfIconPixels(const ShelfIconRequest&,const ehud::data::ShelfFileAccess*)>;
    NativeShelfIconProvider(LayerImageSource&,Resolver,ShelfIconRoute={},Extractor={});
    ~NativeShelfIconProvider(); // nonblocking stop; no owner callback afterward
    NativeShelfIconProvider(const NativeShelfIconProvider&)=delete;
    NativeShelfIconProvider&operator=(const NativeShelfIconProvider&)=delete;
    // Content/visibility event only. Latest set replaces queued/results work;
    // unchanged sets do nothing unless owner explicitly requests a retry.
    bool setVisible(std::span<const ShelfIconRequest>,bool retry=false);
    void hide(); // cancel generation, retain the worker/cache for later show
    // hide(), then let the idle worker end (no wait); the cache stays. The
    // next setVisible with a request missing from the cache starts a worker.
    void park();
    void setRoute(ShelfIconRoute); // clear old route before destroying its HWND
    // Coalesced WM_APP message is wParam=route.generation,lParam=0; no pointers.
    // Ignore stale route generations. No callbacks execute on either thread.
    std::vector<ShelfIconCompletion>drain(UINT_PTR routeGeneration);
    void stop()noexcept;
    ShelfIconProviderStats stats()const;
    // Explicit optional shutdown/test observation of the latest worker thread;
    // caller owns CloseHandle. Waiting is never done by this provider or its
    // destructor.
    HANDLE duplicateWorkerHandle()const;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
} // namespace endfield::native
