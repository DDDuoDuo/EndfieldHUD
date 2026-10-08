#pragma once
#include "native/audio_session_worker.hpp"
#include "native/shelf_icon_provider.hpp"

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
#endif
} // namespace endfield::native
