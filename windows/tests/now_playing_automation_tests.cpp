#ifdef _WIN32
#include "native/now_playing_automation.hpp"
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

// UI Automation fragments for the projected Now Playing controls over the
// real NowPlayingPresentation with injected host callbacks: no window is
// shown and no media session, audio route or user data is touched.
namespace {
namespace n=endfield::native;namespace m=endfield::modules;namespace c=endfield::core;using Microsoft::WRL::ComPtr;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Apartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};

// Synthetic HUD window root: lists the Now Playing subtree as its children.
class Root final:public IRawElementProviderFragmentRoot,public IRawElementProviderFragment,public IRawElementProviderSimple {
    std::atomic<ULONG>refs{1};
public:
    n::NativeNowPlayingAutomation*tree{};
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
std::wstring valueText(IRawElementProviderFragment*f){
    ComPtr<IRawElementProviderSimple>s;f->QueryInterface(IID_PPV_ARGS(&s));ComPtr<IUnknown>u;check(SUCCEEDED(s->GetPatternProvider(UIA_ValuePatternId,&u))&&u,"Sliders expose the source value text");
    ComPtr<IValueProvider>v;u.As(&v);BSTR text{};v->get_Value(&text);std::wstring out(text,SysStringLen(text));SysFreeString(text);return out;
}

// The presentation the owner drives, with the host callbacks of
// app/areas/now_playing_module.cpp (ids -> source actions and sliders).
struct Fixture {
    m::NowPlayingPresentation view;double time{1};bool hidden{};std::vector<m::NowPlayingIntent>intents;
    Fixture(){view.setActive(true,time);view.sync(input(true,true),time);}
    static m::NowPlayingViewInput input(bool track,bool seek){
        // A playing-app route is an explicit capability (the volume bridge sets it).
        m::NowPlayingViewInput i;i.session=7;i.capabilities={true,true,true,true,true,seek};i.capabilities.volume=true;i.volumeAvailable=true;i.volume=.5;
        if(track)i.track=m::NowPlayingTrack{"Synthetic title","Synthetic artist","Album",240,12,false,1,{},{},{},true};
        return i;
    }
    n::NowPlayingAutomationHost host(Root*root){
        return {root,[this]{return m::nowPlayingAccessibility(view,time);},
            [this](c::Rect r)->std::optional<c::Rect>{if(hidden)return std::nullopt;return c::Rect{100+r.x*1.5,50+r.y*1.5,r.width*1.5,r.height*1.5};},
            [this](std::string_view id){
                for(const auto action:{m::NowPlayingAction::lyrics,m::NowPlayingAction::previous,m::NowPlayingAction::playPause,m::NowPlayingAction::next,m::NowPlayingAction::volume})
                    if(m::nowPlayingActionID(action)==id){const auto&face=view.actions()[static_cast<std::size_t>(action)];if(!face.enabled)return false;if(auto i=view.perform(action,time+=.01))intents.push_back(*i);return true;}
                return false;},
            [this](std::string_view id,double v){
                const auto kind=id=="seek"?m::NowPlayingSliderKind::seek:m::NowPlayingSliderKind::volume;if(id!="seek"&&id!="appVolume")return false;
                const auto i=view.setSlider(kind,v,time+=.01);if(i)intents.push_back(*i);return i.has_value();}};
    }
};
ComPtr<IRawElementProviderFragment>find(n::NativeNowPlayingAutomation&tree,const std::wstring&automationID){
    for(std::size_t i=0;i<tree.size();++i){ComPtr<IRawElementProviderFragment>f;tree.child(i,&f);if(property(f.Get(),UIA_AutomationIdPropertyId)==automationID)return f;}
    throw std::runtime_error("Missing projected Now Playing control");
}

void run(){
    Fixture fixture;auto*root=new Root();
    {
        n::NativeNowPlayingAutomation tree(fixture.host(root));root->tree=&tree;
        const auto expected=m::nowPlayingAccessibility(fixture.view,fixture.time);
        check(tree.refresh()&&tree.size()==expected.buttons.size()+expected.sliders.size()&&tree.size()==6,"Five source buttons and the seek slider become UIA fragments");
        check(!tree.refresh()&&tree.stats().created==6,"An unchanged refresh keeps the tree and creates nothing");
        const std::wstring status=L"Synthetic title · Synthetic artist · Album";
        for(std::size_t i=0;i<tree.size();++i){
            ComPtr<IRawElementProviderFragment>f;check(SUCCEEDED(tree.child(i,&f)),"Child");
            const bool slider=i>=expected.buttons.size();
            const auto&label=slider?expected.sliders[i-expected.buttons.size()].label:expected.buttons[i].label;
            check(property(f.Get(),UIA_NamePropertyId)==wide(label),"Name is the source accessibility label");
            if(!slider)check(property(f.Get(),UIA_HelpTextPropertyId)==status,"Buttons carry the accessible status as help");
            auto type=raw(f.Get(),UIA_ControlTypePropertyId);check(type.vt==VT_I4&&type.lVal==(slider?UIA_SliderControlTypeId:UIA_ButtonControlTypeId),"Buttons and sliders keep their source roles");
            auto focusable=raw(f.Get(),UIA_IsKeyboardFocusablePropertyId);check(focusable.vt==VT_BOOL&&focusable.boolVal==VARIANT_FALSE,"Projected controls are not tab stops");
        }
        check(property(find(tree,L"playPause").Get(),UIA_NamePropertyId)==L"Play"&&property(find(tree,L"seek").Get(),UIA_NamePropertyId)==L"Playback position","Source labels");

        // Invoke: the same source actions as a click.
        auto play=find(tree,L"playPause");ComPtr<IInvokeProvider>invoke;check(SUCCEEDED(play.As(&invoke)),"Buttons expose Invoke");
        const auto playRuntime=runtime(play.Get());
        check(SUCCEEDED(invoke->Invoke())&&fixture.intents.size()==1&&fixture.intents.back().kind==m::NowPlayingIntentKind::playPause&&fixture.intents.back().session==7,"Invoke performs Play/Pause for the current session");
        ComPtr<IRawElementProviderSimple>playSimple;play.As(&playSimple);ComPtr<IUnknown>pattern;
        check(SUCCEEDED(playSimple->GetPatternProvider(UIA_RangeValuePatternId,&pattern))&&!pattern,"Buttons expose no RangeValue");
        ComPtr<IInvokeProvider>lyrics;find(tree,L"lyrics").As(&lyrics);
        check(SUCCEEDED(lyrics->Invoke())&&!fixture.view.lyricsSelected()&&fixture.intents.size()==1,"Lyrics is a local toggle, not a media command");
        check(!tree.refresh()&&runtime(find(tree,L"playPause").Get())==playRuntime,"Local toggles keep the structure and identities");

        // Seek: source range, 5 s increment, one command per SetValue.
        auto seek=find(tree,L"seek");ComPtr<IRangeValueProvider>range;check(SUCCEEDED(seek.As(&range)),"Sliders expose RangeValue");
        double d{};range->get_Value(&d);check(std::abs(d-12)<1e-9,"Seek value is the elapsed position");
        range->get_Minimum(&d);check(d==0,"Seek minimum");range->get_Maximum(&d);check(d==240,"Seek maximum is the duration");
        range->get_SmallChange(&d);check(d==5,"Seek increment is the source five seconds");range->get_LargeChange(&d);check(d==5,"Large change matches the source increment");
        check(valueText(seek.Get())==L"0:12"&&property(seek.Get(),UIA_HelpTextPropertyId)==L"0:12","Seek value text and help are the source clock text");
        check(SUCCEEDED(range->SetValue(60))&&fixture.intents.back().kind==m::NowPlayingIntentKind::seek&&fixture.intents.back().value==60,"SetValue seeks once");
        const auto sent=fixture.intents.size();
        check(range->SetValue(500)==E_INVALIDARG&&range->SetValue(NAN)==E_INVALIDARG&&fixture.intents.size()==sent,"Out-of-range values are refused");

        // App volume: appears with the menu, 2% increment, percentage text.
        ComPtr<IInvokeProvider>volumeButton;find(tree,L"volume").As(&volumeButton);
        check(SUCCEEDED(volumeButton->Invoke())&&fixture.view.capturesPointer()&&tree.refresh()&&tree.size()==7,"Opening the volume menu adds the app-volume slider");
        auto volume=find(tree,L"appVolume");ComPtr<IRangeValueProvider>volumeRange;volume.As(&volumeRange);
        volumeRange->get_Value(&d);check(std::abs(d-.5)<1e-12,"App volume value is the route gain");
        volumeRange->get_SmallChange(&d);check(std::abs(d-.02)<1e-12,"App volume increment is the source 2%");
        check(valueText(volume.Get())==L"50%"&&property(volume.Get(),UIA_NamePropertyId)==L"Playing app volume","App volume text and label");
        check(SUCCEEDED(volumeRange->SetValue(.3))&&fixture.intents.back().kind==m::NowPlayingIntentKind::volume&&std::abs(fixture.intents.back().value-.3)<1e-12,"SetValue changes the app volume");

        // Projection: screen rect, hit testing, siblings, hidden projection.
        UiaRect r{};play->get_BoundingRectangle(&r);const auto face=fixture.view.actions()[2].rect;
        check(r.left==100+face.x*1.5&&r.top==50+face.y*1.5&&r.width==face.width*1.5&&r.height==face.height*1.5,"Bounding rectangle is the projected canvas rect in screen pixels");
        ComPtr<IRawElementProviderFragment>hit;check(SUCCEEDED(root->ElementProviderFromPoint(r.left+r.width/2,r.top+r.height/2,&hit))&&hit.Get()==play.Get(),"Hit testing finds the projected control");
        ComPtr<IRawElementProviderFragment>parent,next,previous;play->Navigate(NavigateDirection_Parent,&parent);
        ComPtr<IRawElementProviderFragment>rootFragment;root->QueryInterface(IID_PPV_ARGS(&rootFragment));check(parent.Get()==rootFragment.Get(),"Parent is the window root");
        auto first=find(tree,L"lyrics");first->Navigate(NavigateDirection_PreviousSibling,&previous);first->Navigate(NavigateDirection_NextSibling,&next);
        check(!previous&&next&&property(next.Get(),UIA_AutomationIdPropertyId)==L"previous","Siblings follow the source order");
        fixture.hidden=true;UiaRect none{};play->get_BoundingRectangle(&none);auto offscreen=raw(play.Get(),UIA_IsOffscreenPropertyId);
        ComPtr<IRawElementProviderFragment>missed;root->ElementProviderFromPoint(r.left+r.width/2,r.top+r.height/2,&missed);
        check(none.width==0&&none.height==0&&offscreen.boolVal==VARIANT_TRUE&&!missed,"Without a projection the controls are offscreen and never hit");fixture.hidden=false;

        // Closing the menu disconnects the app-volume slider only.
        check(SUCCEEDED(volumeButton->Invoke())&&!fixture.view.capturesPointer()&&tree.refresh()&&tree.size()==6,"Closing the menu removes the slider");
        VARIANT gone;VariantInit(&gone);ComPtr<IRawElementProviderSimple>volumeSimple;volume.As(&volumeSimple);
        check(volumeSimple->GetPropertyValue(UIA_NamePropertyId,&gone)==UIA_E_ELEMENTNOTAVAILABLE&&volumeRange->SetValue(.4)==UIA_E_ELEMENTNOTAVAILABLE,"The vanished slider is disconnected");
        check(runtime(find(tree,L"playPause").Get())==playRuntime,"Controls that stay keep their identity");

        // No seek capability: the slider stays listed but becomes read-only.
        fixture.view.sync(Fixture::input(true,false),fixture.time+=.01);
        check(!tree.refresh(),"A capability change is a property change, not a structure change");
        BOOL readOnly{};range->get_IsReadOnly(&readOnly);check(readOnly&&range->SetValue(30)==UIA_E_ELEMENTNOTENABLED,"A player without seeking refuses UIA writes");

        // No player: source "No music playing" status, transport disabled.
        fixture.view.sync(Fixture::input(false,true),fixture.time+=.01);tree.refresh();
        check(property(find(tree,L"playPause").Get(),UIA_HelpTextPropertyId)==L"No music playing"&&valueText(find(tree,L"seek").Get())==L"Unavailable","Without a track the status and seek text follow the source");
        auto enabled=raw(find(tree,L"next").Get(),UIA_IsEnabledPropertyId);ComPtr<IInvokeProvider>nextInvoke;find(tree,L"next").As(&nextInvoke);
        check(enabled.vt==VT_BOOL&&enabled.boolVal==VARIANT_FALSE&&nextInvoke->Invoke()==UIA_E_ELEMENTNOTENABLED,"Disabled transport cannot be invoked");
        check(root->count()>1,"The tree holds the window root while connected");
    }
    check(root->count()==1,"Destruction releases the borrowed window root");
    root->Release();
}

// The COM boundary: a throwing owner callback or projection never unwinds
// into the UIA client; the call fails and the tree stays usable.
void throwingCallbacks(){
    Fixture fixture;auto*root=new Root();
    {
        auto host=fixture.host(root);bool throwing=false;const auto screen=host.toScreen;
        host.toScreen=[&throwing,screen](c::Rect r)->std::optional<c::Rect>{if(throwing)throw std::runtime_error("synthetic projection failure");return screen(r);};
        host.invoke=[](std::string_view)->bool{throw std::runtime_error("synthetic owner failure");};
        host.setValue=[](std::string_view,double)->bool{throw std::bad_alloc();};
        n::NativeNowPlayingAutomation tree(std::move(host));root->tree=&tree;
        check(tree.refresh()&&tree.size()==6,"The tree lists the projected controls");
        ComPtr<IInvokeProvider>play;check(SUCCEEDED(find(tree,L"playPause").As(&play)),"Invoke pattern");
        check(play->Invoke()==E_FAIL&&property(find(tree,L"playPause").Get(),UIA_NamePropertyId)==L"Play","A throwing invoke fails the call and leaves the element usable");
        ComPtr<IRangeValueProvider>seek;check(SUCCEEDED(find(tree,L"seek").As(&seek)),"RangeValue pattern");
        check(seek->SetValue(30)==E_OUTOFMEMORY,"An allocation failure while seeking is reported as E_OUTOFMEMORY");
        auto next=find(tree,L"next");ComPtr<IRawElementProviderSimple>simple;check(SUCCEEDED(next.As(&simple)),"Simple provider");
        throwing=true;UiaRect r{};VARIANT offscreen;VariantInit(&offscreen);ComPtr<IRawElementProviderFragment>hit;
        check(next->get_BoundingRectangle(&r)==E_FAIL&&simple->GetPropertyValue(UIA_IsOffscreenPropertyId,&offscreen)==E_FAIL&&offscreen.vt==VT_EMPTY&&
            tree.fromPoint(10,10,&hit)==E_FAIL&&!hit,"A throwing projection fails bounds, offscreen and hit tests");
        throwing=false;
        check(SUCCEEDED(next->get_BoundingRectangle(&r))&&r.width>0&&tree.fromPoint(NAN,0,&hit)==E_INVALIDARG&&!hit,"The projection recovers; non-finite hit tests are refused");
    }
    check(root->count()==1,"Destruction releases the root after failed callbacks");
    root->Release();
}
}
int main(){Apartment apartment;try{check(SUCCEEDED(apartment.hr),"Owned test STA");run();throwingCallbacks();std::cout<<"PASS "<<checks<<" Now Playing UI Automation checks (synthetic presentation; no window, media session or audio)\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
