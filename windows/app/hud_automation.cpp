#include "app/hud_automation.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <string>
#include <string_view>

namespace endfield::app {
namespace {
using Microsoft::WRL::ComPtr;
std::wstring wide(std::string_view s){
    if(s.empty()||s.size()>INT_MAX)return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);if(n<=0)return {};
    std::wstring w(static_cast<std::size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);return w;
}
VARIANT text(std::string_view s){VARIANT v;VariantInit(&v);v.vt=VT_BSTR;v.bstrVal=SysAllocString(wide(s).c_str());return v;}
VARIANT flag(bool b){VARIANT v;VariantInit(&v);v.vt=VT_BOOL;v.boolVal=b?VARIANT_TRUE:VARIANT_FALSE;return v;}
// Runtime ids of the group fragments (UiaAppendRuntimeId + this number).
std::atomic<int>nextGroupRuntime{0x45480001};
template<class F>HRESULT guarded(F&&f)noexcept{try{return f();}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}}
}

class HudAutomationRoot;
class HudAutomationGroup;
struct HudAutomation::State {
    HWND window{};HudAutomationSource source;HudAutomationStats statistics;bool alive{true};
    struct Entry {ModuleOwner*owner{};ComPtr<HudAutomationGroup>group;std::size_t count{};};
    std::vector<Entry>entries;std::vector<ModuleOwner*>scratch;ComPtr<HudAutomationRoot>root;
    // Live child count of an owner still registered here; 0 otherwise.
    std::size_t count(const ModuleOwner*owner)const noexcept{
        for(const auto&e:entries)if(e.owner==owner){try{return e.owner->automationCount();}catch(...){return 0;}}
        return 0;
    }
    bool listed(const Entry&e)const noexcept{return count(e.owner)>0;}
    const Entry*entry(const HudAutomationGroup*group)const noexcept{for(const auto&e:entries)if(e.group.Get()==group)return &e;return nullptr;}
    HRESULT group(std::size_t index,bool fromEnd,IRawElementProviderFragment**out)const;
    HRESULT sibling(const HudAutomationGroup*group,bool next,IRawElementProviderFragment**out)const;
    HRESULT hitTest(double x,double y,const ModuleOwner*only,IRawElementProviderFragment**out);
};

