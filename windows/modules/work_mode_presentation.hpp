#pragma once
#include "modules/work_mode.hpp"
#include "core/data/json.hpp"
#include "core/scene.hpp"
#include <array>
#include <span>
#include <vector>
namespace endfield::modules {
struct WorkModeStrings {
    std::string heading="工作模式",countdown="倒计时",stopwatch="秒表",pause="暂停",reset="重置",resume="继续",start="开始",custom="自定",ready="就绪",stopped="已结束",completed="已完成",invalidDuration="输入 0:01–1440:00（分:秒）",focusAccess="打开辅助功能设置";
    std::array<std::string,3>presets{"5 分","30 分","60 分"};
};
struct WorkModeAction {std::string id,label;core::Rect rect;bool operator==(const WorkModeAction&)const=default;};
struct WorkModeViewHooks {std::function<void(core::Rect)>editDuration;std::function<void()>requestFocusAccess;};
struct WorkModeLayout {double clockY{218.5},clockScale{1},phaseY{276},configurationOpacity{1};bool active{};};
struct WorkModeFeedback {std::string_view action;double scale{1};core::Matrix4 clock;double focusOpacity{1};bool active{};};
struct WorkModeRing {double fraction{1},rotation{},lineWidth{4};bool animated{};};
// Exact authored four-cubic path parameter endpoint/tangent. CAShapeLayer's
// strokeEnd is NOT linear polar angle. Normal/offset are centered at (220,220).
struct WorkModeRingCut {double endAngle{};std::array<double,3>endPlane{};};
WorkModeRingCut workModeRingCut(double fraction);
double workModeLayoutProgress(double normalizedTime);
class WorkModePresentation final {
public:
    WorkModePresentation(WorkModeController&,WorkModeStrings={},WorkModeViewHooks={});
    void activate(double);void deactivate(double);void setReduceMotion(bool,double);void refresh(double,bool animated=true);
    bool perform(std::string_view,double);bool setCustomDuration(std::string_view,double);void cancelCustomEditing(double);
    void setFocusStatus(std::string,bool needsPermission,double);
    WorkModeLayout layout(double)const;WorkModeFeedback feedback(double)const;WorkModeRing ring(double)const;
    bool requiresFrames(double)const;std::optional<std::string_view>actionAt(core::Point)const;
    std::span<const WorkModeAction>actions()const noexcept{return actions_;}
    std::span<const WorkModeAction>configurationActions()const noexcept{return configuration_;}
    const WorkModeStrings&strings()const noexcept{return strings_;}const WorkModeSnapshot&value()const noexcept{return snapshot_;}
    const std::string&timeText()const noexcept{return timeText_;}const std::string&phaseText()const noexcept{return phaseText_;}
    const std::string&focusStatus()const noexcept{return focus_;}bool focusPermission()const noexcept{return focusPermission_;}
    bool active()const noexcept{return active_;}bool reducedMotion()const noexcept{return reduced_;}
    std::uint64_t revision()const noexcept{return revision_;}std::uint64_t clockRevision()const noexcept{return clockRevision_;}
    std::string accessibilityStatus()const;
    static constexpr core::Rect bounds(){return {0,0,440,440};}static constexpr core::Rect durationEditorRect(){return {64,184,312,68};}
private:
    WorkModeController*controller_;WorkModeStrings strings_;WorkModeViewHooks hooks_;WorkModeSnapshot snapshot_;
    std::vector<WorkModeAction>actions_,configuration_;std::string timeText_,phaseText_,error_,focus_,feedbackAction_;
    WorkModeLayout layoutFrom_,layoutTo_;double layoutStart_{},actionStart_{},clockStart_{},focusStart_{},clockDirection_{1};
    bool active_{},reduced_{},layoutSet_{},layoutMoving_{},expanded_{},configurationInteractive_{true},focusPermission_{},actionFeedback_{},clockFeedback_{},focusFeedback_{};
    std::uint64_t seenRevision_{},revision_{},clockRevision_{};
    void rebuildActions();void animateAction(std::string_view,const WorkModeSnapshot&,bool,double);void settle();
};
struct WorkModeAppearance {bool dark{true};double scale{2};std::array<double,4>accent{250./255,212./255,31./255,1};bool operator==(const WorkModeAppearance&)const=default;};
struct WorkModeSurface {std::string id;core::Matrix4 local;std::string action;bool tint{},rim{};float opacity{1};};
struct WorkModePart {ehud::data::Json layers;std::vector<WorkModeSurface>surfaces;};
struct WorkModeArtwork {WorkModePart header,ringBase,ring,clock,phase,controls,configuration,focus;};
// Content events only. Dynamic ring, clock wrapper, button and layout transforms
// are numeric inputs above; no artwork generation occurs on the frame path.
WorkModeArtwork prepareWorkModeArtwork(const WorkModePresentation&,const WorkModeAppearance& = {});
}
