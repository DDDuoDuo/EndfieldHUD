#include "native/volume_automation.hpp"
#ifdef _WIN32
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <climits>
#include <cmath>
#include <string>
#include <vector>

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
VARIANT number(double d){VARIANT v;VariantInit(&v);v.vt=VT_R8;v.dblVal=d;return v;}
// One projected source control, in canvas coordinates.
struct Data {
    std::string id,name,help,valueText;core::Rect rect;bool slider{},enabled{};
    std::optional<double>value;double minimum{},maximum{1};
    // NSSlider shows the midpoint while its reported value is unavailable.
    double shown()const noexcept{return value&&std::isfinite(*value)?*value:(minimum+maximum)/2;}
    double step()const noexcept{return (maximum-minimum)*.02;}
};
std::vector<Data>flatten(const VolumeAccessibility&a){
    std::vector<Data>out;out.reserve(a.buttons.size()+a.sliders.size());
    for(const auto&b:a.buttons)out.push_back({b.id,b.label,b.help,{},b.rect,false,b.enabled,std::nullopt,0,1});
    for(const auto&s:a.sliders)out.push_back({s.id,s.label,s.help,s.valueText,s.rect,true,s.enabled,s.value,s.minimum,s.maximum});
    return out;
}
}

class VolumeAutomationElement;
struct NativeVolumeAutomation::State {
    VolumeAutomationHost host;std::vector<ComPtr<VolumeAutomationElement>>order;VolumeAutomationStats statistics;
    std::optional<core::Rect>screen(core::Rect r)const{
        if(!host.toScreen)return std::nullopt;const auto out=host.toScreen(r);
        if(!out||!std::isfinite(out->x)||!std::isfinite(out->y)||!std::isfinite(out->width)||!std::isfinite(out->height)||out->width<=0||out->height<=0)return std::nullopt;
        return out;
    }
};

