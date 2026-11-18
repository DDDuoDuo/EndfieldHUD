#pragma once
// Windows shortcut center: pinned overview, horizontally scrolling group tabs,
// original-icon square grid, explicit edit mode and atomic multi-selection actions.
// The existing design library owns layout/input; the area serializes storage work.
#include "modules/app_shortcut_artwork.hpp"
#include "modules/app_shortcut_model.hpp"
#include "modules/hud_design_page.hpp"

namespace endfield::modules {
inline constexpr int appGridColumns=6;
inline constexpr double appGridGap=6;
inline constexpr double appGridPitch=(design::metric::boxWidth-design::ScrollArea::stripWidth-(appGridColumns-1)*appGridGap)/appGridColumns+appGridGap;
// One unit of store work (the Mac's synchronous AppShortcutStore calls).
struct AppShortcutRequest {
    enum class Kind : std::uint8_t {
        add,    // importURLs: inspect `path`, save it with its own name and the App icon preset (`id`: relink that record)
        save,   // rename / Choose icon: reinspect record `id`, save `name` and `icon`
        remove, // the confirmed Remove of record `id`
        saveGroup, removeGroup, moveGroup, membership, selectGroup, scanDesktop, restoreDesktop,
        move    // record `id` to slot `index` (clamped): a grip drag or Move up / Move down
    };
    Kind kind{Kind::add};
    std::string path, id, name, icon;
    std::ptrdiff_t index{};
    std::string groupID, sourceGroupID;
    std::vector<std::string> ids;
    bool moveMembership{};
    bool operator==(const AppShortcutRequest&) const = default;
};
struct AppShortcutCanvasServices {
    // Store work; false refuses it at once (the owner shows nothing). The
    // owner answers each accepted request with requestFinished, in order.
    std::function<bool(const AppShortcutRequest&)> request;
    // NSOpenPanel: opens the host's application chooser (false: it cannot
    // open now). The owner answers with chooserFinished; cancelChooser
    // closes it (the module left), after which no answer is expected.
    std::function<bool()> choose;
    std::function<void()> cancelChooser;
    std::function<void()> close; // close the HUD through its normal lifecycle
    std::function<void(std::string_view id)> launch;      // OverlayController.requestAppShortcutLaunch (close-first)
    std::function<bool(std::string_view path)> isApplication; // applicationURLs' ".app" filter
    std::function<std::vector<std::string>()> pasteboard; // Ctrl+V: the clipboard's file paths
    std::function<std::string(std::string_view)> trimmed; // .whitespacesAndNewlines (ShortcutTextRules::trimmed)
    // AppShortcutStore.icon(for:) as published by the owner (nullopt: none,
    // not yet extracted, or not an App icon target) and its tile colour.
    std::function<std::optional<design::ImageSource>(std::string_view id)> icon;
    std::function<std::optional<design::Color>(std::string_view id)> tileColor;
};

class AppShortcutCanvas final : public design::Canvas {
public:
    static constexpr double rowHeight = 56, pitch = rowHeight + design::metric::rowGap;
    static constexpr std::size_t nameLimit = 24;
    AppShortcutCanvas(design::Context&, const AppShortcutArtwork&, AppShortcutCanvasServices);
    ~AppShortcutCanvas() override;
    // The shell list re-binds. In the app the owner's committed-list notice
    // re-binds the right-hand list after every save, removal and move (the
    // same path for all three); these mirror the Mac canvas's callbacks.
    std::function<void(const ShortcutRecord&)> onSaved;
    std::function<void(const std::string& name)> onRemoved;
    std::function<void()> onReordered;    // the order changed (a grip drag or Move up / Move down)

    // The store: its committed file once loaded, or its failure (the Mac's
    // AppShortcutCanvas(store: nil, error:)): Add App disabled, every
    // activation toasts the error.
    void setStore(ShortcutFile);
    void setStoreError(std::string);
    bool storeAvailable() const noexcept { return store_.has_value(); }
    const std::vector<ShortcutRecord>& items() const noexcept { return items_; }
    // reload: rebuilds when the records changed (also before the first build).
    void reload(const ShortcutFile&);
    void showError(std::string message);
    // The owner's answer to an accepted request (committed: the file the
    // worker wrote; error: its localized message).
    void requestFinished(const AppShortcutRequest&, std::optional<ShortcutFile> committed, std::optional<std::string> error);
    // The chooser closed: nullopt cancelled; otherwise the chosen paths.
    void chooserFinished(std::optional<std::vector<std::string>> paths);
    // An application dragged over the screen (the plane holds still while it is).
    void setDropTarget(bool);
    bool dropTarget() const noexcept { return dropTarget_; }
    // acceptsApplications: exactly one application path.
    bool acceptsApplications(std::span<const std::string> paths) const;
    // importPasteboard (a drop): only while active and without the chooser.
    bool importDrop(std::span<const std::string> paths);
    // importURLs: adds the one application in `paths` (chooser, drop, paste).
    bool importPaths(std::span<const std::string> paths);
    bool chooserOpen() const noexcept { return chooser_; }
    const std::vector<AppShortcutRequest>& pendingRequests() const noexcept { return pending_; }
    // OverlayController builds a new SystemHUDView (and canvas) on every
    // opening: while inactive, the list returns to its top and no drop target
    // or relink survives (the committed records stay).
    void resetSession() override;
    // A published icon changed (the owner extracted, released or replaced
    // one): each row's tile takes its artwork in place (no rebuild).
    void iconsChanged();

