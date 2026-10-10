#ifdef _WIN32
#include "native/media_assembly_accessibility.hpp"
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

namespace {
namespace m=endfield::modules;namespace n=endfield::native;namespace c=endfield::core;using Microsoft::WRL::ComPtr;
unsigned checks{};
void check(bool value,const std::string&why){++checks;if(!value)throw std::runtime_error(why);}
struct Apartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};

// Synthetic HUD window root: lists the Media Assembly subtree as its children.
class Root final:public IRawElementProviderFragmentRoot,public IRawElementProviderFragment,public IRawElementProviderSimple {
    std::atomic<ULONG>refs{1};
public:
    n::NativeMediaAssemblyAccessibility*tree{};HWND window{};
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
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id,VARIANT*o)override{VariantInit(o);if(id==UIA_NamePropertyId){o->vt=VT_BSTR;o->bstrVal=SysAllocString(L"Synthetic HUD");}else if(id==UIA_ControlTypePropertyId){o->vt=VT_I4;o->lVal=UIA_PaneControlTypeId;}return S_OK;}
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple**o)override{return window?UiaHostProviderFromHwnd(window,o):(*o=nullptr,S_OK);}
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
std::wstring name(IRawElementProviderFragment*f){ComPtr<IRawElementProviderSimple>s;check(SUCCEEDED(f->QueryInterface(IID_PPV_ARGS(&s))),"Simple provider");VARIANT v;s->GetPropertyValue(UIA_NamePropertyId,&v);auto out=bstr(v);VariantClear(&v);return out;}
std::vector<int>runtime(IRawElementProviderFragment*f){SAFEARRAY*a{};check(SUCCEEDED(f->GetRuntimeId(&a))&&a,"Runtime id");std::vector<int>out(2);for(LONG i=0;i<2;++i)SafeArrayGetElement(a,&i,&out[std::size_t(i)]);SafeArrayDestroy(a);return out;}

