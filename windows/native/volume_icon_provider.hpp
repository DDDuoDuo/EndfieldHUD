#pragma once
#include "native/audio_session_worker.hpp"
#include "native/shelf_icon_provider.hpp"
#include <functional>
#include <memory>
#include <span>

namespace endfield::native {
// The caller provides an ephemeral UUID for this exact process identity. It is
// never a Shelf record, persisted ID, PID substitution or name-derived token.
// Reuse the token while the process survives; allocate a new UUID for a new
// process. Metadata comes from the existing audio worker's discovery snapshot.
struct VolumeIconApplication {
    std::string applicationID,requestToken;
    std::optional<AudioApplicationExecutable> executable;
    bool operator==(const VolumeIconApplication&)const=default;
};
struct VolumeIconBinding {
    std::string applicationID,imageKey;std::uint64_t revision{};
    std::shared_ptr<const LayerMemoryImage>image;
    std::int32_t result{};bool typeFallback{};
    bool operator==(const VolumeIconBinding&)const=default;
};
struct VolumeIconResult {
    std::string imageKey,requestToken;std::uint64_t revision{};
    std::shared_ptr<const LayerMemoryImage>image;
    std::int32_t result{};bool typeFallback{};
};
// Pure content-event planner. Retains at most24 small immutable image handles,
// not copied pixel buffers; all pixels stay in the shared LayerImageSource
// lifetime budget. Visible work is bounded by the existing worker's8 requests.
// No file access, OS APIs, timer, worker, renderer or persistence exists here.
class VolumeIconPlan final {
public:
    static constexpr std::size_t maximumVisible=8,maximumRetained=24;
    VolumeIconPlan();
    // Validates the whole candidate before mutation. Explicit retry advances
    // icon revisions (including negative/type-only results); no timed retry.
    bool setVisible(std::span<const VolumeIconApplication>,bool retry=false);
    // Stale/nonvisible generations are ignored. A type-only Shell fallback is
    // intentionally not exposed as an app icon, preserving source nil artwork.
    bool receive(std::span<const VolumeIconResult>);
    bool hide()noexcept;
    std::span<const ShelfIconRequest>requests()const noexcept{return requests_;}
    std::span<const VolumeIconApplication>applications()const noexcept{return visible_;}
    std::span<const VolumeIconBinding>bindings()const noexcept{return bindings_;}
    const VolumeIconBinding*binding(std::string_view applicationID)const noexcept;
    std::uint64_t contentRevision()const noexcept{return contentRevision_;}
    std::size_t retainedCount()const noexcept{return retained_.size();}
    bool active()const noexcept{return active_;}
private:
    struct Entry {VolumeIconApplication app;VolumeIconBinding binding;std::uint64_t used{};};
    std::vector<Entry>retained_;std::vector<VolumeIconApplication>visible_;
    std::vector<ShelfIconRequest>requests_;std::vector<VolumeIconBinding>bindings_;
    std::uint64_t serial_{},nextRevision_{},contentRevision_{};bool active_{};
};
#ifdef _WIN32
// Borrow the ONE existing Shelf Shell worker and its cache. This facade never
// starts/stops a worker or owns its notice route. The shell owner must select
// exactly one request owner (Shelf or Volume), call hide before transferring
// ownership, and route drain to that active owner. Other modules' outgoing GPU
// artwork is retained independently; inactive consumers submit no work.
// Provider/cache outlive this facade. All calls use their creating thread.
class NativeVolumeIconProvider final {
public:
    NativeVolumeIconProvider(NativeShelfIconProvider&,LayerImageSource&);
    ~NativeVolumeIconProvider(); // cancel its active request set; never stop the shared worker
    NativeVolumeIconProvider(const NativeVolumeIconProvider&)=delete;
    NativeVolumeIconProvider&operator=(const NativeVolumeIconProvider&)=delete;
    bool setVisible(std::span<const VolumeIconApplication>,bool retry=false);
    bool drain(UINT_PTR routeGeneration);
    bool hide();
    const VolumeIconPlan&plan()const noexcept{return plan_;}
private:
    NativeShelfIconProvider*provider_;LayerImageSource*images_;DWORD owner_;
    VolumeIconPlan plan_;
    void onThread()const;
};

// Volume app-row icons on the host's shared utility queue (the built-in
// Volume owner of EndfieldHUD.exe). NativeShelfIconProvider allows one Shell
// icon worker per process and the Shelf owns it (started on its first
// showing, parked while hidden), so the Volume owner does not borrow it. This
// provider has no thread, timer, window route or polling of its own: one
// bounded job (at most the plan's eight visible requests) goes to the
// existing queue through `submit`, and its completion runs on the owner
// thread (the queue's drain): the 64 px pixels are published into images()
// at the plan's exact key and revision, and `changed` runs when a visible
// row's artwork changed (the owner then calls VolumePreview::iconsChanged and
// asks for a frame). One job at a time; requests that change meanwhile are
// submitted when it completes, and a full queue retries at
// queueCapacityAvailable(). An answered request is never extracted again for
// its revision (an explicit retry advances it); an icon evicted from the
// cache is asked for again on the next visibility event, as the shared
// facade above does. The default extractor (volumeExecutableIcon) opens the
// executable by its identity first, as the Shelf worker's lease does, so a
// path that now names another file gives no icon, and reads the file's own
// icon resource (SHDefExtractIcon: resource data only, no Shell icon handler
// or thumbnail extension is loaded). An executable without one keeps the
// source nil icon (the Mac shows only an application bundle's icon). Owner
// thread only; the extractor runs on the queue's worker with value-owned
// inputs and never touches this object.
struct VolumeQueuedIconStats {
    std::uint64_t jobs{},extractions{},published{},unavailable{},backpressure{};
    bool inFlight{};
};
class VolumeQueuedIconProvider final {
public:
    using Done=std::function<void(bool failed)>;
    // Queue one value-owned job; false when the queue is full (nothing ran).
    using Submit=std::function<bool(std::function<void()>work,Done done)>;
    using Extractor=std::function<ShelfIconPixels(const ShelfIconRequest&)>;
    VolumeQueuedIconProvider(Submit,std::function<void()>changed,Extractor={});
    ~VolumeQueuedIconProvider(); // a completion that arrives later does nothing
    VolumeQueuedIconProvider(const VolumeQueuedIconProvider&)=delete;
    VolumeQueuedIconProvider&operator=(const VolumeQueuedIconProvider&)=delete;
    bool setVisible(std::span<const VolumeIconApplication>,bool retry=false);
    bool hide();
    // The shared queue has room again: a job refused for a full queue is sent.
    void queueCapacityAvailable();
    const VolumeIconPlan&plan()const noexcept;
    LayerImageSource&images()noexcept;
    VolumeQueuedIconStats stats()const noexcept;
private:
    struct Impl;std::shared_ptr<Impl>impl_;
};
// The default extractor above: a 64 px straight-RGBA icon of the executable
// that still has the request's file identity, from its own icon resource.
// Failure (missing, replaced or unreadable file) and an executable without an
// icon resource (typeFallback) give no pixels.
ShelfIconPixels volumeExecutableIcon(const ShelfIconRequest&);
#endif
} // namespace endfield::native
