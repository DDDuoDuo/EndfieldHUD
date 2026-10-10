#pragma once
#include "modules/media_assembly_model.hpp"

namespace endfield::modules {
struct MediaAssemblyDocumentInfo {
    std::string id;bool video{};core::Point pixels;double duration{};
};
// A borrowed controller snapshot. The presentation never copies source media,
// stores edits separately, starts a worker, or owns a playback clock.
struct MediaAssemblyView {
    const MediaAssemblyDocumentInfo*document{};
    const MediaAssemblyAdjustments*adjustments{};
    core::Point previewPixels;
    bool busy{},exporting{},playing{};double currentTime{};
    bool editable()const noexcept{return document&&adjustments&&!busy&&!exporting;}
};
enum class MediaAssemblyDrawer {tools,filters,stickers};
enum class MediaAssemblyTool {crop,adjust,curves,levels,trim};
struct MediaAssemblyAction {std::string id,title;core::Rect rect;bool enabled{},selected{},draw{true};};
struct MediaAssemblyParameter {std::string_view id,english,chinese;double low{},high{},value{},step{.01};};
struct MediaAssemblyParameters {
    std::array<MediaAssemblyParameter,8>values;std::size_t count{};
    std::span<const MediaAssemblyParameter>items()const noexcept{return {values.data(),count};}
};
enum class MediaAssemblyCommand {none,adjust,trim,seek,play,open,exportMedia,cancelExport,closeMedia,reset};
struct MediaAssemblyRequest {
    bool consumed{};MediaAssemblyCommand command{MediaAssemblyCommand::none};
    std::optional<MediaAssemblyAdjustments>adjustments;double time{};
};
class MediaAssemblyPresentation final {
public:
    static constexpr core::Rect bounds{0,0,440,440},previewRect{12,42,416,312},
        seekRect{49,335,306,17},presetRect{19,84,173,234},inlineRect{25,120,161,191},trimRect{30,164,151,24};
    void synchronize(const MediaAssemblyView&); // source identity changes only
    void hide()noexcept;
    const MediaAssemblyViewport&viewport()const noexcept{return viewport_;}
    std::optional<MediaAssemblyDrawer>drawer()const noexcept{return drawer_;}
    std::optional<MediaAssemblyTool>tool()const noexcept{return tool_;}
    std::string_view selectedSticker()const noexcept{return selected_;}
    double drawerOffset()const noexcept{return drawerOffset_;}
    bool dragging()const noexcept{return drag_.kind!=DragKind::none||seeking_;}
    std::optional<double>seekFraction()const noexcept{return seek_;}
    core::Point imageSize(const MediaAssemblyView&)const noexcept;
    core::Rect imageRect(const MediaAssemblyView&)const noexcept;
    core::Rect drawerBounds()const noexcept;
    core::Rect cropRect(const MediaAssemblyView&)const noexcept;
    std::array<core::Point,4>cropHandles(const MediaAssemblyView&)const noexcept;
    std::array<core::Point,6>stickerHandles(const MediaAssemblySticker&,const MediaAssemblyView&)const;
    MediaAssemblyParameters parameters(const MediaAssemblyView&)const noexcept;
    double maximumDrawerOffset(const MediaAssemblyView&)const noexcept;
    std::vector<MediaAssemblyAction>actions(const MediaAssemblyView&,core::Language)const;
    MediaAssemblyRequest perform(std::string_view,const MediaAssemblyView&,std::string newStickerID={});
    MediaAssemblyRequest parameter(std::string_view,double,const MediaAssemblyView&)const;
    MediaAssemblyRequest trim(bool beginning,double,const MediaAssemblyView&)const;
    MediaAssemblyRequest adjustSelected(double dx,double dy,double size,double rotation,const MediaAssemblyView&)const;
    MediaAssemblyRequest pointerDown(core::Point,const MediaAssemblyView&,core::Language,std::string newStickerID={});
    MediaAssemblyRequest pointerDrag(core::Point,const MediaAssemblyView&);
    MediaAssemblyRequest pointerUp(const MediaAssemblyView&)noexcept;
    bool zoom(double factor,core::Point,const MediaAssemblyView&)noexcept;
    bool scroll(core::Point,double dx,double dy,bool zoom,const MediaAssemblyView&)noexcept;
private:
    enum class DragKind {none,pan,parameter,trim,crop,move,scale,rotate};
    struct Drag {DragKind kind{};core::Point point;core::Rect crop;unsigned corner{};bool beginning{};double scalar{};std::string parameter;MediaAssemblySticker sticker;};
    MediaAssemblyViewport viewport_;std::optional<MediaAssemblyDrawer>drawer_;
    std::optional<MediaAssemblyTool>tool_;std::string sourceID_,selected_;
    double drawerOffset_{};bool seeking_{};std::optional<double>seek_;Drag drag_;
    const MediaAssemblySticker*selected(const MediaAssemblyView&)const noexcept;
    MediaAssemblyRequest replace(const MediaAssemblySticker&,const MediaAssemblyView&)const;
    MediaAssemblyRequest dragParameter(std::string_view,core::Point,const MediaAssemblyView&)const;
    MediaAssemblyRequest dragTrim(bool,core::Point,const MediaAssemblyView&)const;
};
}
