#include "native/media_assembly_accessibility.hpp"
#ifdef _WIN32
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;namespace m=endfield::modules;
std::wstring wide(std::string_view s){
    if(s.empty())return {};const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring w(std::size_t(n>0?n:0),L'\0');
    if(n>0)MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n);return w;
}
std::atomic<int>nextRuntime{1};
VARIANT text(std::string_view s){VARIANT v;VariantInit(&v);v.vt=VT_BSTR;v.bstrVal=SysAllocString(wide(s).c_str());return v;}
VARIANT flag(bool b){VARIANT v;VariantInit(&v);v.vt=VT_BOOL;v.boolVal=b?VARIANT_TRUE:VARIANT_FALSE;return v;}
VARIANT number(double d){VARIANT v;VariantInit(&v);v.vt=VT_R8;v.dblVal=d;return v;}
}

class MediaAssemblyAccessibleElement;
struct NativeMediaAssemblyAccessibility::State {
    MediaAssemblyAccessibilityHost host;std::vector<ComPtr<MediaAssemblyAccessibleElement>>order;MediaAssemblyAccessibilityStats statistics;
    core::Rect screen(core::Rect r)const{return host.toScreen?host.toScreen(r):r;}
};

// One projected source AX control. Identity (runtime id, COM object) is
// stable for its element id while it stays listed.
class MediaAssemblyAccessibleElement final:public IRawElementProviderSimple,public IRawElementProviderFragment,public IInvokeProvider,public IRangeValueProvider {
public:
    using State=NativeMediaAssemblyAccessibility::State;
    MediaAssemblyAccessibleElement(std::weak_ptr<State>s,m::MediaAssemblyAccessible d):state(std::move(s)),data(std::move(d)),runtime(nextRuntime++){}
    std::weak_ptr<State>state;m::MediaAssemblyAccessible data;int runtime;bool connected{true};
    bool slider()const noexcept{return data.role==m::MediaAssemblyAccessibleRole::slider;}
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid,void**out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(riid==__uuidof(IUnknown)||riid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(riid==__uuidof(IRawElementProviderFragment))*out=static_cast<IRawElementProviderFragment*>(this);
        else if(riid==__uuidof(IInvokeProvider)&&!slider())*out=static_cast<IInvokeProvider*>(this);
        else if(riid==__uuidof(IRangeValueProvider)&&slider())*out=static_cast<IRangeValueProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    IFACEMETHODIMP_(ULONG)AddRef()override{return ++refs;}
    IFACEMETHODIMP_(ULONG)Release()override{const ULONG n=--refs;if(!n)delete this;return n;}
    // IRawElementProviderSimple
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions*out)override{if(!out)return E_POINTER;*out=ProviderOptions(ProviderOptions_ServerSideProvider|ProviderOptions_UseComThreading);return S_OK;}
    IFACEMETHODIMP GetPatternProvider(PATTERNID id,IUnknown**out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        if(id==UIA_InvokePatternId&&!slider()){*out=static_cast<IInvokeProvider*>(this);AddRef();}
        else if(id==UIA_RangeValuePatternId&&slider()){*out=static_cast<IRangeValueProvider*>(this);AddRef();}
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id,VARIANT*out)override{
        if(!out)return E_POINTER;VariantInit(out);if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        switch(id){
        case UIA_NamePropertyId:*out=text(data.name);break;
        case UIA_AutomationIdPropertyId:*out=text(data.id);break;
        case UIA_ControlTypePropertyId:out->vt=VT_I4;out->lVal=slider()?UIA_SliderControlTypeId:UIA_ButtonControlTypeId;break;
        case UIA_IsEnabledPropertyId:*out=flag(data.enabled);break;
        // The HUD routes its own keyboard input; projected controls are not tab stops.
        case UIA_IsKeyboardFocusablePropertyId:*out=flag(false);break;
        case UIA_FrameworkIdPropertyId:*out=text("EndfieldHUD");break;
        default:break;
        }
        return S_OK;
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple**out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    // IRawElementProviderFragment
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
        const auto r=s->screen(data.rect);if(r.width>0&&r.height>0&&std::isfinite(r.x)&&std::isfinite(r.y))*out={r.x,r.y,r.width,r.height};return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY**out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    IFACEMETHODIMP SetFocus()override{return connected?S_OK:UIA_E_ELEMENTNOTAVAILABLE;}
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot**out)override{
        if(!out)return E_POINTER;*out=nullptr;const auto s=state.lock();if(!s||!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        if(s->host.root){s->host.root->AddRef();*out=s->host.root;}return S_OK;
    }
    // IInvokeProvider: source AX press -> the same action as a click.
    IFACEMETHODIMP Invoke()override{
        if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;if(!data.enabled)return UIA_E_ELEMENTNOTENABLED;
        const auto s=state.lock();if(!s||!s->host.invoke)return UIA_E_ELEMENTNOTAVAILABLE;
        // The action may refresh the tree; keep this element and its id alive.
        const ComPtr<MediaAssemblyAccessibleElement>self(this);const std::string id=data.id;++s->statistics.invokes;
        return s->host.invoke(id)?S_OK:UIA_E_INVALIDOPERATION;
    }
    // IRangeValueProvider: source AX slider value/increment. Like the source
    // NSSlider setAccessibilityValue, finite values clamp into the range (so
    // an increment near an end reaches it).
    IFACEMETHODIMP SetValue(double value)override{
        if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;if(!data.enabled)return UIA_E_ELEMENTNOTENABLED;
        if(!std::isfinite(value))return E_INVALIDARG;value=std::min(data.maximum,std::max(data.minimum,value));
        const auto s=state.lock();if(!s||!s->host.setValue)return UIA_E_ELEMENTNOTAVAILABLE;
        const ComPtr<MediaAssemblyAccessibleElement>self(this);const std::string id=data.id;++s->statistics.values;
        return s->host.setValue(id,value)?S_OK:UIA_E_INVALIDOPERATION;
    }
    IFACEMETHODIMP get_Value(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=data.value;return S_OK;}
    IFACEMETHODIMP get_IsReadOnly(BOOL*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=data.enabled?FALSE:TRUE;return S_OK;}
    IFACEMETHODIMP get_Maximum(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=data.maximum;return S_OK;}
    IFACEMETHODIMP get_Minimum(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=data.minimum;return S_OK;}
    IFACEMETHODIMP get_LargeChange(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=data.step;return S_OK;}
    IFACEMETHODIMP get_SmallChange(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=data.step;return S_OK;}
    void disconnect()noexcept{if(!connected)return;connected=false;UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));}
