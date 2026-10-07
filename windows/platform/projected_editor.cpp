#include "projected_editor.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cwctype>

namespace endfield::platform {
namespace {
bool high(char16_t c) { return c >= 0xd800 && c <= 0xdbff; }
bool low(char16_t c) { return c >= 0xdc00 && c <= 0xdfff; }
}
std::size_t EditorModel::begin() const noexcept { return (std::min)(state_.anchor, state_.caret); }
std::size_t EditorModel::end() const noexcept { return (std::max)(state_.anchor, state_.caret); }
bool EditorModel::valid_utf16(std::u16string_view text) noexcept {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (high(text[i])) { if (++i == text.size() || !low(text[i])) return false; }
        else if (low(text[i])) return false;
    }
    return true;
}
std::size_t EditorModel::scalar_boundary(std::u16string_view text, std::size_t offset) noexcept {
    offset = (std::min)(offset, text.size());
    if (offset && offset < text.size() && high(text[offset-1]) && low(text[offset])) --offset;
    return offset;
}
bool EditorModel::set_text(std::u16string text) {
    if (text.size() > maximum_units || !valid_utf16(text)) return false;
    state_ = {std::move(text), 0, 0}; undo_.clear(); redo_.clear(); clusters_.clear();
    composition_ = false; ++revision_; return true;
}
void EditorModel::select(std::size_t anchor, std::size_t caret) {
    state_.anchor = scalar_boundary(state_.text, anchor);
    state_.caret = scalar_boundary(state_.text, caret);
}
void EditorModel::trim_history() {
    std::size_t units = 0;
    for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) units += it->text.size();
    while (!undo_.empty() && (undo_.size() > 100 || units > 2'097'152)) {
        units -= undo_.front().text.size(); undo_.erase(undo_.begin());
    }
}
void EditorModel::remember() { if (!composition_) { undo_.push_back(state_); trim_history(); } redo_.clear(); }
bool EditorModel::replace(std::size_t start, std::size_t finish, std::u16string_view text) {
    if (start > finish || finish > state_.text.size() || !valid_utf16(text) ||
        scalar_boundary(state_.text, start) != start || scalar_boundary(state_.text, finish) != finish ||
        state_.text.size() - (finish-start) + text.size() > maximum_units) return false;
    if (start == finish && text.empty()) return true;
    // Copy first: the view is permitted to reference our own canonical storage.
    std::u16string incoming(text); remember();
    state_.text.replace(start, finish-start, incoming);
    state_.anchor = state_.caret = start + incoming.size();
    clusters_.clear(); ++revision_; return true;
}
bool EditorModel::insert(std::u16string_view text) { return replace(begin(), end(), text); }
void EditorModel::set_clusters(std::vector<std::size_t> boundaries) {
    boundaries.push_back(0); boundaries.push_back(state_.text.size());
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::remove_if(boundaries.begin(), boundaries.end(), [this](std::size_t v) {
        return v > state_.text.size() || scalar_boundary(state_.text, v) != v;
    }), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    clusters_ = std::move(boundaries);
}
std::size_t EditorModel::previous(std::size_t offset) const {
    if (!offset) return 0;
    if (!clusters_.empty()) {
        auto found = std::lower_bound(clusters_.begin(), clusters_.end(), offset);
        return found == clusters_.begin() ? 0 : *--found;
    }
    return scalar_boundary(state_.text, offset-1);
}
std::size_t EditorModel::next(std::size_t offset) const {
    if (offset >= state_.text.size()) return state_.text.size();
    if (!clusters_.empty()) {
        auto found = std::upper_bound(clusters_.begin(), clusters_.end(), offset);
        return found == clusters_.end() ? state_.text.size() : *found;
    }
    return offset + (high(state_.text[offset]) ? 2 : 1);
}
std::size_t EditorModel::navigation_boundary(std::size_t offset,bool forward) const {
    offset=scalar_boundary(state_.text,offset); return forward ? next(offset) : previous(offset);
}
bool EditorModel::erase(bool forward) {
    if (begin() != end()) return replace(begin(), end(), {});
    return forward ? replace(caret(), next(caret()), {}) : replace(previous(caret()), caret(), {});
}
void EditorModel::move(bool forward, bool extend) {
    auto position = !extend && begin() != end() ? (forward ? end() : begin()) :
        (forward ? next(caret()) : previous(caret()));
    select(extend ? anchor() : position, position);
}
bool EditorModel::undo() {
    if (composition_ || undo_.empty()) return false;
    redo_.push_back(state_); state_ = std::move(undo_.back()); undo_.pop_back();
    clusters_.clear(); ++revision_; return true;
}
bool EditorModel::redo() {
    if (composition_ || redo_.empty()) return false;
    undo_.push_back(state_); state_ = std::move(redo_.back()); redo_.pop_back();
    trim_history(); clusters_.clear(); ++revision_; return true;
}
void EditorModel::begin_composition() {
    if (!composition_) { composition_before_ = state_; composition_ = true; }
}
void EditorModel::end_composition() {
    if (!composition_) return;
    composition_ = false;
    if (composition_before_.text != state_.text) {
        undo_.push_back(std::move(composition_before_)); redo_.clear(); trim_history();
    }
    composition_before_ = {};
}
bool ProjectiveMapping::project(double x, double y, double& ox, double& oy) const noexcept {
    const auto& m = values; const double w = m[6]*x+m[7]*y+m[8];
    if (!std::isfinite(w) || std::abs(w) < 1e-10) return false;
    ox = (m[0]*x+m[1]*y+m[2])/w; oy = (m[3]*x+m[4]*y+m[5])/w;
    return std::isfinite(ox) && std::isfinite(oy);
}
bool ProjectiveMapping::unproject(double x, double y, double& ox, double& oy) const noexcept {
    const auto& a = values;
    const double d = a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
    if (!std::isfinite(d) || std::abs(d) < 1e-10) return false;
    ProjectiveMapping inverse{{(a[4]*a[8]-a[5]*a[7])/d, (a[2]*a[7]-a[1]*a[8])/d, (a[1]*a[5]-a[2]*a[4])/d,
        (a[5]*a[6]-a[3]*a[8])/d, (a[0]*a[8]-a[2]*a[6])/d, (a[2]*a[3]-a[0]*a[5])/d,
        (a[3]*a[7]-a[4]*a[6])/d, (a[1]*a[6]-a[0]*a[7])/d, (a[0]*a[4]-a[1]*a[3])/d}};
    return inverse.project(x,y,ox,oy);
}
}

#ifdef _WIN32
#include <msctf.h>
#include <olectl.h>
#include <textstor.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <UIAutomation.h>
#include <oleauto.h>
#include <atomic>
#include <cstring>

