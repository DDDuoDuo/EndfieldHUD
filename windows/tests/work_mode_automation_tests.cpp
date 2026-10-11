// UI Automation peers for the projected Work Mode controls over a real
// WorkModeController/WorkModePresentation: no window, Focus service, UIA
// client or user data is involved.
#ifdef _WIN32
#include "native/work_mode_automation.hpp"
#include "modules/module_strings.hpp"
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace n=endfield::native;namespace m=endfield::modules;namespace c=endfield::core;using Microsoft::WRL::ComPtr;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Apartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};
std::wstring text(IRawElementProviderFragment*f,PROPERTYID id){
    ComPtr<IRawElementProviderSimple>s;VARIANT v;VariantInit(&v);std::wstring out;
    if(f&&SUCCEEDED(f->QueryInterface(IID_PPV_ARGS(&s)))&&SUCCEEDED(s->GetPropertyValue(id,&v))&&v.vt==VT_BSTR&&v.bstrVal)out.assign(v.bstrVal,SysStringLen(v.bstrVal));
    VariantClear(&v);return out;
}
ComPtr<IRawElementProviderFragment>peer(const n::NativeProfileAutomation&a,const wchar_t*id){
    for(std::size_t i=0;i<a.size();++i){ComPtr<IRawElementProviderFragment>f;if(SUCCEEDED(a.child(i,&f))&&f&&text(f.Get(),UIA_AutomationIdPropertyId)==id)return f;}
    return nullptr;
}
HRESULT invoke(const ComPtr<IRawElementProviderFragment>&f){ComPtr<IInvokeProvider>i;if(!f||FAILED(f.As(&i)))return E_NOINTERFACE;return i->Invoke();}
bool has(const std::vector<n::ProfileAutomationControl>&list,std::string_view id){return std::any_of(list.begin(),list.end(),[&](const auto&p){return p.id==id;});}

