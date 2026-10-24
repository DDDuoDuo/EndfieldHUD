#pragma once
// Personal ID on the design library: the port of PersonalProfileCanvas,
// ProfileEditable, ProfileCircleButton and ProfileThemeButton
// (Sources/PersonalProfileCanvas.swift, Mac 1.3.0; MODS-800..848) over the
// record model (modules/profile_state.hpp, the Mac's UserProfileStore). It
// replaces the v1.2.0 Windows page (modules/profile_presentation,
// native/profile_scene, native/profile_editor_field, native/profile_automation,
// tools/profile_preview: the 400 x 334 card with its own pop-overs, inline
// error line and UIA peers).
//
// The 590 x 422 card below the header: the tinted avatar frame over the
// portrait well, name + #, registration mark, date chip, UID, the ⋯ menu,
// level bars, counters, REGION CONSTRUCTION, the Work Mode hours and OrbiPom
// best plates, the quotation marks around the introduction, and the toolbar
// (Card theme, the eye). Every value edits inline (a design TextField placed
// over it); invalid values are discarded. The Card theme pop-over (colours,
// Follow HUD, Background / Default, seven geometry sliders) and the portrait
// pop-over preview live and save once on release; an interrupted drag is
// reverted.
//
// Windows adaptations (recorded in the port report):
//  - Images are decoded and prepared by the owner on the shared utility
//    worker (the Mac decodes in buildPage): the canvas asks Services::portrait
//    for the panning image of a zoom step (HUDPortraitArtwork.panningImage)
//    and Services::backgroundReady for the art, and the owner calls
//    imagesChanged() when a decode or a step lands. While an avatar decodes
//    the well stays empty (no silhouette flash).
//  - The background art spans the centre disc as on the Mac, but its pixels
//    are owner-drawn (the decoded <= 2048 px photo as one texture, the Media
//    Assembly insertion pattern): the canvas keeps the art's geometry as
//    numbers on three layers (artSlot / artBox / artPicture) whose motion the
//    owner samples, so slider drags and the .2 s glide never re-rasterize, and
//    the owner clips it by the box and the shared centre-backdrop ramp
//    (HUDCentreBackdrop.mask).
//  - The portrait surface is rastered at the zoom step's size (bounds x step,
//    scale zoom / step), the default silhouette too, so a zoomed portrait is
//    as sharp as the Mac's re-rendered layer.
//  - The file chooser and the colour dialog are the owner's (the host's
//    shared file dialog; the modal ChooseColorW, whose Cancel restores the
//    colour it started from, as v1.2.0 Windows); the canvas only asks and
//    hears back (chooserFinished, colorChanged, colorPanelClosed).
//  - Until the record has loaded (the worker reads profile.json) the page
//    shows the header only.
#include "modules/hud_design_page.hpp"
#include "modules/profile_state.hpp"
#include <map>

namespace endfield::modules {
class ProfileEditable;
class ProfileCircleButton;
class ProfileThemeButton;
struct ProfileSideArena;

// HUDPortraitArtwork.panningImage of one zoom step: the whole oriented avatar
// downsampled once (<= 2048 px) and its aspect-fill size in well sides.
struct ProfilePortrait {
    design::ImageSource image;
    double fillWidth{1}, fillHeight{1};
    int step{1};
    bool operator==(const ProfilePortrait&) const = default;
};
struct ProfileCanvasServices {
    std::function<double()> workSeconds;                                // the live absolute Work Mode total
    std::function<std::int64_t()> minigameBest;                         // OrbiPomSession.bestSoFar
    std::function<void(const PersonalProfile&)> geometryPreview;        // onGeometryPreview (the shell ID card)
    // The decoded avatar's panning image for `step` (the owner prepares that
    // step and returns the nearest one it has meanwhile); pending: the avatar
    // decodes (no image yet); neither: no usable avatar (the silhouette).
    struct Portrait { std::optional<ProfilePortrait> image; bool pending{}; };
    std::function<Portrait(int step)> portrait;
    std::function<bool()> backgroundReady;                              // backgroundImage() != nil
    // HUDPortraitArtwork.frameImage(accent:): the accent-tinted source frame.
    std::function<std::optional<design::ImageSource>(std::array<double, 3> accent)> frame;
    // NSOpenPanel / NSColorPanel: false when it cannot open now. The owner
    // answers with chooserFinished / colorChanged + colorPanelClosed.
    std::function<bool(ProfileImageKind)> chooseImage;
    std::function<bool(std::array<double, 3> initial)> chooseColor;
    std::function<void()> dismissPanels;                                // didDeactivate: cancel the chooser, close the colour panel
    // store.importImage of the chosen file (asynchronous on the owner's
    // worker): false when it cannot start; the owner answers with
    // imageImported or showError.
    std::function<bool(ProfileImageKind, const std::string& path)> importImage;
};

class ProfileCanvas final : public design::Canvas {
public:
    static constexpr design::Point cardOrigin{0, design::metric::headerHeight};
    static constexpr design::Size cardSize{590, 422};
    static constexpr design::Rect artFrame{-180, -120, 950, 650};
    static constexpr design::Rect wellFrame{30, 52, 82, 82}, avatarFrame{12.5, 34.2, 117.6, 117.6};
    static constexpr design::Rect moreFrame{22, 143, 28, 28}, introFrame{376, 196, 182, 112};
    static constexpr std::array<double, 2> levelYs{180, 220};
    static constexpr double countersY = 268, regionY = 336;
    static constexpr std::array<design::Rect, 2> readoutFrames{{{22, 352, 300, 28}, {22, 384, 300, 28}}};
    static constexpr design::Size quoteBox{13.5, 10};
    static constexpr double quoteScale = 1.4;
    // HUDCentreBackdrop.frame offset by -cardOrigin, and the art host (artFrame ∪ maskFrame).
    static design::Rect maskFrame() noexcept;
    static design::Rect artHostFrame() noexcept;
    static design::Rect artBoxFrame(const PersonalProfile&) noexcept;

