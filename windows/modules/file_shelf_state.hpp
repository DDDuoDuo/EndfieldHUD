#pragma once
#include "core/scene.hpp"
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>

namespace endfield::modules {
// Session metadata only. The reference owner retains native file identities,
// access leases and persisted locators; none are decoded or accessed here.
struct FileShelfItem {
    std::string id,name,lastKnownPath,typeDescription;
    std::optional<std::int64_t> byteCount;
    bool isDirectory{};
    std::optional<std::string> availabilityError;
    bool operator==(const FileShelfItem&) const = default;
};
struct FileShelfStrings {
    std::string add{"Add files"},clear{"Clear all"},cancel{"Cancel"},confirmClear{"Clear shelf"};
    std::string clearQuestion{"Clear references only?"},unavailable{"Unavailable"};
    std::string previewPrefix{"Quick Look: "},revealPrefix{"Reveal in Finder: "},removePrefix{"Remove from shelf: "};
    std::string storageUnavailable{"File shelf storage is unavailable."};
    std::string drop{"Release to keep file references"},emptyStatus{"Files stay in their original locations"};
    std::string selectedStatus{"{count} selected · Drag to copy"},itemsStatus{"{count} items · Shift-click to select"};
    std::string folder{"Folder"},image{"Image"},video{"Video"},archive{"Archive"},file{"File"};
    static FileShelfStrings simplifiedChinese();
};
// Source: FileShelfCanvas.swift + the semantic keyboard/drag commands in
// HUDFileShelfInteraction.swift. UI-thread, synchronous non-reentrant callbacks.
// Canvas input semantics are preserved: the host gates input while inactive and
// owns chooser/preview/native-drag generations and access leases. Callbacks must
// not destroy this state. active controls animation intents, not OS focus.
// No timer, filesystem, clipboard, UUID generator, persistence or native window.
class FileShelfState final {
public:
    using Item=FileShelfItem;using Rect=core::Rect;using Point=core::Point;
    struct Store {
        // All five callbacks or none. snapshot is an in-memory, non-throwing read of
        // committed references. remove/clear erase REFERENCES ONLY, never files.
        // add must atomically validate/deduplicate by actual file identity; its
        // opaque caller tokens are NOT interpreted as trusted paths here.
        std::function<std::vector<Item>()> snapshot;
        std::function<void()> refresh;
        std::function<std::size_t(std::span<const std::string>)> add;
        std::function<void(std::string_view)> remove;
        std::function<void()> clear;
    };
    struct PlatformActions {
        std::function<void()> chooseFiles;
        std::function<void(std::string_view)> preview,reveal;
        // Optional source-equivalent ByteCountFormatter replacement. No size
        // approximation is invented if the owner has not supplied a formatter.
        std::function<std::string(std::int64_t)> formatFileSize;
    };
    struct Modifiers {bool shift{},toggle{};}; // toggle = Mac Command / host mapping
    struct Action {std::string id,label;Rect rect;};
    struct VisibleRange {std::size_t begin{},end{};bool operator==(const VisibleRange&)const=default;};
    struct Card {
        Rect full,clipped;bool selected{},available{};
        double selectionY{},selectionZ{},borderWidth{},iconOpacity{};
    };
    struct Style {
        double cardWhite,unselectedBorderWhite,unselectedBorderAlpha{.75};
        double cardInk{.14},primary,muted,headerFont{15},statusFont{9.5};
        double toolbarWhite,toolbarBorderWhite,toolbarBorderAlpha{.65};
    };
    enum class EventKind {settle,selection,toolbar,dropTrace,collectionReveal,itemsAdded,itemRemoved,shelfCleared};
    struct Event {
        EventKind kind;bool animated{};int direction{};std::size_t count{};
        std::vector<std::string> values; // selection changed IDs / added or removed names
    };
    FileShelfState(std::vector<Item>,Store={},PlatformActions={},FileShelfStrings={},std::optional<std::string> error={});
    std::span<const Item> items() const noexcept{return items_;}
    const Item* item(std::string_view) const noexcept;
    const std::optional<std::string>& selectedID() const noexcept{return selected_;}
    const std::set<std::string,std::less<>>& selectedIDs() const noexcept{return selectedIDs_;}
    const std::optional<std::string>& error() const noexcept{return error_;}
    double scrollOffset() const noexcept{return scroll_;}
    double maximumOffset() const noexcept;
    bool active() const noexcept{return active_;}
    bool confirmingClear() const noexcept{return confirmingClear_;}
    bool dropTarget() const noexcept{return drop_;}
    std::uint64_t revision() const noexcept{return revision_;}
    // Owner drains after each input command; these are effects, not a history log.
    std::vector<Event> takeEvents(); // delivery after state mutation, never a reentrant callback
    void setReduceMotion(bool);
    void activate(); // explicit refresh on activation, never polling
    void deactivate(); // clears confirmation/drop/pending click, preserves selection/scroll
    void refreshFromStore(std::optional<std::string_view> revealing={});
    bool importFiles(std::span<const std::string> opaqueTokens);
    void revealItems(std::span<const std::string> ids);
    bool mouseDown(Point,int clickCount,Modifiers);
    bool mouseDown(Point p,int clicks=1){return mouseDown(p,clicks,{});}
    void finishPointerSelection();
    void beginSelectionDrag();
    std::optional<std::string_view> itemAt(Point) const noexcept;
    std::vector<std::string> dragSelection(std::string_view primaryID) const;
    bool scroll(Point,double delta);
    void scrollBy(double delta);
    void selectNext(int direction,bool extending=false);
    void selectItem(std::optional<std::string_view>,Modifiers,bool preserveForDrag=false,bool revealing=false);
    void setDropTarget(bool);
    void showError(std::string);
    void deleteSelection();
    void previewSelection();
    void revealSelection();
    void perform(std::string_view actionID);
    // Geometry/hit queries are allocation-free. Caller retains per-card artwork
    // and only changes positions while scrolling; at most 8 source cards visible.
    VisibleRange visibleRange() const noexcept;
    std::optional<Card> card(std::string_view) const noexcept;
    std::optional<Rect> scrollIndicator() const noexcept;
    std::vector<Action> accessibleActions() const; // content/scroll events, not frame sampling
    std::vector<Action> toolbarActions() const;
    std::vector<Action> cardActions(std::string_view,bool clipped=true) const;
    std::string statusText() const;
    std::string typeLabel(const Item&) const;
    std::string sizeLabel(const Item&) const;
    static constexpr Rect bounds(){return {0,0,400,334};}
    static constexpr Rect contentRect(){return {9,40,382,248};}
    static constexpr Rect iconRect(){return {9,9,30,30};}
    static constexpr Rect nameRect(){return {45,9,129,17};}
    static constexpr Rect typeRect(){return {45,28,129,14};}
    static constexpr Rect sizeRect(){return {10,54,97,13};}
    static Style style(bool dark) noexcept;
    static std::array<Point,6> cutCorner(Rect,double corner) noexcept;
    // Original animation requests only; no approximate CA transform sampling.
    static constexpr double selectionDuration=.18,toolbarDuration=.18,dropDuration=.20,collectionDuration=.26;
    static constexpr std::array<double,4> selectionTiming{.16,.78,.25,1};
private:
    using Indices=std::map<std::string,std::size_t,std::less<>>;
    std::vector<Item> items_;Indices indices_;Store store_;PlatformActions platform_;FileShelfStrings strings_;
    std::set<std::string,std::less<>> selectedIDs_;
    std::optional<std::string> selected_,anchor_,pending_,error_;
    std::vector<Event> events_;double scroll_{};bool active_{},reduceMotion_{},confirmingClear_{},drop_{};mutable bool busy_{};
    std::uint64_t revision_{1};
    void writable() const;
    void event(EventKind,bool animated=false,int direction=0,std::vector<std::string> values={},std::size_t count=0);
    void load(std::vector<Item>,std::optional<std::string_view> revealing={});
    void select(std::optional<std::string_view>,Modifiers,bool preserveForDrag=false,bool revealing=false);
    void remove(std::span<const std::string>);
    bool revealRow(std::size_t);
    void setScrollOffset(double);
    Rect fullCard(std::size_t) const noexcept;
    static std::optional<Rect> clipped(Rect) noexcept;
    static bool contains(Rect,Point) noexcept; // CGRect excludes maximum edges
    std::string actionID(std::string_view,std::string_view) const;
    void request(const std::function<void(std::string_view)>&,std::string_view);
};
} // namespace endfield::modules
