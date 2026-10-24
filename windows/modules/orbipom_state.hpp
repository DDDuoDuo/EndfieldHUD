#pragma once
#include "core/scene.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace endfield::modules {
enum class OrbiPomSkill {clear,wind,shake,swap};
int orbiPomEnergyCost(OrbiPomSkill)noexcept;
struct OrbiPomBody {
    std::int64_t id{};int level{};double x{},y{},angle{},size{},opacity{1},scale{1};
    bool operator==(const OrbiPomBody&)const=default;
};
// The original engine publishes these fields. No collision, scoring, skill or
// fixed-step approximation lives in this module. Negative body IDs are ghosts.
struct OrbiPomSnapshot {
    std::string state{"idle"};bool paused{};
    std::int64_t score{},highScore{},mergeCount{},skillUseCount{};int energy{},swapCharge{};
    double energyProgress{};int currentLevel{1},nextLevel{1};
    double previewX{115},previewY{-10},previewSize{19.2};bool previewVisible{};double previewScale{1};
    std::optional<double>dangerSeconds;std::optional<OrbiPomSkill>skill;std::optional<std::string>skillPhase;
    std::vector<std::int64_t>selectedBodyIDs;std::optional<std::int64_t>hoverBodyID;
    std::optional<double>windSurfaceY;std::vector<OrbiPomBody>bodies;double simulationTime{};
    bool isPlaying()const noexcept{return state=="playing";}
    bool canAdvance()const noexcept{return isPlaying()&&!paused;}
    bool canUse(OrbiPomSkill)const noexcept;
    bool operator==(const OrbiPomSnapshot&)const=default;
};
// An owning-thread adapter MUST execute bundled, unchanged Matter0.20.0 plus
// orbipom.js. This seam does not supply an engine, scheduler, IO or native host
// callbacks. Each result/error stays owned by the adapter until the next call.
class OrbiPomRuntimePort {
public:
    virtual ~OrbiPomRuntimePort()=default;
    virtual const OrbiPomSnapshot&snapshot()const noexcept=0;
    virtual const std::optional<std::string>&error()const noexcept=0;
    virtual void start(std::optional<std::uint32_t> seed)=0;
    virtual void advance(double seconds)=0;
    virtual void move(core::Point)=0;virtual void pointerUp(core::Point)=0;
    virtual bool drop()=0;virtual bool activate(OrbiPomSkill)=0;
    virtual void cancelSkill()=0;virtual void pause(bool)=0;virtual void highScore(std::int64_t)=0;
    // Memory trace only (owner thread, never per frame): bytes the VM heap and
    // its retained snapshot buffers hold. Zero when not reported.
    virtual std::size_t residentBytes()const{return 0;}
};
enum class OrbiPomEvent {started,restarted,finished};
struct OrbiPomSessionCallbacks {
    std::function<std::unique_ptr<OrbiPomRuntimePort>()>makeRuntime;
    // Owner persistence/event callbacks are synchronous and must not throw.
    std::function<void(std::int64_t)>saveBest;
    std::function<void(OrbiPomEvent)>event;
};
class OrbiPomSession final {
public:
    explicit OrbiPomSession(std::int64_t bestScore=0,OrbiPomSessionCallbacks={});
    const OrbiPomSnapshot&snapshot()const noexcept;
    const std::optional<std::string>&error()const noexcept{return error_;}
    std::int64_t bestScore()const noexcept{return best_;}
    bool hasRuntime()const noexcept{return bool(runtime_);}
    // The source keeps one VM for the app's lifetime once a game started (the
    // board survives hiding and reopening); the memory trace reads its size.
    std::size_t residentBytes()const{return runtime_?runtime_->residentBytes():0;}
    bool manuallyPaused()const noexcept{return manual_;}
    // OrbiPomSession.bestSoFar (MODS-228): the best including the current
    // game, from the stored snapshot only (never creates the runtime or runs
    // script); saveBest commits it. The Personal ID plate and the canvas's
    // BEST read it.
    std::int64_t bestSoFar()const noexcept;
    bool start(std::optional<std::uint32_t> seed={});void advance(double seconds);
    void setManuallyPaused(bool);void pause(bool);void saveBest();
    void move(core::Point);void pointerUp(core::Point);bool drop();bool activate(OrbiPomSkill);void cancelSkill();
private:
    OrbiPomSessionCallbacks callbacks_;std::unique_ptr<OrbiPomRuntimePort>runtime_;
    OrbiPomSnapshot idle_;std::optional<std::string>error_;std::int64_t best_{};
    bool manual_{},recordedFinish_{};
};
}
