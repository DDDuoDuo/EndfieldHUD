// UI Automation fragments for the projected Personal Profile controls over a
// real ProfileState with injected callbacks: no window is shown, no UIA client
// is attached, and no profile record, image or user data is touched.
#ifdef _WIN32
#include "native/profile_automation.hpp"
#include "native/profile_text.hpp"
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace n=endfield::native;namespace m=endfield::modules;namespace c=endfield::core;using Microsoft::WRL::ComPtr;using Role=n::ProfileAutomationRole;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Apartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};
std::wstring wide(std::string_view s){if(s.empty())return {};const int k=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring w(std::size_t(k),L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),k);return w;}
HRESULT property(IRawElementProviderFragment*f,PROPERTYID id,VARIANT&v){VariantInit(&v);ComPtr<IRawElementProviderSimple>s;if(!f||FAILED(f->QueryInterface(IID_PPV_ARGS(&s))))return E_NOINTERFACE;return s->GetPropertyValue(id,&v);}
std::wstring text(IRawElementProviderFragment*f,PROPERTYID id){VARIANT v;std::wstring out;if(SUCCEEDED(property(f,id,v))&&v.vt==VT_BSTR&&v.bstrVal)out.assign(v.bstrVal,SysStringLen(v.bstrVal));VariantClear(&v);return out;}
long integer(IRawElementProviderFragment*f,PROPERTYID id){VARIANT v;long out=-1;if(SUCCEEDED(property(f,id,v))&&v.vt==VT_I4)out=v.lVal;VariantClear(&v);return out;}
bool boolean(IRawElementProviderFragment*f,PROPERTYID id){VARIANT v;bool out=false;if(SUCCEEDED(property(f,id,v))&&v.vt==VT_BOOL)out=v.boolVal==VARIANT_TRUE;VariantClear(&v);return out;}
ComPtr<IRawElementProviderFragment>peer(const n::NativeProfileAutomation&a,std::string_view id){
    for(std::size_t i=0;i<a.size();++i){ComPtr<IRawElementProviderFragment>f;if(SUCCEEDED(a.child(i,&f))&&f&&text(f.Get(),UIA_AutomationIdPropertyId)==wide(id))return f;}
    return nullptr;
}

void builder(){
    // HUDPersonalProfileInteraction.layoutAccessibility over the page state.
    const auto rules=n::nativeProfileTextRules();const auto dates=n::nativeProfileDateRules(u"UTC");
    auto record=m::PersonalProfile::defaults();record.name="Endministrator";
    m::ProfileState state(record,rules,dates,{},c::Language::english);
    auto controls=n::profileAutomationControls(state,std::nullopt);
    check(!state.actions().empty()&&state.sliders().empty()&&controls.size()==state.actions().size(),"The page lists one peer per accessible action");
    for(std::size_t i=0;i<controls.size();++i){const auto&a=state.actions()[i];const auto&p=controls[i];
        check(p.id==a.id&&p.label==a.label&&p.rect==a.rect&&p.role==Role::button&&p.help==state.accessibilityStatus(),"Action peers keep the source order, label, rect and the page status as help");}
    const auto edited=std::find_if(state.actions().begin(),state.actions().end(),[](const auto&a){return a.field.has_value();});
    check(edited!=state.actions().end(),"The page offers an editable field action");
    const auto without=n::profileAutomationControls(state,n::ProfileAutomationEditor{*edited->field,{97,37,286,26},"Draft"});
    const auto editor=std::find_if(without.begin(),without.end(),[](const auto&p){return p.role==Role::edit;});
    check(without.size()==controls.size()&&std::none_of(without.begin(),without.end(),[&](const auto&p){return p.id==edited->id;})&&editor!=without.end()&&
          editor->id=="profile:editor:"+std::string(m::profileFieldID(*edited->field))&&editor->label==m::profileFieldTitle(*edited->field,c::Language::english)&&
          editor->text=="Draft"&&editor->help==state.accessibilityStatus()&&editor->rect==c::Rect{97,37,286,26},
          "The field being edited is represented by its editor, named by the field, not by its button");
    check(state.perform("profile:backgroundMenu")&&!state.sliders().empty(),"The card theme popover shows its crop sliders");
    controls=n::profileAutomationControls(state,std::nullopt);
    check(controls.size()==state.actions().size()+state.sliders().size(),"Sliders follow the actions");
    for(std::size_t i=0;i<state.sliders().size();++i){const auto&s=state.sliders()[i];const auto&p=controls[state.actions().size()+i];
        check(p.role==Role::slider&&p.id==m::profileFieldID(s.field)&&p.label==s.label&&p.help==s.valueDescription&&p.rect==s.rect&&p.value==s.value&&p.minimum==s.minimum&&p.maximum==s.maximum&&p.step==s.step,
              "Slider peers carry the field id, title, value description, range and step");}
}