class VolumeAutomationElement final:public IRawElementProviderSimple,public IRawElementProviderFragment,public IInvokeProvider,public IRangeValueProvider {
public:
    using State=NativeVolumeAutomation::State;
    VolumeAutomationElement(std::weak_ptr<State>s,Data d):state(std::move(s)),data(std::move(d)),runtime(nextRuntime++){}
    std::weak_ptr<State>state;Data data;int runtime;bool connected{true};
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid,void**out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(riid==__uuidof(IUnknown)||riid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(riid==__uuidof(IRawElementProviderFragment))*out=static_cast<IRawElementProviderFragment*>(this);
        else if(riid==__uuidof(IInvokeProvider)&&!data.slider)*out=static_cast<IInvokeProvider*>(this);
        else if(riid==__uuidof(IRangeValueProvider)&&data.slider)*out=static_cast<IRangeValueProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    IFACEMETHODIMP_(ULONG)AddRef()override{return ++refs;}
    IFACEMETHODIMP_(ULONG)Release()override{const ULONG n=--refs;if(!n)delete this;return n;}
    // IRawElementProviderSimple
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions*out)override{if(!out)return E_POINTER;*out=ProviderOptions(ProviderOptions_ServerSideProvider|ProviderOptions_UseComThreading);return S_OK;}
    IFACEMETHODIMP GetPatternProvider(PATTERNID id,IUnknown**out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        if(id==UIA_InvokePatternId&&!data.slider){*out=static_cast<IInvokeProvider*>(this);AddRef();}
        else if(id==UIA_RangeValuePatternId&&data.slider){*out=static_cast<IRangeValueProvider*>(this);AddRef();}
        else if(id==UIA_ValuePatternId&&data.slider)return valueText(out);
        return S_OK;
    }
    HRESULT valueText(IUnknown**out);
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id,VARIANT*out)override{
        if(!out)return E_POINTER;VariantInit(out);if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;
        switch(id){
        case UIA_NamePropertyId:*out=text(data.name);break;
        case UIA_HelpTextPropertyId:if(!data.help.empty())*out=text(data.help);break;
        case UIA_AutomationIdPropertyId:*out=text(data.id);break;
        case UIA_ControlTypePropertyId:out->vt=VT_I4;out->lVal=data.slider?UIA_SliderControlTypeId:UIA_ButtonControlTypeId;break;
        case UIA_IsEnabledPropertyId:*out=flag(data.enabled);break;
        // The HUD routes its own keyboard input (arrow nudge of the selected
        // slider); projected controls are not tab stops, as in the source.
        case UIA_IsKeyboardFocusablePropertyId:*out=flag(false);break;
        case UIA_IsOffscreenPropertyId:{const auto s=state.lock();*out=flag(!s||!s->screen(data.rect));break;}
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
        if(const auto r=s->screen(data.rect))*out={r->x,r->y,r->width,r->height};return S_OK;
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
        const ComPtr<VolumeAutomationElement>self(this);const std::string id=data.id;++s->statistics.invokes;
        return s->host.invoke(id)?S_OK:UIA_E_INVALIDOPERATION;
    }
    // IRangeValueProvider: source NSSlider value and 2% increment.
    IFACEMETHODIMP SetValue(double value)override{
        if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;if(!data.enabled)return UIA_E_ELEMENTNOTENABLED;
        if(!std::isfinite(value)||value<data.minimum||value>data.maximum)return E_INVALIDARG;
        const auto s=state.lock();if(!s||!s->host.setValue)return UIA_E_ELEMENTNOTAVAILABLE;
        const ComPtr<VolumeAutomationElement>self(this);const std::string id=data.id;++s->statistics.values;
        return s->host.setValue(id,value)?S_OK:UIA_E_INVALIDOPERATION;
    }
    IFACEMETHODIMP get_Value(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=data.shown();return S_OK;}
    // A disabled slider, or one without a reported value, cannot be adjusted.
    IFACEMETHODIMP get_IsReadOnly(BOOL*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=data.enabled?FALSE:TRUE;return S_OK;}
    IFACEMETHODIMP get_Maximum(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=data.maximum;return S_OK;}
    IFACEMETHODIMP get_Minimum(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=data.minimum;return S_OK;}
    IFACEMETHODIMP get_LargeChange(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=data.step();return S_OK;}
    IFACEMETHODIMP get_SmallChange(double*out)override{if(!out)return E_POINTER;if(!connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=data.step();return S_OK;}
    void disconnect()noexcept{if(!connected)return;connected=false;UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));}
private:
    ~VolumeAutomationElement()=default;
    std::atomic<ULONG>refs{1};
};

// Value pattern tear-off: the source accessibility value text ("45%",
// "L 25%", "Centered", "Unavailable"). Always read-only; values are set
// through RangeValue exactly like the source NSSlider.
class VolumeValueText final:public IValueProvider {
public:
    explicit VolumeValueText(ComPtr<VolumeAutomationElement>e):element(std::move(e)){}
    IFACEMETHODIMP QueryInterface(REFIID riid,void**out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(riid==__uuidof(IUnknown)||riid==__uuidof(IValueProvider))*out=static_cast<IValueProvider*>(this);else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    IFACEMETHODIMP_(ULONG)AddRef()override{return ++refs;}
    IFACEMETHODIMP_(ULONG)Release()override{const ULONG n=--refs;if(!n)delete this;return n;}
    IFACEMETHODIMP SetValue(LPCWSTR)override{return element->connected?UIA_E_INVALIDOPERATION:UIA_E_ELEMENTNOTAVAILABLE;}
    IFACEMETHODIMP get_Value(BSTR*out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!element->connected)return UIA_E_ELEMENTNOTAVAILABLE;
        *out=SysAllocString(wide(element->data.valueText).c_str());return *out?S_OK:E_OUTOFMEMORY;
    }
    IFACEMETHODIMP get_IsReadOnly(BOOL*out)override{if(!out)return E_POINTER;if(!element->connected)return UIA_E_ELEMENTNOTAVAILABLE;*out=TRUE;return S_OK;}
private:
    ~VolumeValueText()=default;
    ComPtr<VolumeAutomationElement>element;std::atomic<ULONG>refs{1};
};
HRESULT VolumeAutomationElement::valueText(IUnknown**out){
    try{auto*value=new VolumeValueText(ComPtr<VolumeAutomationElement>(this));*out=static_cast<IValueProvider*>(value);return S_OK;}
    catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}

