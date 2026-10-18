#pragma once
#include "native/map_painter.hpp"
#include "app/utility_executor.hpp"

namespace endfield::native {
// Event/deadline owner equivalent to WorldMapRasterController. Borrowed executor
// must outlive this object. UI supplies ONE shared clock and handles nextWakeTime
// via OverlayHost's existing deadline. Never call advance from an idle timer.
class NativeMapRaster final {
public:
    using Clock=std::function<double()>;using Changed=std::function<void()>;
    NativeMapRaster(app::UtilityExecutor&,Clock,Changed,MapPaintFunction,
                    std::function<void()>releaseWorkerCaches={});
    ~NativeMapRaster();
    NativeMapRaster(const NativeMapRaster&)=delete;
    NativeMapRaster&operator=(const NativeMapRaster&)=delete;
    void setGeography(std::shared_ptr<const modules::MapGeography>,double time);
    void configure(bool dark,modules::MapColor accent,double contentsScale,double time);
    void setActive(bool,double time);
    void update(modules::MapViewport,bool animated,double time);
    void finishGesture(double time);
    // Shared deadline delivery, and explicit retry after another executor route
    // drains. A queue-full request retains only the latest camera, no retry loop.
    bool advance(double time);bool submitPending(double time);
    std::optional<double>nextWakeTime()const noexcept;
    bool settled()const noexcept;
    const std::optional<modules::MapRasterFrame>&frame()const noexcept;
    std::shared_ptr<const MapPaintImage>detail()const noexcept;
    std::shared_ptr<const MapPaintImage>backdrop()const noexcept;
    struct Stats {std::uint64_t submitted{},completed{},published{},discarded{},failed{},backpressure{},cacheReleases{};bool painting{},waiting{},active{},interacting{};double cooldown{.125};};
    Stats stats()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
