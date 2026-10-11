#include "native/notes_automation.hpp"
#ifdef _WIN32
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
using Role=NotesAutomationRole;
std::wstring wide(std::string_view s){
    if(s.empty()||s.size()>INT_MAX)return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);if(n<=0)return {};
    std::wstring w(static_cast<std::size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);return w;
}
std::string narrow(std::wstring_view w){
    if(w.empty()||w.size()>INT_MAX)return {};
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr);if(n<=0)return {};
    std::string s(static_cast<std::size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),s.data(),n,nullptr,nullptr);return s;
}
std::atomic<int>nextRuntime{1};
VARIANT text(std::string_view s){VARIANT v;VariantInit(&v);v.vt=VT_BSTR;v.bstrVal=SysAllocString(wide(s).c_str());return v;}
VARIANT flag(bool b){VARIANT v;VariantInit(&v);v.vt=VT_BOOL;v.boolVal=b?VARIANT_TRUE:VARIANT_FALSE;return v;}
VARIANT number(double d){VARIANT v;VariantInit(&v);v.vt=VT_R8;v.dblVal=d;return v;}
}

std::function<std::optional<core::Point>()> notesAutomationWindowOrigin(HWND window){
    return [window]()->std::optional<core::Point>{POINT p{};if(!IsWindow(window)||!ClientToScreen(window,&p))return std::nullopt;return core::Point{double(p.x),double(p.y)};};
}

struct NativeNotesAutomation::State {
    NotesAutomationHost host;std::vector<ComPtr<NotesAutomationPeer>>order;NotesAutomationStats statistics;std::vector<NotesAutomationElement>scratch;
    std::optional<core::Rect>screen(const core::Rect&r)const{
        if(!host.origin)return std::nullopt;const auto o=host.origin();if(!o||!std::isfinite(o->x)||!std::isfinite(o->y))return std::nullopt;
        if(!std::isfinite(r.x)||!std::isfinite(r.y)||!std::isfinite(r.width)||!std::isfinite(r.height)||r.width<=0||r.height<=0)return std::nullopt;
        return core::Rect{o->x+r.x,o->y+r.y,r.width,r.height};
    }
};

