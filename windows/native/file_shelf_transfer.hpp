#pragma once
#include "core/data/file_shelf_store.hpp"
#include <cstdint>
#include <span>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <oleidl.h>
#endif

namespace endfield::native {
// Explicit first-slice bounds, not macOS limits. No truncation or materializing
// virtual files. This codec accepts only Unicode CF_HDROP / existing-path form;
// ANSI HDROP, FILECONTENTS, CIDA and cloud hydration are separate capabilities.
inline constexpr std::size_t shelfTransferMaximumBytes=4*1024*1024;
inline constexpr std::size_t shelfTransferMaximumPaths=4096;
std::vector<std::uint8_t> encodeShelfDropPaths(std::span<const std::string>);
std::vector<std::string> decodeShelfDropPaths(std::span<const std::uint8_t>);

#ifdef _WIN32
struct ShelfTransferRoute {HWND owner{};UINT message{};UINT_PTR generation{};};
// Posted notices have wParam=generation/lParam=0 and coalesce until drained.
// Owner supplies an existing OLE-initialized STA and message pump. These objects
// never initialize COM, create windows, timers, clipboard access or workers.
struct ShelfDragChanges {
    bool started{},finished{},asyncFinished{};
    // Native drag-loop and eventual extraction results can arrive in either
    // order; coalescing never replaces one phase's outcome with the other's.
    HRESULT result{S_OK};DWORD effect{};
    HRESULT asyncResult{S_OK};DWORD asyncEffect{};
};
class ShelfDragTransfer final {
public:
    using Resolver=std::function<ehud::data::ShelfFileAccess(const ehud::data::ShelfRecord&)>;
    // Entire bundle comes from FileShelfStore::prepareCopy. Resolver owns its
    // dependencies independently of the HUD/store, and may perform metadata
    // access only. GetData rechecks every current path against retained identity
    // before returning any paths. Ancestor namespace races remain an OS limit.
    ShelfDragTransfer(ehud::data::ShelfCopyBundle,Resolver,ShelfTransferRoute={});
    ~ShelfDragTransfer();
    ShelfDragTransfer(const ShelfDragTransfer&)=delete;
    ShelfDragTransfer&operator=(const ShelfDragTransfer&)=delete;
    IDataObject* dataObject()const noexcept; // borrowed; callers retaining it AddRef
    IDropSource* dropSource()const noexcept;
    // Explicit user-initiated mouse gesture only. Release host pointer ownership
    // first. No test invokes the actual nested drag loop or shows a window.
    HRESULT run(DWORD* finalEffect) noexcept;
    void cancel()noexcept;
    // run calls this after DoDragDrop; also available to an injected loop/test.
    // First completion wins. Does not release async/medium-owned file leases.
    void complete(HRESULT,DWORD)noexcept;
    ShelfDragChanges takeChanges(UINT_PTR generation)noexcept;
private:
    struct Object;Object* object_{};
};

struct ShelfDropChanges {
    bool feedbackChanged{},hovering{},committed{};
    std::vector<std::string> paths;
    std::optional<std::string> error;
};
class ShelfDropTarget final {
public:
    struct Callbacks {
        // Read-only projected hit test; owns its dependencies. No rendering,
        // focus, navigation or mutation. POINTL is physical screen coordinates.
        std::function<bool(POINTL)> acceptsPoint;
        // Synchronous non-UI transaction AFTER external data/medium cleanup.
        // Normally calls FileShelfStore::add (injected metadata provider). False
        // or exception reports NONE. Duplicate-only successful import is true.
        // Must own store dependencies and obey its non-reentry/lifetime rules.
        std::function<bool(std::span<const std::string>)> commit;
    };
    ShelfDropTarget(ShelfTransferRoute,Callbacks);
    ~ShelfDropTarget(); // owner thread; revoke before destroying borrowed HWND
    ShelfDropTarget(const ShelfDropTarget&)=delete;
    ShelfDropTarget&operator=(const ShelfDropTarget&)=delete;
    IDropTarget* dropTarget()const noexcept; // borrowed; supports synthetic calls
    HRESULT registerTarget()noexcept;
    void setEnabled(bool)noexcept;
    void stop()noexcept;
    // Drain in owner message handling, outside COM callbacks; no UI callback is
    // invoked internally. A pending committed notice must drain before new drop.
    ShelfDropChanges takeChanges(UINT_PTR generation);
private:
    struct Object;Object* object_{};
};
#endif
} // namespace endfield::native
