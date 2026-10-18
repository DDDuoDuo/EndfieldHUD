#pragma once
#include "modules/notes_drawing.hpp"
#include "modules/notes_media_presentation.hpp"
#include <memory>
#include <optional>

namespace endfield::modules {
// Validated original Notes common media metadata plus the existing Windows
// locator. Session-only: no Notes row, timestamp, file copy or new persistence.
class ProjectionMediaReference final {
public:
    static std::shared_ptr<const ProjectionMediaReference>fromWindowsReference(std::string,std::shared_ptr<void> accessLease={});
    const std::string&encoded()const noexcept{return encoded_;}
    const std::string&path()const noexcept{return path_;}const std::string&name()const noexcept{return name_;}
    NotesMediaKind kind()const noexcept{return kind_;}unsigned width()const noexcept{return width_;}unsigned height()const noexcept{return height_;}
    unsigned frames()const noexcept{return frames_;}std::optional<double>duration()const noexcept{return duration_;}
    const std::shared_ptr<void>&accessLease()const noexcept{return lease_;}
private:
    std::string encoded_,path_,name_;NotesMediaKind kind_{};unsigned width_{},height_{},frames_{};std::optional<double>duration_;std::shared_ptr<void>lease_;
};
struct ProjectionMediaItem {std::uint64_t id{};std::shared_ptr<const ProjectionMediaReference>reference;core::Rect frame;};
struct ProjectionMediaGeometry {core::Rect header,close,title,content,play,seek,time,rail,resize;bool moving{},video{};};
ProjectionMediaGeometry projectionMediaGeometry(core::Point size,NotesMediaKind);
struct ProjectionWorkspaceGeometry {core::Rect bounds,toolbar,message;};
ProjectionWorkspaceGeometry projectionWorkspaceGeometry(core::Point size,double safeAreaTop=0,double visibleTop=0);
core::Rect projectionMenuFrame(const ProjectionWorkspaceGeometry&,core::Point menuSize);
// Mirrors ProjectionModel.swift; it deliberately keeps drawings/preferences
// across hide/show. Caller owns returned opaque IDs and playback/provider routes.
class ProjectionModel final {
public:
    static constexpr std::size_t maximumMedia=16;
    ProjectionModel(NotesColor accent,double darkness,double blur);
    const NotesDrawing&drawing()const noexcept{return drawing_;}
    std::span<const ProjectionMediaItem>media()const noexcept{return media_;}
    const ProjectionMediaItem*find(std::uint64_t)const noexcept;
    NotesColor color()const noexcept{return color_;}double brushWidth()const noexcept{return width_;}
    bool erasing()const noexcept{return erasing_;}bool backgroundEnabled()const noexcept{return background_;}
    double darkness()const noexcept{return darkness_;}double blur()const noexcept{return blur_;}
    bool setColor(NotesColor);bool setBrushWidth(double);bool setDarkness(double);bool setBlur(double);
    bool setErasing(bool);bool setBackgroundEnabled(bool);
    bool append(DrawingStroke);bool erase(core::Point,core::Point size);
    std::optional<std::uint64_t>addMedia(std::shared_ptr<const ProjectionMediaReference>,core::Point,core::Rect bounds);
    bool setFrame(core::Rect,std::uint64_t,core::Rect bounds);bool bringForward(std::uint64_t);bool removeMedia(std::uint64_t);
    bool clearContent();
    std::uint64_t drawingRevision()const noexcept{return drawingRevision_;}std::uint64_t mediaRevision()const noexcept{return mediaRevision_;}
    std::uint64_t preferencesRevision()const noexcept{return preferencesRevision_;}
    static core::Rect constrain(core::Rect frame,core::Rect bounds);
private:
    NotesDrawing drawing_;std::vector<ProjectionMediaItem>media_;NotesColor color_;double width_{5},darkness_{},blur_{};bool erasing_{},background_{true};
    std::uint64_t nextID_{1},drawingRevision_{1},mediaRevision_{1},preferencesRevision_{1};
};
}
