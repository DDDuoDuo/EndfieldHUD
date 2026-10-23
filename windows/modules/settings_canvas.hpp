#pragma once
// Settings on the redesign: the port of Sources/HUDSettingsCanvas.swift
// (HUDSettingsCanvas, HUDIconTile) over the shared design library
// (modules/hud_design_page.hpp). Four canvases (System with its Modules page,
// Display with its Battery alert and App icon pages, Hotkeys, About with its
// Credits page) share one SettingsController (modules/settings.hpp), which
// keeps the preference transactions; this canvas presents and edits them.
//
// What stays on the host side (tools/settings_preview.hpp): the platform
// panels the Mac's HUDSettingsInteraction opens (the colour wheel, the logo
// import file picker) and their results, the display list, the decoded logo
// artwork and the right-hand list glyphs (resources/watch-appearance), the
// app icon roster (resources/application-icons) and fanning the controller's
// change notification out to the four canvases.
//
// Windows additions, restyled in the library's rows (no Mac counterpart):
// System > "Import macOS data…" (the macImport area's nav row, shown while
// its title hook returns one); the Display menu lists Windows monitors by
// their persistent id (windowsDisplayID / windowsDisplayName instead of the
// Mac's hudDisplayUUID / hudDisplayName); the About update row and toggle are
// the windowsUpdates area's (SettingsUpdateStatus: its toggle title, whether a
// check or the toggle is available). Shortcut keycaps name the Windows
// modifiers (CTRL, ALT, SHIFT, WIN) in that order.
//
// Deliberately not here (Mac 1.3.0 removed them): the System "Battery alerts"
// switch (Display > Battery alert > Display method "Don't display" is the off
// switch), the in-page restore card and the separate "Keep this UI scale?"
// card (both confirm requests now), the language / display / logo / reading
// sub-pages (menus now), the status line under the pages (Notice toasts),
// arrow-key slider nudging and keyboard page scrolling.
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_page.hpp"
#include "modules/settings.hpp"
#include <array>
#include <map>

namespace endfield::modules {
// A connected monitor (HUDDisplayDescriptor): id = the persistent display id.
struct SettingsDisplay {std::string id,name;bool operator==(const SettingsDisplay&)const=default;};
// An offered application icon (HUDApplicationIcon.pickerCases): its English
// title (the canvas localizes the ones the Mac localizes) and the image the
// App / menu bar icon page shows.
struct SettingsIcon {std::string id,title;design::ImageSource image;};
// A logo raster aspect-fit into a box (HUDMenu.Icon.raster): `pixels` is the
// image's pixel size (its proportions place the Center logo field's artwork).
struct SettingsLogoImage {design::ImageSource image;design::Size pixels;};
struct SettingsCanvasHooks {
    // HUDDisplayPolicy.connectedDisplays (re-read when the Display menu opens).
    std::function<std::vector<SettingsDisplay>()> displays;
    // HUDSettingsInteraction: the colour wheel (seeded with the current
    // accent), the logo import panel, and a preset pick that cancels an import.
    std::function<void()> chooseColor,chooseLogo,logoSelection;
    // The logo as the centre shows it, fitted into `fitting` (logoArtwork:
    // presets by id, "custom" with its revision); nullopt while it is not
    // decoded yet or unreadable (the tile stays empty, the field shows the
    // title). The host calls SettingsCanvas::artworkChanged when one arrives.
    std::function<std::optional<SettingsLogoImage>(std::string_view logo,std::string_view revision,design::Size fitting)> logoArtwork;
    // Windows only: System > Import macOS data… (title in a language, or none).
    std::function<std::optional<std::string>(core::Language)> macImportTitle;
    std::function<void()> macImport;
    // HUDNavigationEntry.glyphLayer: the right-hand list's glyph of each module
    // as its 32 px source tree (resources/watch-appearance "module:<id>"),
    // asset paths relative to the owner's raster asset root.
    std::map<std::string,design::Json,std::less<>> moduleGlyphs;
    std::vector<SettingsIcon> icons;
    SettingsAbout about;
};
class SettingsCanvas final : public design::Canvas {
public:
    SettingsCanvas(core::Module,design::Context&,SettingsController&,SettingsCanvasHooks);
    ~SettingsCanvas() override;
    // The controller published (the canvas observes it like the Mac's): a
    // structural change re-renders, anything else updates the controls in
    // place; the layout prompt and the Notice toast follow (controllerChanged).
    void controllerChanged();
    // NSApplication.didChangeScreenParametersNotification (System only).
    void displaysChanged();
    // Decoded logo artwork arrived: the Center logo field shows it.
    void artworkChanged();
    // Opened from the Power screen's battery link.
    void showBatterySettings();
    // HUDSettingsInteraction results: the colour wheel (sRGB 0...1), an
    // imported logo revision, an import failure (toast).
    void setCustomColor(std::array<double,3>);
    void setCustomLogo(std::string revision);
    void showImportError(std::string);
    bool capturingShortcut()const noexcept;
    void cancelCapture();
    std::string_view section()const noexcept;  // "main", "battery", "icons", "credits", "modules" (tests)
    // Keycap labels: CTRL, ALT, SHIFT, WIN, then the key (HUDSettingsCanvas.keycaps).
    static std::vector<std::string> keycaps(SettingsShortcut);

