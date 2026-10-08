#pragma once
#include "modules/settings.hpp"
#include "core/motion.hpp"
#include "core/scene.hpp"
#include <array>
#include <span>
#include <vector>
namespace endfield::modules {
struct SettingsDisplay {std::string id,name,dimensions;bool operator==(const SettingsDisplay&)const=default;};
struct SettingsIcon {std::string id,title;bool operator==(const SettingsIcon&)const=default;};
struct SettingsCredit {std::string name,role,url;};
struct SettingsAbout {std::string name="EndfieldHUD",version="Development",author="DDDuoDuo",license="MIT",repository;std::vector<SettingsCredit>credits;};
struct SettingsViewHooks {std::function<std::vector<SettingsDisplay>()>displays;std::function<void()>chooseColor,chooseLogo,logoSelection;};
enum class SettingsPage {main,battery,icons,screens,languages,logos,metrics};
enum class SettingsRowKind {toggle,choice,slider,palette,icons,info,link,heading,selection};
struct SettingsRow {
    std::string id,title,value,detail,url;SettingsRowKind kind{};double height{40},number{},minimum{},maximum{};bool selected{},available{true};std::vector<SettingsIcon>icons;
};
struct SettingsAction {std::string id,label;core::Rect rect;bool enabled{true};};
struct SettingsSlider {std::string id,label,formatted;core::Rect rect;double value{},minimum{},maximum{};};
struct SettingsPageView {
    SettingsPage page{SettingsPage::main};std::string heading,status,accessibilityStatus,back,cancel,restoreTitle,restoreMessage,restoreAccept;std::vector<SettingsRow>rows;std::vector<SettingsAction>actions;std::vector<SettingsSlider>sliders;
    double scroll{},maximumScroll{};bool restore{},capturing{};
};
struct SettingsPagePose {double incomingX{},outgoingX{};core::Rect incomingMask{0,0,400,334},outgoingMask{};bool active{};};
struct SettingsRowPose {double x{},opacity{1};};
// Four retained instances share one preference controller. Only input/content
// changes build strings/vectors; sampling finite transitions is numeric only.
class SettingsPresentation final {
public:
    SettingsPresentation(core::Module,SettingsController&,std::vector<SettingsIcon>,SettingsAbout={},SettingsViewHooks={});
    void activate(double);void deactivate(double);void setReduceMotion(bool,double);bool refresh(double);
    bool mouseDown(core::Point,double);void mouseDragged(core::Point,double);void mouseUp(double);
    bool scroll(core::Point,double delta,double time);bool setSlider(std::string_view,double,double time);void nudgeSlider(double direction,double time);
    bool perform(std::string_view,double);bool escape(double);bool capture(SettingsShortcut,bool repeated,bool escape,double);
    void setCustomColor(std::array<double,3>,double);void setCustomLogo(std::string,double);void showImportError(std::string,double);void showBatterySettings(double);
    const SettingsPageView&view()const noexcept{return view_;}
    const std::optional<SettingsPageView>&outgoing()const noexcept{return outgoing_;}
    SettingsPagePose pagePose(double)const;SettingsRowPose rowPose(std::string_view,double)const;
    bool requiresFrames(double)const;bool dragging()const noexcept{return dragged_.has_value();}bool active()const noexcept{return active_;}bool reduceMotion()const noexcept{return reduced_;}
    std::optional<std::string_view>actionAt(core::Point)const;
    core::Module module()const noexcept{return module_;}std::uint64_t revision()const noexcept{return revision_;}
    static constexpr core::Rect bounds(){return {0,0,400,334};}static constexpr core::Rect viewport(){return {12,40,376,250};}
private:
    core::Module module_;SettingsController*controller_;std::vector<SettingsIcon>icons_;SettingsAbout about_;SettingsViewHooks hooks_;std::vector<SettingsDisplay>displays_;
    SettingsPageView view_;std::optional<SettingsPageView>outgoing_;SettingsPage page_{SettingsPage::main};double scroll_{},mainScroll_{},pageStart_{},direction_{1},rowStart_{};
    std::optional<std::string>dragged_,selectedSlider_;std::optional<double>stagedScale_;std::optional<std::array<double,2>>stagedPosition_;std::string rowAction_,localStatus_;
    bool active_{},reduced_{},restore_{},dirty_{true};std::uint64_t seenController_{},revision_{};
    std::string text(std::string_view,std::string_view)const;std::vector<SettingsRow>rows()const;void rebuild();void changePage(SettingsPage,int,double);void reveal(int,double,const std::function<void()>&);void animateRow(std::string_view,double);void moveSlider(core::Point,double);void settle();
};
struct SettingsAppearance {bool dark{true};double scale{2};std::array<double,4>accent{250./255,212./255,31./255,1};bool operator==(const SettingsAppearance&)const=default;};
struct SettingsSurface {std::string id,action;core::Matrix4 local;bool tint{},rim{};float opacity{1};};
struct SettingsArtworkPart {std::string row;ehud::data::Json layers;std::vector<SettingsSurface>surfaces;};
struct SettingsRequiredIcon {std::string sourceID,layerID;};
struct SettingsArtwork {SettingsArtworkPart chrome;std::vector<SettingsArtworkPart>rows;std::vector<SettingsRequiredIcon>icons;};
SettingsArtwork prepareSettingsArtwork(const SettingsPageView&,const SettingsAppearance& = {});
}