NativeVolumeAutomation::NativeVolumeAutomation(VolumeAutomationHost host):state_(std::make_shared<State>()){
    state_->host=std::move(host);if(state_->host.root)state_->host.root->AddRef();
}
NativeVolumeAutomation::~NativeVolumeAutomation(){disconnect();if(state_->host.root){state_->host.root->Release();state_->host.root=nullptr;}}
std::size_t NativeVolumeAutomation::size()const noexcept{return state_->order.size();}
VolumeAutomationStats NativeVolumeAutomation::stats()const noexcept{return state_->statistics;}
bool NativeVolumeAutomation::refresh(){
    auto&s=*state_;++s.statistics.refreshes;
    auto list=s.host.elements?flatten(s.host.elements()):std::vector<Data>{};
    const bool listening=UiaClientsAreListening()!=FALSE;
    std::vector<ComPtr<VolumeAutomationElement>>next;next.reserve(list.size());bool changed=list.size()!=s.order.size();
    for(std::size_t i=0;i<list.size();++i){
        auto&d=list[i];ComPtr<VolumeAutomationElement>e;
        for(const auto&old:s.order)if(old->connected&&old->data.id==d.id&&old->data.slider==d.slider){
            // An id appears once per role; a duplicate never shares identity.
            bool taken=false;for(const auto&n:next)if(n.Get()==old.Get()){taken=true;break;}
            if(!taken){e=old;break;}
        }
        if(!e){e.Attach(new VolumeAutomationElement(state_,std::move(d)));++s.statistics.created;changed=true;}
        else{
            if(listening){
                auto*simple=static_cast<IRawElementProviderSimple*>(e.Get());
                const auto raise=[&](PROPERTYID id,VARIANT before,VARIANT after){UiaRaiseAutomationPropertyChangedEvent(simple,id,before,after);VariantClear(&before);VariantClear(&after);};
                if(d.slider&&e->data.shown()!=d.shown())raise(UIA_RangeValueValuePropertyId,number(e->data.shown()),number(d.shown()));
                if(d.slider&&e->data.valueText!=d.valueText)raise(UIA_ValueValuePropertyId,text(e->data.valueText),text(d.valueText));
                if(e->data.name!=d.name)raise(UIA_NamePropertyId,text(e->data.name),text(d.name));
                if(e->data.help!=d.help)raise(UIA_HelpTextPropertyId,text(e->data.help),text(d.help));
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
HRESULT NativeVolumeAutomation::child(std::size_t index,IRawElementProviderFragment**out)const{
    if(!out)return E_POINTER;*out=nullptr;if(index>=state_->order.size())return E_INVALIDARG;
    return state_->order[index]->QueryInterface(IID_PPV_ARGS(out));
}
HRESULT NativeVolumeAutomation::fromPoint(double x,double y,IRawElementProviderFragment**out)const{
    if(!out)return E_POINTER;*out=nullptr;const auto&s=*state_;
    for(auto it=s.order.rbegin();it!=s.order.rend();++it)if(const auto r=s.screen((*it)->data.rect))
        if(x>=r->x&&y>=r->y&&x<r->x+r->width&&y<r->y+r->height)return (*it)->QueryInterface(IID_PPV_ARGS(out));
    return S_OK;
}
void NativeVolumeAutomation::disconnect()noexcept{
    auto&s=*state_;for(auto&e:s.order){e->disconnect();++s.statistics.disconnected;}
    if(!s.order.empty())++s.statistics.structureChanges;s.order.clear();
}
}
#endif
