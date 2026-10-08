#pragma once
#include "app/overlay_host.hpp"
#include "native/shelf_assets.hpp"
#include "native/file_shelf_transfer.hpp"
#include "core/source_desktop_chrome.hpp"
#include "core/module_presentation.hpp"
#include "core/subsection_mask.hpp"

namespace endfield::tools {
struct ShelfPreviewOptions {
    std::filesystem::path newDataRoot;
    bool nativeIcons{true},registerDropTarget{true};
    std::shared_ptr<const core::SubsectionMaskSampler> revealSamples;
};
struct ShelfPreviewAction {
    enum class Kind {choose,preview,reveal,drag,paste};Kind kind;
    std::string itemID;
};
// Connected reference-only shelf. Borrows the shell's sampled module transition,
// renderer, HWND and frame clock; never owns another module-selection state.
// Test construction requires a fresh data root and may disable native services.
class ShelfPreview final {
public:
    static constexpr UINT noticeMessage=WM_APP+185,actionMessage=WM_APP+186;
    ShelfPreview(HWND,native::LayerRasterizer&,const native::NativeShelfAssets&,ShelfPreviewOptions);
    ~ShelfPreview();
    void resize(const app::ClientMetrics&);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double time)const;
    bool covers(core::Point logicalClientPoint)const;
    bool pointer(const app::PointerEvent&,double time);
    bool wheel(const app::WheelEvent&,double time);
    bool key(const app::KeyEvent&,double time);
    bool message(const app::NativeMessage&,double time);
    bool pointerLocked()const;
    bool preservesFocusOnLoss()const;
    std::optional<ShelfPreviewAction> takeAction();
    bool importFiles(std::span<const std::string>,double time);
    void showError(std::string,double time);
    void requestFiles();
    void requestPreview(std::string_view itemID);
    bool filterKey(const app::NativeMessage&);
    void cancelPanels();
    void cancelInteraction();
    void setNativeDragActive(bool);
    ehud::data::ShelfFileAccess access(std::string_view);
    std::unique_ptr<native::ShelfDragTransfer> prepareDrag(std::string_view);
    const modules::FileShelfState& state()const;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> entries();
    void collected(native::Renderer&);
    void release(native::Renderer&);
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
}
