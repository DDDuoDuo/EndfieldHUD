#pragma once
#include "core/source_animation.hpp"
#include <memory>
#include <optional>
#include <span>

namespace endfield::core::source {
enum class ButtonState : unsigned { normal, highlighted, pressed, disabled };
std::string_view buttonStateName(ButtonState) noexcept;
std::optional<ButtonState> buttonState(std::string_view) noexcept;
struct AnimatorBinding {
    std::string rootID;
    std::array<std::string,4> boundClipIDs;
    static std::vector<AnimatorBinding> fromJson(const Json& animatorArray);
    // Current packets omit controller_instances. This preserves transition
    // instance order and requires exactly one bound clip identity per state.
    // Its equivalence is tested against the original clips.json instance list.
    static std::vector<AnimatorBinding> fromTransitions(const Json& transitionData);
    bool operator==(const AnimatorBinding&) const = default;
};
// Exact finite Animator playback of HUDSourceWatchButtonAnimation. Scene and
// library must outlive it. Calls use the host's monotonic animation time; this
// owns no timer and requests no OS events. Four cached state samples per button,
// plus at most one immutable interrupted-blend snapshot per active instance.
class ButtonAnimation final {
public:
    ButtonAnimation(const SceneDefinition&,const Library&,std::span<const AnimatorBinding>,const Json& transitionData);
    ~ButtonAnimation();
    ButtonAnimation(ButtonAnimation&&) noexcept;
    ButtonAnimation& operator=(ButtonAnimation&&) noexcept;
    ButtonAnimation(const ButtonAnimation&)=delete;
    ButtonAnimation& operator=(const ButtonAnimation&)=delete;
    std::span<const std::string> instanceIDs() const noexcept;
    std::optional<ButtonState> state(std::string_view nodeID) const noexcept;
    std::uint64_t stateGeneration() const noexcept;
    bool requiresFrames(double time);
    void setState(ButtonState,std::string_view nodeID,double time,bool reduceMotion=false);
    void setHovered(bool,std::string_view nodeID,double time,bool reduceMotion=false);
    void apply(Pose&,double time,bool reduceMotion=false);
    void reset(double time,bool reduceMotion=false);
    std::uint64_t sampledCurveCount() const noexcept;
    std::size_t cachedStateCount() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct DomainState {
    std::optional<std::string> currentLevelID;
    std::optional<double> selectionElapsed;
    std::map<std::string,double,std::less<>> hoverClipTimes;
    double ambientTime{};
};
// Source level wrapper scheduling only; the caller provides the already
// assembled Domain scene, loaded level IDs, and original level-clips.json.
// No domain assembly, account inference, or invented hover scheduling occurs.
class DomainAnimation final {
public:
    DomainAnimation(const Json& source,const SceneDefinition&,std::string domainName,
                    std::set<std::string,std::less<>> loadedLevelIDs);
    ~DomainAnimation();
    DomainAnimation(DomainAnimation&&) noexcept;
    DomainAnimation& operator=(DomainAnimation&&) noexcept;
    DomainAnimation(const DomainAnimation&)=delete;
    DomainAnimation& operator=(const DomainAnimation&)=delete;
    Pose pose(const DomainState&,const Overrides& overrides={}) const;
    std::size_t bindingCount() const noexcept;
    std::size_t autoLoopCount() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::core::source
