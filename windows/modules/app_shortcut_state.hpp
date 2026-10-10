#pragma once
#include "modules/app_shortcut_model.hpp"
#include "core/scene.hpp"
#include <array>
#include <span>

namespace endfield::modules {
inline constexpr core::Rect shortcutListRect{12,44,376,252};
inline constexpr core::Rect shortcutNameRect{78,77,304,27};
inline constexpr std::array<std::string_view,14>shortcutIconOrder{
    "original","bolt","star","terminal","globe","folder","music","play",
    "brush","code","game","camera","grid","textBubble"};
core::Rect shortcutIconRect(std::size_t);
enum class ShortcutActionKind {choose,name,icon,cancel,save,launch,edit,remove};
struct ShortcutAction {
    ShortcutActionKind kind{};std::string value;core::Rect rect;
    bool operator==(const ShortcutAction&)const=default;
};
struct ShortcutScreenTransition {
    std::uint64_t generation{};double start{};int direction{1};bool active{};
    bool operator==(const ShortcutScreenTransition&)const=default;
};
struct ShortcutPresetTransition {
    std::uint64_t generation{};double start{};int direction{1};std::string icon;
};
// Source AppShortcutCanvas state only. OS selection/reinspection, atomic store
// commits, native icons and close-first application activation remain caller
// actions. A rejected operation reports an error without changing the draft.
// All changes are discrete events; sample/idle and pointer hits allocate nothing.
class AppShortcutState final {
public:
    explicit AppShortcutState(ShortcutFile={},std::optional<std::string>error={});
    void setActive(bool,double time);void setReducedMotion(bool,double time);
    bool active()const noexcept{return active_;}bool editing()const noexcept{return candidate_.has_value();}
    bool transitioning()const noexcept{return transition_.active;}
    const ShortcutFile&file()const noexcept{return file_;}
    const std::optional<ShortcutCandidate>&candidate()const noexcept{return candidate_;}
    const std::optional<std::string>&editingID()const noexcept{return editingID_;}
    const std::string&draftName()const noexcept{return name_;}
    const std::string&draftIcon()const noexcept{return icon_;}
    const std::optional<std::string>&error()const noexcept{return error_;}
    double scrollOffset()const noexcept{return scroll_;}double maximumScroll()const noexcept;
    std::uint64_t revision()const noexcept{return revision_;}
    const ShortcutScreenTransition&transition()const noexcept{return transition_;}
    const ShortcutPresetTransition&presetTransition()const noexcept{return preset_;}
    bool dropTarget()const noexcept{return dropTarget_;}
    void replaceItems(ShortcutFile);void showError(std::optional<std::string>);
    void beginDraft(ShortcutCandidate,std::optional<std::string>editingID,double time);
    void cancelDraft(double time);void setDraftName(std::string);
    bool selectIcon(std::string_view,double time);bool setDropTarget(bool);
    // Called only after the injected durable transaction succeeds.
    void saved(ShortcutFile,double time);
    bool scroll(core::Point,double delta);
    std::optional<core::Rect>rawCardRect(std::string_view)const noexcept;
    std::optional<core::Rect>cardRect(std::string_view)const noexcept;
    std::span<const ShortcutAction>actions()const noexcept{return actions_;}
    const ShortcutAction*hit(core::Point)const noexcept;
    // Arrival completion requests the shared name editor once. Deactivation,
    // cancellation or a newer transition invalidates it before delivery.
    bool advance(double time);bool takeNameRequest()noexcept;
    void cancelTransitions();bool requiresFrames(double time)const noexcept;
private:
    ShortcutFile file_;std::optional<ShortcutCandidate>candidate_;
    std::optional<std::string>editingID_,error_;std::string name_,icon_{"original"};
    std::vector<ShortcutAction>actions_;double scroll_{},time_{};
    bool active_{},reduced_{},dropTarget_{},nameAfter_{},nameRequested_{};
    std::uint64_t revision_{};ShortcutScreenTransition transition_;ShortcutPresetTransition preset_;
    void clock(double);void changed();void rebuildActions();void transition(int,bool,double);
};
}