    ProfileCanvas(design::Context&, ProfileTextRules, ProfileDateRules, ProfileCanvasServices);
    // WM_TIMECHANGE / WM_SETTINGCHANGE: the shown dates and parsing follow the zone.
    void setDateRules(ProfileDateRules);
    ~ProfileCanvas() override;
    // The loaded record (store) or the load failure (store nil with `error`):
    // editing, picture changes and restores are disabled and each activation
    // toasts the error. Before either the page shows the header only.
    void setStore(ProfileState*);
    void setStoreError(PersonalProfile defaults, std::string error);
    ProfileState* store() const noexcept { return store_; }
    // canEdit: a store, and the field's sync owner not in the store's lock
    // (Mac gameSyncActive + syncedFields = accountController.gameSyncedFields,
    // which the account owner sets as ProfileState::setSyncLocked).
    bool canEdit(ProfileField) const;
    // The HUD accent (makeContent's style.accent, sRGB).
    void setHudAccent(std::array<double, 3>);
    std::array<double, 3> resolvedAccent() const;
    // An image decode or a portrait step the owner prepared landed.
    void imagesChanged();
    // Owner answers to the panels and imports.
    void chooserFinished(ProfileImageKind, std::optional<std::string> path);
    void colorChanged(std::array<double, 3> srgb);
    void colorPanelClosed();
    void imageImported(ProfileImageKind);
    void showError(std::string message);
    // The record changed (UserProfileStore.observe); the store's own observer
    // calls it, the owner after changes the store does not see.
    void storeChanged();
    // The Work Mode hours (30 s while active) and the minigame best (per visit).
    void refreshWorkDuration();
    void refreshMinigameBest();

    const PersonalProfile& displayed() const noexcept { return preview_ ? *preview_ : profile_; }
    bool previewing() const noexcept { return preview_.has_value(); }
    bool textHidden() const noexcept { return textHidden_; }
    std::optional<ProfileField> editingField() const noexcept { return editing_ ? std::optional(editing_->field) : std::nullopt; }
    bool chooserOpen() const noexcept { return chooser_.has_value(); }
    bool colorPanelOpen() const noexcept { return colorPanel_; }
    // The art's geometry layers (null without art): the owner draws the photo
    // aspect-filled in artPicture's bounds, clipped by artBox and the ramp,
    // composed right after artSlot's part (below the card).
    const design::Layer* artSlot() const noexcept { return refs_.artHost; }
    const design::Layer* artBox() const noexcept { return refs_.artBox; }
    const design::Layer* artPicture() const noexcept { return refs_.artImage; }
    const design::Layer* artWrapper() const noexcept { return refs_.wrapper; }
    std::string hoursText() const;
    std::string bestText() const;
    // PersonalProfileCanvas.isValid (:794-813): what an inline edit may save
    // (input trimmed; dates through the store's calendar, none without one).
    static bool isValid(ProfileField, std::string_view raw, const ProfileTextRules&, const ProfileDateRules*);
    // UIA / tests: what the controls do.
    bool perform(std::string_view id, double now);