// One module owner's fragments under the HUD root.
class HudAutomationGroup final:public IRawElementProviderSimple,public IRawElementProviderFragment,public IRawElementProviderFragmentRoot {
public:
    HudAutomationGroup(std::weak_ptr<HudAutomation::State>s,const ModuleOwner*o,std::string n,std::string id)
        :state(std::move(s)),owner(o),name(std::move(n)),automationId(std::move(id)),runtime(nextGroupRuntime++){}
    std::weak_ptr<HudAutomation::State>state;const ModuleOwner*owner;std::string name,automationId;int runtime;bool connected{true};
    std::shared_ptr<HudAutomation::State>live()const{auto s=state.lock();return s&&s->alive&&connected&&s->entry(this)?s:nullptr;}
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid,void**out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(riid==__uuidof(IUnknown)||riid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(riid==__uuidof(IRawElementProviderFragment))*out=static_cast<IRawElementProviderFragment*>(this);
        else if(riid==__uuidof(IRawElementProviderFragmentRoot))*out=static_cast<IRawElementProviderFragmentRoot*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    IFACEMETHODIMP_(ULONG)AddRef()override{return ++refs;}
    IFACEMETHODIMP_(ULONG)Release()override{const ULONG n=--refs;if(!n)delete this;return n;}
    // IRawElementProviderSimple
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions*out)override{if(!out)return E_POINTER;*out=ProviderOptions(ProviderOptions_ServerSideProvider|ProviderOptions_UseComThreading);return S_OK;}
    IFACEMETHODIMP GetPatternProvider(PATTERNID,IUnknown**out)override{if(!out)return E_POINTER;*out=nullptr;return live()?S_OK:UIA_E_ELEMENTNOTAVAILABLE;}
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id,VARIANT*out)override{
        if(!out)return E_POINTER;VariantInit(out);if(!live())return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]{
            switch(id){
            case UIA_NamePropertyId:*out=text(name);break;
            case UIA_AutomationIdPropertyId:*out=text(automationId);break;
            case UIA_ControlTypePropertyId:out->vt=VT_I4;out->lVal=UIA_GroupControlTypeId;break;
            case UIA_LocalizedControlTypePropertyId:break; // the system's localized "group"
            case UIA_IsKeyboardFocusablePropertyId:*out=flag(false);break;
            case UIA_IsOffscreenPropertyId:{UiaRect r{};(void)get_BoundingRectangle(&r);*out=flag(!(r.width>0&&r.height>0));break;}
            case UIA_FrameworkIdPropertyId:*out=text("EndfieldHUD");break;
            default:break;
            }
            return S_OK;});
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple**out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    // IRawElementProviderFragment
    IFACEMETHODIMP Navigate(NavigateDirection direction,IRawElementProviderFragment**out)override;
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY**out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!live())return UIA_E_ELEMENTNOTAVAILABLE;
        int ids[2]{UiaAppendRuntimeId,runtime};SAFEARRAY*a=SafeArrayCreateVector(VT_I4,0,2);if(!a)return E_OUTOFMEMORY;
        for(LONG i=0;i<2;++i){const HRESULT hr=SafeArrayPutElement(a,&i,&ids[i]);if(FAILED(hr)){SafeArrayDestroy(a);return hr;}}
        *out=a;return S_OK;
    }
    // The union of the owner's fragment rectangles (screen pixels).
    IFACEMETHODIMP get_BoundingRectangle(UiaRect*out)override{
        if(!out)return E_POINTER;*out={};const auto s=live();if(!s)return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]{
            const auto n=s->count(owner);double left=0,top=0,right=0,bottom=0;bool any=false;
            for(std::size_t i=0;i<n;++i){
                ComPtr<IRawElementProviderFragment>child;if(owner->automationChild(i,child.GetAddressOf())<0||!child)continue;
                UiaRect r{};if(FAILED(child->get_BoundingRectangle(&r))||!(r.width>0&&r.height>0))continue;
                if(!any){left=r.left;top=r.top;right=r.left+r.width;bottom=r.top+r.height;any=true;}
                else{left=std::min(left,r.left);top=std::min(top,r.top);right=std::max(right,r.left+r.width);bottom=std::max(bottom,r.top+r.height);}
            }
            if(any)*out={left,top,right-left,bottom-top};return S_OK;});
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY**out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    IFACEMETHODIMP SetFocus()override{return live()?S_OK:UIA_E_ELEMENTNOTAVAILABLE;}
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot**out)override;
    // IRawElementProviderFragmentRoot: the owner's fragments are reported with
    // this group as their fragment root; hit tests stay inside this owner.
    IFACEMETHODIMP ElementProviderFromPoint(double x,double y,IRawElementProviderFragment**out)override{
        if(!out)return E_POINTER;*out=nullptr;const auto s=live();if(!s)return UIA_E_ELEMENTNOTAVAILABLE;
        return s->hitTest(x,y,owner,out);
    }
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment**out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    void disconnect()noexcept{if(!connected)return;connected=false;UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));}
private:
    ~HudAutomationGroup()=default;
    std::atomic<ULONG>refs{1};
};

