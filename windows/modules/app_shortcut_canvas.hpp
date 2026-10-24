#pragma once
// App Shortcuts on the design library: the port of AppShortcutCanvas and
// AppShortcutTile (Sources/AppShortcutCanvas.swift, Mac 1.3.0; SHELL-107..109).
// It replaces the v1.2.0 Windows page (modules/app_shortcut_state,
// app_shortcut_presentation, app_shortcut_motion, app_shortcut_trace,
// native/app_shortcut_scene, native/app_shortcut_automation,
// native/app_shortcut_field_text, tools/app_shortcut_preview): no edit page any
// more. The screen lists the saved shortcuts (56 px rows: the icon tile that
// opens the preset grid, the inline name field, Open and Remove); Add App,
// a dropped application or Ctrl+V adds one at once with its own name and the
// App icon preset; Remove asks through the shared confirm bar.
//
// The Mac calls AppShortcutStore synchronously. Here the store lives on the
// owner's one transaction worker (modules/app_shortcut_tasks), so the canvas
// asks its owner (Services::request) and the owner reports the committed
// file (requestFinished): the same toasts, reloads and notices follow then.
// Windows adaptations (recorded in the port report):
//  - Applications are Windows targets (.exe / .lnk, or a packaged application
//    "shell:AppsFolder\<AUMID>" from the chooser); the owner says which paths
//    are applications (Services::isApplication) and inspects them.
//  - An imported Mac shortcut (no Windows target yet) relinks instead of
//    opening: Open asks for its Windows application through the same chooser
//    and the chosen one replaces only its target (id, name, preset and order
//    stay). The Mac has no imported records.
//  - The tile colour of an application's own icon is computed by the owner
//    from the extracted pixels (shortcutTileColor) with the icon itself.
#include "modules/app_shortcut_artwork.hpp"
#include "modules/app_shortcut_model.hpp"
#include "modules/hud_design_page.hpp"

namespace endfield::modules {
// One unit of store work (the Mac's synchronous AppShortcutStore calls).
struct AppShortcutRequest {
    enum class Kind : std::uint8_t {
        add,    // importURLs: inspect `path`, save it with its own name and the App icon preset (`id`: relink that record)
        save,   // rename / Choose icon: reinspect record `id`, save `name` and `icon`
        remove  // the confirmed Remove of record `id`
    };
    Kind kind{Kind::add};
    std::string path, id, name, icon;
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
    static constexpr double rowHeight = 56;
    static constexpr std::size_t nameLimit = 24;
    AppShortcutCanvas(design::Context&, const AppShortcutArtwork&, AppShortcutCanvasServices);
    ~AppShortcutCanvas() override;
    std::function<void(const ShortcutRecord&)> onSaved;   // the shell list re-binds
    std::function<void(const std::string& name)> onRemoved;

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
    void resetSession();
    // A published icon changed (the owner extracted, released or replaced
    // one): the rows re-render in place.
    void iconsChanged();

    void buildPage(design::Page&) override;
    void didActivate() override;
    void didDeactivate() override;
    void willLeave() override { closeChooser(); }
    bool handleKey(const design::KeyInput&) override;
    bool presentingPanel() const override { return chooser_; }
    bool inputLocked() const override { return Canvas::inputLocked() || chooser_ || dropTarget_; }
private:
    const AppShortcutArtwork* artwork_;
    AppShortcutCanvasServices services_;
    std::optional<ShortcutFile> store_;
    std::optional<std::string> storeError_;
    std::vector<ShortcutRecord> items_;
    std::vector<AppShortcutRequest> pending_;
    std::optional<std::string> relink_;     // the imported record the open chooser relinks
    bool chooser_{}, dropTarget_{}, revealsEnd_{}, built_{};
    design::Menu::IconTile artwork(std::string_view preset, const ShortcutRecord&) const;
    design::Color originalColor(const ShortcutRecord&) const;
    design::RowPlate& makeRow(const ShortcutRecord&, double width, design::Page&);
    void openIconMenu(const ShortcutRecord&, const design::Control& anchor);
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
