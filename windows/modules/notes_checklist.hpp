#pragma once
#include "modules/notes_presentation.hpp"
namespace endfield::modules {
struct NotesChecklistMeasurement {
    std::string itemID;
    // Actual item text controls row height; an empty item's separately measured
    // localized placeholder is only display content, exactly as NotesCanvas.
    std::shared_ptr<const NotesMeasuredText> text,display;
    bool checked{};
};
struct NotesChecklistRow {
    std::string itemID;
    std::shared_ptr<const NotesMeasuredText> text,display;
    double origin{},height{};bool checked{};
};
struct NotesChecklistEdit {
    std::size_t row{};
    core::Rect localRect; // clipped source text rectangle in card coordinates
    double noteScrollOffset{},editorScrollOffset{};
    double fontSize{11};bool multiline{false};
};
struct NotesChecklistStrings {
    std::string check{"Check"},uncheck{"Uncheck"},edit{"Edit item"},up{"Move up"},down{"Move down"},remove{"Delete item"},add{"Add item"};
};
// Original NotesCanvas TODO geometry, independent of state/persistence and of
// native text shaping. The workspace supplies actual font11 measured indices
// from its ONE measurer. This object retains current immutable indices only;
// scrolling/hits/edit placement perform no allocation or text measurement.
// Mutations remain in the caller's existing NotesState, never a second model.
class NotesChecklistLayout final {
public:
    static constexpr std::size_t maximumRows=100000; // explicit native metadata bound; never truncate
    NotesChecklistLayout(std::string noteID,double cardWidth,double cardHeight,
        std::span<const NotesChecklistMeasurement>);
    const std::string& noteID()const noexcept{return noteID_;}
    std::span<const NotesChecklistRow> rows()const noexcept{return rows_;}
    core::Rect viewport()const noexcept{return viewport_;}
    double contentHeight()const noexcept{return contentHeight_;}
    double textWidth()const noexcept{return textWidth_;}
    double maximumScrollOffset()const noexcept;
    double clampOffset(double)const;
    std::pair<std::size_t,std::size_t> visibleRows(double offset)const;
    core::Rect textRect(std::size_t row,double offset)const;
    NotesChecklistEdit beginEditing(std::size_t row,double offset)const;
    // Source only remaps a row's viewport back to note scroll if the beginning
    // or finishing row-editor offset was positive; otherwise preserve the note.
    double finishEditingOffset(std::size_t row,double noteOffset,double initialEditorOffset,double finalEditorOffset)const;
    // Content/scroll events only. These are local card actions; caller adds the
    // card's current projected origin. Partial actions <=1pt are not exposed.
    std::vector<NotesAction> actions(double offset,const NotesChecklistStrings& = {})const;
private:
    std::string noteID_;double width_{},height_{},textWidth_{},contentHeight_{};
    core::Rect viewport_;std::vector<NotesChecklistRow> rows_;
};
} // namespace endfield::modules