// The HWND-hosted fragment root.
class HudAutomationRoot final:public IRawElementProviderSimple,public IRawElementProviderFragment,public IRawElementProviderFragmentRoot {
public:
    explicit HudAutomationRoot(std::weak_ptr<HudAutomation::State>s):state(std::move(s)){}
    std::weak_ptr<HudAutomation::State>state;bool connected{true};
    std::shared_ptr<HudAutomation::State>live()const{auto s=state.lock();return s&&s->alive&&connected?s:nullptr;}
    IFACEMETHODIMP QueryInterface(REFIID riid,void**out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(riid==__uuidof(IUnknown)||riid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(riid==__uuidof(IRawElementProviderFragment))*out=static_cast<IRawElementProviderFragment*>(this);
        else if(riid==__uuidof(IRawElementProviderFragmentRoot))*out=static_cast<IRawElementProviderFragmentRoot*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    IFACEMETHODIMP_(ULONG)AddRef()override{return ++refs;}
    IFACEMETHODIMP_(ULONG)Release()override{const ULONG n=--refs;if(!n)delete this;return n;}
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions*out)override{if(!out)return E_POINTER;*out=ProviderOptions(ProviderOptions_ServerSideProvider|ProviderOptions_UseComThreading);return S_OK;}
    IFACEMETHODIMP GetPatternProvider(PATTERNID,IUnknown**out)override{if(!out)return E_POINTER;*out=nullptr;return live()?S_OK:UIA_E_ELEMENTNOTAVAILABLE;}
    // Name, control type and bounds come from the HWND host provider.
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id,VARIANT*out)override{
        if(!out)return E_POINTER;VariantInit(out);if(!live())return UIA_E_ELEMENTNOTAVAILABLE;
        return guarded([&]{if(id==UIA_FrameworkIdPropertyId)*out=text("EndfieldHUD");return S_OK;});
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple**out)override{
        if(!out)return E_POINTER;*out=nullptr;const auto s=live();if(!s||!IsWindow(s->window))return UIA_E_ELEMENTNOTAVAILABLE;
        return UiaHostProviderFromHwnd(s->window,out);
    }
    IFACEMETHODIMP Navigate(NavigateDirection direction,IRawElementProviderFragment**out)override{
        if(!out)return E_POINTER;*out=nullptr;const auto s=live();if(!s)return UIA_E_ELEMENTNOTAVAILABLE;
        if(direction==NavigateDirection_FirstChild)return s->group(0,false,out);
        if(direction==NavigateDirection_LastChild)return s->group(0,true,out);
        return S_OK; // parent and siblings come from the HWND host
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY**out)override{if(!out)return E_POINTER;*out=nullptr;return live()?S_OK:UIA_E_ELEMENTNOTAVAILABLE;}
    IFACEMETHODIMP get_BoundingRectangle(UiaRect*out)override{if(!out)return E_POINTER;*out={};return live()?S_OK:UIA_E_ELEMENTNOTAVAILABLE;}
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY**out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    IFACEMETHODIMP SetFocus()override{return live()?S_OK:UIA_E_ELEMENTNOTAVAILABLE;}
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot**out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!live())return UIA_E_ELEMENTNOTAVAILABLE;
        *out=static_cast<IRawElementProviderFragmentRoot*>(this);AddRef();return S_OK;
    }
    // Owners are asked in reverse registration order (later owners draw above).
    IFACEMETHODIMP ElementProviderFromPoint(double x,double y,IRawElementProviderFragment**out)override{
        if(!out)return E_POINTER;*out=nullptr;const auto s=live();if(!s)return UIA_E_ELEMENTNOTAVAILABLE;
        return s->hitTest(x,y,nullptr,out);
    }
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment**out)override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    void disconnect()noexcept{if(!connected)return;connected=false;UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));}
private:
    ~HudAutomationRoot()=default;
    std::atomic<ULONG>refs{1};
};

