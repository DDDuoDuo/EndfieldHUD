#pragma once
// Account on the design library: the port of HUDAccountCanvas
// (Sources/HUDAccountCanvas.swift:61-475, MODS-849..866) over the portable
// HypergryphAccountController presentation. It replaces the v1.2.0 400 x 334
// face (modules/hypergryph_account_canvas, native/hypergryph_account_canvas_scene,
// tools/account_preview: the "// Account" heading, the Region / Game account
// dropdown menus, the in-canvas disconnect popover, the painted status line
// and the orange failure text): the region is a segmented control, the
// linked page has the sanity / recovery plates, the Game role / Header
// display / Sync rows and the field chips, the unlinked page only NOT LINKED
// with Connect, Disconnect asks through the shared confirm bar and every
// status message is a Notice toast.
//
// The canvas only presents: every action goes to the owner through onAction
// (the controller's perform / login), credentials, persistence and refresh
// scheduling stay in the controller. Presentation changes reach it through
// update(presentation): the page is rebuilt only when its Structure changes
// (linked, region, Reconnect, the sync row's height, language) or on a
// link / region transition (a sub-page slide); everything else runs the bound
// updaters in place. Sanity is projected locally from the selected role's
// snapshot on the injected wall clock (Mac `now`), re-rendered once a second
// only while the linked page is active and the snapshot has stamina (the
// Mac's 1 s Timer becomes the canvas's nextWakeTime / wake; nothing while
// hidden or settled otherwise).
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_page.hpp"
#include "modules/hypergryph_account_controller.hpp"

namespace endfield::modules {
class AccountCanvas final : public design::Canvas {
public:
    using Presentation = hypergryph::AccountPresentation;
    explicit AccountCanvas(design::Context&);
    ~AccountCanvas() override;
    // HUDAccountCanvas.Action as the controller's AccountAction (connect
    // carries the page's region: the controller connects its current one).
    std::function<void(const hypergryph::AccountAction&)> onAction;
    // Foundation reference seconds (Mac `now: () -> Date`, tests inject time).
    std::function<hypergryph::Time()> wallClock;
    // HUDAccountCanvas.update (:92-118).
    void update(const Presentation&);
    const Presentation& presentation() const noexcept { return presentation_; }
    // The projected sanity of the selected role (nullopt without stamina).
    std::optional<hypergryph::SanityPresentation> sanityPresentation() const;
    // The hero row as drawn: current, "/ max", filled cells, next, full.
    struct SanityState { std::string current, limit; int filled{}; std::string next, full; bool operator==(const SanityState&) const = default; };
    std::optional<SanityState> renderedSanity() const;

    void buildPage(design::Page&) override;
    void didActivate() override;
    void didDeactivate() override;
    void willLeave() override { didDeactivate(); }
    std::optional<double> nextWakeTime(double) const override;
    void wake(double now) override;
private:
    class SanityReadout;
    class Chip;
    struct Structure {
        bool linked{}; Presentation::RegionChoice region{}; bool reconnect{}, synced{}; core::Language language{};
        bool operator==(const Structure&) const = default;
    };
    Presentation presentation_;
    std::optional<Structure> renderedStructure_;
    std::vector<std::function<void()>> binders_;
    SanityReadout* sanity_{};
    std::optional<double> clockStart_;          // the 1 s readout clock (host seconds) while it runs
    bool awaitingRefresh_{}, awaitingUnlink_{};
    Structure structure() const;
    void bind(std::function<void(const Presentation&)>);
    void add(design::Layer&, design::Point origin, design::Page&);
    void buildOffline(design::Page&, double top);
    void buildLinked(design::Page&, double top);
    design::RowPlate& row(design::Page&, const std::string& id, const std::string& title, std::vector<design::Layer*> cluster,
        std::optional<std::string> sublabel = {});
    design::Dropdown& dropdown(design::Page&, const std::string& id, const std::string& label, std::optional<double> width,
        std::function<void(const design::Dropdown&)> open);
    void confirmDisconnect();
    void showRecovery();
    void openRoleMenu(const design::Dropdown&);
    void openHeaderMenu(const design::Dropdown&);
    void updateClock();
    void act(hypergryph::AccountAction);
    hypergryph::Time wallNow() const;
};
} // namespace endfield::modules