    void buildPage(design::Page&) override;
    void didActivate() override;
    void didDeactivate() override;
    void willLeave() override { didDeactivate(); }
    void didClosePopover() override;
    void recordingModeChanged() override;
    bool presentingPanel() const override { return chooser_ || colorPanel_; }
    bool inputLocked() const override { return Canvas::inputLocked() || presentingPanel(); }
private:
    struct Editing { ProfileField field; design::TextField* text; };
    struct Readout { design::Layer *layer{}, *value{}, *unit{}; };
    struct Refs {
        design::Layer *wrapper{}, *card{}, *main{}, *overlay{};
        std::vector<design::Layer*> plate;
        design::Layer *artHost{}, *artBox{}, *artImage{};
        design::Layer *frame{}, *well{}, *portrait{}, *silhouette{};
        design::Layer *dateKind{}, *uid{};
        Readout hours, best;
        ProfileCircleButton *more{}, *eye{};
        ProfileThemeButton* theme{};
        std::map<ProfileField, design::Layer*> values;
        std::map<ProfileField, ProfileEditable*> editables;
        std::optional<ProfilePortrait> shownPortrait;
        int silhouetteStep{};
    };
    ProfileTextRules text_;
    ProfileDateRules dates_;
    ProfileCanvasServices services_;
    ProfileState* store_{};
    std::uint64_t observer_{};
    PersonalProfile profile_;
    std::optional<PersonalProfile> preview_; // in-memory geometry while a slider drags
    std::optional<std::string> errorMessage_;
    bool loaded_{}, textHidden_{}, needsRerender_{}, colorPanel_{};
    std::optional<ProfileImageKind> chooser_;
    std::string lastThemeHex_{"C9A2FF"};
    std::array<double, 3> hudAccent_{250. / 255, 212. / 255, 31. / 255};
    ehud::data::ProfileSyncFields locked_{};   // the lock the page was last built with
    std::unique_ptr<ProfileSideArena> side_; // the theme pop-over's second Follow HUD button
    Refs refs_;
    std::vector<std::function<void(design::Color)>> accentBinders_;
    std::vector<std::function<void()>> popoverBinders_;
    std::optional<Editing> editing_;
    design::Deadlines::Token hoursTimer_{};
    std::uint64_t dialogGeneration_{};

    design::Color accent() const;
    void editabilityChanged();
    void rerenderWhenIdle();
    void buildArt(design::Page&, design::Layer& wrapper);
    void buildPlate(design::Page&, design::Layer& card, bool art);
    void buildIdentity(design::Page&, design::Layer& main, std::vector<design::Control*>&);
    void buildDateChip(design::Page&, design::Layer& main, std::vector<design::Control*>&);
    void buildUID(design::Page&, design::Layer& main, std::vector<design::Control*>&);
    void buildLevels(design::Page&, design::Layer& main, std::vector<design::Control*>&);
    void buildIntroduction(design::Page&, design::Layer& main, std::vector<design::Control*>&);
    void buildToolbar(design::Page&, design::Layer& card, std::vector<design::Control*>&);
    Readout plateReadout(design::Page&, std::string_view id, std::string label, std::optional<std::string> unit, design::Rect frame);
    void setReadout(Readout&, const std::string&);
    ProfileEditable& editable(design::Page&, ProfileField, design::Layer& main, std::vector<design::Control*>&);
    void applyAccent();
    void layoutGeometry(bool animated);
    void layoutArt(bool animated);
    void layoutPortrait(bool animated);
    int portraitStep() const;
    void renderVisibility(bool animated);
    void toggleTextHidden();
    void toggleDateKind();
    void beginEdit(ProfileField);
    void finishEdit(ProfileField, std::optional<std::string>);
    bool isValid(ProfileField field, std::string_view raw) const { return isValid(field, raw, text_, &dates_); }
    void commit(ProfileField, const std::string&);
    void playValueChange(design::Layer&);
    void openMoreMenu(const design::Control& anchor);
    void restoreAvatar();
    void openThemePopover(const design::Control& anchor);
    void openPortraitPopover(const design::Control& anchor);
    design::Slider& sliderRow(design::Popover&, ProfileField, double y, double width);
    void sliderChanged(ProfileField, double value, bool final);
    void revertPreview(bool animated);
    void setThemeColor(std::optional<std::string>);
    void toggleFollow();
    void setCustomColor(std::array<double, 3>);
    void resetBackground();
    void chooseImage(ProfileImageKind);
    void chooseColor(std::array<double, 3> initial);
    std::string value(ProfileField, const PersonalProfile&) const;
    std::string formattedHours() const;
    std::string formattedBest() const;
    void stopHoursTimer();
    void scheduleHours();
    void tickHours();
};
} // namespace endfield::modules