HRESULT HudAutomation::State::group(std::size_t index,bool fromEnd,IRawElementProviderFragment**out)const{
    std::size_t seen=0;
    if(!fromEnd){for(const auto&e:entries)if(listed(e)){if(seen++==index)return e.group->QueryInterface(IID_PPV_ARGS(out));}}
    else for(auto it=entries.rbegin();it!=entries.rend();++it)if(listed(*it)){if(seen++==index)return it->group->QueryInterface(IID_PPV_ARGS(out));}
    return S_OK;
}
HRESULT HudAutomation::State::sibling(const HudAutomationGroup*g,bool next,IRawElementProviderFragment**out)const{
    for(std::size_t i=0;i<entries.size();++i)if(entries[i].group.Get()==g){
        if(next){for(std::size_t j=i+1;j<entries.size();++j)if(listed(entries[j]))return entries[j].group->QueryInterface(IID_PPV_ARGS(out));}
        else for(std::size_t j=i;j-->0;)if(listed(entries[j]))return entries[j].group->QueryInterface(IID_PPV_ARGS(out));
        break;
    }
    return S_OK;
}
HRESULT HudAutomation::State::hitTest(double x,double y,const ModuleOwner*only,IRawElementProviderFragment**out){
    ++statistics.hitTests;if(!std::isfinite(x)||!std::isfinite(y))return E_INVALIDARG;
    return guarded([&]{
        // Modal owners (the import card, the red power button card, an open
        // menu that captures the pointer) answer first; while one is up, the
        // controls beneath it are not reachable by point, as for the pointer.
        bool modal=false;
        for(auto it=entries.rbegin();it!=entries.rend();++it){
            if(only&&it->owner!=only)continue;if(!listed(*it))continue;
            if(!it->owner->capturesPointer())continue;modal=true;
            IRawElementProviderFragment*found{};const long hr=it->owner->automationFromPoint(x,y,&found);
            if(hr<0){if(found)found->Release();continue;}
            if(found){*out=found;return S_OK;}
        }
        if(modal)return S_OK;
        for(auto it=entries.rbegin();it!=entries.rend();++it){
            if(only&&it->owner!=only)continue;if(!listed(*it))continue;
            IRawElementProviderFragment*found{};const long hr=it->owner->automationFromPoint(x,y,&found);
            if(hr<0){if(found)found->Release();continue;}
            if(found){*out=found;return S_OK;}
        }
        return S_OK;});
}
HRESULT STDMETHODCALLTYPE HudAutomationGroup::Navigate(NavigateDirection direction,IRawElementProviderFragment**out){
    if(!out)return E_POINTER;*out=nullptr;const auto s=live();if(!s)return UIA_E_ELEMENTNOTAVAILABLE;
    return guarded([&]()->HRESULT{
        switch(direction){
        case NavigateDirection_Parent:return s->root?s->root->QueryInterface(IID_PPV_ARGS(out)):S_OK;
        case NavigateDirection_NextSibling:return s->sibling(this,true,out);
        case NavigateDirection_PreviousSibling:return s->sibling(this,false,out);
        case NavigateDirection_FirstChild:case NavigateDirection_LastChild:{
            const auto n=s->count(owner);if(!n)return S_OK;
            const long hr=owner->automationChild(direction==NavigateDirection_FirstChild?0:n-1,out);
            if(hr<0&&*out){(*out)->Release();*out=nullptr;}
            return S_OK;
        }
        default:return S_OK;
        }});
}
HRESULT STDMETHODCALLTYPE HudAutomationGroup::get_FragmentRoot(IRawElementProviderFragmentRoot**out){
    if(!out)return E_POINTER;*out=nullptr;const auto s=live();if(!s)return UIA_E_ELEMENTNOTAVAILABLE;
    if(s->root){*out=static_cast<IRawElementProviderFragmentRoot*>(s->root.Get());(*out)->AddRef();}
    return S_OK;
}

