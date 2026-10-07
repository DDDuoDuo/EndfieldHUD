#include "projected_editor.h"
#include <algorithm>
#include <cmath>
#include <limits>

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
#include <atomic>
#include <cstring>

namespace endfield::platform {
using Microsoft::WRL::ComPtr;
struct ProjectedEditor::Impl {
    class TextStore;
    HWND owner{};
    ComPtr<IDWriteFactory> factory;
    ComPtr<IDWriteTextFormat> format;
    ComPtr<IDWriteTextLayout> layout;
    ComPtr<ITfThreadMgr> thread;
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfDocumentMgr> previous_document;
    ComPtr<ITfContext> context;
    ComPtr<ITfKeystrokeMgr> keys;
    ComPtr<TextStore> store;
    TfClientId client_id{};
    TfEditCookie edit_cookie{};
    HRESULT tsf_result{E_PENDING};
    EditorModel model;
    D2D1_RECT_F rectangle{0,0,500,220};
    ProjectiveMapping projection;
    std::function<void()> invalidate;
    bool active{}, dragging{};
    std::uint64_t layout_revision{(std::numeric_limits<std::uint64_t>::max)()};
    char16_t pending_high{};
    void update_layout();
    void changed(std::size_t old_length, bool from_tsf = false);
    bool screen_rect(D2D1_RECT_F source, RECT* result);
    bool hit(POINT client, LONG* position, bool require_inside);
    void notify_selection();
    void terminate_composition();
};

// TSF talks directly to this UTF-16 store; DirectWrite supplies artwork only.
// No visible or hidden native EDIT window is used to own input.
class ProjectedEditor::Impl::TextStore final : public ITextStoreACP, public ITfContextOwnerCompositionSink {
public:
    explicit TextStore(Impl& owner) : host(owner) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITextStoreACP) *out = static_cast<ITextStoreACP*>(this);
        else if (iid == IID_ITfContextOwnerCompositionSink) *out = static_cast<ITfContextOwnerCompositionSink*>(this);
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
        if (layout_dirty) { layout_dirty = false; host.update_layout(); layout_change(); }
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
        if (host.invalidate) host.invalidate(); return S_OK;
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
        *clipped=rect.top<0 || rect.left<0 || rect.right>host.rectangle.right-host.rectangle.left || rect.bottom>host.rectangle.bottom-host.rectangle.top;
        rect.left+=host.rectangle.left; rect.right+=host.rectangle.left; rect.top+=host.rectangle.top; rect.bottom+=host.rectangle.top;
        return host.screen_rect(rect,output) ? S_OK : TS_E_NOLAYOUT;
    }
    STDMETHODIMP GetScreenExt(TsViewCookie view, RECT* output) override {
        if (!output) return E_POINTER; if (view) return E_INVALIDARG;
        if (!host.active) { *output={}; return S_OK; } return host.screen_rect(host.rectangle,output) ? S_OK : TS_E_NOLAYOUT;
    }
    STDMETHODIMP GetWnd(TsViewCookie view, HWND* hwnd) override { if (!hwnd) return E_POINTER; if (view) return E_INVALIDARG; *hwnd=host.owner; return S_OK; }
    STDMETHODIMP OnStartComposition(ITfCompositionView*, BOOL* allowed) override {
        if (!allowed) return E_POINTER; *allowed=!host.model.composing();
        if (*allowed) host.model.begin_composition(); return S_OK;
    }
    STDMETHODIMP OnUpdateComposition(ITfCompositionView*, ITfRange*) override { if (host.invalidate) host.invalidate(); return S_OK; }
    STDMETHODIMP OnEndComposition(ITfCompositionView*) override { host.model.end_composition(); if (host.invalidate) host.invalidate(); return S_OK; }
    void text_change(LONG old_length) {
        if (sink && (mask & TS_AS_TEXT_CHANGE)) { TS_TEXTCHANGE change{0,old_length,static_cast<LONG>(host.model.text().size())}; sink->OnTextChange(0,&change); }
    }
    void selection_change() { if (sink && (mask & TS_AS_SEL_CHANGE)) sink->OnSelectionChange(); }
    void layout_change() { if (sink && (mask & TS_AS_LAYOUT_CHANGE)) sink->OnLayoutChange(TS_LC_CHANGE,0); }
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