namespace endfield::platform {
using Microsoft::WRL::ComPtr;
struct ProjectedEditor::Impl {
    class TextStore;
    class AccessibilityProvider;
    class AccessibilityRange;
    struct AccessibilityLifetime { Impl* owner{}; DWORD thread_id{}; };
    struct MarkedRun { LONG start{}, length{}; TF_DISPLAYATTRIBUTE attribute{}; };
    HWND owner{};
    ComPtr<IDWriteFactory> factory;
    ComPtr<IDWriteTextFormat> format;
    ComPtr<IDWriteTextLayout> layout;
    ComPtr<ITfThreadMgr> thread;
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfDocumentMgr> previous_document;
    ComPtr<ITfContext> context;
    ComPtr<ITfKeystrokeMgr> keys;
    ComPtr<ITfSource> context_source;
    ComPtr<ITfCategoryMgr> category_manager;
    ComPtr<ITfDisplayAttributeMgr> display_attributes;
    ComPtr<ITfCompositionView> active_composition;
    ComPtr<TextStore> store;
    ComPtr<AccessibilityProvider> accessibility;
    std::shared_ptr<AccessibilityLifetime> accessibility_lifetime;
    DWORD edit_sink_cookie{TF_INVALID_COOKIE};
    TfClientId client_id{};
    TfEditCookie edit_cookie{};
    HRESULT tsf_result{E_PENDING};
    EditorModel model;
    D2D1_RECT_F rectangle{0,0,500,220};
    ProjectiveMapping projection;
    std::function<void()> invalidate;
    std::wstring accessible_name{L"Text editor"};
    std::vector<MarkedRun> marked_runs;
    LONG marked_start{},marked_length{};
    float scroll_y{},text_height{};
    std::uint64_t artwork_version{};
    bool active{}, dragging{};
    std::uint64_t layout_revision{(std::numeric_limits<std::uint64_t>::max)()};
    char16_t pending_high{};
    void update_layout();
    void changed(std::size_t old_length, bool from_tsf = false);
    bool screen_rect(D2D1_RECT_F source, RECT* result);
    bool hit(POINT client, LONG* position, bool require_inside);
    void notify_selection();
    void terminate_composition();
    void artwork_changed();
    void ensure_caret_visible();
    bool scroll(float logical_y);
    std::vector<D2D1_RECT_F> range_rectangles(std::size_t start,std::size_t finish,bool clip);
    void set_composition_range(ITfCompositionView*,ITfRange* range=nullptr);
    void read_display_attributes(ITfContext*,TfEditCookie);
    void accessibility_event(EVENTID event);
};

// TSF talks directly to this UTF-16 store; DirectWrite supplies artwork only.
// No visible or hidden native EDIT window is used to own input.
class ProjectedEditor::Impl::TextStore final : public ITextStoreACP, public ITfContextOwnerCompositionSink, public ITfTextEditSink {
public:
    explicit TextStore(Impl& owner) : host(owner) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITextStoreACP) *out = static_cast<ITextStoreACP*>(this);
        else if (iid == IID_ITfContextOwnerCompositionSink) *out = static_cast<ITfContextOwnerCompositionSink*>(this);
        else if (iid == IID_ITfTextEditSink) *out = static_cast<ITfTextEditSink*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references; }
    STDMETHODIMP_(ULONG) Release() override { auto left = --references; if (!left) delete this; return left; }
    STDMETHODIMP AdviseSink(REFIID iid, IUnknown* unknown, DWORD flags) override {
        if (iid != IID_ITextStoreACPSink || !unknown) return E_INVALIDARG;
        ComPtr<IUnknown> identity; unknown->QueryInterface(IID_PPV_ARGS(&identity));
        if (sink) {
            ComPtr<IUnknown> prior; sink.As(&prior);
            if (identity.Get() != prior.Get()) return CONNECT_E_ADVISELIMIT;
        }
        HRESULT hr = unknown->QueryInterface(IID_PPV_ARGS(&sink)); if (SUCCEEDED(hr)) mask = flags; return hr;
    }
    STDMETHODIMP UnadviseSink(IUnknown* unknown) override {
        if (!sink || !unknown) return CONNECT_E_NOCONNECTION;
        ComPtr<IUnknown> expected, actual; sink.As(&expected); unknown->QueryInterface(IID_PPV_ARGS(&actual));
        if (actual.Get() != expected.Get()) return CONNECT_E_NOCONNECTION;
        sink.Reset(); return S_OK;
    }
    STDMETHODIMP RequestLock(DWORD flags, HRESULT* session) override {
        if (!session) return E_POINTER; if (!sink) return E_UNEXPECTED;
        if (lock) {
            if (flags & TS_LF_SYNC) { *session = TS_E_SYNCHRONOUS; return S_OK; }
            pending_lock |= flags & TS_LF_READWRITE; *session = TS_S_ASYNC; return S_OK;
        }
        lock = flags & TS_LF_READWRITE;
        *session = sink->OnLockGranted(flags); lock = 0;
        // A queued read-to-write upgrade runs after the current read session.
        while (pending_lock) { DWORD next = pending_lock; pending_lock = 0; lock = next; sink->OnLockGranted(next); lock = 0; }
        if (layout_dirty) { layout_dirty = false; host.update_layout(); host.ensure_caret_visible(); layout_change(); }
        return S_OK;
    }
    STDMETHODIMP GetStatus(TS_STATUS* status) override {
        if (!status) return E_POINTER; *status = {0, TS_SS_NOHIDDENTEXT}; return S_OK;
    }
    STDMETHODIMP QueryInsert(LONG start, LONG finish, ULONG count, LONG* out_start, LONG* out_end) override {
        if (!out_start || !out_end) return E_POINTER;
        if (!range(start, finish) || host.model.text().size()-(finish-start)+count > EditorModel::maximum_units) return TS_E_INVALIDPOS;
        // QueryInsert's output must remain inside the existing document. Actual
        // insertion reports the new end through TS_TEXTCHANGE separately.
        *out_start = start; *out_end = finish; return S_OK;
    }
    STDMETHODIMP GetSelection(ULONG index, ULONG count, TS_SELECTION_ACP* selection, ULONG* fetched) override {
        if (!fetched || (count && !selection)) return E_POINTER; *fetched = 0;
        if (!read_lock()) return TS_E_NOLOCK;
        if (index != TS_DEFAULT_SELECTION && index != 0) return E_INVALIDARG;
        if (count) { selection[0] = {static_cast<LONG>(host.model.begin()),static_cast<LONG>(host.model.end()),
            {host.model.caret() == host.model.begin() ? TS_AE_START : TS_AE_END, FALSE}}; *fetched = 1; }
        return S_OK;
    }
    STDMETHODIMP SetSelection(ULONG count, const TS_SELECTION_ACP* selection) override {
        if (!write_lock()) return TS_E_NOLOCK;
        if (count != 1 || !selection || !range(selection[0].acpStart, selection[0].acpEnd)) return E_INVALIDARG;
        if (selection[0].style.ase == TS_AE_START) host.model.select(selection[0].acpEnd, selection[0].acpStart);
        else host.model.select(selection[0].acpStart, selection[0].acpEnd);
        host.ensure_caret_visible(); host.artwork_changed(); host.accessibility_event(UIA_Text_TextSelectionChangedEventId); return S_OK;
    }
    STDMETHODIMP GetText(LONG start, LONG finish, WCHAR* plain, ULONG requested, ULONG* copied,
        TS_RUNINFO* runs, ULONG requested_runs, ULONG* copied_runs, LONG* next) override {
        if (!copied || !copied_runs || !next || (requested && !plain) || (requested_runs && !runs)) return E_POINTER;
        *copied = *copied_runs = 0; if (!read_lock()) return TS_E_NOLOCK;
        if (finish == -1) finish = static_cast<LONG>(host.model.text().size());
        if (!range(start, finish)) return TS_E_INVALIDPOS;
        ULONG amount = static_cast<ULONG>(finish-start);
        if (requested) amount = (std::min)(amount,requested);
        else if (!requested_runs) amount = 0;
        if (requested) { std::memcpy(plain,host.model.text().data()+start,amount*sizeof(WCHAR)); *copied = amount; }
        if (requested_runs && amount) { runs[0] = {amount,TS_RT_PLAIN}; *copied_runs = 1; }
        *next = start+static_cast<LONG>(amount); return S_OK;
    }
    STDMETHODIMP SetText(DWORD, LONG start, LONG finish, const WCHAR* text, ULONG count, TS_TEXTCHANGE* change) override {
        if (!write_lock()) return TS_E_NOLOCK; if (!change || (count && !text)) return E_POINTER;
        if (!range(start,finish)) return TS_E_INVALIDPOS;
        auto old = host.model.text().size();
        if (!host.model.replace(start,finish,{reinterpret_cast<const char16_t*>(text),count})) return TS_E_INVALIDPOS;
        *change = {start,finish,start+static_cast<LONG>(count)}; layout_dirty = true;
        host.changed(old,true); return S_OK;
    }
    STDMETHODIMP GetFormattedText(LONG, LONG, IDataObject** object) override { if (object) *object=nullptr; return E_NOTIMPL; }
    STDMETHODIMP GetEmbedded(LONG, REFGUID, REFIID, IUnknown** object) override { if (object) *object=nullptr; return TS_E_NOOBJECT; }
    STDMETHODIMP QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* allowed) override { if (!allowed) return E_POINTER; *allowed=FALSE; return S_OK; }
    STDMETHODIMP InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP InsertTextAtSelection(DWORD flags, const WCHAR* text, ULONG count, LONG* start, LONG* finish, TS_TEXTCHANGE* change) override {
        if (!write_lock()) return TS_E_NOLOCK;
        if ((flags & TS_IAS_QUERYONLY) && (flags & TS_IAS_NOQUERY)) return E_INVALIDARG;
        LONG begin = static_cast<LONG>(host.model.begin()), end = static_cast<LONG>(host.model.end());
        if (host.model.text().size()-(end-begin)+count > EditorModel::maximum_units) return TS_E_INVALIDPOS;
        if (start) *start=begin; if (finish) *finish=end;
        if (flags & TS_IAS_QUERYONLY) return S_OK;
        if (!change) return E_POINTER;
        HRESULT hr=SetText(0,begin,end,text,count,change);
        if (SUCCEEDED(hr)) {
            host.model.select(begin,begin+static_cast<LONG>(count));
            if (finish) *finish=begin+static_cast<LONG>(count);
        }
        return hr;
    }
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP RequestSupportedAttrs(DWORD, ULONG, const TS_ATTRID*) override { return S_OK; }
    STDMETHODIMP RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override { return S_OK; }
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override { return S_OK; }
    STDMETHODIMP FindNextAttrTransition(LONG start, LONG halt, ULONG, const TS_ATTRID*, DWORD, LONG* next, BOOL* found, LONG* offset) override {
        if (!next || !found || !offset) return E_POINTER; if (!read_lock()) return TS_E_NOLOCK;
        if (!range(start,halt)) return TS_E_INVALIDPOS; *next=halt; *found=FALSE; *offset=0; return S_OK;
    }
    STDMETHODIMP RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG* fetched) override { if (!fetched) return E_POINTER; *fetched=0; return S_OK; }
    STDMETHODIMP GetEndACP(LONG* end) override { if (!end) return E_POINTER; if (!read_lock()) return TS_E_NOLOCK; *end=static_cast<LONG>(host.model.text().size()); return S_OK; }
    STDMETHODIMP GetActiveView(TsViewCookie* view) override { if (!view) return E_POINTER; *view=0; return S_OK; }
    STDMETHODIMP GetACPFromPoint(TsViewCookie view, const POINT* screen, DWORD flags, LONG* position) override {
        if (!screen || !position) return E_POINTER; if (!read_lock()) return TS_E_NOLOCK; if (view) return E_INVALIDARG;
        POINT client_point=*screen; ScreenToClient(host.owner,&client_point);
        return host.hit(client_point,position,!(flags & (GXFPF_NEAREST|GXFPF_ROUND_NEAREST))) ? S_OK : TS_E_INVALIDPOINT;
    }
    STDMETHODIMP GetTextExt(TsViewCookie view, LONG start, LONG finish, RECT* output, BOOL* clipped) override {
        if (!output || !clipped) return E_POINTER; if (!read_lock()) return TS_E_NOLOCK;
        if (view || !range(start,finish)) return TS_E_INVALIDPOS;
        host.update_layout(); if (!host.active || !host.layout) { *output={}; *clipped=TRUE; return TS_E_NOLAYOUT; }
        D2D1_RECT_F rect{};
        if (start==finish) {
            FLOAT x{},y{}; DWRITE_HIT_TEST_METRICS metrics{};
            HRESULT hr=host.layout->HitTestTextPosition(start,FALSE,&x,&y,&metrics); if (FAILED(hr)) return TS_E_NOLAYOUT;
            rect={x,y,x+1,y+metrics.height};
        } else {
            UINT32 count{}; host.layout->HitTestTextRange(start,finish-start,0,0,nullptr,0,&count);
            std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
            if (!count || FAILED(host.layout->HitTestTextRange(start,finish-start,0,0,metrics.data(),count,&count))) return TS_E_NOLAYOUT;
            rect={metrics[0].left,metrics[0].top,metrics[0].left+metrics[0].width,metrics[0].top+metrics[0].height};
            for (const auto& m:metrics) { rect.left=(std::min)(rect.left,m.left); rect.top=(std::min)(rect.top,m.top);
                rect.right=(std::max)(rect.right,m.left+m.width); rect.bottom=(std::max)(rect.bottom,m.top+m.height); }
        }
        rect.top-=host.scroll_y; rect.bottom-=host.scroll_y;
        *clipped=rect.top<0 || rect.left<0 || rect.right>host.rectangle.right-host.rectangle.left || rect.bottom>host.rectangle.bottom-host.rectangle.top;
        rect.left=(std::max)(0.f,rect.left); rect.top=(std::max)(0.f,rect.top);
        rect.right=(std::min)(host.rectangle.right-host.rectangle.left,rect.right);
        rect.bottom=(std::min)(host.rectangle.bottom-host.rectangle.top,rect.bottom);
        if (rect.bottom<=rect.top || rect.right<=rect.left) { *output={}; return TS_E_NOLAYOUT; }
        rect.left+=host.rectangle.left; rect.right+=host.rectangle.left; rect.top+=host.rectangle.top; rect.bottom+=host.rectangle.top;
        return host.screen_rect(rect,output) ? S_OK : TS_E_NOLAYOUT;
    }
    STDMETHODIMP GetScreenExt(TsViewCookie view, RECT* output) override {
        if (!output) return E_POINTER; if (view) return E_INVALIDARG;
        if (!host.active) { *output={}; return S_OK; } return host.screen_rect(host.rectangle,output) ? S_OK : TS_E_NOLAYOUT;
    }
    STDMETHODIMP GetWnd(TsViewCookie view, HWND* hwnd) override { if (!hwnd) return E_POINTER; if (view) return E_INVALIDARG; *hwnd=host.owner; return S_OK; }
    STDMETHODIMP OnStartComposition(ITfCompositionView* composition, BOOL* allowed) override {
        if (!allowed) return E_POINTER; *allowed=!host.model.composing();
        if (*allowed) { host.model.begin_composition(); host.set_composition_range(composition); host.artwork_changed(); } return S_OK;
    }
    STDMETHODIMP OnUpdateComposition(ITfCompositionView* composition, ITfRange* range) override {
        host.set_composition_range(composition,range); host.artwork_changed(); return S_OK;
    }
    STDMETHODIMP OnEndComposition(ITfCompositionView*) override {
        host.model.end_composition(); host.active_composition.Reset(); host.marked_runs.clear(); host.marked_length=0; host.artwork_changed(); return S_OK;
    }
    STDMETHODIMP OnEndEdit(ITfContext* context,TfEditCookie cookie,ITfEditRecord*) override {
        host.read_display_attributes(context,cookie); host.ensure_caret_visible(); host.artwork_changed(); return S_OK;
    }
    void text_change(LONG old_length) {
        if (sink && (mask & TS_AS_TEXT_CHANGE)) { TS_TEXTCHANGE change{0,old_length,static_cast<LONG>(host.model.text().size())}; sink->OnTextChange(0,&change); }
    }
    void selection_change() { if (sink && (mask & TS_AS_SEL_CHANGE)) sink->OnSelectionChange(); }
    void layout_change() {
        if (lock) { layout_dirty=true; return; }
        if (sink && (mask & TS_AS_LAYOUT_CHANGE)) sink->OnLayoutChange(TS_LC_CHANGE,0);
    }
    bool locked() const { return lock!=0; }