void direct(){
    std::vector<m::MediaAssemblyAccessible>elements{
        {"closeMedia","Close",m::MediaAssemblyAccessibleRole::button,{10,20,30,40},true},
        {"seek","Playback position",m::MediaAssemblyAccessibleRole::slider,{50,60,200,17},true,2,0,10,5},
        {"export","Export",m::MediaAssemblyAccessibleRole::button,{300,20,40,40},false}};
    std::vector<std::string>invoked;std::vector<std::pair<std::string,double>>values;
    auto*root=new Root();
    n::MediaAssemblyAccessibilityHost host{root,[&]{return elements;},[&](std::string_view id){invoked.emplace_back(id);return true;},[&](std::string_view id,double v){values.emplace_back(std::string(id),v);return true;},
        [](c::Rect r){return c::Rect{100+r.x*1.5,50+r.y*1.5,r.width*1.5,r.height*1.5};}};
    {
        n::NativeMediaAssemblyAccessibility tree(host);root->tree=&tree;
        check(tree.refresh()&&tree.size()==3,"Projected source AX elements become UIA fragments");
        ComPtr<IRawElementProviderFragment>close,seek,exporter;check(SUCCEEDED(tree.child(0,&close))&&SUCCEEDED(tree.child(1,&seek))&&SUCCEEDED(tree.child(2,&exporter)),"Children");
        check(name(close.Get())==L"Close"&&name(seek.Get())==L"Playback position","Source accessibility labels");
        ComPtr<IRawElementProviderSimple>simple;close.As(&simple);VARIANT v;
        simple->GetPropertyValue(UIA_ControlTypePropertyId,&v);check(v.vt==VT_I4&&v.lVal==UIA_ButtonControlTypeId,"Buttons are UIA buttons");
        simple->GetPropertyValue(UIA_AutomationIdPropertyId,&v);check(bstr(v)==L"closeMedia","Automation id is the source action id");VariantClear(&v);
        ComPtr<IUnknown>pattern;check(SUCCEEDED(simple->GetPatternProvider(UIA_InvokePatternId,&pattern))&&pattern,"Buttons expose Invoke");
        check(SUCCEEDED(simple->GetPatternProvider(UIA_RangeValuePatternId,&pattern))&&!pattern,"Buttons expose no RangeValue");
        UiaRect r{};close->get_BoundingRectangle(&r);check(r.left==115&&r.top==80&&r.width==45&&r.height==60,"Bounding rectangle is the projected rect in screen pixels");
        ComPtr<IInvokeProvider>invoke;close.As(&invoke);check(SUCCEEDED(invoke->Invoke())&&invoked==std::vector<std::string>{"closeMedia"},"Invoke routes the source action");
        ComPtr<IInvokeProvider>disabled;exporter.As(&disabled);check(disabled->Invoke()==UIA_E_ELEMENTNOTENABLED&&invoked.size()==1,"Disabled controls refuse Invoke");
        ComPtr<IRangeValueProvider>range;check(SUCCEEDED(seek.As(&range)),"Sliders expose RangeValue");double d{};
        range->get_Value(&d);check(d==2,"Slider value");range->get_Maximum(&d);check(d==10,"Slider maximum");range->get_SmallChange(&d);check(d==5,"Playback position steps 5 s like the source AX slider");
        check(SUCCEEDED(range->SetValue(7))&&values.size()==1&&values[0]==std::pair<std::string,double>{"seek",7},"SetValue routes the source slider");
        check(range->SetValue(NAN)==E_INVALIDARG&&values.size()==1,"Non-finite values are refused");
        check(SUCCEEDED(range->SetValue(11))&&SUCCEEDED(range->SetValue(-3))&&values.size()==3&&values[1].second==10&&values[2].second==0,"Out-of-range values clamp like the source NSSlider");
        ComPtr<IRawElementProviderFragment>parent,next,previous;close->Navigate(NavigateDirection_Parent,&parent);close->Navigate(NavigateDirection_NextSibling,&next);close->Navigate(NavigateDirection_PreviousSibling,&previous);
        ComPtr<IRawElementProviderFragment>rootFragment;root->QueryInterface(IID_PPV_ARGS(&rootFragment));
        check(parent.Get()==rootFragment.Get()&&next.Get()==seek.Get()&&!previous,"Navigation: parent root, siblings in source order");
        ComPtr<IRawElementProviderFragmentRoot>owner;close->get_FragmentRoot(&owner);check(owner.Get()==static_cast<IRawElementProviderFragmentRoot*>(root),"Fragment root");
        const auto a=runtime(close.Get()),b=runtime(seek.Get());check(a[0]==UiaAppendRuntimeId&&a[1]!=b[1],"Distinct runtime ids");
        ComPtr<IRawElementProviderFragment>hit;check(SUCCEEDED(tree.fromPoint(200,150,&hit))&&hit.Get()==seek.Get(),"Hit testing in screen pixels");
        check(SUCCEEDED(tree.fromPoint(1,1,&hit))&&!hit,"Misses return no element");
        // Refresh keeps identity per id; vanished elements disconnect.
        elements[1].value=4;check(!tree.refresh(),"Value-only changes keep the structure");ComPtr<IRawElementProviderFragment>again;tree.child(1,&again);
        check(again.Get()==seek.Get()&&runtime(again.Get())==b,"Same element keeps its COM identity and runtime id");range->get_Value(&d);check(d==4,"Updated value");
        elements.erase(elements.begin());check(tree.refresh()&&tree.size()==2,"Removing an element changes the structure");
        check(invoke->Invoke()==UIA_E_ELEMENTNOTAVAILABLE&&name(close.Get()).empty(),"Vanished elements are disconnected");
        tree.disconnect();check(tree.size()==0&&range->SetValue(1)==UIA_E_ELEMENTNOTAVAILABLE,"Disconnect retires every element");
        const auto s=tree.stats();check(s.invokes==1&&s.values==3&&s.created==3&&s.disconnected==3,"Accessibility statistics");
        root->tree=nullptr;
    }
    root->Release();
}

