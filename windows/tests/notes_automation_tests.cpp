// UI Automation peers of the projected Notes/Archive/Projection controls
// (native/notes_automation.hpp) over synthetic control lists: patterns,
// properties, identity across refreshes, hit testing, clamping and refusal.
// No window is shown and no UIA client is required.
#ifdef _WIN32
#include "native/notes_automation.hpp"
#include <objbase.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace gpu=endfield::native;namespace core=endfield::core;using Microsoft::WRL::ComPtr;using Role=gpu::NotesAutomationRole;
unsigned checks{};void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Apartment{Apartment(){check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Initialize the test apartment");}~Apartment(){CoUninitialize();}};
gpu::NotesAutomationElement button(std::string id,std::string label,core::Rect r,bool enabled=true,bool selected=false){gpu::NotesAutomationElement e;e.id=std::move(id);e.label=std::move(label);e.rect=r;e.enabled=enabled;e.selected=selected;return e;}
std::wstring property(IRawElementProviderFragment*f,PROPERTYID id){ComPtr<IRawElementProviderSimple>s;check(SUCCEEDED(f->QueryInterface(IID_PPV_ARGS(&s))),"Peer is a simple provider");VARIANT v;VariantInit(&v);check(SUCCEEDED(s->GetPropertyValue(id,&v)),"Property reads");std::wstring out=v.vt==VT_BSTR&&v.bstrVal?v.bstrVal:L"";VariantClear(&v);return out;}
long integer(IRawElementProviderFragment*f,PROPERTYID id){ComPtr<IRawElementProviderSimple>s;f->QueryInterface(IID_PPV_ARGS(&s));VARIANT v;VariantInit(&v);check(SUCCEEDED(s->GetPropertyValue(id,&v)),"Property reads");const long out=v.vt==VT_I4?v.lVal:v.vt==VT_BOOL?(v.boolVal?1:0):-1;VariantClear(&v);return out;}

void run(){
    std::vector<gpu::NotesAutomationElement> controls;std::vector<std::string> invoked;std::vector<std::pair<std::string,double>> values;std::vector<std::pair<std::string,std::string>> texts;
    std::optional<core::Point> origin{core::Point{100,200}};bool accept=true;
    gpu::NotesAutomationHost host;host.elements=[&](std::vector<gpu::NotesAutomationElement>&out){out.insert(out.end(),controls.begin(),controls.end());};
    host.origin=[&]{return origin;};host.invoke=[&](std::string_view id){invoked.emplace_back(id);return accept;};
    host.setValue=[&](std::string_view id,double v){values.emplace_back(std::string(id),v);return accept;};
    host.setText=[&](std::string_view id,std::string_view t){texts.emplace_back(std::string(id),std::string(t));return accept;};
    gpu::NativeNotesAutomation automation(std::move(host));
    check(!automation.refresh()&&automation.size()==0,"An empty owner lists nothing and reports no change");

    controls.push_back(button("note:a:pin","Pin note",{10,10,20,20}));
    controls.push_back(button("note:a:delete","Delete note",{40,10,20,20},false));
    auto slider=button("a","Playback position",{10,40,100,12});slider.role=Role::slider;slider.minimum=0;slider.maximum=30;slider.value=4;slider.smallChange=slider.largeChange=5;controls.push_back(slider);
    auto edit=button("notes:editor:a","Edit text note",{10,60,200,80});edit.role=Role::edit;edit.text="草稿 draft";edit.help="Escape or Ctrl+Enter saves.";edit.selected=true;controls.push_back(edit);
    check(automation.refresh()&&automation.size()==4&&automation.stats().created==4&&automation.stats().focusEvents==1,"Four peers are created; the newly listed field announces focus");
    check(automation.element(2)->id=="a"&&automation.element(9)==nullptr,"Listed controls are readable in order");

    ComPtr<IRawElementProviderFragment>pin,remove,seek,field;
    check(SUCCEEDED(automation.child(0,&pin))&&SUCCEEDED(automation.child(1,&remove))&&SUCCEEDED(automation.child(2,&seek))&&SUCCEEDED(automation.child(3,&field)),"Every peer is reachable by index");
    ComPtr<IRawElementProviderFragment>none;check(automation.child(4,&none)==E_INVALIDARG&&!none,"An index past the list is refused");
    check(property(pin.Get(),UIA_NamePropertyId)==L"Pin note"&&property(pin.Get(),UIA_AutomationIdPropertyId)==L"note:a:pin"&&integer(pin.Get(),UIA_ControlTypePropertyId)==UIA_ButtonControlTypeId,"A button peer names its source action");
    check(integer(remove.Get(),UIA_IsEnabledPropertyId)==0&&integer(pin.Get(),UIA_IsEnabledPropertyId)==1,"Enabled state follows the painted control");
    check(integer(seek.Get(),UIA_ControlTypePropertyId)==UIA_SliderControlTypeId&&integer(field.Get(),UIA_ControlTypePropertyId)==UIA_EditControlTypeId,"Slider and edit roles map to UIA control types");
    check(integer(field.Get(),UIA_HasKeyboardFocusPropertyId)==1&&integer(pin.Get(),UIA_HasKeyboardFocusPropertyId)==0&&property(field.Get(),UIA_HelpTextPropertyId)==L"Escape or Ctrl+Enter saves.","The open field holds the HUD's keyboard focus and its help");
    UiaRect r{};check(SUCCEEDED(pin->get_BoundingRectangle(&r))&&r.left==110&&r.top==210&&r.width==20&&r.height==20,"Rectangles are client pixels offset to the screen");
    {ComPtr<IInvokeProvider>invoke;check(SUCCEEDED(pin.As(&invoke))&&SUCCEEDED(invoke->Invoke())&&invoked.size()==1&&invoked[0]=="note:a:pin","Invoke runs the source action by id");
     ComPtr<IInvokeProvider>disabled;check(SUCCEEDED(remove.As(&disabled))&&disabled->Invoke()==UIA_E_ELEMENTNOTENABLED&&invoked.size()==1,"A disabled control refuses Invoke");
     accept=false;check(invoke->Invoke()==UIA_E_INVALIDOPERATION,"A refused action reports an invalid operation");accept=true;
     ComPtr<IRangeValueProvider>notRange;check(FAILED(pin.As(&notRange)),"Buttons offer no range pattern");}
    {ComPtr<IRangeValueProvider>range;check(SUCCEEDED(seek.As(&range)),"The slider offers RangeValue");double v{};range->get_Value(&v);double lo{},hi{},step{};range->get_Minimum(&lo);range->get_Maximum(&hi);range->get_SmallChange(&step);
     check(v==4&&lo==0&&hi==30&&step==5,"RangeValue reports the source value, range and step");
     check(SUCCEEDED(range->SetValue(99))&&values.back().second==30&&SUCCEEDED(range->SetValue(-3))&&values.back().second==0,"Writes are clamped into the range");
     check(range->SetValue(std::numeric_limits<double>::quiet_NaN())==E_INVALIDARG&&values.size()==2,"A non-finite value is refused");}
    {ComPtr<IValueProvider>value;check(SUCCEEDED(field.As(&value)),"The field offers Value");BSTR text{};check(SUCCEEDED(value->get_Value(&text))&&std::wstring_view(text)==L"草稿 draft","Value reads the draft");SysFreeString(text);
     check(SUCCEEDED(value->SetValue(L"新的 text"))&&texts.back().first=="notes:editor:a"&&texts.back().second=="新的 text","Value writes reach the owner as UTF-8");
     const wchar_t lone[]{0xD800,0};check(value->SetValue(lone)==E_INVALIDARG&&texts.size()==1,"An unpaired surrogate is refused");}
    // Navigation and hit testing (screen pixels; the last listed is topmost).
    {ComPtr<IRawElementProviderFragment>next;check(SUCCEEDED(pin->Navigate(NavigateDirection_NextSibling,&next))&&next.Get()==remove.Get(),"Siblings follow the listed order");
     ComPtr<IRawElementProviderFragment>hit;check(SUCCEEDED(automation.fromPoint(115,215,&hit))&&hit.Get()==pin.Get(),"A point inside a button hits it");
     controls[0].rect={10,10,300,200};automation.refresh();hit.Reset();check(SUCCEEDED(automation.fromPoint(150,280,&hit))&&hit.Get()==field.Get(),"The later field is above an overlapping earlier control");
     hit.Reset();check(SUCCEEDED(automation.fromPoint(5,5,&hit))&&!hit,"A point outside every control hits nothing");}
    // Identity survives content changes; removals disconnect.
    controls[0].label="Unpin note";check(!automation.refresh(),"A label change keeps the structure");ComPtr<IRawElementProviderFragment>same;automation.child(0,&same);check(same.Get()==pin.Get()&&property(pin.Get(),UIA_NamePropertyId)==L"Unpin note","The same peer reports its new label");
    std::swap(controls[0],controls[1]);check(automation.refresh(),"A reorder is a structural change");std::swap(controls[0],controls[1]);automation.refresh();
    controls.erase(controls.begin()+1);check(automation.refresh()&&automation.size()==3&&automation.stats().disconnected==1,"A vanished control is disconnected");
    {ComPtr<IRawElementProviderSimple>gone;remove.As(&gone);VARIANT v;VariantInit(&v);check(gone->GetPropertyValue(UIA_NamePropertyId,&v)==UIA_E_ELEMENTNOTAVAILABLE,"A disconnected peer answers element-not-available");
     ComPtr<IInvokeProvider>stale;remove.As(&stale);check(stale->Invoke()==UIA_E_ELEMENTNOTAVAILABLE&&invoked.size()==2,"A disconnected peer never invokes");}
    origin.reset();check(integer(pin.Get(),UIA_IsOffscreenPropertyId)==1,"Without a client origin peers are offscreen");origin=core::Point{0,0};
    automation.disconnect();check(automation.size()==0,"Disconnect clears every peer");check(automation.refresh()&&automation.size()==3&&automation.stats().focusEvents==2,"The next refresh connects new peers; the field announces focus again");
}
// The shared bounds helper: all four projected corners, refusing failures.
void bounds(){
    const auto scaled=gpu::notesAutomationBounds({10,20,30,40},[](core::Point p)->std::optional<core::Point>{return core::Point{p.x*2+5,p.y*2+7};});
    check(scaled&&scaled->x==25&&scaled->y==47&&scaled->width==60&&scaled->height==80,"Scaled rectangles project exactly");
    const auto turned=gpu::notesAutomationBounds({0,0,10,10},[](core::Point p)->std::optional<core::Point>{return core::Point{-p.y,p.x};});
    check(turned&&turned->x==-10&&turned->y==0&&turned->width==10&&turned->height==10,"A rotated plane bounds all corners");
    check(!gpu::notesAutomationBounds({0,0,10,10},[](core::Point p)->std::optional<core::Point>{if(p.x>5)return std::nullopt;return p;}),"An unprojectable corner hides the control");
    check(!gpu::notesAutomationBounds({0,0,0,10},[](core::Point p)->std::optional<core::Point>{return p;}),"Empty controls are not listed");
}
}
int main(){
    try{Apartment apartment;run();bounds();std::cout<<"PASS "<<checks<<" Notes automation contracts\n";return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
