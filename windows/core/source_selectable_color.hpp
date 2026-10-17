#pragma once
#include "core/source_watch_layout.hpp"
#include <array>
#include <optional>
#include <span>

namespace endfield::core::source {
using SelectableTint = std::array<float,4>;
using SelectableTints = std::map<std::string,SelectableTint,std::less<>>;
enum class SelectableState : unsigned { normal,highlighted,pressed,selected,disabled };
struct SelectableColorBlock {
    std::array<SelectableTint,5> states{};
    float multiplier{1};
    double fadeDuration{}; // Double(Float(serialized duration)), as in Swift
    SelectableTint color(SelectableState) const;
};
struct SelectableColorBinding {
    std::string buttonNodeID,buttonComponentID,targetGraphicID,targetNodeID;
    bool sourceInteractable{true};
    SelectableColorBlock colors;
};

// Exact HUDSourceSelectableColor, with construction-time ID resolution and a
// retained output map. Host time is monotonic-clamped; no timer, clock read,
// JSON, allocation or serialized Graphic.m_Color mutation occurs in sampling.
// A shared target node owns ONE tween: the most recent button transition wins.
class SelectableColor final {
public:
    static constexpr std::size_t maximumBindings=4096,maximumComponents=65536;
    SelectableColor(const SceneDefinition&,const MountedLayoutDocument&);
    SelectableColor(const SelectableColor&)=delete;
    SelectableColor& operator=(const SelectableColor&)=delete;
    std::span<const SelectableColorBinding> bindings() const noexcept{return bindings_;}
    std::span<const std::string> instanceIDs() const noexcept{return instanceIDs_;}
    std::span<const std::string> ignoredNullTargetButtonIDs() const noexcept{return ignored_;}
    std::optional<SelectableState> state(std::string_view button) const noexcept;
    void reset(double time);
    void setState(SelectableState,std::string_view button,double time,bool reduceMotion=false);
    void setEnabled(bool,std::string_view button,double time);
    // Reference and keys remain stable; values update in-place at the next call.
    const SelectableTints& colors(double time,bool reduceMotion=false);
    bool requiresFrames(double time) const noexcept;
private:
    struct Tween {
        SelectableTint start{},target{};
        double started{},duration{};
        float progress(double time) const noexcept;
        SelectableTint color(double time) const noexcept;
    };
    struct Instance {SelectableState state{};bool enabled{true};std::size_t renderer{};};
    std::vector<SelectableColorBinding> bindings_;
    std::vector<std::string> instanceIDs_,ignored_;
    std::map<std::string,std::size_t,std::less<>> byButton_;
    std::vector<Instance> instances_;
    std::vector<Tween> renderers_;
    SelectableTints output_;
    std::vector<SelectableTints::iterator> outputSlots_;
    std::optional<double> clock_;
    std::optional<double> advance(double) noexcept;
    void assign(std::size_t,SelectableTint,double) noexcept;
};

// Exact exported mounted-card membership; root/button identities must come from
// the selected Mac card metadata, not guessed names or asset-ID constants.
struct DesktopHoverProfile {
    std::string rootID;
    std::vector<std::string> buttonIDs,nodeIDs;
    static std::optional<DesktopHoverProfile> fromJson(const Json& profileHover);
};
class DesktopHoverFeedback final {
public:
    static constexpr float sideEdgeOpacity=.18f;
    DesktopHoverFeedback(const SceneDefinition&,const MountedLayoutDocument&,
        const SelectableColor&,std::optional<DesktopHoverProfile> profile={});
    DesktopHoverFeedback(const DesktopHoverFeedback&)=delete;
    DesktopHoverFeedback& operator=(const DesktopHoverFeedback&)=delete;
    const std::set<std::string,std::less<>>& sideEdgeIDs() const noexcept{return sideEdges_;}
    const std::set<std::string,std::less<>>& profileButtonIDs() const noexcept{return profileButtons_;}
    const std::optional<std::string>& profileRootID() const noexcept{return profileRoot_;}
    const std::optional<std::string>& profileHighlightNodeID() const noexcept{return profileHighlight_;}
    // The returned ungrouped view borrows the caller's input ID; grouped view
    // borrows this object. Neither path allocates or mutates selection state.
    std::optional<std::string_view> groupedButton(std::optional<std::string_view>) const noexcept;
    const std::map<std::string,float,std::less<>>& opacities(const SelectableTints&);
private:
    using Opacities=std::map<std::string,float,std::less<>>;
    struct Target {std::string targetNodeID;SelectableColorBlock colors;};
    struct Decoration {Opacities::iterator output;float authoredAlpha;};
    struct Quit {Opacities::iterator output;Target target;};
    std::set<std::string,std::less<>> sideEdges_,profileButtons_;
    std::optional<std::string> profileRoot_,profileHighlight_;
    std::optional<Target> profile_;
    Opacities output_;
    std::vector<Decoration> decorations_;
    std::vector<Quit> quit_;
    static float progress(const Target&,const SelectableTints&) noexcept;
};
} // namespace endfield::core::source
