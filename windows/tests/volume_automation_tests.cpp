#ifdef _WIN32
#include "native/volume_automation.hpp"
#include "native/volume_strings.hpp"
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// UI Automation fragments for the projected Volume controls over a real
// VolumeController with injected callbacks: no window is shown, no audio
// provider, device, mixer or user data is touched.
namespace {
namespace n=endfield::native;namespace c=endfield::core;using Microsoft::WRL::ComPtr;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Apartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};

// Synthetic HUD window root: lists the Volume subtree as its children.
class Root final:public IRawElementProviderFragmentRoot,public IRawElementProviderFragment,public IRawElementProviderSimple {
    std::atomic<ULONG>refs{1};
public:
    n::NativeVolumeAutomation*tree{};
    ULONG count()const noexcept{return refs.load();}
    IFACEMETHODIMP QueryInterface(REFIID riid,void**out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(riid==__uuidof(IUnknown)||riid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(riid==__uuidof(IRawElementProviderFragment))*out=static_cast<IRawElementProviderFragment*>(this);
        else if(riid==__uuidof(IRawElementProviderFragmentRoot))*out=static_cast<IRawElementProviderFragmentRoot*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    IFACEMETHODIMP_(ULONG)AddRef()override{return ++refs;}
    IFACEMETHODIMP_(ULONG)Release()override{const ULONG v=--refs;if(!v)delete this;return v;}
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions*o)override{*o=ProviderOptions(ProviderOptions_ServerSideProvider|ProviderOptions_UseComThreading);return S_OK;}
    IFACEMETHODIMP GetPatternProvider(PATTERNID,IUnknown**o)override{*o=nullptr;return S_OK;}
    IFACEMETHODIMP GetPropertyValue(PROPERTYID,VARIANT*o)override{VariantInit(o);return S_OK;}
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple**o)override{*o=nullptr;return S_OK;}
    IFACEMETHODIMP Navigate(NavigateDirection d,IRawElementProviderFragment**o)override{
        *o=nullptr;if(!tree||!tree->size())return S_OK;
        if(d==NavigateDirection_FirstChild)return tree->child(0,o);if(d==NavigateDirection_LastChild)return tree->child(tree->size()-1,o);return S_OK;}
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY**o)override{*o=nullptr;return S_OK;}
    IFACEMETHODIMP get_BoundingRectangle(UiaRect*o)override{*o={};return S_OK;}
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY**o)override{*o=nullptr;return S_OK;}
    IFACEMETHODIMP SetFocus()override{return S_OK;}
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot**o)override{AddRef();*o=this;return S_OK;}
    IFACEMETHODIMP ElementProviderFromPoint(double x,double y,IRawElementProviderFragment**o)override{*o=nullptr;return tree?tree->fromPoint(x,y,o):S_OK;}
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment**o)override{*o=nullptr;return S_OK;}
private:~Root()=default;
};

std::wstring bstr(const VARIANT&v){return v.vt==VT_BSTR&&v.bstrVal?std::wstring(v.bstrVal,SysStringLen(v.bstrVal)):std::wstring{};}
std::wstring wide(std::string_view s){if(s.empty())return {};const int k=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring w(std::size_t(k),L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),k);return w;}
std::wstring property(IRawElementProviderFragment*f,PROPERTYID id){ComPtr<IRawElementProviderSimple>s;check(SUCCEEDED(f->QueryInterface(IID_PPV_ARGS(&s))),"Simple provider");VARIANT v;check(SUCCEEDED(s->GetPropertyValue(id,&v)),"Property read");auto out=bstr(v);VariantClear(&v);return out;}
VARIANT raw(IRawElementProviderFragment*f,PROPERTYID id){ComPtr<IRawElementProviderSimple>s;f->QueryInterface(IID_PPV_ARGS(&s));VARIANT v;VariantInit(&v);s->GetPropertyValue(id,&v);return v;}
int runtime(IRawElementProviderFragment*f){SAFEARRAY*a{};check(SUCCEEDED(f->GetRuntimeId(&a))&&a,"Runtime id");int value{};LONG i=1;SafeArrayGetElement(a,&i,&value);SafeArrayDestroy(a);return value;}

