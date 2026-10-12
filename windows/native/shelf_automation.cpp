#include "native/shelf_automation.hpp"
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
std::wstring wide(std::string_view s){
    if(s.empty()||s.size()>INT_MAX)return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);if(n<=0)return {};
    std::wstring w(static_cast<std::size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);return w;
}
std::atomic<int>nextRuntime{1};
VARIANT text(std::string_view s){VARIANT v;VariantInit(&v);v.vt=VT_BSTR;v.bstrVal=SysAllocString(wide(s).c_str());return v;}
VARIANT flag(bool b){VARIANT v;VariantInit(&v);v.vt=VT_BOOL;v.boolVal=b?VARIANT_TRUE:VARIANT_FALSE;return v;}
}

struct NativeShelfAutomation::State {
    ShelfAutomationHost host;std::vector<ComPtr<ShelfAutomationElement>>order;ShelfAutomationStats statistics;
    int focused{}; // runtime id of the element last announced as focused (0: none)
    std::optional<core::Rect>screen(core::Rect r)const{
        if(!host.toScreen)return std::nullopt;const auto out=host.toScreen(r);
        if(!out||!std::isfinite(out->x)||!std::isfinite(out->y)||!std::isfinite(out->width)||!std::isfinite(out->height)||out->width<=0||out->height<=0)return std::nullopt;
        return out;
    }
};

