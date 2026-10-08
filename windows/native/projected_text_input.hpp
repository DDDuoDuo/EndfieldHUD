#pragma once
#include "core/text_input.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <msctf.h>
#include <textstor.h>
#include <memory>

namespace endfield::native {
enum class TextInputChange:unsigned {text=1,selection=2,composition=4,layout=8};
// One owner-drawn field. Document/Layout and HWND belong to the caller, must
// outlive this object, and are touched only on their creating UI thread. The
// caller activates one shared ITfThreadMgr; this adapter never creates/activates
// a duplicate TSF service, EDIT window, timer, clipboard reader or global hook.
// Placement maps logical text coordinates to physical owner-client pixels.
// Existing core::Projection drives drawing, hits and TSF candidate/caret rects.
// Host must relayout using the mutated Document revision before layoutChanged().
// Rich hosts keep their UTF-16 formatting runs and undo model in Document;
// this adapter neither flattens those runs nor writes persistent data.
class ProjectedTextInput final {
public:
    static constexpr TsViewCookie viewCookie=1;
    // message must be a private WM_APP..0xBFFF; generation identifies this
    // caller-owned field lifetime, so stale queued messages cannot touch it.
    ProjectedTextInput(HWND owner,core::text::Document&,core::text::Layout&,UINT message,UINT_PTR generation);
    ~ProjectedTextInput(); // Must run on creating UI thread; misuse fails fast.
    ProjectedTextInput(const ProjectedTextInput&)=delete;
    ProjectedTextInput&operator=(const ProjectedTextInput&)=delete;
    // Borrowed COM interface for integration and fake-sink contract tests.
    ITextStoreACP* textStore()const noexcept;
    HRESULT connect(ITfThreadMgr& alreadyActivatedManager,TfClientId clientID) noexcept;
    HRESULT focus() noexcept;
    HRESULT blur(bool cancelComposition=true) noexcept;
    HRESULT stop() noexcept;
    bool focused()const noexcept;
    HRESULT cancelComposition() noexcept;
    // Host edits are rejected while TSF holds a lock or composition is active.
    // Successful host edits notify the advised TSF sink, outside document locks.
    HRESULT replaceFromHost(core::text::Range,std::u16string_view) noexcept;
    HRESULT selectFromHost(core::text::Selection) noexcept;
    HRESULT setPlacement(const core::text::Placement&) noexcept;
    HRESULT layoutChanged() noexcept;
    // WM_KEYDOWN/UP and WM_SYSKEYDOWN/UP only, routed by the focused owner.
    // Return true when TSF consumed the key; never use as a global key hook.
    bool filterKeyMessage(UINT,WPARAM,LPARAM) noexcept;
    // Owner message has wParam=generation, lParam=0. At most one message is
    // pending; flags describe host work. Draining does not notify TSF/relayout.
    unsigned takeChanges(UINT_PTR generation) noexcept;
private:
    struct Store;
    Store* store_{};
};
} // namespace endfield::native
#endif