private:
    ~TextStore() = default;
    std::atomic<ULONG> references{1};
    Impl& host;
    ComPtr<ITextStoreACPSink> sink;
    DWORD mask{},lock{},pending_lock{};
    bool layout_dirty{};
    bool read_lock() const { return (lock & TS_LF_READ)!=0; }
    bool write_lock() const { return (lock & TS_LF_READWRITE)==TS_LF_READWRITE; }
    bool range(LONG start, LONG finish) const {
        // ACP read requests may divide UTF-16 chunks; edits still reject splitting
        // a surrogate pair through EditorModel::replace.
        return start>=0 && finish>=start && static_cast<std::size_t>(finish)<=host.model.text().size();
    }
};

class ProjectedEditor::Impl::AccessibilityProvider final : public IRawElementProviderSimple,
    public IRawElementProviderFragment,public IRawElementProviderFragmentRoot,public ITextProvider,public IValueProvider {
public:
    explicit AccessibilityProvider(std::shared_ptr<AccessibilityLifetime> lifetime) : lifetime(std::move(lifetime)) {}
    STDMETHODIMP QueryInterface(REFIID iid,void** value) override {
        if (!value) return E_POINTER; *value=nullptr;
        if (iid==IID_IUnknown || iid==__uuidof(IRawElementProviderSimple)) *value=static_cast<IRawElementProviderSimple*>(this);
        else if (iid==__uuidof(IRawElementProviderFragment)) *value=static_cast<IRawElementProviderFragment*>(this);
        else if (iid==__uuidof(IRawElementProviderFragmentRoot)) *value=static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (iid==__uuidof(ITextProvider)) *value=static_cast<ITextProvider*>(this);
        else if (iid==__uuidof(IValueProvider)) *value=static_cast<IValueProvider*>(this);
        else return E_NOINTERFACE; AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references; }
    STDMETHODIMP_(ULONG) Release() override { auto count=--references; if (!count) delete this; return count; }
    HRESULT owner(Impl*& value) const {
        value=nullptr;
        // UIA is instructed to use COM apartment marshaling. Never access TSF,
        // DirectWrite or mutable state from a client's worker thread.
        if (GetCurrentThreadId()!=lifetime->thread_id) return RPC_E_WRONG_THREAD;
        if (!lifetime->owner) return UIA_E_ELEMENTNOTAVAILABLE;
        value=lifetime->owner; return S_OK;
    }
    STDMETHODIMP get_ProviderOptions(ProviderOptions* value) override {
        if (!value) return E_POINTER;
        *value=static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider|ProviderOptions_UseComThreading); return S_OK;
    }
    STDMETHODIMP GetPatternProvider(PATTERNID id,IUnknown** value) override {
        if (!value) return E_POINTER; *value=nullptr;
        if (id==UIA_TextPatternId) *value=static_cast<ITextProvider*>(this);
        else if (id==UIA_ValuePatternId) *value=static_cast<IValueProvider*>(this);
        if (*value) AddRef(); return S_OK;
    }
    STDMETHODIMP GetPropertyValue(PROPERTYID id,VARIANT* value) override {
        if (!value) return E_POINTER; VariantInit(value); Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        switch (id) {
        case UIA_ControlTypePropertyId: value->vt=VT_I4; value->lVal=UIA_EditControlTypeId; break;
        case UIA_NamePropertyId: value->vt=VT_BSTR; value->bstrVal=SysAllocString(p->accessible_name.c_str()); if (!value->bstrVal) return E_OUTOFMEMORY; break;
        case UIA_IsKeyboardFocusablePropertyId: case UIA_IsControlElementPropertyId: case UIA_IsContentElementPropertyId:
        case UIA_IsTextPatternAvailablePropertyId: case UIA_IsValuePatternAvailablePropertyId: case UIA_IsEnabledPropertyId:
            value->vt=VT_BOOL; value->boolVal=VARIANT_TRUE; break;
        case UIA_HasKeyboardFocusPropertyId: value->vt=VT_BOOL; value->boolVal=p->active ? VARIANT_TRUE : VARIANT_FALSE; break;
        case UIA_IsPasswordPropertyId: value->vt=VT_BOOL; value->boolVal=VARIANT_FALSE; break;
        case UIA_IsOffscreenPropertyId: value->vt=VT_BOOL; value->boolVal=IsWindowVisible(p->owner) ? VARIANT_FALSE : VARIANT_TRUE; break;
        default: break;
        }
        return S_OK;
    }
    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER; *value=nullptr; Impl* p{}; HRESULT hr=owner(p); return FAILED(hr) ? hr : UiaHostProviderFromHwnd(p->owner,value);
    }
    STDMETHODIMP Navigate(NavigateDirection,IRawElementProviderFragment** value) override { if (!value) return E_POINTER; *value=nullptr; return S_OK; }
    STDMETHODIMP GetRuntimeId(SAFEARRAY** value) override {
        if (!value) return E_POINTER; *value=SafeArrayCreateVector(VT_I4,0,2); if (!*value) return E_OUTOFMEMORY;
        LONG index=0; int first=UiaAppendRuntimeId,second=1;
        HRESULT hr=SafeArrayPutElement(*value,&index,&first); ++index;
        if (SUCCEEDED(hr)) hr=SafeArrayPutElement(*value,&index,&second); return hr;
    }
    STDMETHODIMP get_BoundingRectangle(UiaRect* value) override {
        if (!value) return E_POINTER; *value={}; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        RECT rect{}; if (!p->screen_rect(p->rectangle,&rect)) return UIA_E_ELEMENTNOTAVAILABLE;
        *value={static_cast<double>(rect.left),static_cast<double>(rect.top),static_cast<double>(rect.right-rect.left),static_cast<double>(rect.bottom-rect.top)}; return S_OK;
    }
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** value) override { if (!value) return E_POINTER; *value=nullptr; return S_OK; }
    STDMETHODIMP SetFocus() override {
        Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        if (!IsWindowVisible(p->owner)) return UIA_E_ELEMENTNOTAVAILABLE;
        ::SetFocus(p->owner); p->active=true; if (p->thread) p->thread->SetFocus(p->document.Get());
        p->ensure_caret_visible(); p->artwork_changed(); p->accessibility_event(UIA_AutomationFocusChangedEventId); return S_OK;
    }
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** value) override { if (!value) return E_POINTER; *value=this; AddRef(); return S_OK; }
    STDMETHODIMP ElementProviderFromPoint(double x,double y,IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER; *value=nullptr; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x)>1e8 || std::abs(y)>1e8) return E_INVALIDARG;
        POINT point{static_cast<LONG>(x),static_cast<LONG>(y)}; ScreenToClient(p->owner,&point); LONG offset{};
        if (p->hit(point,&offset,true)) { *value=this; AddRef(); } return S_OK;
    }
    STDMETHODIMP GetFocus(IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER; *value=nullptr; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        if (p->active) { *value=this; AddRef(); } return S_OK;
    }
    STDMETHODIMP GetSelection(SAFEARRAY** value) override;
    STDMETHODIMP GetVisibleRanges(SAFEARRAY** value) override;
    STDMETHODIMP RangeFromChild(IRawElementProviderSimple*,ITextRangeProvider** value) override { if (!value) return E_POINTER; *value=nullptr; return E_INVALIDARG; }
    STDMETHODIMP RangeFromPoint(UiaPoint point,ITextRangeProvider** value) override;
    STDMETHODIMP get_DocumentRange(ITextRangeProvider** value) override;
    STDMETHODIMP get_SupportedTextSelection(SupportedTextSelection* value) override { if (!value) return E_POINTER; *value=SupportedTextSelection_Single; return S_OK; }
    STDMETHODIMP SetValue(LPCWSTR value) override {
        if (!value) return E_POINTER; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        std::size_t count=0; while (count<=EditorModel::maximum_units && value[count]) ++count;
        if (count>EditorModel::maximum_units || (p->store && p->store->locked())) return E_INVALIDARG;
        p->terminate_composition(); auto old=p->model.text().size();
        if (!p->model.replace(0,old,{reinterpret_cast<const char16_t*>(value),count})) return E_INVALIDARG;
        p->changed(old); return S_OK;
    }
    STDMETHODIMP get_Value(BSTR* value) override {
        if (!value) return E_POINTER; *value=nullptr; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        *value=SysAllocStringLen(reinterpret_cast<const wchar_t*>(p->model.text().data()),static_cast<UINT>(p->model.text().size()));
        return *value ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP get_IsReadOnly(BOOL* value) override { if (!value) return E_POINTER; *value=FALSE; return S_OK; }
    std::shared_ptr<AccessibilityLifetime> lifetime;
private:
    std::atomic<ULONG> references{1};
};