void run(){
    m::WorkModeController controller;
    m::WorkModePresentation state(controller,m::workModeStrings(c::Language::english));
    double now=1;state.activate(now);
    // WorkModeCanvas.accessibleActions with the source help strings.
    auto controls=n::workModeAutomationControls(state,c::Language::english);
    const auto status=state.accessibilityStatus()+". Space starts, pauses or resumes.";
    check(!controls.empty()&&controls.size()==state.actions().size(),"One peer per accessible Work Mode action");
    for(std::size_t i=0;i<controls.size();++i){const auto&a=state.actions()[i];const auto&p=controls[i];
        check(p.id==a.id&&p.label==a.label&&p.rect==a.rect&&p.role==n::ProfileAutomationRole::button&&p.help==status,"Peers keep the source order, label and rect; help is the status and the Space hint");}
    check(has(controls,"work:countdown")&&has(controls,"work:stopwatch")&&has(controls,"work:start")&&has(controls,"work:reset")&&!has(controls,"work:pause"),"An idle page lists its configuration, Start and Reset");
    const auto chinese=n::workModeAutomationControls(state,c::Language::simplifiedChinese);
    check(!chinese.empty()&&chinese.front().help==state.accessibilityStatus()+"。空格键开始、暂停或继续。","The Space hint follows the HUD language");
    state.setFocusStatus("Focus needs permission",true,now);controls=n::workModeAutomationControls(state,c::Language::english);
    const auto focus=std::find_if(controls.begin(),controls.end(),[](const auto&p){return p.id=="work:focusAccess";});
    check(focus!=controls.end()&&focus->help==focus->label,"The Focus permission peer's help is its label");
    state.setFocusStatus({},false,now);
    // The open custom duration field (HUDWorkDurationField, a projected
    // NSTextView): an Edit after the actions with the source label and help.
    {
        const auto same=n::workModeAutomationControls(state,c::Language::english,std::nullopt);
        check(same==n::workModeAutomationControls(state,c::Language::english),"Without an open field the list is the action list");
        const auto editing=n::workModeAutomationControls(state,c::Language::english,std::string("25:00"));
        check(editing.size()==same.size()+1&&std::equal(same.begin(),same.end(),editing.begin()),"The actions keep their order before the field");
        const auto&field=editing.back();
        check(field.id==n::workModeDurationEditorID&&field.role==n::ProfileAutomationRole::edit&&field.label=="Custom countdown: minutes and seconds"&&
              field.help=="Enter minutes or min:sec, between 0:01 and 1440:00. Return saves; Escape cancels."&&field.rect==m::WorkModePresentation::durationEditorRect()&&field.text=="25:00",
              "The field is an Edit over the editor rect with the source label, help and draft");
        const auto chinese=n::workModeAutomationControls(state,c::Language::simplifiedChinese,std::string("5"));
        check(chinese.back().label=="自定义倒计时：分和秒"&&chinese.back().help=="输入分钟数或分:秒，范围为 0:01 至 1440:00。回车保存，Esc 取消。"&&chinese.back().text=="5","The field's label and help follow the HUD language");
        const auto japanese=n::workModeAutomationControls(state,c::Language::japanese,std::string("5"));
        check(japanese.back().label=="カスタムカウントダウン：分と秒","Other languages use the source catalog");
    }
    // The field's peer: focused, holding the draft, written through setText.
    {
        std::optional<std::string>draft;std::vector<std::string>written;
        n::ProfileAutomationHost host;
        host.controls=[&]{return n::workModeAutomationControls(state,c::Language::english,draft);};
        host.toScreen=[](c::Rect r)->std::optional<c::Rect>{return c::Rect{r.x,r.y,r.width,r.height};};
        host.invoke=[](std::string_view){return true;};
        host.setText=[&](std::string_view id,std::string_view value){written.push_back(std::string(id)+"="+std::string(value));draft=std::string(value);return true;};
        n::NativeProfileAutomation automation(std::move(host));
        check(automation.refresh()&&!peer(automation,L"work:editor:duration"),"No field peer while the field is closed");
        draft="25:00";
        check(automation.refresh()&&automation.size()==state.actions().size()+1,"Opening the field adds its peer");
        const auto field=peer(automation,L"work:editor:duration");
        ComPtr<IValueProvider>value;BSTR shown{};BOOL readOnly{TRUE};
        check(field&&text(field.Get(),UIA_NamePropertyId)==L"Custom countdown: minutes and seconds"&&SUCCEEDED(field.As(&value))&&SUCCEEDED(value->get_Value(&shown))&&shown&&std::wstring_view(shown)==L"25:00"&&
              SUCCEEDED(value->get_IsReadOnly(&readOnly))&&!readOnly,"The field peer is named by the source label and holds the draft");
        SysFreeString(shown);shown=nullptr;
        ComPtr<IRawElementProviderSimple>simple;VARIANT focused;VariantInit(&focused);
        check(SUCCEEDED(field.As(&simple))&&SUCCEEDED(simple->GetPropertyValue(UIA_HasKeyboardFocusPropertyId,&focused))&&focused.vt==VT_BOOL&&focused.boolVal==VARIANT_TRUE,"The open field has the HUD's keyboard focus (first responder)");
        VariantClear(&focused);
        const auto r=m::WorkModePresentation::durationEditorRect();ComPtr<IRawElementProviderFragment>hit;
        check(SUCCEEDED(automation.fromPoint(r.x+r.width/2,r.y+r.height/2,&hit))&&hit&&hit.Get()==field.Get(),"The field is the topmost control over its rect");
        check(value->SetValue(L"1:30")==S_OK&&written==std::vector<std::string>{"work:editor:duration=1:30"},"A Value write reaches the field once as UTF-8");
        check(!automation.refresh()&&peer(automation,L"work:editor:duration").Get()==field.Get()&&SUCCEEDED(value->get_Value(&shown))&&shown&&std::wstring_view(shown)==L"1:30","The same peer reports the new draft");
        SysFreeString(shown);
        draft.reset();
        check(automation.refresh()&&!peer(automation,L"work:editor:duration")&&value->SetValue(L"2")==UIA_E_ELEMENTNOTAVAILABLE&&written.size()==1,"Closing the field removes its peer");
        automation.disconnect();
    }
    // The peers drive the page exactly like clicks (WorkModePreview::perform).
    std::vector<std::string>pressed;
    n::ProfileAutomationHost host;
    host.controls=[&]{return n::workModeAutomationControls(state,c::Language::english);};
    host.toScreen=[](c::Rect r)->std::optional<c::Rect>{return c::Rect{r.x,r.y,r.width,r.height};};
    host.invoke=[&](std::string_view id){pressed.emplace_back(id);return state.perform(id,now+=.5);};
    n::NativeProfileAutomation automation(std::move(host));
    check(automation.refresh()&&automation.size()==state.actions().size(),"The Work Mode group lists the page's actions");
    check(invoke(peer(automation,L"work:stopwatch"))==S_OK&&controller.snapshot(now).kind==m::WorkModeKind::stopwatch,"The Stopwatch peer selects the stopwatch");
    state.refresh(now+=1);(void)automation.refresh();
    check(invoke(peer(automation,L"work:start"))==S_OK&&controller.snapshot(now).phase==m::WorkModePhase::running,"The Start peer starts the stopwatch");
    check(automation.refresh()&&peer(automation,L"work:pause")&&!peer(automation,L"work:start")&&!peer(automation,L"work:stopwatch"),"A running stopwatch offers Pause and hides its configuration");
    check(invoke(peer(automation,L"work:pause"))==S_OK&&controller.snapshot(now).phase==m::WorkModePhase::paused&&controller.trackedWorkSeconds(now)>0,"The Pause peer pauses with tracked time");
    check(automation.refresh()&&peer(automation,L"work:resume")&&!peer(automation,L"work:pause"),"A paused stopwatch offers Resume");
    check(pressed==std::vector<std::string>{"work:stopwatch","work:start","work:pause"},"Each press reaches the page once");
    automation.disconnect();check(automation.size()==0,"Disconnecting empties the group");
}
}
int main(){
    Apartment apartment;
    try{run();std::cout<<"PASS "<<checks<<" Work Mode UI Automation checks\n";return 0;}
    catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