bool HudAutomation::rootRequest(std::intptr_t lParam)noexcept{return static_cast<long>(lParam)==static_cast<long>(UiaRootObjectId);}
HudAutomation::HudAutomation(void*window,HudAutomationSource source):state_(std::make_shared<State>()){
    state_->window=static_cast<HWND>(window);state_->source=std::move(source);
    state_->root.Attach(new HudAutomationRoot(state_));
}
HudAutomation::~HudAutomation(){shutdown();}
std::intptr_t HudAutomation::answer(std::uintptr_t wParam,std::intptr_t lParam){
    auto&s=*state_;if(!s.alive||!s.root)return 0;++s.statistics.rootRequests;
    return static_cast<std::intptr_t>(UiaReturnRawElementProvider(s.window,static_cast<WPARAM>(wParam),static_cast<LPARAM>(lParam),static_cast<IRawElementProviderSimple*>(s.root.Get())));
}
bool HudAutomation::refresh(){
    auto&s=*state_;if(!s.alive||!s.source.owners)return false;++s.statistics.refreshes;
    s.scratch.clear();s.source.owners(s.scratch);
    bool rootChanged=false,anyChanged=false;
    // Owners that left the registry (failure isolation) lose their group.
    for(auto it=s.entries.begin();it!=s.entries.end();){
        if(std::find(s.scratch.begin(),s.scratch.end(),it->owner)!=s.scratch.end()){++it;continue;}
        if(it->count)rootChanged=true;it->group->disconnect();++s.statistics.groupsDisconnected;it=s.entries.erase(it);
    }
    for(std::size_t index=0;index<s.scratch.size();++index){
        auto*owner=s.scratch[index];
        if(index>=s.entries.size()||s.entries[index].owner!=owner){
            const auto found=std::find_if(s.entries.begin()+static_cast<std::ptrdiff_t>(std::min(index,s.entries.size())),s.entries.end(),[&](const auto&e){return e.owner==owner;});
            State::Entry e;
            if(found!=s.entries.end()){e=*found;s.entries.erase(found);if(e.count)rootChanged=true;}
            else{
                e.owner=owner;const std::string name=s.source.name?s.source.name(*owner):std::string(owner->name());
                e.group.Attach(new HudAutomationGroup(state_,owner,name,"module."+std::string(owner->name())));++s.statistics.groupsCreated;
            }
            s.entries.insert(s.entries.begin()+static_cast<std::ptrdiff_t>(index),std::move(e));
        }
        auto&entry=s.entries[index];auto*group=entry.group.Get();
        if(s.source.name){auto name=s.source.name(*owner);if(name!=group->name){
            if(entry.count&&UiaClientsAreListening()){VARIANT before=text(group->name),after=text(name);UiaRaiseAutomationPropertyChangedEvent(static_cast<IRawElementProviderSimple*>(group),UIA_NamePropertyId,before,after);VariantClear(&before);VariantClear(&after);}
            group->name=std::move(name);}}
        const bool changed=s.source.refresh&&s.source.refresh(*owner,static_cast<IRawElementProviderFragmentRoot*>(group));
        std::size_t count{};try{count=owner->automationCount();}catch(...){count=0;}
        if((entry.count>0)!=(count>0))rootChanged=true;
        if(changed&&count>0&&UiaClientsAreListening())UiaRaiseStructureChangedEvent(static_cast<IRawElementProviderSimple*>(group),StructureChangeType_ChildrenInvalidated,nullptr,0);
        anyChanged=anyChanged||changed;entry.count=count;
    }
    if(rootChanged&&UiaClientsAreListening())UiaRaiseStructureChangedEvent(static_cast<IRawElementProviderSimple*>(s.root.Get()),StructureChangeType_ChildrenInvalidated,nullptr,0);
    if(rootChanged||anyChanged)++s.statistics.structureChanges;
    return rootChanged||anyChanged;
}
void HudAutomation::disconnectOwners()noexcept{
    auto&s=*state_;if(!s.alive)return;bool listed=false;
    for(auto&e:s.entries){try{e.owner->automationDisconnect();}catch(...){}listed=listed||e.count>0;e.count=0;}
    if(listed){++s.statistics.structureChanges;if(UiaClientsAreListening())UiaRaiseStructureChangedEvent(static_cast<IRawElementProviderSimple*>(s.root.Get()),StructureChangeType_ChildrenInvalidated,nullptr,0);}
}
void HudAutomation::remove(const ModuleOwner&owner)noexcept{
    auto&s=*state_;
    for(auto it=s.entries.begin();it!=s.entries.end();++it)if(it->owner==&owner){
        try{it->owner->automationDisconnect();}catch(...){}
        it->group->disconnect();++s.statistics.groupsDisconnected;s.entries.erase(it);return;
    }
}
void HudAutomation::shutdown()noexcept{
    auto&s=*state_;if(!s.alive)return;
    for(auto&e:s.entries){try{e.owner->automationDisconnect();}catch(...){}e.group->disconnect();++s.statistics.groupsDisconnected;}
    s.entries.clear();s.alive=false;
    if(s.root)s.root->disconnect();
    // The window's provider is gone (also before the HWND is destroyed).
    if(s.window&&IsWindow(s.window))(void)UiaReturnRawElementProvider(s.window,0,0,nullptr);
    s.root.Reset();
}
IRawElementProviderFragmentRoot*HudAutomation::root()const noexcept{return state_->root?static_cast<IRawElementProviderFragmentRoot*>(state_->root.Get()):nullptr;}
std::size_t HudAutomation::groups()const noexcept{std::size_t n=0;for(const auto&e:state_->entries)if(state_->listed(e))++n;return n;}
HudAutomationStats HudAutomation::stats()const noexcept{return state_->statistics;}
} // namespace endfield::app