class ProjectedEditor::Impl::AccessibilityRange final : public ITextRangeProvider {
public:
    AccessibilityRange(AccessibilityProvider* provider,std::size_t start,std::size_t finish) : provider(provider),start(start),finish(finish) {}
    STDMETHODIMP QueryInterface(REFIID iid,void** value) override {
        if (!value) return E_POINTER; *value=nullptr;
        if (iid!=IID_IUnknown && iid!=__uuidof(ITextRangeProvider)) return E_NOINTERFACE;
        *value=static_cast<ITextRangeProvider*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references; }
    STDMETHODIMP_(ULONG) Release() override { auto count=--references; if (!count) delete this; return count; }
    HRESULT owner(Impl*& value) {
        HRESULT hr=provider->owner(value); if (SUCCEEDED(hr)) {
            start=EditorModel::scalar_boundary(value->model.text(),start);
            finish=(std::max)(start,EditorModel::scalar_boundary(value->model.text(),finish));
        } return hr;
    }
    AccessibilityRange* compatible(ITextRangeProvider* range) const {
        auto* concrete=dynamic_cast<AccessibilityRange*>(range);
        return concrete && concrete->provider->lifetime==provider->lifetime ? concrete : nullptr;
    }
    STDMETHODIMP Clone(ITextRangeProvider** value) override {
        if (!value) return E_POINTER; *value=nullptr; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        *value=new AccessibilityRange(provider.Get(),start,finish); return S_OK;
    }
    STDMETHODIMP Compare(ITextRangeProvider* range,BOOL* value) override {
        if (!value) return E_POINTER; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        auto* other=compatible(range); if (other) { Impl* same{}; hr=other->owner(same); if (FAILED(hr)) return hr; }
        *value=other && other->start==start && other->finish==finish; return S_OK;
    }
    STDMETHODIMP CompareEndpoints(TextPatternRangeEndpoint endpoint,ITextRangeProvider* range,TextPatternRangeEndpoint other_endpoint,int* value) override {
        if (!value) return E_POINTER; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        auto* other=compatible(range); if (!other) return E_INVALIDARG; Impl* same{}; hr=other->owner(same); if (FAILED(hr)) return hr;
        *value=static_cast<int>(endpoint==TextPatternRangeEndpoint_Start ? start : finish)-static_cast<int>(other_endpoint==TextPatternRangeEndpoint_Start ? other->start : other->finish); return S_OK;
    }
    std::vector<std::size_t> units(Impl& p,TextUnit unit) {
        std::vector<std::size_t> boundaries{0}; const auto& text=p.model.text();
        if (unit==TextUnit_Character) {
            p.update_layout(); for (std::size_t index=0;index<text.size();) { auto next=p.model.navigation_boundary(index,true); if (next<=index) break; index=next; boundaries.push_back(index); }
        } else if (unit==TextUnit_Word) {
            for (std::size_t index=1;index<text.size();++index)
                if (EditorModel::scalar_boundary(text,index)==index && std::iswspace(static_cast<wchar_t>(text[index-1]))!=std::iswspace(static_cast<wchar_t>(text[index]))) boundaries.push_back(index);
        } else if (unit==TextUnit_Line) {
            p.update_layout(); UINT32 count{}; if (p.layout) p.layout->GetLineMetrics(nullptr,0,&count);
            if (count<=EditorModel::maximum_units+1) {
                std::vector<DWRITE_LINE_METRICS> lines(count);
                if (p.layout && SUCCEEDED(p.layout->GetLineMetrics(lines.data(),count,&count))) {
                    std::size_t position=0; for (const auto& line:lines) { position+=line.length; boundaries.push_back(position); }
                }
            }
        } else if (unit==TextUnit_Paragraph) {
            for (std::size_t index=0;index<text.size();++index) if (text[index]==u'\n') boundaries.push_back(index+1);
        }
        boundaries.push_back(text.size()); std::sort(boundaries.begin(),boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end()); return boundaries;
    }
    STDMETHODIMP ExpandToEnclosingUnit(TextUnit unit) override {
        Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        auto boundaries=units(*p,unit); auto it=std::upper_bound(boundaries.begin(),boundaries.end(),start);
        if (it==boundaries.end()) { start=boundaries.size()>1 ? boundaries[boundaries.size()-2] : 0; finish=p->model.text().size(); }
        else { finish=*it; start=it==boundaries.begin() ? 0 : *std::prev(it); } return S_OK;
    }
    STDMETHODIMP FindAttribute(TEXTATTRIBUTEID id,VARIANT value,BOOL,ITextRangeProvider** output) override {
        if (!output) return E_POINTER; *output=nullptr; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        if (id==UIA_IsReadOnlyAttributeId && value.vt==VT_BOOL && value.boolVal==VARIANT_FALSE) return Clone(output);
        return S_OK;
    }
    STDMETHODIMP FindText(BSTR query,BOOL backward,BOOL ignore_case,ITextRangeProvider** output) override {
        if (!output || !query) return E_POINTER; *output=nullptr; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        std::size_t length=SysStringLen(query); if (!length || length>finish-start) return S_OK;
        const auto& text=p->model.text();
        for (std::size_t iteration=0;iteration<=finish-start-length;++iteration) {
            std::size_t at=backward ? finish-length-iteration : start+iteration;
            if (EditorModel::scalar_boundary(text,at)!=at || EditorModel::scalar_boundary(text,at+length)!=at+length) continue;
            if (CompareStringOrdinal(reinterpret_cast<LPCWCH>(text.data()+at),static_cast<int>(length),query,static_cast<int>(length),ignore_case)==CSTR_EQUAL) {
                *output=new AccessibilityRange(provider.Get(),at,at+length); break;
            }
        } return S_OK;
    }
    STDMETHODIMP GetAttributeValue(TEXTATTRIBUTEID id,VARIANT* value) override {
        if (!value) return E_POINTER; VariantInit(value); Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        if (id==UIA_IsReadOnlyAttributeId) { value->vt=VT_BOOL; value->boolVal=VARIANT_FALSE; return S_OK; }
        value->vt=VT_UNKNOWN; return UiaGetReservedNotSupportedValue(&value->punkVal);
    }
    STDMETHODIMP GetBoundingRectangles(SAFEARRAY** value) override {
        if (!value) return E_POINTER; *value=nullptr; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        auto source=p->range_rectangles(start,finish,true); std::vector<double> rectangles;
        for (auto local:source) { RECT screen{}; if (p->screen_rect(local,&screen)) rectangles.insert(rectangles.end(),
            {static_cast<double>(screen.left),static_cast<double>(screen.top),static_cast<double>(screen.right-screen.left),static_cast<double>(screen.bottom-screen.top)}); }
        *value=SafeArrayCreateVector(VT_R8,0,static_cast<ULONG>(rectangles.size())); if (!*value) return E_OUTOFMEMORY;
        double* data{}; hr=SafeArrayAccessData(*value,reinterpret_cast<void**>(&data));
        if (SUCCEEDED(hr)) { std::copy(rectangles.begin(),rectangles.end(),data); SafeArrayUnaccessData(*value); } return hr;
    }
    STDMETHODIMP GetEnclosingElement(IRawElementProviderSimple** value) override { if (!value) return E_POINTER; *value=provider.Get(); provider->AddRef(); return S_OK; }
    STDMETHODIMP GetText(int maximum,BSTR* value) override {
        if (!value) return E_POINTER; *value=nullptr; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr; if (maximum<-1) return E_INVALIDARG;
        auto end=maximum==-1 ? finish : (std::min)(finish,start+static_cast<std::size_t>(maximum));
        end=EditorModel::scalar_boundary(p->model.text(),end);
        *value=SysAllocStringLen(reinterpret_cast<const wchar_t*>(p->model.text().data()+start),static_cast<UINT>(end-start)); return *value ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP Move(TextUnit unit,int count,int* moved) override {
        if (!moved) return E_POINTER; *moved=0; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        if (!count) return S_OK; bool collapsed=start==finish; auto boundaries=units(*p,unit);
        auto it=std::upper_bound(boundaries.begin(),boundaries.end(),start); std::ptrdiff_t index=it==boundaries.begin() ? 0 : std::distance(boundaries.begin(),it)-1;
        auto maximum=static_cast<std::ptrdiff_t>(boundaries.size()-1)-(collapsed ? 0 : 1);
        auto target=std::clamp(index+static_cast<std::int64_t>(count),std::ptrdiff_t(0),(std::max)(std::ptrdiff_t(0),maximum));
        *moved=static_cast<int>(target-index); start=boundaries[target]; finish=collapsed ? start : boundaries[target+1]; return S_OK;
    }
    STDMETHODIMP MoveEndpointByUnit(TextPatternRangeEndpoint endpoint,TextUnit unit,int count,int* moved) override {
        if (!moved) return E_POINTER; *moved=0; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
        if (!count) return S_OK;
        auto boundaries=units(*p,unit); auto position=endpoint==TextPatternRangeEndpoint_Start ? start : finish;
        auto it=count>=0 ? std::lower_bound(boundaries.begin(),boundaries.end(),position) : std::upper_bound(boundaries.begin(),boundaries.end(),position);
        std::ptrdiff_t index=std::distance(boundaries.begin(),it); if (count<0 && index) --index;
        const bool interior=index<static_cast<std::ptrdiff_t>(boundaries.size()) && boundaries[index]!=position;
        const auto adjustment=interior ? (count>0 ? -1 : 1) : 0;
        auto target=std::clamp(index+static_cast<std::int64_t>(count)+adjustment,std::ptrdiff_t(0),static_cast<std::ptrdiff_t>(boundaries.size()-1));
        *moved=static_cast<int>(target-index)+(interior ? (count>0 ? 1 : -1) : 0); if (endpoint==TextPatternRangeEndpoint_Start) { start=boundaries[target]; finish=(std::max)(finish,start); }
        else { finish=boundaries[target]; start=(std::min)(start,finish); } return S_OK;
    }
    STDMETHODIMP MoveEndpointByRange(TextPatternRangeEndpoint endpoint,ITextRangeProvider* range,TextPatternRangeEndpoint target) override {
        Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr; auto* other=compatible(range); if (!other) return E_INVALIDARG;
        Impl* same{}; hr=other->owner(same); if (FAILED(hr)) return hr;
        auto position=target==TextPatternRangeEndpoint_Start ? other->start : other->finish;
        if (endpoint==TextPatternRangeEndpoint_Start) { start=position; finish=(std::max)(finish,start); }
        else { finish=position; start=(std::min)(start,finish); } return S_OK;
    }
    STDMETHODIMP Select() override {
        Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr; if (p->store && p->store->locked()) return E_FAIL;
        p->terminate_composition(); p->model.select(start,finish); p->notify_selection(); return S_OK;
    }
    STDMETHODIMP AddToSelection() override { return UIA_E_INVALIDOPERATION; }
    STDMETHODIMP RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    STDMETHODIMP ScrollIntoView(BOOL top) override {
        Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr; p->update_layout(); if (!p->layout) return E_FAIL;
        FLOAT x{},y{}; DWRITE_HIT_TEST_METRICS metric{};
        hr=p->layout->HitTestTextPosition(static_cast<UINT32>(top ? start : finish),FALSE,&x,&y,&metric);
        if (SUCCEEDED(hr)) p->scroll(top ? y : y+metric.height-(p->rectangle.bottom-p->rectangle.top)); return hr;
    }
    STDMETHODIMP GetChildren(SAFEARRAY** value) override { if (!value) return E_POINTER; *value=SafeArrayCreateVector(VT_UNKNOWN,0,0); return *value ? S_OK : E_OUTOFMEMORY; }
private:
    ComPtr<AccessibilityProvider> provider;
    std::size_t start{},finish{};
    std::atomic<ULONG> references{1};
};

STDMETHODIMP ProjectedEditor::Impl::AccessibilityProvider::get_DocumentRange(ITextRangeProvider** value) {
    if (!value) return E_POINTER; *value=nullptr; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
    *value=new AccessibilityRange(this,0,p->model.text().size()); return S_OK;
}
namespace {
HRESULT range_array(ITextRangeProvider* range,SAFEARRAY** value) {
    *value=SafeArrayCreateVector(VT_UNKNOWN,0,1); if (!*value) return E_OUTOFMEMORY;
    LONG index=0; HRESULT hr=SafeArrayPutElement(*value,&index,static_cast<IUnknown*>(range));
    if (FAILED(hr)) { SafeArrayDestroy(*value); *value=nullptr; } return hr;
}
}
STDMETHODIMP ProjectedEditor::Impl::AccessibilityProvider::GetSelection(SAFEARRAY** value) {
    if (!value) return E_POINTER; *value=nullptr; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
    ComPtr<ITextRangeProvider> range; range.Attach(new AccessibilityRange(this,p->model.begin(),p->model.end())); return range_array(range.Get(),value);
}
STDMETHODIMP ProjectedEditor::Impl::AccessibilityProvider::GetVisibleRanges(SAFEARRAY** value) {
    if (!value) return E_POINTER; *value=nullptr; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
    p->update_layout(); if (!p->layout) return E_FAIL;
    BOOL trailing{},inside{}; DWRITE_HIT_TEST_METRICS first{},last{};
    p->layout->HitTestPoint(0,p->scroll_y,&trailing,&inside,&first);
    p->layout->HitTestPoint(p->rectangle.right-p->rectangle.left,p->scroll_y+p->rectangle.bottom-p->rectangle.top,&trailing,&inside,&last);
    ComPtr<ITextRangeProvider> range; range.Attach(new AccessibilityRange(this,first.textPosition,(std::max)(first.textPosition,last.textPosition+last.length)));
    return range_array(range.Get(),value);
}
STDMETHODIMP ProjectedEditor::Impl::AccessibilityProvider::RangeFromPoint(UiaPoint point,ITextRangeProvider** value) {
    if (!value) return E_POINTER; *value=nullptr; Impl* p{}; HRESULT hr=owner(p); if (FAILED(hr)) return hr;
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x)>1e8 || std::abs(point.y)>1e8) return E_INVALIDARG;
    POINT client{static_cast<LONG>(point.x),static_cast<LONG>(point.y)}; ScreenToClient(p->owner,&client); LONG position{};
    if (!p->hit(client,&position,false)) return E_INVALIDARG; *value=new AccessibilityRange(this,position,position); return S_OK;
}
void ProjectedEditor::Impl::accessibility_event(EVENTID event) {
    if (accessibility && UiaClientsAreListening()) UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(accessibility.Get()),event);
}

