#pragma once
// Volume (spec section 3.13): the port of Sources/VolumeCanvas.swift on the
// design library. The Devices tab (output and input menus, the output volume
// with its mute button, centre-zero balance) and the App volume tab (one row
// per audio process: three visible, two while a per-app status line shows;
// the list glides on the wheel and drags). The shared audio runtime owns the
// hardware and the per-app routes and validates every change
// (native/audio_runtime.*, native/volume_provider.*); the canvas presents its
// snapshot and updates its controls in place.
//
// Gone with the v1.2.0 page (inventory MODS-258, MODS-260): the device
// chooser sub-page and its paging, the Headphones / Bluetooth list, the
// header subtitle, the page-reveal depth motion, the 2 px indicator, the
// projected NSButton/NSSlider stand-ins with their help strings.
// Windows: the runtime switches the default device through IPolicyConfig
// (native/audio_default_device.hpp). The menus list every device, the ones
// that cannot become the default disabled, as on the Mac. Where the
// interface is missing, or after one of its calls failed, the runtime reports
// canSetDefaultOutput / canSetDefaultInput false: both dropdowns then show
// the current device and are disabled.
#include "modules/hud_design_page.hpp"
#include "native/volume_snapshot.hpp"

namespace endfield::modules {
// App-row icons (VolumeCanvas.iconImage / renderAppIcon): the Mac reads the
// bundle icon synchronously; Windows extracts executables' icons on the
// shared utility queue (native/volume_icon_provider.hpp). The canvas names
// the apps whose rows are in or next to the view (<= 8, content events
// only; on Devices the rows App volume opens on, so the swap rarely waits),
// reads an exact successful binding when it builds a row, and places the
// icons the request bound from the cache at once, as the Mac's build does.
// An icon still being extracted keeps its square clear (no grey
// placeholder) and fades in when iconsChanged() places it. Without hooks a
// row shows the Mac's grey square (no icon).
struct VolumeIconHooks {
    std::function<void(std::span<const std::string> applicationIDs)> show;
    std::function<void()> hide;
    std::function<std::optional<design::ImageSource>(std::string_view applicationID)> icon;
    // True while the app's icon is not known: not asked for (or no longer,
    // after its row left the request window or the page went inactive), or
    // asked for and not answered yet. Such a square keeps what it shows.
    // Absent: every icon is known (no icon is the grey square).
    std::function<bool(std::string_view applicationID)> pending;
};

class VolumeCanvas final : public design::Canvas {
public:
    VolumeCanvas(design::Context&, native::VolumeSnapshot initial, native::VolumeCallbacks, VolumeIconHooks = {});
    ~VolumeCanvas() override;
    // A new snapshot from the runtime (the controllers' observers, :48).
    void receive(native::VolumeSnapshot);
    // Icons that arrived since the rows were built (the queue's completion
    // at `now`): a square that was still clear fades in; true when one changed.
    bool iconsChanged(double now);
    // An app row's icon square: clear while its icon is not known yet, then
    // the icon or the grey no-icon square.
    struct IconSquare {
        enum class Shows { clear, icon, none };
        std::string applicationID; design::Layer* layer{}; Shows shows{Shows::clear};
    };
    const IconSquare* iconSquareForTesting(std::string_view applicationID) const;
    const native::VolumeSnapshot& snapshot() const noexcept { return snapshot_; }
    int tab() const noexcept { return tab_; }   // 0 Devices, 1 App volume; kept across visits (spec 2.9) inside one HUD session
    // Rows the app list shows: three, or two while the status line takes the third.
    int visibleApplicationRows() const;
    double applicationScrollOffset() const noexcept;
    // Audio processes with an available route, plus every enabled route even
    // while its app is silent (:86-96).
    std::vector<const native::VolumeApplication*> listedApplications() const;
    // At least one listed app whose route accepts a gain change (the orange dot, :151-152).
    bool hasAdjustableApplication() const;
    // 100% is an ordinary gain on an enabled route; untouched direct playback
    // stays untouched at 100%; back at 100% a failed route is retired (:387-400).
    bool setGain(double value, const native::VolumeApplication&);

    // --- design::Canvas ---
    void buildPage(design::Page&) override;
    void show(double now) override;               // makeContent (:72-75)
    void didActivate() override;
    void didDeactivate() override;
    // A new HUD session (the Mac's new VolumeCanvas, :15, :23): Devices, no
    // slider remembered for the arrow keys, the app list at its top.
    void resetSession() override;
    void recordingModeChanged() override {}       // app names stay visible (:77)
    bool handleScroll(design::Point, design::Point delta) override;
    bool handleKey(const design::KeyInput&) override;
    void accessibilityItems(std::vector<design::AccessibilityItem>&) const override;
private:
    struct Structure {
        core::Language language{}; int tab{}; std::vector<std::string> apps; bool supported{}, status{};
        bool operator==(const Structure&) const = default;
    };
    struct SliderRow { design::RowPlate* row; design::Slider* slider; };
    class ListPan;
    native::VolumeSnapshot snapshot_;
    native::VolumeCallbacks actions_;
    VolumeIconHooks icons_;
    bool started_{};
    int tab_{};
    std::optional<Structure> renderedStructure_;
    // Updates of the visible controls from the current snapshot (the Mac's binders).
    std::vector<std::function<void()>> binders_;
    design::Slider *selectedSlider_{}, *masterSlider_{};
    design::ScrollArea* appList_{};
    design::Tabs* tabs_{}; bool news_{};
    std::optional<std::pair<double, double>> panOrigin_;     // (press y, offset at the press)
    // A slider gesture belongs to the output device on which it began.
    struct Gesture { std::string output; };
    std::optional<Gesture> gesture_;
    std::optional<std::string> lastStatus_;
    std::vector<IconSquare> iconSquares_;
    std::vector<std::string> iconRequest_;
    const native::VolumeDevice* currentOutput() const;
    const native::VolumeApplication* application(std::string_view id) const;
    std::optional<std::string> perAppStatus() const;  // PerAppAudioController.statusMessage
    Structure structure() const;
    design::Rect appViewport() const;
    void changed();
    bool sliding() const;
    void cancelSliderGestures();
    std::string counterText() const;
    std::string muteLabel() const;
    std::string deviceName(bool input) const;
    bool routeAdjustable(const native::VolumeApplication&) const;
    std::string routeState(const native::VolumeApplication&) const;
    void buildDevices(design::Page&);
    void buildApplications(design::Page&);
    void buildStatus(design::Page&, double top);
    design::Dropdown& dropdown(design::Page&, std::string id, std::string label, bool input);
    void openDeviceMenu(bool input, const design::Dropdown& anchor);
    SliderRow sliderRow(design::Page&, std::string id, std::string title, std::string accessibility, design::Slider::Range,
        design::ValueFormat, design::IconButton* trailing, double width, std::optional<double> height,
        std::function<std::optional<double>()> read, std::function<bool()> writable, std::function<void(double, bool)> change);
    design::RowPlate& applicationRow(design::Page&, const native::VolumeApplication&, double width);
    bool placeIcon(IconSquare&, bool animated);
    bool placeIcons(bool animated);
    void requestIcons();
};
} // namespace endfield::modules