    void buildPage(design::Page&)override;
    void didActivate()override;
    void didDeactivate()override;
    void willLeave()override{didDeactivate();}
    bool interceptKey(const design::KeyInput&)override;
    std::optional<design::Cursor> cursor(std::optional<design::Point>)const override;
    bool inputLocked()const override;
    bool capturesKeys()const override{return capturingShortcut();}
    // The layout preview's 12 s deadline (the controller's seconds); the
    // update strip's 650 ms steps are context deadlines.
    std::optional<double> nextWakeTime(double)const override;
    void wake(double)override;
private:
    enum class Section:std::uint8_t{main,battery,icons,credits,modules};
    struct Structure {
        Section section;core::Language language;std::string languageSetting;std::vector<SettingsDisplay> displays;
        bool capturing;std::string icon,accent;SettingsUpdatePhase updatePhase;std::string latest;
        bool operator==(const Structure&)const=default;
    };
    struct ModuleRow {design::RowPlate* plate;design::DragGrip* grip;design::Layer* glyph;design::Toggle* toggle;design::IconButton* up;design::IconButton* down;double top;};
    struct Drag {std::string module;double grab;int slot;};
    SettingsController* controller_;SettingsCanvasHooks hooks_;std::uint64_t observer_{};
    std::vector<SettingsDisplay> displays_;
    Section section_{Section::main};
    std::optional<Structure> renderedStructure_;
    std::vector<std::function<void()>> binders_;
    std::optional<double> stagedScale_,stagedX_,stagedY_;
    bool layoutPromptOpen_{};
    std::optional<std::string> revertedScaleText_,lastStatus_;
    std::optional<core::Language> lastStatusLanguage_;
    std::optional<int> checkProgress_;design::Deadlines::Token checkTimer_{};
    design::Cells* updateCells_{};design::Layer* updateText_{};design::Readout* updateSpoken_{};design::Layer* updateStamp_{};
    design::Layer* updateStrip_{};design::Page* aboutPage_{};std::uint64_t stampSerial_{};
    design::KeycapGroup* keycaps_{};
    std::map<std::string,ModuleRow,std::less<>> moduleRows_;
    design::ScrollArea* modulesScroll_{};
    std::optional<Drag> moduleDrag_;
    const ehud::data::Settings& config()const noexcept{return controller_->configuration();}
    template<class F> void update(F&& mutation);
    Structure structure()const;
    std::optional<std::string> currentStatus()const;
    void updateLayoutPrompt();
    void go(Section,bool forward);
    void buildSystem(design::Page&);void openDisplayMenu(const design::Control&);void confirmRestore();
    std::string modulesCount()const;
    void buildModules(design::Page&);void renderModuleStates();void layoutModuleRows(const std::vector<std::string>&order,bool animated);
    void moveModule(const std::string&,int offset);void dragModule(const std::string&,design::DragGrip::Phase,design::Point);void confirmModulesReset();
    void buildDisplay(design::Page&);void confirmFacilityOff(std::function<void()> commit);void stagePosition(std::optional<double>x,std::optional<double>y,bool final);
    design::RowPlate& paletteRow(design::Page&);
    void buildBattery(design::Page&);std::string shownDisplayMode()const;
    design::RowPlate& logoRow(design::Page&);void openLogoMenu(const design::Dropdown&);
    void buildIcons(design::Page&);void buildHotkeys(design::Page&);
    void buildAbout(design::Page&);void buildCredits(design::Page&);design::Layer& aboutHero(design::Page&);
    void checkForUpdates();void scheduleCheckStep();void renderUpdateStrip(bool animated);
    double stack(const std::vector<design::RowPlate*>&,design::Layer& parent,design::Page&,double top,double width=design::metric::boxWidth);
    design::RowPlate& row(const std::string& id,const std::string& title,std::vector<design::Layer*> cluster,design::Page&,
        std::optional<std::string> sublabel={},std::vector<design::Control*> controls={});
    design::RowPlate& row(const std::string& id,const std::string& title,design::Dropdown&,design::Page&);
    double rowWidth()const;
    design::RowPlate& navRow(const std::string& id,const std::string& title,design::Page&,std::function<void()> action,std::string chevron="›",std::vector<design::Layer*> cluster={});
    design::RowPlate& toggleRow(const std::string& id,const std::string& title,std::string key,design::Page&,
        std::function<void(std::function<void()>)> confirmOff={},std::function<void(bool)> changed={});
    design::RowPlate& sliderRow(const std::string& id,const std::string& title,double value,design::Slider::Range,design::ValueFormat,design::Page&,
        std::function<double(const ehud::data::Settings&)> read,std::function<void(double,bool)> change);
    design::RowPlate& percentRow(const std::string& id,const std::string& title,double maximum,std::string key,design::Page&,bool inverted=false);
    design::Dropdown& dropdown(design::Page&,const std::string& id,const std::string& label,std::string value,std::optional<design::TextRegion> language,
        std::function<void(const design::Dropdown&)> open);
    design::RowPlate& valueRow(const std::string& id,const std::string& title,const std::string& value,design::Page&);
    design::Readout& spokenValue(const std::string& id,design::RowPlate&,const std::string& value,design::Page&);
    design::Layer& readout(design::Page&,const std::string& id,const std::string& value);
    design::IconButton& linkButton(design::Page&,const std::string& id,const std::string& label,const std::string& url);
    std::string languageTitle(std::string_view)const;std::string themeTitle(std::string_view)const;std::string methodTitle(std::string_view)const;
    std::string placementTitle(std::string_view)const;std::string clockStyleTitle(std::string_view)const;std::string metricTitle(std::string_view)const;
    std::string logoTitle(std::string_view)const;std::string displaySelectionTitle()const;std::string iconTitle(std::string_view id,const std::string& english)const;
};
}