    // Verification (the Mac's ...ForTesting): a row's grip and icon tile, a
    // whole grip drag of `id` to `slot`, and whether a row is lifted.
    design::DragGrip* rowGrip(std::string_view id) const;
    design::Control* iconTile(std::string_view id) const;
    void dragRow(const std::string& id, int slot);
    bool reordering() const noexcept { return reorder_.has_value(); }

    void showGroup(std::string id);
    void showGroups();
    void createGroup();
    const std::string& selectedGroup() const noexcept { return groupID_; }
    bool interceptPointerDown(design::Point, const design::PointerInput&) override;
    bool handleScroll(design::Point, design::Point) override;
    bool handleRightPointerDown(design::Point) override;
    void buildPage(design::Page&) override;
    void didActivate() override;
    void didDeactivate() override;
    void willLeave() override { cancelReorder(); closeChooser(); }
    // The open hand over a grip only while active with more than one item and
    // the chooser closed; the closed hand while a row is lifted.
    std::optional<design::Cursor> cursor(std::optional<design::Point>) const override;
    bool handleKey(const design::KeyInput&) override;
    bool presentingPanel() const override { return chooser_; }
    bool inputLocked() const override { return Canvas::inputLocked() || chooser_ || dropTarget_; }
private:
    std::string groupID_, selectedItem_;
    bool restoredGroup_{};
    double tabOffset_{}, tabWidth_{118};
    design::Layer* tabContent_{};
    std::vector<std::pair<std::string, design::Control*>> tabs_;
    void optionsMenu(const design::Control&);
    void membershipMenu(const design::Control&, bool move);
    std::vector<std::string> selectedItems_;
    void refreshSelection();
    void confirmRemoveSelected();
    bool groupsOverview_{}, editingGroup_{};
    void buildGroupsPage(design::Page&);
    void buildGroupPage(design::Page&);
    void removeGroup();
    const AppShortcutArtwork* artwork_;
    AppShortcutCanvasServices services_;
    std::optional<ShortcutFile> store_;
    std::optional<std::string> storeError_;
    std::vector<ShortcutRecord> items_;
    std::vector<AppShortcutRequest> pending_;
    std::optional<std::string> relink_;     // the imported record the open chooser relinks
    bool chooser_{}, dropTarget_{}, revealsEnd_{}, built_{};
    std::vector<std::function<void()>> binders_; // each row's tile artwork (iconsChanged)
    std::vector<std::pair<std::string, design::RowPlate*>> rows_;   // list order
    std::vector<std::pair<std::string, design::Control*>> tiles_;
    std::vector<design::DragGrip*> grips_;
    design::ScrollArea* list_{};
    std::optional<design::ListReorder> reorder_;
    std::optional<AppShortcutRequest> landing_;  // a dropped order the owner has not answered
    design::Deadlines::Token timers_{};
    design::Menu::IconTile artwork(std::string_view preset, const ShortcutRecord&) const;
    design::Color originalColor(const ShortcutRecord&) const;
    design::RowPlate& makeRow(const ShortcutRecord&, double width, design::Page&);
    void openIconMenu(const ShortcutRecord&, const design::Control& anchor);
    void openItemMenu(const std::string& id, const design::Control* anchor);
    void moveRow(const std::string& id, std::ptrdiff_t index);
    void dragRow(const std::string& id, design::DragGrip::Phase, design::Point rootPoint);
    void cancelReorder();
    design::ListReorder::Rows reorderRows() const;
    std::vector<std::string> itemIDs() const;
    bool moving() const;
    void rename(const std::string& id, const std::string& text, design::TextField&);
    void save(const std::string& id, std::string name, std::string preset);
    void confirmRemove(const ShortcutRecord&);
    void open(const ShortcutRecord&);
    void chooseApplication(std::optional<std::string> relink = {});
    void closeChooser();
    bool submit(AppShortcutRequest);
    const ShortcutRecord* item(std::string_view id) const;
};
} // namespace endfield::modules
