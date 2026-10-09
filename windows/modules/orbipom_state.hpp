#pragma once
#include "core/scene.hpp"
#include <array>
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
    bool manuallyPaused()const noexcept{return manual_;}
    bool start(std::optional<std::uint32_t> seed={});void advance(double seconds);
    void setManuallyPaused(bool);void pause(bool);void saveBest();
    void move(core::Point);void pointerUp(core::Point);bool drop();bool activate(OrbiPomSkill);void cancelSkill();
private:
    OrbiPomSessionCallbacks callbacks_;std::unique_ptr<OrbiPomRuntimePort>runtime_;
    OrbiPomSnapshot idle_;std::optional<std::string>error_;std::int64_t best_{};
    bool manual_{},recordedFinish_{};
};
inline constexpr core::Rect orbiPomBoard{87,88,253,308},orbiPomPlayArea{87,55,253,341};
core::Point orbiPomWorld(core::Point)noexcept;
struct OrbiPomPlacement {core::Point center;double size{},angle{},opacity{};bool operator==(const OrbiPomPlacement&)const=default;};
OrbiPomPlacement orbiPomPlacement(const OrbiPomBody&)noexcept;
enum class OrbiPomAction {start,pause,restart,cancelRestart,confirmRestart,cancelSkill,clear,wind,shake,swap,rules};
struct OrbiPomActionHit {OrbiPomAction action{};core::Rect rect;bool enabled{};};
struct OrbiPomActions {std::array<OrbiPomActionHit,8>items{};std::size_t count{};};
enum class OrbiPomKey {escape,space,left,right,down,up,pause,clear,wind,shake,swap};
// Borrowed session survives canvas/HUD recreation. Host routes shared frame
// cadence only while requiresFrames(); hide/foreground loss resets its elapsed
// baseline. Menus/rendering/accessibility use the same fixed actions and state.
class OrbiPomState final {
public:
    explicit OrbiPomState(OrbiPomSession&);~OrbiPomState();
    OrbiPomState(const OrbiPomState&)=delete;OrbiPomState&operator=(const OrbiPomState&)=delete;
    void setPresented(bool);void setActive(bool);void setForeground(bool);void setRulesPresented(bool);
    bool active()const noexcept{return active_;}bool presented()const noexcept{return presented_;}
    bool rulesPresented()const noexcept{return rules_;}bool restartConfirmation()const noexcept{return restart_;}
    bool requiresFrames()const noexcept;bool boardDimmed()const noexcept;
    OrbiPomActions actions()const noexcept;
    bool perform(OrbiPomAction);void advance(double seconds);
    void move(core::Point);bool down(core::Point);void up(std::optional<core::Point>);
    bool key(OrbiPomKey,bool repeated=false,bool commandControlOrAlt=false);
    core::Point keyboardPoint()const noexcept{return keyboard_;}
private:
    OrbiPomSession&session_;bool presented_{},active_{},foreground_{true},rules_{},restart_{},pointerDown_{};
    core::Point keyboard_{115,120};void reconcile();bool shouldRun()const noexcept;
};
}