void ProjectedEditor::Impl::artwork_changed() {
    ++artwork_version; if (invalidate) invalidate();
}
bool ProjectedEditor::Impl::scroll(float logical_y) {
    update_layout();
    if (!std::isfinite(logical_y)) return false;
    const float maximum=(std::max)(0.f,text_height-(rectangle.bottom-rectangle.top));
    const float target=std::clamp(logical_y,0.f,maximum);
    if (target==scroll_y) return false;
    scroll_y=target; if (store) store->layout_change(); artwork_changed(); return true;
}
void ProjectedEditor::Impl::ensure_caret_visible() {
    update_layout(); if (!layout) return;
    FLOAT x{},y{}; DWRITE_HIT_TEST_METRICS metric{};
    if (FAILED(layout->HitTestTextPosition(static_cast<UINT32>(model.caret()),FALSE,&x,&y,&metric))) return;
    const float height=rectangle.bottom-rectangle.top;
    if (y<scroll_y) scroll(y);
    else if (y+metric.height>scroll_y+height) scroll(y+metric.height-height);
}
std::vector<D2D1_RECT_F> ProjectedEditor::Impl::range_rectangles(std::size_t start,std::size_t finish,bool clip) {
    update_layout(); std::vector<D2D1_RECT_F> result; if (!layout) return result;
    start=(std::min)(start,model.text().size()); finish=std::clamp(finish,start,model.text().size());
    if (start==finish) {
        FLOAT x{},y{}; DWRITE_HIT_TEST_METRICS metric{};
        if (SUCCEEDED(layout->HitTestTextPosition(static_cast<UINT32>(start),FALSE,&x,&y,&metric)))
            result.push_back(D2D1::RectF(rectangle.left+x,rectangle.top+y-scroll_y,rectangle.left+x+1.5f,rectangle.top+y-scroll_y+metric.height));
    } else {
        UINT32 count{}; layout->HitTestTextRange(static_cast<UINT32>(start),static_cast<UINT32>(finish-start),rectangle.left,rectangle.top-scroll_y,nullptr,0,&count);
        // The text model is bounded. Do not allocate megabytes of hit rectangles
        // for invisible lines: query the visible text span when a huge range is
        // selected or an accessibility client requests the entire document.
        if (clip && count>4096) {
            BOOL trailing{},inside{}; DWRITE_HIT_TEST_METRICS first{},last{};
            layout->HitTestPoint(0,scroll_y,&trailing,&inside,&first);
            layout->HitTestPoint(rectangle.right-rectangle.left,scroll_y+rectangle.bottom-rectangle.top,&trailing,&inside,&last);
            start=(std::max)(start,static_cast<std::size_t>(first.textPosition));
            finish=(std::min)(finish,static_cast<std::size_t>(last.textPosition+last.length));
            if (finish<start) return result;
            count=0; layout->HitTestTextRange(static_cast<UINT32>(start),static_cast<UINT32>(finish-start),rectangle.left,rectangle.top-scroll_y,nullptr,0,&count);
        }
        if (!count || count>65536) return result;
        std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
        if (SUCCEEDED(layout->HitTestTextRange(static_cast<UINT32>(start),static_cast<UINT32>(finish-start),rectangle.left,rectangle.top-scroll_y,metrics.data(),count,&count)))
            for (const auto& metric:metrics) result.push_back(D2D1::RectF(metric.left,metric.top,metric.left+metric.width,metric.top+metric.height));
    }
    if (clip) {
        for (auto& r:result) { r.left=(std::max)(r.left,rectangle.left); r.top=(std::max)(r.top,rectangle.top);
            r.right=(std::min)(r.right,rectangle.right); r.bottom=(std::min)(r.bottom,rectangle.bottom); }
        result.erase(std::remove_if(result.begin(),result.end(),[](auto r) { return r.right<=r.left || r.bottom<=r.top; }),result.end());
    }
    return result;
}
void ProjectedEditor::Impl::set_composition_range(ITfCompositionView* composition,ITfRange* supplied) {
    if (composition) active_composition=composition;
    ComPtr<ITfRange> range=supplied;
    if (!range && composition) composition->GetRange(&range);
    ComPtr<ITfRangeACP> acp;
    if (range && SUCCEEDED(range.As(&acp))) {
        LONG start{},length{}; if (SUCCEEDED(acp->GetExtent(&start,&length)) && start>=0 && length>=0 &&
            static_cast<std::size_t>(start)+length<=model.text().size()) { marked_start=start; marked_length=length; }
    }
}
void ProjectedEditor::Impl::read_display_attributes(ITfContext* text_context,TfEditCookie cookie) {
    marked_runs.clear(); if (!model.composing()) return;
    set_composition_range(active_composition.Get());
    if (!text_context || !category_manager || !display_attributes) return;
    ComPtr<ITfProperty> property; ComPtr<IEnumTfRanges> ranges;
    if (FAILED(text_context->GetProperty(GUID_PROP_ATTRIBUTE,&property)) || FAILED(property->EnumRanges(cookie,&ranges,nullptr))) return;
    for (unsigned index=0;index<4096;++index) {
        ComPtr<ITfRange> range; ULONG fetched{}; if (ranges->Next(1,&range,&fetched)!=S_OK || !fetched) break;
        VARIANT value; VariantInit(&value);
        if (SUCCEEDED(property->GetValue(cookie,range.Get(),&value)) && value.vt==VT_I4) {
            GUID guid{}; ComPtr<ITfDisplayAttributeInfo> info; ComPtr<ITfRangeACP> acp;
            TF_DISPLAYATTRIBUTE attribute{}; LONG start{},length{};
            if (SUCCEEDED(category_manager->GetGUID(value.lVal,&guid)) &&
                SUCCEEDED(display_attributes->GetDisplayAttributeInfo(guid,&info,nullptr)) &&
                SUCCEEDED(info->GetAttributeInfo(&attribute)) && SUCCEEDED(range.As(&acp)) &&
                SUCCEEDED(acp->GetExtent(&start,&length)) && start>=0 && length>0 && static_cast<std::size_t>(start)+length<=model.text().size())
                marked_runs.push_back({start,length,attribute});
        }
        VariantClear(&value);
    }
}

