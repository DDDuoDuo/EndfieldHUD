#pragma once
// The projection workspace's chrome in the redesign's language: the port of
// Sources/ProjectionControls.swift (ProjectionToolbar, Mac 1.3.0) and of the
// chrome half of Sources/ProjectionWorkspaceView.swift (perform, the menus
// and pop-overs, their placement and lifecycle, the router, the status
// message, the wheel / key / right-click rules over the chrome). The drawing
// plane, the ink, the media cards and the imports stay in the workspace owner
// (tools/projection_preview.hpp), which draws this layer tree with a
// NativeDesignScene and lists its controls for UI Automation.
//
// Replaces the v1.2.0 NotesRetainedMenu toolbar (336 x 42, glyph labels),
// the retained clear confirmation and slider menus with their -/+ steppers
// (modules/projection_presentation.*, native/projection_controls_scene.*)
// and the Notes colour chooser and shelf picker sheet (DOCS-616, DOCS-626).
// The colour pop-over is the shared one (modules/notes_format_bar.hpp).
#include "modules/hud_design_menu.hpp"
#include "modules/notes_shelf_choice.hpp"
#include "modules/projection_model.hpp"
#include <array>

namespace endfield::modules {
// ProjectionToolbar (ProjectionControls.swift:9-92): a HUDPanelChrome panel
// 46 high whose width is its content: the six swatches, the custom swatch,
// pen / eraser, the brush dropdown, background / appearance / media, Clear
// all and Return, 8 apart from x 12 with three 1 px ringIcon dividers.
class ProjectionToolbar final {
public:
    static constexpr double height = 46;
    // Room for the menu bar and the camera housing (:24-28): max(64, max(safe, visible) + 24).
    static double topInset(double safeAreaTop, double visibleTop) noexcept;
    ProjectionToolbar(design::Builder&, std::array<std::uint32_t, 6> palette);
    design::Layer& layer() const noexcept { return *layer_; }
    const std::vector<design::Control*>& controls() const noexcept { return controls_; }
    design::Size contentSize() const noexcept { return layer_->bounds(); }
    design::Control* control(std::string_view id) const noexcept;
    // Every control's onActivate reports its id (:70).
    std::function<void(std::string_view)> onAction;
    // In place from the session model (:79-88).
    void update(const ProjectionModel&);
    // The brush dropdown shows its pop-over open (:91).
    void setOpen(std::optional<std::string_view> id);
private:
    design::Layer* layer_;
    std::vector<design::Control*> controls_;
    std::vector<std::pair<std::uint32_t, design::Swatch*>> swatches_;
    design::Swatch* custom_;
    design::IconButton *pen_, *eraser_, *background_;
    design::Dropdown* brush_;
};

// The workspace's events that its owner logs (and ProjectionSettings captures).
enum class ProjectionChromeEvent : std::uint8_t { brushChanged, backgroundChanged, cleared };
struct ProjectionChromeServices {
    std::function<void(ProjectionChromeEvent)> event;
    std::function<void()> close;                              // Return / Esc: onClose
    std::function<void()> chooseFinder;                       // the File Explorer chooser (several files)
    std::function<std::vector<NotesShelfChoice>()> shelfChoices;
    std::function<void(std::string shelfID)> chooseShelf;     // one click imports the entry
    // clearContent (:329-339): the owner cancels imports and gestures and
    // clears the model; true when anything changed or was pending (cleared).
    std::function<bool()> clearContent;
    std::function<void()> backgroundChanged;                  // refreshBackground (the plane)
    std::function<void()> penChanged;                         // updateCursor (the ring)
};

class ProjectionWorkspaceChrome final {
public:
    ProjectionWorkspaceChrome(design::Environment&, ProjectionModel&, std::array<std::uint32_t, 6> palette, ProjectionChromeServices);
    ~ProjectionWorkspaceChrome();
    ProjectionWorkspaceChrome(const ProjectionWorkspaceChrome&) = delete;
    ProjectionWorkspaceChrome& operator=(const ProjectionWorkspaceChrome&) = delete;
    design::Layer& root() const noexcept { return *chrome_; }           // workspace logical px (y down)
    ProjectionToolbar& toolbar() noexcept { return *toolbar_; }
    // layoutWorkspace (:140-151): centred toolbar under the top inset, the
    // message line under it, the open popup re-placed.
    void layout(core::Point size, double topInset);
    core::Rect toolbarFrame() const noexcept;
    core::Rect messageFrame() const noexcept;
    void setActive(bool);
    // refreshBackground / setPen: the toolbar follows the model.
    void refresh();
    // perform (:232-277): color:RRGGBB, color:custom (or color), pen, eraser,
    // brush, background, appearance, media, clear, clear:confirm, close.
    void perform(std::string_view action);
    // Input over the chrome (:479-572). contains: the toolbar or the popup.
    bool contains(core::Point) const;
    bool tracking() const noexcept { return tracking_; }
    void pointerDown(core::Point);
    void pointerDragged(core::Point);
    void pointerUp(core::Point);
    void pointerMoved(std::optional<core::Point>, bool pressed);   // the highlight pass
    void cancelInteraction();                                    // capture lost: the router forgets its press
    // The wheel: an open menu under the point scrolls (delta box px, + down)
    // and true; the chrome swallows it (true); false anywhere else.
    bool scroll(core::Point, double delta);
    // An open menu takes keys first; Esc closes the popup, else returns.
    bool key(const design::KeyInput&);
    bool popupOpen() const noexcept { return popup_.has_value(); }
    std::optional<std::string> popupID() const;
    void closePopup(bool animated = true);
    void showMessage(std::string);
    const std::string& message() const noexcept { return message_; }
    // Accessibility (HUDAccessibilityStandIns over the router's items).
    void accessibilityItems(std::vector<design::AccessibilityItem>&) const;
    bool performControl(std::string_view id);                 // activates a routed control
    bool adjust(std::string_view id, double value);            // a slider: setValue + onChange(value, true)
    // Content key for the native scene and the frame / deadline rules.
    std::uint64_t contentRevision() const noexcept;
    bool requiresFrames(double now) const;
    std::optional<double> nextDeadline() const noexcept;
    bool advance(double now);
private:
    struct Popup {
        std::string id, anchor;
        std::unique_ptr<design::Menu> menu;
        std::unique_ptr<design::Popover> popover;
        std::vector<design::Control*> controls;
        design::Layer& layer() const noexcept { return menu ? menu->layer() : popover->layer(); }
    };
    design::Environment* env_;
    ProjectionModel* model_;
    ProjectionChromeServices services_;
    design::LayerArena arena_;
    design::ControlSet set_;
    design::Builder builder_;
    design::Layer *chrome_, *messageLayer_;
    std::unique_ptr<ProjectionToolbar> toolbar_;
    design::Router router_;
    std::optional<Popup> popup_;
    std::vector<std::shared_ptr<Popup>> closing_;            // fading out (.14 s), then destroyed
    std::optional<ProjectionChromeEvent> pendingMenuEvent_;
    std::optional<core::Point> pointer_;
    core::Point size_{1, 1};
    std::string message_;
    bool active_{}, tracking_{};
    std::uint64_t structure_{};
    mutable std::array<std::uint64_t, 4> revisionKey_{};
    mutable std::uint64_t revision_{};
    design::Deadlines::Token token_{};
    void setPen(std::uint32_t hex, bool closing = true);
    design::Slider& adjustment(design::Builder&, std::string id, std::string title, double value, double minimum, double maximum,
        design::ValueFormat, std::function<void(double)> change);
    std::unique_ptr<design::Popover> sliderPopover(std::string title, std::span<design::Slider* const>, std::unique_ptr<design::Popover>);
    void showPopover(std::string id, std::unique_ptr<design::Popover>, std::vector<design::Control*>, std::string anchor);
    void showMenu(std::string id, std::vector<design::Menu::Item>, std::string anchor, std::function<void(const design::Menu::Item&)> pick);
    void open(Popup);
    void positionPopup();
    void refreshRouter();
    void showClearConfirmation();
    void clearContent();
    void chooseShelf();
    void emit(ProjectionChromeEvent);
};
} // namespace endfield::modules
