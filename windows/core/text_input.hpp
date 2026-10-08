#pragma once
#include "core/scene.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace endfield::core::text {
// ACP and all ranges count UTF-16 code units, matching NSTextStorage and TSF.
struct Range { std::uint32_t start{},end{}; bool operator==(const Range&) const = default; };
enum class ActiveEnd { none,start,end };
struct Selection { Range range;ActiveEnd activeEnd{ActiveEnd::end};bool interim{};bool operator==(const Selection&) const = default; };
struct Change { std::uint32_t start{},oldEnd{},newEnd{};bool operator==(const Change&) const = default; };

// The host owns this model and its rich formatting/undo/persistence. TSF only
// accesses it during document locks. A rich host must snapshot its styles along
// with text in beginComposition and restore both on cancellation.
class Document {
public:
    virtual ~Document()=default;
    virtual std::u16string_view text()const noexcept=0;
    virtual Selection selection()const noexcept=0;
    virtual std::uint64_t revision()const noexcept=0;
    virtual std::uint32_t maximumUnits()const noexcept=0;
    virtual bool readOnly()const noexcept=0;
    // A TSF service may insert its first provisional text before announcing a
    // composition in the same write lock. Capture the pre-edit text/styles
    // lazily on the first replace, with selection from transaction start;
    // promote that snapshot in beginComposition, or discard it when the write
    // lock ends without a composition. No snapshot/copy for read-only queries.
    virtual void beginInputTransaction()=0;
    virtual void endInputTransaction()noexcept=0;
    virtual void setSelection(Selection)=0;
    virtual Change replace(Range,std::u16string_view)=0;
    virtual void beginComposition(Range)=0;
    virtual void updateComposition(Range)=0;
    virtual std::optional<Range> composition()const noexcept=0;
    virtual std::optional<Change> endComposition(bool cancel)=0;
};
// Plain-text model for simple fields and isolated tests. Attachment-free rich
// document hosts can implement Document without flattening their UTF-16 runs.
class Buffer final : public Document {
public:
    static constexpr std::uint32_t safetyMaximumUnits=4*1024*1024;
    explicit Buffer(std::u16string value={},std::uint32_t maximum=safetyMaximumUnits);
    std::u16string_view text()const noexcept override{return text_;}
    Selection selection()const noexcept override{return selection_;}
    std::uint64_t revision()const noexcept override{return revision_;}
    std::uint32_t maximumUnits()const noexcept override{return maximum_;}
    bool readOnly()const noexcept override{return readOnly_;}
    void setReadOnly(bool value)noexcept{readOnly_=value;}
    void beginInputTransaction()override;
    void endInputTransaction()noexcept override;
    void setSelection(Selection)override;
    Change replace(Range,std::u16string_view)override;
    void beginComposition(Range)override;
    void updateComposition(Range)override;
    std::optional<Range> composition()const noexcept override{return composition_;}
    std::optional<Change> endComposition(bool cancel)override;
    static bool validUTF16(std::u16string_view)noexcept;
private:
    std::u16string text_;Selection selection_;std::uint32_t maximum_;std::uint64_t revision_{1};bool readOnly_{};
    struct Snapshot {std::u16string text;Selection selection;};
    std::optional<Snapshot> snapshot_,preparedSnapshot_;std::optional<Range> composition_;
    std::optional<Selection> inputSelection_;
};
struct RangeBounds { Rect bounds;bool clipped{}; };
// Logical text-content coordinates from the same layout used to paint glyphs,
// selection and caret. Projection changes never relayout or rasterize text.
class Layout {
public:
    virtual ~Layout()=default;
    virtual std::uint64_t textRevision()const noexcept=0;
    virtual std::optional<RangeBounds> bounds(Range)const=0; // zero-length range: caret
    virtual std::optional<std::uint32_t> hit(Point,bool nearest,bool roundNearest)const=0;
};
struct Placement {
    Projection projection;Rect viewport;Point scroll;bool visible{true};
    bool operator==(const Placement&)const;
};
struct ProjectedBounds { Rect clientBounds;bool clipped{}; };
bool validPlacement(const Placement&)noexcept;
std::optional<Rect> projectedViewport(const Placement&);
std::optional<ProjectedBounds> projectedRange(const Document&,const Layout&,Range,const Placement&);
std::optional<std::uint32_t> projectedHit(const Document&,const Layout&,Point client,const Placement&,bool nearest,bool roundNearest);
} // namespace endfield::core::text