// The real UI Automation client (another thread) reads and drives the
// provider tree of a hidden synthetic window through WM_GETOBJECT.
Root*windowRoot{};
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    if(msg==WM_GETOBJECT&&static_cast<long>(l)==static_cast<long>(UiaRootObjectId)&&windowRoot)return UiaReturnRawElementProvider(hwnd,w,l,static_cast<IRawElementProviderSimple*>(windowRoot));
    return DefWindowProcW(hwnd,msg,w,l);
}
void client(){
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"OwnedMediaAssemblyAccessibilityFixture";check(RegisterClassW(&cls)!=0,"Register synthetic window");
    HWND hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Synthetic HUD",WS_POPUP,0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);check(hwnd!=nullptr,"Synthetic hidden window");
    std::vector<m::MediaAssemblyAccessible>elements{{"closeMedia","Close",m::MediaAssemblyAccessibleRole::button,{10,20,30,40},true},{"seek","Playback position",m::MediaAssemblyAccessibleRole::slider,{50,60,200,17},true,2,0,10,5}};
    std::vector<std::string>invoked;std::vector<double>values;
    windowRoot=new Root();windowRoot->window=hwnd;
    n::NativeMediaAssemblyAccessibility tree({windowRoot,[&]{return elements;},[&](std::string_view id){invoked.emplace_back(id);return true;},[&](std::string_view,double v){values.push_back(v);return true;},[](c::Rect r){return r;}});
    windowRoot->tree=&tree;tree.refresh();
    struct Result {HRESULT hr{E_FAIL};int children{};std::wstring first,second;bool invoked{},set{};double value{};}result;
    std::atomic<bool>done{};
    std::thread worker([&]{
        HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(hr)){result.hr=hr;done=true;return;}
        {ComPtr<IUIAutomation>uia;hr=CoCreateInstance(__uuidof(CUIAutomation),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&uia));
            ComPtr<IUIAutomationElement>top;if(SUCCEEDED(hr))hr=uia->ElementFromHandle(hwnd,&top);
            ComPtr<IUIAutomationCondition>all;if(SUCCEEDED(hr))hr=uia->CreateTrueCondition(&all);
            ComPtr<IUIAutomationElementArray>found;if(SUCCEEDED(hr))hr=top->FindAll(TreeScope_Children,all.Get(),&found);
            if(SUCCEEDED(hr)){found->get_Length(&result.children);
                for(int i=0;i<result.children&&i<2;++i){ComPtr<IUIAutomationElement>e;found->GetElement(i,&e);BSTR b{};e->get_CurrentName(&b);(i?result.second:result.first)=b?b:L"";SysFreeString(b);
                    if(i==0){ComPtr<IUIAutomationInvokePattern>p;if(SUCCEEDED(e->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&p)))&&p)result.invoked=SUCCEEDED(p->Invoke());}
                    else{ComPtr<IUIAutomationRangeValuePattern>p;if(SUCCEEDED(e->GetCurrentPatternAs(UIA_RangeValuePatternId,IID_PPV_ARGS(&p)))&&p){p->get_CurrentValue(&result.value);result.set=SUCCEEDED(p->SetValue(6));}}}}
            result.hr=hr;}
        CoUninitialize();done=true;});
    // The provider lives on this STA: pump until the client finishes.
    const auto start=GetTickCount64();while(!done&&GetTickCount64()-start<20000){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);}
    worker.join();
    std::wcout<<L"UIA client hr=0x"<<std::hex<<unsigned(result.hr)<<std::dec<<L" children="<<result.children<<L" first="<<result.first<<L" second="<<result.second<<L'\n';
    check(SUCCEEDED(result.hr)&&result.children==2&&result.first==L"Close"&&result.second==L"Playback position","The UI Automation client sees the projected controls");
    check(result.invoked&&invoked==std::vector<std::string>{"closeMedia"},"Invoke through UI Automation routes the action on the UI thread");
    check(result.value==2&&result.set&&values==std::vector<double>{6},"RangeValue through UI Automation reads and sets the slider");
    tree.disconnect();windowRoot->tree=nullptr;UiaReturnRawElementProvider(hwnd,0,0,nullptr);DestroyWindow(hwnd);windowRoot->Release();windowRoot=nullptr;UnregisterClassW(cls.lpszClassName,cls.hInstance);
}
}
int wmain(){
    Apartment apartment;
    try{check(SUCCEEDED(apartment.hr),"STA");direct();client();std::cout<<"media_assembly_accessibility_tests: "<<checks<<" checks passed (synthetic hidden window only)\n";return 0;}
    catch(const std::exception&e){std::cerr<<"media_assembly_accessibility_tests failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