void ProjectedEditor::Impl::update_layout() {
    if (!factory || !format || layout_revision==model.revision()) return;
    layout.Reset();
    if (FAILED(factory->CreateTextLayout(reinterpret_cast<const WCHAR*>(model.text().data()),
        static_cast<UINT32>(model.text().size()),format.Get(),(std::max)(1.f,rectangle.right-rectangle.left),
        (std::max)(1.f,rectangle.bottom-rectangle.top),&layout))) return;
    // DirectWrite clusters prevent arrow/backspace from dividing shaped glyphs.
    UINT32 count{}; layout->GetClusterMetrics(nullptr,0,&count);
    std::vector<DWRITE_CLUSTER_METRICS> metrics(count); std::vector<std::size_t> offsets{0};
    if (SUCCEEDED(layout->GetClusterMetrics(metrics.data(),count,&count))) {
        std::size_t offset=0; for (const auto& metric:metrics) { offset+=metric.length; offsets.push_back(offset); }
        model.set_clusters(std::move(offsets));
    }
    layout_revision=model.revision();
}
void ProjectedEditor::Impl::changed(std::size_t old_length, bool from_tsf) {
    if (!from_tsf) { update_layout(); if (store) { store->text_change(static_cast<LONG>(old_length)); store->selection_change(); store->layout_change(); } }
    if (invalidate) invalidate();
}
void ProjectedEditor::Impl::notify_selection() { if (store) store->selection_change(); if (invalidate) invalidate(); }
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
    if (FAILED(layout->HitTestPoint(static_cast<FLOAT>(x-rectangle.left),static_cast<FLOAT>(y-rectangle.top),&trailing,&inside,&metric))) return false;
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
    return p.tsf_result;
}
void ProjectedEditor::shutdown() {
    auto& p=*impl_; p.active=false; p.terminate_composition();
    if (p.thread && p.owner && p.document) {
        ComPtr<ITfDocumentMgr> associated;
        p.thread->AssociateFocus(p.owner,p.previous_document.Get(),&associated);
    }
    p.previous_document.Reset();
    if (p.document) p.document->Pop(TF_POPF_ALL); p.context.Reset(); p.document.Reset(); p.keys.Reset(); p.store.Reset();
    if (p.thread && p.client_id) p.thread->Deactivate(); p.thread.Reset(); p.client_id=0;
    p.layout.Reset(); p.format.Reset(); p.factory.Reset(); p.owner=nullptr; p.pending_high=0; p.invalidate={};
    p.layout_revision=(std::numeric_limits<std::uint64_t>::max)();
}
bool ProjectedEditor::set_text(std::u16string text) {
    auto& p=*impl_; if (p.store && p.store->locked()) return false; p.terminate_composition();
    auto old=p.model.text().size(); if (!p.model.set_text(std::move(text))) return false; p.changed(old); return true;
}
void ProjectedEditor::set_rectangle(D2D1_RECT_F rectangle) {
    auto& p=*impl_; if (std::memcmp(&rectangle,&p.rectangle,sizeof(rectangle))==0) return;
    p.rectangle=rectangle; p.layout_revision=(std::numeric_limits<std::uint64_t>::max)(); p.update_layout();
    if (p.store) p.store->layout_change(); if (p.invalidate) p.invalidate();
}
void ProjectedEditor::set_projection(ProjectiveMapping mapping) {
    auto& p=*impl_; if (mapping.values==p.projection.values) return; p.projection=mapping;
    if (p.store) p.store->layout_change();
}
void ProjectedEditor::focus(bool active) {
    auto& p=*impl_; if (p.active==active) return; if (!active) p.terminate_composition(); p.active=active;
    if (p.thread) p.thread->SetFocus(active ? p.document.Get() : nullptr);
    if (!active && p.dragging) { p.dragging=false; ReleaseCapture(); }
    if (p.store) p.store->layout_change(); if (p.invalidate) p.invalidate();
}
bool ProjectedEditor::focused() const { return impl_->active; }
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
    if (message==WM_CAPTURECHANGED || message==WM_CANCELMODE) { p.dragging=false; return false; }
    if (message==WM_KILLFOCUS) { focus(false); return false; }
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
            p.model.select(shift ? p.model.anchor() : position,position); p.notify_selection(); return true;
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
        UINT32 count{}; p.layout->HitTestTextRange(static_cast<UINT32>(p.model.begin()),static_cast<UINT32>(p.model.end()-p.model.begin()),p.rectangle.left,p.rectangle.top,nullptr,0,&count);
        std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
        if (count && SUCCEEDED(p.layout->HitTestTextRange(static_cast<UINT32>(p.model.begin()),static_cast<UINT32>(p.model.end()-p.model.begin()),p.rectangle.left,p.rectangle.top,metrics.data(),count,&count)))
            for (const auto& metric:metrics) target->FillRectangle(D2D1::RectF(metric.left,metric.top,metric.left+metric.width,metric.top+metric.height),selected.Get());
    }
    target->DrawTextLayout(D2D1::Point2F(p.rectangle.left,p.rectangle.top),p.layout.Get(),ink.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (p.active && p.model.begin()==p.model.end()) {
        FLOAT x{},y{}; DWRITE_HIT_TEST_METRICS metrics{};
        if (SUCCEEDED(p.layout->HitTestTextPosition(static_cast<UINT32>(p.model.caret()),FALSE,&x,&y,&metrics)))
            target->FillRectangle(D2D1::RectF(p.rectangle.left+x,p.rectangle.top+y,p.rectangle.left+x+1.5f,p.rectangle.top+y+metrics.height),ink.Get());
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
