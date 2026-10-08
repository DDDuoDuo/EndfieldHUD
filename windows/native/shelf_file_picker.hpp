#pragma once
#include "core/data/file_shelf_store.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace endfield::native {
struct ShelfPickerLabels {
    std::string title{"Add to Temporary File Shelf"};
    std::string addReferences{"Add references"};
    // Windows' standard folder mode excludes files. This native dialog action
    // accepts its current mixed file/folder selection without following links.
    std::string addSelection{"Add selected references"};
};
// Pure validation: all selections are bounded ordinary absolute Windows paths.
// No filesystem, Shell namespace lookup or implicit deduplication takes place.
inline constexpr std::size_t shelfPickerMaximumItems=10000,
    shelfPickerMaximumSelectionBytes=ehud::data::FileShelfStore::maximumArchiveBytes;
bool validShelfPickerSelection(std::span<const std::string>)noexcept;

#ifdef _WIN32
struct ShelfPickerRoute {HWND owner{};UINT message{};UINT_PTR generation{};};
enum class ShelfPickerNotice:LPARAM {showRequested=1,completed=2};
struct ShelfPickerCompletion {
    HRESULT result{E_FAIL};std::vector<std::string>paths;
    bool canceled()const noexcept{return result==HRESULT_FROM_WIN32(ERROR_CANCELLED);}
};
// Injectable dialog boundary for hidden synthetic tests. Production uses ONE
// native IFileOpenDialog and its documented custom selection action. The host
// already owns STA COM initialization; this class does not activate a service.
class ShelfPickerDialog {
public:
    virtual ~ShelfPickerDialog()=default;
    virtual HRESULT configure(const ShelfPickerLabels&)=0;
    virtual HRESULT show(HWND owner)=0;
    virtual HRESULT selection(std::vector<std::string>&)=0;
    virtual void cancel()noexcept=0;
};
struct ShelfPickerStats {
    bool queued{},presenting{},hasCompletion{};
    std::uint64_t requests{},shows{},notices{},cancellations{};
};
class NativeShelfFilePicker final {
public:
    using Factory=std::function<std::shared_ptr<ShelfPickerDialog>()>;
    explicit NativeShelfFilePicker(ShelfPickerRoute,ShelfPickerLabels={},Factory={});
    ~NativeShelfFilePicker(); // creating-thread lifetime; cancels its own dialog
    NativeShelfFilePicker(const NativeShelfFilePicker&)=delete;
    NativeShelfFilePicker&operator=(const NativeShelfFilePicker&)=delete;
    // Safe inside FileShelfState's synchronous choose callback: queues only.
    // The owner calls handleMessage on the private showRequested notification,
    // after the state/store callback has returned, to enter the native modal UI.
    bool request();
    bool handleMessage(UINT_PTR routeGeneration,LPARAM notice);
    // completed notification carries only wParam=generation,lParam=completed.
    // Move tokens to FileShelfStore::add OUTSIDE any state/provider callback.
    std::optional<ShelfPickerCompletion>drain(UINT_PTR routeGeneration);
    void cancel(); // stale dialog completions are discarded, no import
    void setRoute(ShelfPickerRoute); // cancels before replacing/destroying HWND
    ShelfPickerStats stats()const;
private:
    struct State;std::shared_ptr<State>state_;
};

using ShelfRevealResolver=std::function<ehud::data::ShelfFileAccess(const ehud::data::ShelfRecord&)>;
using ShelfRevealOperation=std::function<HRESULT(const ehud::data::ShelfFileAccess&)>;
// Move a validated store.access() lease here AFTER the source HUD close/focus
// handoff. Revalidate identity under a second lease before Explorer selection;
// both remain open through Shell handoff. Never default-app-open, copy, move or
// delete the file. Injected operation is for synthetic tests (no Explorer UI).
// Like the provider, this cannot freeze an ancestor namespace or file contents.
HRESULT revealShelfReference(ehud::data::ShelfFileAccess,
    ShelfRevealResolver={},ShelfRevealOperation={})noexcept;
#endif
} // namespace endfield::native