class ShelfAutomationElement final:public IRawElementProviderSimple,public IRawElementProviderFragment,public IInvokeProvider,public IValueProvider {
public:
    using State=NativeShelfAutomation::State;
    ShelfAutomationElement(std::weak_ptr<State>s,ShelfAutomationControl c):state(std::move(s)),control(std::move(c)),runtime(nextRuntime++){}
    std::weak_ptr<State>state;ShelfAutomationControl control;int runtime;bool connected{true};
    IFACEMETHODIMP QueryInterface(REFIID riid,void**out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(riid==__uuidof(IUnknown)||riid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(riid==__uuidof(IRawElementProviderFragment))*out=static_cast<IRawElementProviderFragment*>(this);
        else if(riid==__uuidof(IInvokeProvider))*out=static_cast<IInvokeProvider*>(this);
        else if(riid==__uuidof(IValueProvider)&&control.plate)*out=static_cast<IValueProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    IFACEMETHODIMP_(ULONG)AddRef()override{return ++refs;}
    IFACEMETHODIMP_(ULONG)Release()override{const ULONG n=--refs;if(!n)delete this;return n;}
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions*out)override{if(!out)return E_POINTER;*out=ProviderOptions(ProviderOptions_ServerSideProvider|ProviderOptions_UseComThreading);return S_OK;}
    IFACEMETHODIMP GetPatternProvider(PATTERNID id,IUnknown**out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        if(id==UIA_InvokePatternId){*out=static_cast<IInvokeProvider*>(this);AddRef();}
        else if(id==UIA_ValuePatternId&&control.plate){*out=static_cast<IValueProvider*>(this);AddRef();}
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id,VARIANT*out)override{
        if(!out)return E_POINTER;VariantInit(out);if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        switch(id){
        case UIA_NamePropertyId:*out=text(control.label);break;
        case UIA_HelpTextPropertyId:if(!control.help.empty())*out=text(control.help);break;
        case UIA_AutomationIdPropertyId:*out=text(control.id);break;
        case UIA_ControlTypePropertyId:out->vt=VT_I4;out->lVal=UIA_ButtonControlTypeId;break;
        case UIA_IsEnabledPropertyId:*out=flag(true);break;
        case UIA_IsKeyboardFocusablePropertyId:case UIA_HasKeyboardFocusPropertyId:*out=flag(control.focused);break;
        case UIA_IsOffscreenPropertyId:{const auto s=state.lock();*out=flag(!s||!s->screen(control.rect));break;}
        case UIA_FrameworkIdPropertyId:*out=text("EndfieldHUD");break;
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
    // IInvokeProvider: HUDFileShelfInteraction.activateAction -> canvas.perform.
    IFACEMETHODIMP Invoke()override{
        if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        const auto s=state.lock();if(!s||!s->host.invoke)return UIA_E_ELEMENTNOTAVAILABLE;
        // The action may rebuild the tree; keep this element and its id alive.
        const ComPtr<ShelfAutomationElement>self(this);const std::string id=control.id;++s->statistics.invokes;
        return s->host.invoke(id)?S_OK:UIA_E_INVALIDOPERATION;
    }
    // IValueProvider (read-only, card plates): "Selected" or empty.
    IFACEMETHODIMP SetValue(LPCWSTR)override{return connected?UIA_E_INVALIDOPERATION:UIA_E_ELEMENTNOTAVAILABLE;}
    IFACEMETHODIMP get_Value(BSTR*out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        *out=SysAllocString(wide(control.value).c_str());return *out?S_OK:E_OUTOFMEMORY;
    }
    IFACEMETHODIMP get_IsReadOnly(BOOL*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=TRUE;return S_OK;}
    void disconnect()noexcept{if(!connected)return;connected=false;UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));}
private:
    ~ShelfAutomationElement()=default;
    std::atomic<ULONG>refs{1};
};

NativeShelfAutomation::NativeShelfAutomation(ShelfAutomationHost host):state_(std::make_shared<State>()){
    state_->host=std::move(host);if(state_->host.root)state_->host.root->AddRef();
}
NativeShelfAutomation::~NativeShelfAutomation(){disconnect();if(state_->host.root){state_->host.root->Release();state_->host.root=nullptr;}}
std::size_t NativeShelfAutomation::size()const noexcept{return state_->order.size();}
ShelfAutomationStats NativeShelfAutomation::stats()const noexcept{return state_->statistics;}
bool NativeShelfAutomation::refresh(){
    auto&s=*state_;++s.statistics.refreshes;
    auto list=s.host.controls?s.host.controls():std::vector<ShelfAutomationControl>{};
    const bool listening=UiaClientsAreListening()!=FALSE;
    std::vector<ComPtr<ShelfAutomationElement>>next;next.reserve(list.size());bool changed=list.size()!=s.order.size();
    for(std::size_t i=0;i<list.size();++i){
        auto&c=list[i];ComPtr<ShelfAutomationElement>e;
        for(const auto&old:s.order)if(old->connected&&old->control.id==c.id&&old->control.plate==c.plate){
            bool taken=false;for(const auto&n:next)if(n.Get()==old.Get()){taken=true;break;}
            if(!taken){e=old;break;}
        }
        if(!e){e.Attach(new ShelfAutomationElement(state_,std::move(c)));++s.statistics.created;changed=true;}
        else{
            if(listening){
                auto*simple=static_cast<IRawElementProviderSimple*>(e.Get());
                const auto raise=[&](PROPERTYID id,VARIANT before,VARIANT after){UiaRaiseAutomationPropertyChangedEvent(simple,id,before,after);VariantClear(&before);VariantClear(&after);++s.statistics.propertyEvents;};
                if(e->control.label!=c.label)raise(UIA_NamePropertyId,text(e->control.label),text(c.label));
                if(e->control.help!=c.help)raise(UIA_HelpTextPropertyId,text(e->control.help),text(c.help));
                if(c.plate&&e->control.value!=c.value)raise(UIA_ValueValuePropertyId,text(e->control.value),text(c.value));
                if(e->control.focused!=c.focused)raise(UIA_HasKeyboardFocusPropertyId,flag(e->control.focused),flag(c.focused));
            }
            e->control=std::move(c);
        }
        if(i>=s.order.size()||s.order[i].Get()!=e.Get())changed=true;
        next.push_back(std::move(e));
    }
    for(auto&old:s.order){bool kept=false;for(const auto&n:next)if(n.Get()==old.Get()){kept=true;break;}if(!kept){old->disconnect();++s.statistics.disconnected;}}
    s.order=std::move(next);if(changed)++s.statistics.structureChanges;
    // Keyboard focus moved (the properties card opened, or another element
    // took it): announce the focused element once it is listed in the tree.
    ShelfAutomationElement*focus{};for(const auto&e:s.order)if(e->control.focused){focus=e.Get();break;}
    const int runtime=focus?focus->runtime:0;
    if(runtime!=s.focused){
        s.focused=runtime;
        if(focus){++s.statistics.focusMoves;if(listening)UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(focus),UIA_AutomationFocusChangedEventId);}
    }
    return changed;
}
HRESULT NativeShelfAutomation::child(std::size_t index,IRawElementProviderFragment**out)const{
    if(!out)return E_POINTER;*out=nullptr;if(index>=state_->order.size())return E_INVALIDARG;
    return state_->order[index]->QueryInterface(IID_PPV_ARGS(out));
}
HRESULT NativeShelfAutomation::fromPoint(double x,double y,IRawElementProviderFragment**out)const{
    if(!out)return E_POINTER;*out=nullptr;const auto&s=*state_;
    for(auto it=s.order.rbegin();it!=s.order.rend();++it)if(const auto r=s.screen((*it)->control.rect))
        if(x>=r->x&&y>=r->y&&x<r->x+r->width&&y<r->y+r->height)return (*it)->QueryInterface(IID_PPV_ARGS(out));
    return S_OK;
}
void NativeShelfAutomation::disconnect()noexcept{
    auto&s=*state_;for(auto&e:s.order){e->disconnect();++s.statistics.disconnected;}
    if(!s.order.empty())++s.statistics.structureChanges;s.order.clear();s.focused=0;
}
}
#endif
