#pragma once
#include "modules/reader_viewport.hpp"
namespace endfield::modules {
using ReaderColor=std::array<double,4>;
struct ReaderAppearance {bool dark{true};ReaderColor accent{250./255,212./255,31./255,1};bool operator==(const ReaderAppearance&)const=default;};
struct ReaderStrings {
    std::string open{"Open"},library{"Library"},reading{"Reading"},bookmarks{"Bookmarks"},
        title{"E-Reader"},empty{"Open a book to begin reading."},loading{"Loading book…"},
        chooseFile{"Choose in Finder"},chooseShelf{"Choose from Shelf"},shelfTitle{"Choose from Shelf"},
        font{"Font"},fontSize{"Font size"},lineSpacing{"Line spacing"},margin{"Margins"},
        horizontal{"Left to right"},vertical{"Top to bottom"};
    bool operator==(const ReaderStrings&)const=default;
    static ReaderStrings simplifiedChinese();
};
struct ReaderAction {std::string id,label;core::Rect rect;bool enabled{true},selected{};};
struct ReaderCanvasInput {
    ReaderAppearance appearance;ReaderStrings strings;
    std::optional<std::string>bookTitle,error;bool hasPage{},loading{},illustration{},next{},previous{},bookmarked{};
    double progress{};bool operator==(const ReaderCanvasInput&)const=default;
};
struct ReaderFeedback {std::string action,tintID,rimID;core::Rect rect;bool enabled{};float restingRim{};};
// Root-local model tree in original source paint order. Page raster bindings
// remain external immutable ReaderPagePixels; no filesystem/font/worker work.
struct ReaderArtwork {ReaderJson root;std::vector<ReaderAction>actions;std::vector<ReaderFeedback>feedback;core::Rect bounds,progressRect;};
ReaderArtwork prepareReaderCanvas(const ReaderCanvasInput&);
enum class ReaderMenuType {open,library,bookmarks,shelf,settings,font};
struct ReaderMenuChoice {std::string id,title;bool operator==(const ReaderMenuChoice&)const=default;};
struct ReaderMenuInput {
    ReaderMenuType type{ReaderMenuType::library};ReaderStrings strings;ReaderColor accent{250./255,212./255,31./255,1};
    ReaderPreferences preferences;std::vector<ReaderMenuChoice>choices;std::size_t firstRow{};
    std::optional<std::string>deletion,selectedFont; // font catalog supplied once by existing rasterizer
    bool operator==(const ReaderMenuInput&)const=default;
};
ReaderArtwork prepareReaderMenu(const ReaderMenuInput&);
// Exact source bounds excluding max edges, action ordering and cut-corner
// feedback path. ReaderRetainedMenu uses its last enabled overlapping item.
std::optional<std::string_view>readerActionAt(std::span<const ReaderAction>,core::Point,bool last=true)noexcept;
ReaderPreferences changeReaderSetting(ReaderPreferences,std::string_view action);
} // namespace endfield::modules