void ProjectedEditor::Impl::update_layout() {
    if (!factory || !format || layout_revision==model.revision()) return;
    layout.Reset();
    if (FAILED(factory->CreateTextLayout(reinterpret_cast<const WCHAR*>(model.text().data()),
        static_cast<UINT32>(model.text().size()),format.Get(),(std::max)(1.f,rectangle.right-rectangle.left),
        100'000'000.f,&layout))) return;
    // DirectWrite clusters prevent arrow/backspace from dividing shaped glyphs.
    UINT32 count{}; layout->GetClusterMetrics(nullptr,0,&count);
    std::vector<DWRITE_CLUSTER_METRICS> metrics(count); std::vector<std::size_t> offsets{0};
    if (SUCCEEDED(layout->GetClusterMetrics(metrics.data(),count,&count))) {
        std::size_t offset=0; for (const auto& metric:metrics) { offset+=metric.length; offsets.push_back(offset); }
        model.set_clusters(std::move(offsets));
    }
    DWRITE_TEXT_METRICS text_metrics{};
    if (SUCCEEDED(layout->GetMetrics(&text_metrics))) text_height=(std::max)(text_metrics.height,rectangle.bottom-rectangle.top);
    scroll_y=std::clamp(scroll_y,0.f,(std::max)(0.f,text_height-(rectangle.bottom-rectangle.top)));
    layout_revision=model.revision();
}
void ProjectedEditor::Impl::changed(std::size_t old_length, bool from_tsf) {
    if (!from_tsf) { update_layout(); ensure_caret_visible(); if (store) { store->text_change(static_cast<LONG>(old_length)); store->selection_change(); store->layout_change(); } }
    artwork_changed(); accessibility_event(UIA_Text_TextChangedEventId);
}
void ProjectedEditor::Impl::notify_selection() { ensure_caret_visible(); if (store) store->selection_change(); artwork_changed(); accessibility_event(UIA_Text_TextSelectionChangedEventId); }
void ProjectedEditor::Impl::terminate_composition() {
    if (!context || !model.composing()) return;
    ComPtr<ITfContextOwnerCompositionServices> service;
    if (SUCCEEDED(context.As(&service))) service->TerminateComposition(nullptr);
}
bool ProjectedEditor::Impl::screen_rect(D2D1_RECT_F source, RECT* result) {
    double min_x=1e30,min_y=1e30,max_x=-1e30,max_y=-1e30;
    for (const auto& point:std::array<std::array<double,2>,4>{{{source.left,source.top},{source.right,source.top},{source.right,source.bottom},{source.left,source.bottom}}}) {
        double x{},y{}; if (!projection.project(point[0],point[1],x,y)) return false;
        min_x=(std::min)(min_x,x); min_y=(std::min)(min_y,y); max_x=(std::max)(max_x,x); max_y=(std::max)(max_y,y);
    }
    POINT origin{}; if (!ClientToScreen(owner,&origin)) return false;
    if (min_x<-1e8 || min_y<-1e8 || max_x>1e8 || max_y>1e8) return false;
    *result={static_cast<LONG>(std::floor(min_x))+origin.x,static_cast<LONG>(std::floor(min_y))+origin.y,
        static_cast<LONG>(std::ceil(max_x))+origin.x,static_cast<LONG>(std::ceil(max_y))+origin.y}; return true;
}
bool ProjectedEditor::Impl::hit(POINT client, LONG* position, bool require_inside) {
    double x{},y{}; if (!projection.unproject(client.x,client.y,x,y)) return false;
    if (require_inside && (x<rectangle.left || x>rectangle.right || y<rectangle.top || y>rectangle.bottom)) return false;
    update_layout(); if (!layout) return false;
    BOOL trailing{},inside{}; DWRITE_HIT_TEST_METRICS metric{};
    if (FAILED(layout->HitTestPoint(static_cast<FLOAT>(x-rectangle.left),static_cast<FLOAT>(y-rectangle.top)+scroll_y,&trailing,&inside,&metric))) return false;
    *position=static_cast<LONG>(metric.textPosition+(trailing ? metric.length : 0)); return true;
}
ProjectedEditor::ProjectedEditor() : impl_(std::make_unique<Impl>()) {}
ProjectedEditor::~ProjectedEditor() { shutdown(); }
HRESULT ProjectedEditor::initialize(HWND owner, IDWriteFactory* factory, std::function<void()> invalidate) {
    shutdown(); auto& p=*impl_; if (!owner || !factory) return E_INVALIDARG;
    p.owner=owner; p.factory=factory; p.invalidate=std::move(invalidate);
    HRESULT hr=factory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,18,L"en-US",&p.format); if (FAILED(hr)) return hr;
    p.update_layout();
    p.tsf_result=CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.thread));
    if (SUCCEEDED(p.tsf_result)) p.tsf_result=p.thread->Activate(&p.client_id);
    if (SUCCEEDED(p.tsf_result)) p.tsf_result=p.thread->CreateDocumentMgr(&p.document);
    p.store.Attach(new Impl::TextStore(p));
    if (SUCCEEDED(p.tsf_result)) p.tsf_result=p.document->CreateContext(p.client_id,0,static_cast<ITextStoreACP*>(p.store.Get()),&p.context,&p.edit_cookie);
    if (SUCCEEDED(p.tsf_result)) p.tsf_result=p.document->Push(p.context.Get());
    if (SUCCEEDED(p.tsf_result)) p.tsf_result=p.thread.As(&p.keys);
    if (SUCCEEDED(p.tsf_result)) p.tsf_result=p.thread->AssociateFocus(owner,p.document.Get(),&p.previous_document);
    if (SUCCEEDED(p.tsf_result)) p.tsf_result=p.context.As(&p.context_source);
    if (SUCCEEDED(p.tsf_result)) p.tsf_result=p.context_source->AdviseSink(IID_ITfTextEditSink,
        static_cast<ITfTextEditSink*>(p.store.Get()),&p.edit_sink_cookie);
    CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.category_manager));
    CoCreateInstance(CLSID_TF_DisplayAttributeMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.display_attributes));
    p.accessibility_lifetime=std::make_shared<Impl::AccessibilityLifetime>(Impl::AccessibilityLifetime{&p,GetCurrentThreadId()});
    p.accessibility.Attach(new Impl::AccessibilityProvider(p.accessibility_lifetime));
    return p.tsf_result;
}
void ProjectedEditor::shutdown() {
    auto& p=*impl_; p.active=false; p.terminate_composition();
    if (p.accessibility_lifetime) p.accessibility_lifetime->owner=nullptr;
    if (p.accessibility) UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(p.accessibility.Get()));
    p.accessibility.Reset(); p.accessibility_lifetime.reset();
    if (p.context_source && p.edit_sink_cookie!=TF_INVALID_COOKIE) p.context_source->UnadviseSink(p.edit_sink_cookie);
    p.edit_sink_cookie=TF_INVALID_COOKIE; p.context_source.Reset(); p.category_manager.Reset(); p.display_attributes.Reset();
    if (p.thread && p.owner && p.document) {
        ComPtr<ITfDocumentMgr> associated;
        p.thread->AssociateFocus(p.owner,p.previous_document.Get(),&associated);
    }
    p.previous_document.Reset();
    if (p.document) p.document->Pop(TF_POPF_ALL); p.context.Reset(); p.document.Reset(); p.keys.Reset(); p.store.Reset();
    if (p.thread && p.client_id) p.thread->Deactivate(); p.thread.Reset(); p.client_id=0;
    p.layout.Reset(); p.format.Reset(); p.factory.Reset(); p.owner=nullptr; p.pending_high=0; p.invalidate={};
    p.active_composition.Reset(); p.marked_runs.clear(); p.marked_start=p.marked_length=0; p.scroll_y=p.text_height=0; ++p.artwork_version;
    p.layout_revision=(std::numeric_limits<std::uint64_t>::max)();
}
bool ProjectedEditor::set_text(std::u16string text) {
    auto& p=*impl_; if (p.store && p.store->locked()) return false; p.terminate_composition();
    auto old=p.model.text().size(); if (!p.model.set_text(std::move(text))) return false; p.scroll_y=0; p.changed(old); return true;
}
void ProjectedEditor::set_rectangle(D2D1_RECT_F rectangle) {
    auto& p=*impl_; if (std::memcmp(&rectangle,&p.rectangle,sizeof(rectangle))==0) return;
    if (!std::isfinite(rectangle.left) || !std::isfinite(rectangle.top) || !std::isfinite(rectangle.right) || !std::isfinite(rectangle.bottom) ||
        rectangle.right<=rectangle.left || rectangle.bottom<=rectangle.top) return;
    p.rectangle=rectangle; p.layout_revision=(std::numeric_limits<std::uint64_t>::max)(); p.update_layout(); p.ensure_caret_visible();
    if (p.store) p.store->layout_change(); p.artwork_changed();
}
void ProjectedEditor::set_projection(ProjectiveMapping mapping) {
    auto& p=*impl_; if (mapping.values==p.projection.values) return; p.projection=mapping;
    if (p.store) p.store->layout_change();
}
void ProjectedEditor::focus(bool active) {
    auto& p=*impl_; if (p.active==active) return; if (!active) p.terminate_composition(); p.active=active;
    if (p.thread) p.thread->SetFocus(active ? p.document.Get() : nullptr);
    if (!active && p.dragging) { p.dragging=false; ReleaseCapture(); }
    if (active) p.ensure_caret_visible();
    if (p.store) p.store->layout_change(); p.artwork_changed();
    if (active) p.accessibility_event(UIA_AutomationFocusChangedEventId);
}
bool ProjectedEditor::focused() const { return impl_->active; }
bool ProjectedEditor::pointer_tracking() const { return impl_->dragging; }
std::uint64_t ProjectedEditor::artwork_revision() const { return impl_->artwork_version; }
float ProjectedEditor::scroll_offset() const { return impl_->scroll_y; }
void ProjectedEditor::scroll_to(float logical_y) { impl_->scroll(logical_y); }
void ProjectedEditor::select_range(std::size_t anchor,std::size_t caret) {
    auto& p=*impl_; if (p.store && p.store->locked()) return; p.terminate_composition(); p.model.select(anchor,caret); p.notify_selection();
}
void ProjectedEditor::set_accessible_name(std::wstring name) {
    auto& p=*impl_; if (name.empty() || name.size()>1024 || name==p.accessible_name) return;
    std::wstring previous=std::move(p.accessible_name); p.accessible_name=std::move(name);
    if (p.accessibility && UiaClientsAreListening()) {
        VARIANT old_value,new_value; VariantInit(&old_value); VariantInit(&new_value);
        old_value.vt=new_value.vt=VT_BSTR; old_value.bstrVal=SysAllocString(previous.c_str()); new_value.bstrVal=SysAllocString(p.accessible_name.c_str());
        if (old_value.bstrVal && new_value.bstrVal) UiaRaiseAutomationPropertyChangedEvent(
            static_cast<IRawElementProviderSimple*>(p.accessibility.Get()),UIA_NamePropertyId,old_value,new_value);
        VariantClear(&old_value); VariantClear(&new_value);
    }
}
HRESULT ProjectedEditor::get_accessibility_provider(REFIID iid,void** provider) {
    if (!provider) return E_POINTER; *provider=nullptr;
    return impl_->accessibility ? impl_->accessibility->QueryInterface(iid,provider) : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT ProjectedEditor::tsf_status() const { return impl_->tsf_result; }
const EditorModel& ProjectedEditor::model() const { return impl_->model; }
bool ProjectedEditor::pre_translate(const MSG& message) {
    auto& p=*impl_; if (!p.active || !p.keys) return false; BOOL eaten=FALSE;
    if (message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN) {
        if (SUCCEEDED(p.keys->TestKeyDown(message.wParam,message.lParam,&eaten)) && eaten)
            p.keys->KeyDown(message.wParam,message.lParam,&eaten);
    } else if (message.message==WM_KEYUP || message.message==WM_SYSKEYUP) {
        if (SUCCEEDED(p.keys->TestKeyUp(message.wParam,message.lParam,&eaten)) && eaten)
            p.keys->KeyUp(message.wParam,message.lParam,&eaten);
    }
    return eaten!=FALSE;
}
namespace {
bool clipboard_copy(HWND owner, std::u16string_view text) {
    if (!OpenClipboard(owner)) return false;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(WCHAR));
    if (!memory) { CloseClipboard(); return false; }
    void* data=GlobalLock(memory); if (!data) { GlobalFree(memory); CloseClipboard(); return false; }
    std::memcpy(data,text.data(),text.size()*sizeof(WCHAR)); static_cast<WCHAR*>(data)[text.size()]=0; GlobalUnlock(memory);
    bool done=EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;
    if (!done) GlobalFree(memory); CloseClipboard(); return done;
}
std::u16string clipboard_paste(HWND owner) {
    std::u16string result; if (!OpenClipboard(owner)) return result;
    HANDLE memory=GetClipboardData(CF_UNICODETEXT);
    if (memory) {
        SIZE_T size=GlobalSize(memory); const auto* data=static_cast<const char16_t*>(GlobalLock(memory));
        if (data && size <= (EditorModel::maximum_units+1)*sizeof(char16_t)) {
            std::size_t count=0,capacity=size/sizeof(char16_t); while (count<capacity && data[count]) ++count;
            if (count<capacity) result.assign(data,count);
        }
        if (data) GlobalUnlock(memory);
    }
    CloseClipboard(); return result;
}
}
bool ProjectedEditor::handle_message(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result) {
    auto& p=*impl_; result=0;
    if (message==WM_GETOBJECT && static_cast<LONG>(lparam)==UiaRootObjectId && p.accessibility) {
        result=UiaReturnRawElementProvider(p.owner,wparam,lparam,static_cast<IRawElementProviderSimple*>(p.accessibility.Get())); return true;
    }
    if (message==WM_CAPTURECHANGED || message==WM_CANCELMODE) { p.dragging=false; return false; }
    if (message==WM_KILLFOCUS) { focus(false); return false; }
    if (message==WM_MOUSEWHEEL && p.layout) {
        POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}; ScreenToClient(p.owner,&point); LONG offset{};
        if (!p.hit(point,&offset,true)) return false;
        UINT lines=3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
        const float delta=static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam))/WHEEL_DELTA;
        double local_x{},local_y{},offset_x{},offset_y{};
        const float screen_delta=delta*(lines==WHEEL_PAGESCROLL ? p.rectangle.bottom-p.rectangle.top : lines*21.f)*GetDpiForWindow(p.owner)/96.f;
        if (p.projection.unproject(point.x,point.y,local_x,local_y) &&
            p.projection.unproject(point.x,point.y+screen_delta,offset_x,offset_y)) p.scroll(p.scroll_y-static_cast<float>(offset_y-local_y));
        return true;
    }
    if (message==WM_LBUTTONDOWN || message==WM_MOUSEMOVE || message==WM_LBUTTONUP) {
        if (message==WM_MOUSEMOVE && !p.dragging) return false;
        if (message==WM_LBUTTONUP) { if (!p.dragging) return false; p.dragging=false; ReleaseCapture(); return true; }
        LONG position{}; if (!p.hit({GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)},&position,!p.dragging)) return false;
        if (message==WM_LBUTTONDOWN) { p.terminate_composition(); SetFocus(p.owner); focus(true); p.dragging=true; SetCapture(p.owner);
            p.model.select((GetKeyState(VK_SHIFT)&0x8000) ? p.model.anchor() : position,position); }
        else p.model.select(p.model.anchor(),position);
        p.notify_selection(); return true;
    }
    if (!p.active || (p.store && p.store->locked())) return false;
    if (message==WM_KEYDOWN) {
        bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if (wparam==VK_ESCAPE) { if (p.model.composing()) p.terminate_composition(); else focus(false); return true; }
        if (p.model.composing()) return false;
        auto old=p.model.text().size();
        if (control && wparam=='A') { p.model.select(0,p.model.text().size()); p.notify_selection(); return true; }
        if (control && (wparam=='C' || wparam=='X')) {
            bool copied=clipboard_copy(p.owner,std::u16string_view(p.model.text()).substr(p.model.begin(),p.model.end()-p.model.begin()));
            if (wparam=='X' && copied && p.model.begin()!=p.model.end()) { p.model.insert({}); p.changed(old); } return true;
        }
        if (control && wparam=='V') { auto incoming=clipboard_paste(p.owner); if (!incoming.empty() && p.model.insert(incoming)) p.changed(old); return true; }
        if (control && (wparam=='Z' || wparam=='Y')) {
            if ((wparam=='Y' || shift) ? p.model.redo() : p.model.undo()) p.changed(old); return true;
        }
        p.update_layout();
        if (wparam==VK_LEFT || wparam==VK_RIGHT) { p.model.move(wparam==VK_RIGHT,shift); p.notify_selection(); return true; }
        if (wparam==VK_HOME || wparam==VK_END) {
            std::size_t position=wparam==VK_HOME ? 0 : p.model.text().size();
            if (!control && p.layout) {
                FLOAT x{},y{}; DWRITE_HIT_TEST_METRICS current{},edge{}; BOOL trailing{},inside{};
                if (SUCCEEDED(p.layout->HitTestTextPosition(static_cast<UINT32>(p.model.caret()),FALSE,&x,&y,&current)) &&
                    SUCCEEDED(p.layout->HitTestPoint(wparam==VK_HOME ? -1.f : p.rectangle.right-p.rectangle.left+1.f,y+current.height*.5f,&trailing,&inside,&edge)))
                    position=edge.textPosition+(trailing ? edge.length : 0);
            }
            p.model.select(shift ? p.model.anchor() : position,position); p.notify_selection(); return true;
        }
        if (wparam==VK_PRIOR || wparam==VK_NEXT) {
            FLOAT x{},y{}; DWRITE_HIT_TEST_METRICS current{},edge{}; BOOL trailing{},inside{};
            if (p.layout && SUCCEEDED(p.layout->HitTestTextPosition(static_cast<UINT32>(p.model.caret()),FALSE,&x,&y,&current))) {
                float target=y+(wparam==VK_NEXT ? 1.f : -1.f)*(p.rectangle.bottom-p.rectangle.top);
                if (SUCCEEDED(p.layout->HitTestPoint(x,target,&trailing,&inside,&edge))) {
                    auto position=edge.textPosition+(trailing ? edge.length : 0); p.model.select(shift ? p.model.anchor() : position,position); p.notify_selection();
                }
            } return true;
        }
        if (wparam==VK_UP || wparam==VK_DOWN) {
            FLOAT x{},y{}; DWRITE_HIT_TEST_METRICS metric{};
            if (p.layout && SUCCEEDED(p.layout->HitTestTextPosition(static_cast<UINT32>(p.model.caret()),FALSE,&x,&y,&metric))) {
                BOOL trailing{},inside{}; DWRITE_HIT_TEST_METRICS target{};
                p.layout->HitTestPoint(x,y+metric.height*(wparam==VK_DOWN ? 1.5f : -.5f),&trailing,&inside,&target);
                auto position=target.textPosition+(trailing ? target.length : 0); p.model.select(shift ? p.model.anchor() : position,position); p.notify_selection();
            } return true;
        }
        if (wparam==VK_BACK || wparam==VK_DELETE) { if (p.model.erase(wparam==VK_DELETE)) p.changed(old); return true; }
        return false;
    }
    if (message==WM_CHAR) {
        char16_t unit=static_cast<char16_t>(wparam);
        if (unit<32 && unit!=u'\r' && unit!=u'\t') return true;
        if (p.model.composing()) return true;
        if (high(unit)) { p.pending_high=unit; return true; }
        std::u16string incoming;
        if (low(unit)) { if (!p.pending_high) return true; incoming.push_back(p.pending_high); }
        p.pending_high=0; incoming.push_back(unit==u'\r' ? u'\n' : unit);
        auto old=p.model.text().size(); if (p.model.insert(incoming)) p.changed(old); return true;
    }
    return false;
}
void ProjectedEditor::draw(ID2D1RenderTarget* target) {
    auto& p=*impl_; p.update_layout(); if (!target || !p.layout) return;
    ComPtr<ID2D1SolidColorBrush> ink,selected,outline;
    if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(.91f,.94f,.95f,1),&ink)) ||
        FAILED(target->CreateSolidColorBrush(D2D1::ColorF(.35f,.66f,.72f,.4f),&selected)) ||
        FAILED(target->CreateSolidColorBrush(D2D1::ColorF(.4f,.7f,.76f,p.active ? .9f : .35f),&outline))) return;
    target->DrawRectangle(p.rectangle,outline.Get(),1.f);
    target->PushAxisAlignedClip(p.rectangle,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (p.model.begin()!=p.model.end()) {
        for (auto rect:p.range_rectangles(p.model.begin(),p.model.end(),true)) target->FillRectangle(rect,selected.Get());
    }
    auto color=[](TF_DA_COLOR requested,D2D1_COLOR_F fallback) -> D2D1_COLOR_F {
        COLORREF value{};
        if (requested.type==TF_CT_COLORREF) value=requested.cr;
        else if (requested.type==TF_CT_SYSCOLOR) value=GetSysColor(requested.nIndex);
        else return fallback;
        return D2D1::ColorF(GetRValue(value)/255.f,GetGValue(value)/255.f,GetBValue(value)/255.f,1);
    };
    ComPtr<ID2D1SolidColorBrush> decoration;
    target->CreateSolidColorBrush(D2D1::ColorF(.8f,.9f,1,1),&decoration);
    if (decoration) for (const auto& run:p.marked_runs) if (run.attribute.crBk.type!=TF_CT_NONE) {
        decoration->SetColor(color(run.attribute.crBk,D2D1::ColorF(.2f,.3f,.35f,1)));
        for (auto rect:p.range_rectangles(run.start,run.start+run.length,true)) target->FillRectangle(rect,decoration.Get());
    }
    // Display attributes may request a contrasting glyph color. Effects are
    // scoped to this one draw, so layouts never retain brushes from an obsolete
    // GPU target after resize/device loss.
    std::vector<ComPtr<ID2D1SolidColorBrush>> marked_ink;
    for (const auto& run:p.marked_runs) if (run.attribute.crText.type!=TF_CT_NONE) {
        ComPtr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(target->CreateSolidColorBrush(color(run.attribute.crText,D2D1::ColorF(.91f,.94f,.95f,1)),&brush))) {
            p.layout->SetDrawingEffect(brush.Get(),{static_cast<UINT32>(run.start),static_cast<UINT32>(run.length)});
            marked_ink.push_back(std::move(brush));
        }
    }
    target->DrawTextLayout(D2D1::Point2F(p.rectangle.left,p.rectangle.top-p.scroll_y),p.layout.Get(),ink.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (!marked_ink.empty()) p.layout->SetDrawingEffect(nullptr,{0,static_cast<UINT32>(p.model.text().size())});
    auto underline=[&](LONG start,LONG length,TF_DISPLAYATTRIBUTE attribute) {
        if (!decoration || length<=0 || attribute.lsStyle==TF_LS_NONE) return;
        decoration->SetColor(color(attribute.crLine,D2D1::ColorF(.8f,.9f,1,1)));
        const float thickness=attribute.fBoldLine ? 2.f : 1.f;
        for (auto rect:p.range_rectangles(start,start+length,true)) {
            const float y=rect.bottom-1.f;
            if (attribute.lsStyle==TF_LS_SOLID) target->DrawLine(D2D1::Point2F(rect.left,y),D2D1::Point2F(rect.right,y),decoration.Get(),thickness);
            else if (attribute.lsStyle==TF_LS_SQUIGGLE) {
                bool raised=false; for (float x=rect.left;x<rect.right;x+=2.f) {
                    target->DrawLine(D2D1::Point2F(x,y+(raised ? -1.f : 1.f)),D2D1::Point2F((std::min)(rect.right,x+2),y+(raised ? 1.f : -1.f)),decoration.Get(),thickness); raised=!raised;
                }
            } else {
                const float segment=attribute.lsStyle==TF_LS_DOT ? 1.f : 4.f;
                for (float x=rect.left;x<rect.right;x+=segment+2.f) target->DrawLine(D2D1::Point2F(x,y),D2D1::Point2F((std::min)(rect.right,x+segment),y),decoration.Get(),thickness);
            }
        }
    };
    if (p.model.composing()) {
        if (p.marked_runs.empty()) { TF_DISPLAYATTRIBUTE fallback{}; fallback.lsStyle=TF_LS_SOLID; underline(p.marked_start,p.marked_length,fallback); }
        else for (const auto& run:p.marked_runs) underline(run.start,run.length,run.attribute);
    }
    if (p.active && p.model.begin()==p.model.end()) {
        FLOAT x{},y{}; DWRITE_HIT_TEST_METRICS metrics{};
        if (SUCCEEDED(p.layout->HitTestTextPosition(static_cast<UINT32>(p.model.caret()),FALSE,&x,&y,&metrics)))
            target->FillRectangle(D2D1::RectF(p.rectangle.left+x,p.rectangle.top+y-p.scroll_y,p.rectangle.left+x+1.5f,p.rectangle.top+y-p.scroll_y+metrics.height),ink.Get());
    }
    const float visible_height=p.rectangle.bottom-p.rectangle.top;
    if (p.text_height>visible_height) {
        const float thumb=(std::max)(16.f,visible_height*visible_height/p.text_height);
        const float y=p.rectangle.top+(visible_height-thumb)*p.scroll_y/(p.text_height-visible_height);
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(p.rectangle.right-4,y,p.rectangle.right-1,y+thumb),1.5f,1.5f),outline.Get());
    }
    target->PopAxisAlignedClip();
}
std::wstring ProjectedEditor::font_inventory() const {
    auto& p=*impl_; if (!p.factory) return L"DirectWrite unavailable";
    ComPtr<IDWriteFontCollection> collection; if (FAILED(p.factory->GetSystemFontCollection(&collection))) return L"Font inventory failed";
    std::wstring result;
    for (const wchar_t* name:{L"Segoe UI",L"Segoe UI Emoji",L"Microsoft YaHei UI",L"Microsoft JhengHei UI",L"Yu Gothic UI",L"Meiryo UI",L"Malgun Gothic"}) {
        UINT32 index{}; BOOL exists{}; collection->FindFamilyName(name,&index,&exists);
        result+=name; result+=exists ? L": installed\n" : L": absent; DirectWrite fallback required\n";
    }
    return result;
}
}
#endif