void fragments(){
    std::vector<n::ProfileAutomationControl>list{
        {"profile:menu","Edit personal profile","Personal profile",{10,10,40,20},Role::button},
        {"profile:visibility","Hide profile text","Personal profile",{60,10,40,20},Role::button},
        {"profile:editor:name","Name","Personal profile",{10,70,140,13},Role::edit,0,0,1,1,"Endministrator"},
        {"thumbnailZoom","Thumbnail zoom","2×",{10,40,200,17},Role::slider,2,1,20,.1}};
    std::vector<std::string>invoked,texts;std::vector<std::pair<std::string,double>>values;bool hidden{},refuse{};
    n::ProfileAutomationHost host;
    host.controls=[&]{return list;};
    host.toScreen=[&](c::Rect r)->std::optional<c::Rect>{if(hidden)return std::nullopt;return c::Rect{100+r.x*2,50+r.y*2,r.width*2,r.height*2};};
    host.invoke=[&](std::string_view id){invoked.emplace_back(id);return !refuse;};
    host.setValue=[&](std::string_view id,double v){values.emplace_back(std::string(id),v);return !refuse;};
    host.setText=[&](std::string_view id,std::string_view value){texts.push_back(std::string(id)+"="+std::string(value));return !refuse;};
    n::NativeProfileAutomation automation(std::move(host));
    check(automation.refresh()&&automation.size()==4&&automation.stats().created==4,"One fragment per control");
    // The open field: an Edit with a writable Value holding its draft.
    const auto field=peer(automation,"profile:editor:name");ComPtr<IValueProvider>draft;ComPtr<IInvokeProvider>noInvoke;BSTR shown{};BOOL fieldReadOnly{TRUE};
    check(field&&integer(field.Get(),UIA_ControlTypePropertyId)==UIA_EditControlTypeId&&boolean(field.Get(),UIA_IsKeyboardFocusablePropertyId)&&boolean(field.Get(),UIA_HasKeyboardFocusPropertyId)&&
          SUCCEEDED(field.As(&draft))&&FAILED(field.As(&noInvoke))&&SUCCEEDED(draft->get_Value(&shown))&&shown&&std::wstring_view(shown)==L"Endministrator"&&
          SUCCEEDED(draft->get_IsReadOnly(&fieldReadOnly))&&!fieldReadOnly,"The open field is a focused, writable Edit holding its draft");
    SysFreeString(shown);shown=nullptr;
    check(draft->SetValue(L"Endmin \u7ba1\u7406\u5458")==S_OK&&texts==std::vector<std::string>{"profile:editor:name=Endmin \xe7\xae\xa1\xe7\x90\x86\xe5\x91\x98"}&&draft->SetValue(nullptr)==E_INVALIDARG,
          "Value writes reach the field as UTF-8; a null string is refused");
    list[2].text="Endmin";check(!automation.refresh()&&peer(automation,"profile:editor:name").Get()==field.Get()&&SUCCEEDED(draft->get_Value(&shown))&&shown&&std::wstring_view(shown)==L"Endmin","A new draft updates the same peer");
    SysFreeString(shown);shown=nullptr;
    const auto menu=peer(automation,"profile:menu"),zoomPeer=peer(automation,"thumbnailZoom");
    check(menu&&zoomPeer&&text(menu.Get(),UIA_NamePropertyId)==L"Edit personal profile"&&text(menu.Get(),UIA_HelpTextPropertyId)==L"Personal profile"&&
          integer(menu.Get(),UIA_ControlTypePropertyId)==UIA_ButtonControlTypeId&&integer(zoomPeer.Get(),UIA_ControlTypePropertyId)==UIA_SliderControlTypeId&&
          boolean(menu.Get(),UIA_IsEnabledPropertyId)&&!boolean(menu.Get(),UIA_IsKeyboardFocusablePropertyId)&&text(menu.Get(),UIA_FrameworkIdPropertyId)==L"EndfieldHUD",
          "Buttons and sliders carry the source names, help and roles; projected controls are not tab stops");
    UiaRect bounds{};check(SUCCEEDED(menu->get_BoundingRectangle(&bounds))&&bounds.left==120&&bounds.top==70&&bounds.width==80&&bounds.height==40&&!boolean(menu.Get(),UIA_IsOffscreenPropertyId),"Bounds are the projected screen rect");
    ComPtr<IInvokeProvider>invoke;ComPtr<IRangeValueProvider>range,notRange;ComPtr<IInvokeProvider>notInvoke;
    check(SUCCEEDED(menu.As(&invoke))&&FAILED(menu.As(&notRange))&&SUCCEEDED(zoomPeer.As(&range))&&FAILED(zoomPeer.As(&notInvoke)),"Buttons invoke, sliders expose RangeValue only");
    check(invoke->Invoke()==S_OK&&invoked==std::vector<std::string>{"profile:menu"},"Invoke performs the action like its click");
    double v{},lo{},hi{},smallStep{},largeStep{};BOOL readOnly{TRUE};
    check(SUCCEEDED(range->get_Value(&v))&&SUCCEEDED(range->get_Minimum(&lo))&&SUCCEEDED(range->get_Maximum(&hi))&&SUCCEEDED(range->get_SmallChange(&smallStep))&&SUCCEEDED(range->get_LargeChange(&largeStep))&&SUCCEEDED(range->get_IsReadOnly(&readOnly))&&
          v==2&&lo==1&&hi==20&&smallStep==.1&&largeStep==.1&&!readOnly,"RangeValue reports the source value, range and step");
    check(range->SetValue(std::nan(""))==E_INVALIDARG&&range->SetValue(INFINITY)==E_INVALIDARG&&values.empty(),"Non-finite values are refused");
    check(range->SetValue(25)==S_OK&&range->SetValue(-3)==S_OK&&range->SetValue(5.5)==S_OK&&
          values==std::vector<std::pair<std::string,double>>{{"thumbnailZoom",20},{"thumbnailZoom",1},{"thumbnailZoom",5.5}},"Values are clamped into the range like setAccessibilityValue");
    refuse=true;check(invoke->Invoke()==UIA_E_INVALIDOPERATION&&range->SetValue(3)==UIA_E_INVALIDOPERATION,"A refused press or value reports an invalid operation");refuse=false;
    ComPtr<IRawElementProviderFragment>hit;
    check(SUCCEEDED(automation.fromPoint(125,75,&hit))&&hit&&text(hit.Get(),UIA_AutomationIdPropertyId)==L"profile:menu","The hit test finds the control under a screen point");
    hit.Reset();check(SUCCEEDED(automation.fromPoint(5,5,&hit))&&!hit,"Points outside every control hit nothing");
    ComPtr<IRawElementProviderFragment>next;check(SUCCEEDED(menu->Navigate(NavigateDirection_NextSibling,&next))&&next&&text(next.Get(),UIA_AutomationIdPropertyId)==L"profile:visibility","Siblings follow the listed order");
    // A content event: same ids keep their COM identity; changed values update in place.
    list[3].value=5.5;list[3].help="5.5×";
    check(!automation.refresh()&&peer(automation,"thumbnailZoom").Get()==zoomPeer.Get()&&SUCCEEDED(range->get_Value(&v))&&v==5.5&&text(zoomPeer.Get(),UIA_HelpTextPropertyId)==L"5.5×",
          "An unchanged set keeps each fragment and refreshes its value");
    // A popover replaces the page's controls.
    list={{"profile:popoverClose","Close","Personal profile",{300,10,20,20},Role::button}};
    check(automation.refresh()&&automation.size()==1&&automation.stats().disconnected==4,"A new control set replaces the fragments");
    check(draft->SetValue(L"x")==UIA_E_ELEMENTNOTAVAILABLE&&texts.size()==1,"A closed field's peer refuses writes");
    VARIANT gone;check(property(menu.Get(),UIA_NamePropertyId,gone)==UIA_E_ELEMENTNOTAVAILABLE&&invoke->Invoke()==UIA_E_ELEMENTNOTAVAILABLE&&range->SetValue(2)==UIA_E_ELEMENTNOTAVAILABLE,"Vanished fragments are no longer available");VariantClear(&gone);
    hidden=true;const auto close=peer(automation,"profile:popoverClose");
    check(close&&boolean(close.Get(),UIA_IsOffscreenPropertyId)&&SUCCEEDED(close->get_BoundingRectangle(&bounds))&&bounds.width==0,"A control that does not project is offscreen with empty bounds");
    hit.Reset();check(SUCCEEDED(automation.fromPoint(700,70,&hit))&&!hit,"An unprojected control is never hit");
    automation.disconnect();check(automation.size()==0&&property(close.Get(),UIA_NamePropertyId,gone)==UIA_E_ELEMENTNOTAVAILABLE,"Disconnecting empties the group");VariantClear(&gone);
    list.clear();check(!automation.refresh()&&automation.size()==0,"An inactive page lists nothing");
}
// A host that routes no writes (e.g. a Work Mode owner whose field Value is
// not wired yet): its Edit and Slider report read-only and refuse writes as
// an invalid operation, never as a vanished element.
void readOnlyHost(){
    std::vector<n::ProfileAutomationControl>list{
        {"work:editor:duration","Custom countdown: minutes and seconds","Help",{64,184,312,68},Role::edit,0,0,1,1,"25:00"},
        {"thumbnailZoom","Thumbnail zoom","2×",{10,40,200,17},Role::slider,2,1,20,.1}};
    n::ProfileAutomationHost host;
    host.controls=[&]{return list;};
    host.toScreen=[](c::Rect r)->std::optional<c::Rect>{return r;};
    n::NativeProfileAutomation automation(std::move(host));
    check(automation.refresh()&&automation.size()==2,"A read-only host lists its controls");
    const auto field=peer(automation,"work:editor:duration"),slider=peer(automation,"thumbnailZoom");
    ComPtr<IValueProvider>value;ComPtr<IRangeValueProvider>range;BOOL fieldReadOnly{FALSE},sliderReadOnly{FALSE};BSTR shown{};
    check(field&&slider&&SUCCEEDED(field.As(&value))&&SUCCEEDED(slider.As(&range))&&SUCCEEDED(value->get_IsReadOnly(&fieldReadOnly))&&fieldReadOnly&&
          SUCCEEDED(range->get_IsReadOnly(&sliderReadOnly))&&sliderReadOnly&&SUCCEEDED(value->get_Value(&shown))&&shown&&std::wstring_view(shown)==L"25:00",
          "Without write routes the Edit and Slider are read-only and still report their values");
    SysFreeString(shown);
    check(value->SetValue(L"1:00")==UIA_E_INVALIDOPERATION&&range->SetValue(3)==UIA_E_INVALIDOPERATION&&automation.stats().texts==0&&automation.stats().values==0,
          "Writes to a read-only peer are invalid operations");
    automation.disconnect();
    check(value->get_IsReadOnly(&fieldReadOnly)==UIA_E_ELEMENTNOTAVAILABLE,"A disconnected peer is no longer available");
}
}
int main(){
    Apartment apartment;
    try{builder();fragments();readOnlyHost();std::cout<<"PASS "<<checks<<" Personal Profile UI Automation checks\n";return 0;}
    catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