private:
    ~MediaAssemblyAccessibleElement()=default;
    std::atomic<ULONG>refs{1};
};

NativeMediaAssemblyAccessibility::NativeMediaAssemblyAccessibility(MediaAssemblyAccessibilityHost host):state_(std::make_shared<State>()){
    state_->host=std::move(host);if(state_->host.root)state_->host.root->AddRef();
}
NativeMediaAssemblyAccessibility::~NativeMediaAssemblyAccessibility(){disconnect();if(state_->host.root){state_->host.root->Release();state_->host.root=nullptr;}}
std::size_t NativeMediaAssemblyAccessibility::size()const noexcept{return state_->order.size();}
MediaAssemblyAccessibilityStats NativeMediaAssemblyAccessibility::stats()const noexcept{return state_->statistics;}
bool NativeMediaAssemblyAccessibility::refresh(){
    auto&s=*state_;++s.statistics.refreshes;
    auto list=s.host.elements?s.host.elements():std::vector<m::MediaAssemblyAccessible>{};
    const bool listening=UiaClientsAreListening()!=FALSE;
    std::vector<ComPtr<MediaAssemblyAccessibleElement>>next;next.reserve(list.size());bool changed=list.size()!=s.order.size();
    for(std::size_t i=0;i<list.size();++i){
        auto&d=list[i];ComPtr<MediaAssemblyAccessibleElement>e;
        for(const auto&old:s.order)if(old->connected&&old->data.id==d.id&&old->data.role==d.role){e=old;break;}
        if(!e){e.Attach(new MediaAssemblyAccessibleElement(state_,std::move(d)));++s.statistics.created;changed=true;}
        else{
            if(listening){
                auto*simple=static_cast<IRawElementProviderSimple*>(e.Get());
                const auto raise=[&](PROPERTYID id,VARIANT before,VARIANT after){UiaRaiseAutomationPropertyChangedEvent(simple,id,before,after);VariantClear(&before);VariantClear(&after);};
                if(d.role==m::MediaAssemblyAccessibleRole::slider&&e->data.value!=d.value)raise(UIA_RangeValueValuePropertyId,number(e->data.value),number(d.value));
                if(e->data.name!=d.name)raise(UIA_NamePropertyId,text(e->data.name),text(d.name));
                if(e->data.enabled!=d.enabled)raise(UIA_IsEnabledPropertyId,flag(e->data.enabled),flag(d.enabled));
            }
            e->data=std::move(d);
        }
        if(i>=s.order.size()||s.order[i].Get()!=e.Get())changed=true;
        next.push_back(std::move(e));
    }
    for(auto&old:s.order){bool kept=false;for(const auto&n:next)if(n.Get()==old.Get()){kept=true;break;}if(!kept){old->disconnect();++s.statistics.disconnected;}}
    s.order=std::move(next);if(changed)++s.statistics.structureChanges;return changed;
}
HRESULT NativeMediaAssemblyAccessibility::child(std::size_t index,IRawElementProviderFragment**out)const{
    if(!out)return E_POINTER;*out=nullptr;if(index>=state_->order.size())return E_INVALIDARG;
    return state_->order[index]->QueryInterface(IID_PPV_ARGS(out));
}
HRESULT NativeMediaAssemblyAccessibility::fromPoint(double x,double y,IRawElementProviderFragment**out)const{
    if(!out)return E_POINTER;*out=nullptr;const auto&s=*state_;
    for(auto it=s.order.rbegin();it!=s.order.rend();++it){const auto r=s.screen((*it)->data.rect);
        if(r.width>0&&r.height>0&&x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height)return (*it)->QueryInterface(IID_PPV_ARGS(out));}
    return S_OK;
}
void NativeMediaAssemblyAccessibility::disconnect()noexcept{
    auto&s=*state_;for(auto&e:s.order){e->disconnect();++s.statistics.disconnected;}
    if(!s.order.empty())++s.statistics.structureChanges;s.order.clear();
}
}
#endif