// The audio_reference.swift base() state (two outputs + Bluetooth buds, one
// input, two available direct applications, no default-device switching).
n::VolumeSnapshot base(){
    n::VolumeSnapshot s;s.outputs={{"10","Speakers"},{"11","Headphones",true},{"12","Buds",false,true}};s.inputs={{"20","Microphone"}};
    s.outputID="10";s.inputID="20";s.volume=.55;s.canSetVolume=true;s.muted=false;s.canSetMute=true;s.balance=-.25;s.canSetBalance=true;
    s.applicationActivitySupported=true;s.applications.push_back({"40","Player",1000,true});s.applications.push_back({"41","PID 1001",1001,true});return s;
}
struct Fixture {
    n::VolumeSnapshot data=base();std::unique_ptr<n::VolumeController>controller;
    std::vector<std::string>calls;bool hidden{};double time{};
    Fixture(){
        n::VolumeCallbacks callbacks;
        callbacks.setVolume=[this](std::string_view id,double v){calls.push_back("volume "+std::string(id)+" "+std::to_string(v));data.volume=v;controller->receiveSnapshot(data);return true;};
        callbacks.setBalance=[this](std::string_view id,double v){calls.push_back("balance "+std::string(id)+" "+std::to_string(v));data.balance=v;controller->receiveSnapshot(data);return true;};
        callbacks.setMute=[this](std::string_view id,bool v){calls.push_back("mute "+std::string(id));data.muted=v;controller->receiveSnapshot(data);return true;};
        callbacks.setAppGain=[this](std::string_view id,double v){calls.push_back("gain "+std::string(id)+" "+std::to_string(v));return true;};
        controller=std::make_unique<n::VolumeController>(data,std::move(callbacks),n::volumeStrings(c::Language::english));controller->setActive(true);
    }
    n::VolumeAutomationHost host(Root*root){
        return {root,[this]{return n::volumeAccessibility(*controller,n::volumeAccessibilityStrings(c::Language::english));},
            [this](c::Rect r)->std::optional<c::Rect>{if(hidden)return std::nullopt;return c::Rect{100+r.x*1.5,50+r.y*1.5,r.width*1.5,r.height*1.5};},
            [this](std::string_view id){return controller->perform(id,time+=.01);},
            [this](std::string_view id,double v){return controller->setSlider(id,v);}};
    }
};
ComPtr<IRawElementProviderFragment>find(n::NativeVolumeAutomation&tree,const std::wstring&automationID){
    for(std::size_t i=0;i<tree.size();++i){ComPtr<IRawElementProviderFragment>f;tree.child(i,&f);if(property(f.Get(),UIA_AutomationIdPropertyId)==automationID)return f;}
    throw std::runtime_error("Missing projected Volume control");
}