class NotesAutomationPeer final:public IRawElementProviderSimple,public IRawElementProviderFragment,public IInvokeProvider,public IRangeValueProvider,public IValueProvider {
public:
    using State=NativeNotesAutomation::State;
    NotesAutomationPeer(std::weak_ptr<State>s,NotesAutomationElement e):state(std::move(s)),control(std::move(e)),runtime(nextRuntime++){}
    std::weak_ptr<State>state;NotesAutomationElement control;int runtime;bool connected{true};
    IFACEMETHODIMP QueryInterface(REFIID riid,void**out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(riid==__uuidof(IUnknown)||riid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(riid==__uuidof(IRawElementProviderFragment))*out=static_cast<IRawElementProviderFragment*>(this);
        else if(riid==__uuidof(IInvokeProvider)&&control.role==Role::button)*out=static_cast<IInvokeProvider*>(this);
        else if(riid==__uuidof(IRangeValueProvider)&&control.role==Role::slider)*out=static_cast<IRangeValueProvider*>(this);
        else if(riid==__uuidof(IValueProvider)&&control.role==Role::edit)*out=static_cast<IValueProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    IFACEMETHODIMP_(ULONG)AddRef()override{return ++refs;}
    IFACEMETHODIMP_(ULONG)Release()override{const ULONG n=--refs;if(!n)delete this;return n;}
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions*out)override{if(!out)return E_POINTER;*out=ProviderOptions(ProviderOptions_ServerSideProvider|ProviderOptions_UseComThreading);return S_OK;}
    IFACEMETHODIMP GetPatternProvider(PATTERNID id,IUnknown**out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        if(id==UIA_InvokePatternId&&control.role==Role::button){*out=static_cast<IInvokeProvider*>(this);AddRef();}
        else if(id==UIA_RangeValuePatternId&&control.role==Role::slider){*out=static_cast<IRangeValueProvider*>(this);AddRef();}
        else if(id==UIA_ValuePatternId&&control.role==Role::edit){*out=static_cast<IValueProvider*>(this);AddRef();}
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id,VARIANT*out)override{
        if(!out)return E_POINTER;VariantInit(out);if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        switch(id){
        case UIA_NamePropertyId:*out=text(control.label);break;
        case UIA_HelpTextPropertyId:if(!control.help.empty())*out=text(control.help);break;
        case UIA_AutomationIdPropertyId:*out=text(control.id);break;
        case UIA_ControlTypePropertyId:out->vt=VT_I4;out->lVal=control.role==Role::slider?UIA_SliderControlTypeId:control.role==Role::edit?UIA_EditControlTypeId:UIA_ButtonControlTypeId;break;
        case UIA_IsEnabledPropertyId:*out=flag(control.enabled);break;
        case UIA_IsKeyboardFocusablePropertyId:*out=flag(control.role==Role::edit);break;
        // The field the HUD's keyboard input (its text service) edits.
        case UIA_HasKeyboardFocusPropertyId:*out=flag(control.role==Role::edit&&control.selected);break;
        case UIA_IsOffscreenPropertyId:{const auto s=state.lock();*out=flag(!s||!s->screen(control.rect));break;}
        case UIA_FrameworkIdPropertyId:*out=text("EndfieldHUD");break;
        // Source plates drawn "selected" (accent tint): the chosen font/size
        // row, an active trait, the eraser and background toggles.
        case UIA_ItemStatusPropertyId:if(control.selected&&control.role==Role::button)*out=text("selected");break;
        default:break;
        }
        return S_OK;
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple**out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    IFACEMETHODIMP Navigate(NavigateDirection direction,IRawElementProviderFragment**out)override{
        if(!out)return E_POINTER;*out=nullptr;const auto s=state.lock();if(!s||!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        if(direction==NavigateDirection_Parent)return s->host.root?s->host.root->QueryInterface(IID_PPV_ARGS(out)):S_OK;
        if(direction!=NavigateDirection_NextSibling&&direction!=NavigateDirection_PreviousSibling)return S_OK;
        for(std::size_t i=0;i<s->order.size();++i)if(s->order[i].Get()==this){
            const bool next=direction==NavigateDirection_NextSibling;
            if(next&&i+1<s->order.size())return s->order[i+1]->QueryInterface(IID_PPV_ARGS(out));
            if(!next&&i>0)return s->order[i-1]->QueryInterface(IID_PPV_ARGS(out));
            break;
        }
        return S_OK;
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY**out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        int ids[2]{UiaAppendRuntimeId,runtime};SAFEARRAY*a=SafeArrayCreateVector(VT_I4,0,2);if(!a)return E_OUTOFMEMORY;
        for(LONG i=0;i<2;++i){const HRESULT hr=SafeArrayPutElement(a,&i,&ids[i]);if(FAILED(hr)){SafeArrayDestroy(a);return hr;}}
        *out=a;return S_OK;
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect*out)override{
        if(!out)return E_POINTER;*out={};const auto s=state.lock();if(!s||!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        if(const auto r=s->screen(control.rect))*out={r->x,r->y,r->width,r->height};return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY**out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    IFACEMETHODIMP SetFocus()override{return connected?S_OK:UIA_E_ELEMENTNOTAVAILABLE;}
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot**out)override{
        if(!out)return E_POINTER;*out=nullptr;const auto s=state.lock();if(!s||!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        if(s->host.root){s->host.root->AddRef();*out=s->host.root;}return S_OK;
    }
    // IInvokeProvider: the source button's action (activateAction/perform).
    IFACEMETHODIMP Invoke()override{
        if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;if(!control.enabled)return UIA_E_ELEMENTNOTENABLED;
        const auto s=state.lock();if(!s||!s->host.invoke)return UIA_E_ELEMENTNOTAVAILABLE;
        // The action may rebuild the list; keep this peer and its id alive.
        const ComPtr<NotesAutomationPeer>self(this);const std::string id=control.id;++s->statistics.invokes;
        return s->host.invoke(id)?S_OK:UIA_E_INVALIDOPERATION;
    }
    // IRangeValueProvider: setAccessibilityValue clamps into [minimum, maximum].
    IFACEMETHODIMP SetValue(double value)override{
        if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;if(!control.enabled)return UIA_E_ELEMENTNOTENABLED;if(!std::isfinite(value))return E_INVALIDARG;
        const auto s=state.lock();if(!s||!s->host.setValue)return UIA_E_ELEMENTNOTAVAILABLE;
        const ComPtr<NotesAutomationPeer>self(this);const std::string id=control.id;++s->statistics.values;
        return s->host.setValue(id,std::min(control.maximum,std::max(control.minimum,value)))?S_OK:UIA_E_INVALIDOPERATION;
    }
    IFACEMETHODIMP get_Value(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=control.value;return S_OK;}
    // IValueProvider (the open field): its draft; writes replace it.
    IFACEMETHODIMP SetValue(LPCWSTR value)override{
        if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;if(!value)return E_INVALIDARG;
        const std::wstring_view view(value);const auto utf8=narrow(view);if(!view.empty()&&utf8.empty())return E_INVALIDARG;
        const auto s=state.lock();if(!s||!s->host.setText)return UIA_E_ELEMENTNOTAVAILABLE;
        const ComPtr<NotesAutomationPeer>self(this);const std::string id=control.id;++s->statistics.texts;
        return s->host.setText(id,utf8)?S_OK:UIA_E_INVALIDOPERATION;
    }
    IFACEMETHODIMP get_Value(BSTR*out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        *out=SysAllocString(wide(control.text).c_str());return *out?S_OK:E_OUTOFMEMORY;
    }
    IFACEMETHODIMP get_IsReadOnly(BOOL*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=control.enabled?FALSE:TRUE;return S_OK;}
    IFACEMETHODIMP get_Maximum(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=control.maximum;return S_OK;}
    IFACEMETHODIMP get_Minimum(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=control.minimum;return S_OK;}
    IFACEMETHODIMP get_LargeChange(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=control.largeChange;return S_OK;}
    IFACEMETHODIMP get_SmallChange(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=control.smallChange;return S_OK;}
    void disconnect()noexcept{if(!connected)return;connected=false;UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));}
private:
    ~NotesAutomationPeer()=default;
    std::atomic<ULONG>refs{1};
};

NativeNotesAutomation::NativeNotesAutomation(NotesAutomationHost host):state_(std::make_shared<State>()){
    state_->host=std::move(host);if(state_->host.root)state_->host.root->AddRef();
}
NativeNotesAutomation::~NativeNotesAutomation(){disconnect();if(state_->host.root){state_->host.root->Release();state_->host.root=nullptr;}}
std::size_t NativeNotesAutomation::size()const noexcept{return state_->order.size();}
const NotesAutomationElement* NativeNotesAutomation::element(std::size_t index)const noexcept{return index<state_->order.size()?&state_->order[index]->control:nullptr;}
NotesAutomationStats NativeNotesAutomation::stats()const noexcept{return state_->statistics;}
bool NativeNotesAutomation::refresh(){
    auto&s=*state_;++s.statistics.refreshes;
    auto&list=s.scratch;list.clear();if(s.host.elements)s.host.elements(list);
    const bool listening=UiaClientsAreListening()!=FALSE;
    std::vector<ComPtr<NotesAutomationPeer>>next;next.reserve(list.size());bool changed=list.size()!=s.order.size();
    ComPtr<NotesAutomationPeer>opened;
    for(std::size_t i=0;i<list.size();++i){
        auto&c=list[i];ComPtr<NotesAutomationPeer>e;
        for(const auto&old:s.order)if(old->connected&&old->control.id==c.id&&old->control.role==c.role){
            bool taken=false;for(const auto&n:next)if(n.Get()==old.Get()){taken=true;break;}
            if(!taken){e=old;break;}
        }
        if(!e){e.Attach(new NotesAutomationPeer(state_,std::move(c)));++s.statistics.created;changed=true;if(e->control.role==Role::edit&&e->control.selected)opened=e;}
        else{
            if(listening){
                auto*simple=static_cast<IRawElementProviderSimple*>(e.Get());
                const auto raise=[&](PROPERTYID id,VARIANT before,VARIANT after){UiaRaiseAutomationPropertyChangedEvent(simple,id,before,after);VariantClear(&before);VariantClear(&after);};
                if(c.role==Role::slider&&e->control.value!=c.value)raise(UIA_RangeValueValuePropertyId,number(e->control.value),number(c.value));
                if(c.role==Role::edit&&e->control.text!=c.text)raise(UIA_ValueValuePropertyId,text(e->control.text),text(c.text));
                if(e->control.label!=c.label)raise(UIA_NamePropertyId,text(e->control.label),text(c.label));
                if(e->control.help!=c.help)raise(UIA_HelpTextPropertyId,text(e->control.help),text(c.help));
                if(e->control.enabled!=c.enabled)raise(UIA_IsEnabledPropertyId,flag(e->control.enabled),flag(c.enabled));
            }
            e->control=std::move(c);
        }
        if(i>=s.order.size()||s.order[i].Get()!=e.Get())changed=true;
        next.push_back(std::move(e));
    }
    for(auto&old:s.order){bool kept=false;for(const auto&n:next)if(n.Get()==old.Get()){kept=true;break;}if(!kept){old->disconnect();++s.statistics.disconnected;}}
    s.order=std::move(next);list.clear();if(changed)++s.statistics.structureChanges;
    if(opened){++s.statistics.focusEvents;if(listening)UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(opened.Get()),UIA_AutomationFocusChangedEventId);}
    return changed;
}
HRESULT NativeNotesAutomation::child(std::size_t index,IRawElementProviderFragment**out)const{
    if(!out)return E_POINTER;*out=nullptr;if(index>=state_->order.size())return E_INVALIDARG;
    return state_->order[index]->QueryInterface(IID_PPV_ARGS(out));
}
HRESULT NativeNotesAutomation::fromPoint(double x,double y,IRawElementProviderFragment**out)const{
    if(!out)return E_POINTER;*out=nullptr;const auto&s=*state_;
    for(auto it=s.order.rbegin();it!=s.order.rend();++it)if(const auto r=s.screen((*it)->control.rect))
        if(x>=r->x&&y>=r->y&&x<r->x+r->width&&y<r->y+r->height)return (*it)->QueryInterface(IID_PPV_ARGS(out));
    return S_OK;
}
void NativeNotesAutomation::disconnect()noexcept{
    auto&s=*state_;for(auto&e:s.order){e->disconnect();++s.statistics.disconnected;}
    if(!s.order.empty())++s.statistics.structureChanges;s.order.clear();
}
}
#endif