void run(){
    Fixture fixture;auto*root=new Root();
    {
        n::NativeVolumeAutomation tree(fixture.host(root));root->tree=&tree;
        const auto expected=n::volumeAccessibility(*fixture.controller,n::volumeAccessibilityStrings(c::Language::english));
        check(tree.refresh()&&tree.size()==expected.buttons.size()+expected.sliders.size()&&tree.size()==9,"Every source projected button and slider becomes one UIA fragment");
        check(!tree.refresh()&&tree.stats().created==9,"An unchanged refresh keeps the tree and creates nothing");
        for(std::size_t i=0;i<tree.size();++i){
            ComPtr<IRawElementProviderFragment>f;check(SUCCEEDED(tree.child(i,&f)),"Child");
            const bool slider=i>=expected.buttons.size();
            const auto&label=slider?expected.sliders[i-expected.buttons.size()].label:expected.buttons[i].label;
            const auto&help=slider?expected.sliders[i-expected.buttons.size()].help:expected.buttons[i].help;
            check(property(f.Get(),UIA_NamePropertyId)==wide(label)&&property(f.Get(),UIA_HelpTextPropertyId)==wide(help),"Name and help text are the source accessibility label and help");
            auto type=raw(f.Get(),UIA_ControlTypePropertyId);check(type.vt==VT_I4&&type.lVal==(slider?UIA_SliderControlTypeId:UIA_ButtonControlTypeId),"Buttons and sliders keep their source roles");
            auto focusable=raw(f.Get(),UIA_IsKeyboardFocusablePropertyId);check(focusable.vt==VT_BOOL&&focusable.boolVal==VARIANT_FALSE,"Projected controls are not tab stops");
        }
        // Source chooser rows stay disabled: Windows has no public default-device API.
        auto output=find(tree,L"audio:output");auto enabled=raw(output.Get(),UIA_IsEnabledPropertyId);
        check(enabled.vt==VT_BOOL&&enabled.boolVal==VARIANT_FALSE,"Choose output device is announced as disabled (platform gap)");
        ComPtr<IInvokeProvider>outputInvoke;output.As(&outputInvoke);check(outputInvoke->Invoke()==UIA_E_ELEMENTNOTENABLED&&fixture.calls.empty(),"A disabled chooser cannot be invoked");

        auto mute=find(tree,L"audio:mute");ComPtr<IInvokeProvider>invoke;check(SUCCEEDED(mute.As(&invoke)),"Buttons expose Invoke");
        ComPtr<IRawElementProviderSimple>muteSimple;mute.As(&muteSimple);ComPtr<IUnknown>pattern;
        check(SUCCEEDED(muteSimple->GetPatternProvider(UIA_RangeValuePatternId,&pattern))&&!pattern,"Buttons expose no RangeValue");
        const auto muteRuntime=runtime(mute.Get());
        check(SUCCEEDED(invoke->Invoke())&&fixture.calls.back()=="mute 10"&&fixture.data.muted==true,"Invoke performs the source Mute action on the bound output");
        check(!tree.refresh()&&property(mute.Get(),UIA_NamePropertyId)==L"Unmute"&&runtime(mute.Get())==muteRuntime,"The label follows the state; identity is stable");

        auto volume=find(tree,L"volume");ComPtr<IRangeValueProvider>range;check(SUCCEEDED(volume.As(&range)),"Sliders expose RangeValue");
        double d{};range->get_Value(&d);check(std::abs(d-.55)<1e-12,"Slider value is the reported output volume");
        range->get_Minimum(&d);check(d==0,"Volume minimum");range->get_Maximum(&d);check(d==1,"Volume maximum");
        range->get_SmallChange(&d);check(std::abs(d-.02)<1e-12,"Small change is the source 2% accessibility increment");
        range->get_LargeChange(&d);check(std::abs(d-.02)<1e-12,"Large change matches the source increment");
        BOOL readOnly{};range->get_IsReadOnly(&readOnly);check(!readOnly,"An adjustable slider is writable");
        ComPtr<IRawElementProviderSimple>volumeSimple;volume.As(&volumeSimple);ComPtr<IUnknown>valueUnknown;
        check(SUCCEEDED(volumeSimple->GetPatternProvider(UIA_ValuePatternId,&valueUnknown))&&valueUnknown,"Sliders expose the source value text");
        ComPtr<IValueProvider>valueText;valueUnknown.As(&valueText);BSTR textValue{};valueText->get_Value(&textValue);
        check(std::wstring(textValue,SysStringLen(textValue))==L"55%","Value text is the source percentage");SysFreeString(textValue);
        BOOL textReadOnly{};valueText->get_IsReadOnly(&textReadOnly);check(textReadOnly&&valueText->SetValue(L"10%")==UIA_E_INVALIDOPERATION,"The text value is read-only; values change through RangeValue");
        check(SUCCEEDED(range->SetValue(.3))&&fixture.calls.back().starts_with("volume 10 0.3"),"SetValue routes the source slider to the bound output");
        check(range->SetValue(1.5)==E_INVALIDARG&&range->SetValue(NAN)==E_INVALIDARG&&fixture.calls.back().starts_with("volume 10 0.3"),"Out-of-range values are refused");
        tree.refresh();range->get_Value(&d);check(std::abs(d-.3)<1e-12,"Readback after refresh");

        auto balance=find(tree,L"balance");ComPtr<IRangeValueProvider>balanceRange;balance.As(&balanceRange);
        balanceRange->get_Minimum(&d);check(d==-1,"Balance keeps its signed range");balanceRange->get_SmallChange(&d);check(std::abs(d-.04)<1e-12,"Balance increment is 2% of its range");
        ComPtr<IRawElementProviderSimple>balanceSimple;balance.As(&balanceSimple);ComPtr<IUnknown>balanceValue;balanceSimple->GetPatternProvider(UIA_ValuePatternId,&balanceValue);
        ComPtr<IValueProvider>balanceText;balanceValue.As(&balanceText);BSTR left{};balanceText->get_Value(&left);check(std::wstring(left,SysStringLen(left))==L"L 25%","Balance text uses the source L/R form");SysFreeString(left);

        // Fixed output: the slider stays listed but becomes read-only.
        fixture.data.canSetVolume=false;fixture.controller->receiveSnapshot(fixture.data);
        check(!tree.refresh(),"A capability change is a property change, not a structure change");
        range->get_IsReadOnly(&readOnly);check(readOnly&&range->SetValue(.4)==UIA_E_ELEMENTNOTENABLED,"A fixed-volume output refuses UIA writes");
        check(property(volume.Get(),UIA_HelpTextPropertyId).ends_with(L" · Not adjustable on this device"),"The source not-adjustable hint is announced");

        // Projection: screen rect, hit testing, hidden projection.
        UiaRect r{};mute->get_BoundingRectangle(&r);check(r.left==100+328*1.5&&r.top==50+113*1.5&&r.width==60*1.5&&r.height==27*1.5,"Bounding rectangle is the projected canvas rect in screen pixels");
        ComPtr<IRawElementProviderFragment>hit;check(SUCCEEDED(root->ElementProviderFromPoint(r.left+r.width/2,r.top+r.height/2,&hit))&&hit.Get()==mute.Get(),"Hit testing finds the projected control");
        ComPtr<IRawElementProviderFragment>parent,next,previous;mute->Navigate(NavigateDirection_Parent,&parent);
        ComPtr<IRawElementProviderFragment>rootFragment;root->QueryInterface(IID_PPV_ARGS(&rootFragment));check(parent.Get()==rootFragment.Get(),"Parent is the window root");
        auto first=find(tree,L"audio:output");first->Navigate(NavigateDirection_PreviousSibling,&previous);first->Navigate(NavigateDirection_NextSibling,&next);
        check(!previous&&next&&property(next.Get(),UIA_AutomationIdPropertyId)==L"audio:input","Siblings follow the source order");
        fixture.hidden=true;UiaRect none{};mute->get_BoundingRectangle(&none);auto offscreen=raw(mute.Get(),UIA_IsOffscreenPropertyId);
        ComPtr<IRawElementProviderFragment>missed;root->ElementProviderFromPoint(r.left+r.width/2,r.top+r.height/2,&missed);
        check(none.width==0&&none.height==0&&offscreen.boolVal==VARIANT_TRUE&&!missed,"Without a projection the controls are offscreen and never hit");fixture.hidden=false;

        // Page change: Headphones / Bluetooth replaces the app sliders.
        auto app=find(tree,L"app:40");ComPtr<IInvokeProvider>headphones;find(tree,L"audio:headphones").As(&headphones);
        check(SUCCEEDED(headphones->Invoke())&&tree.refresh(),"Opening Headphones / Bluetooth changes the structure");
        VARIANT gone;VariantInit(&gone);ComPtr<IRawElementProviderSimple>appSimple;app.As(&appSimple);
        check(appSimple->GetPropertyValue(UIA_NamePropertyId,&gone)==UIA_E_ELEMENTNOTAVAILABLE&&tree.stats().disconnected==2,"Vanished app sliders are disconnected");
        check(runtime(find(tree,L"audio:mute").Get())==muteRuntime,"Controls that stay keep their identity");
        check(root->count()>1,"The tree holds the window root while connected");
    }
    check(root->count()==1,"Destruction releases the borrowed window root");
    root->Release();
}
}
int main(){Apartment apartment;try{check(SUCCEEDED(apartment.hr),"Owned test STA");run();std::cout<<"PASS "<<checks<<" Volume UI Automation checks (injected controller; no window or audio)\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
