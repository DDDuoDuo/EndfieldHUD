#include "../platform/projected_editor.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <wrl/client.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <oleauto.h>
#include <windowsx.h>
#endif

using endfield::platform::EditorModel;
using endfield::platform::ProjectiveMapping;
namespace {
unsigned checks{};
void check(bool expression, const char* description) {
    ++checks;
    if (!expression) throw std::runtime_error(description);
}
void text_model() {
    EditorModel model;
    check(model.set_text(u"中文 日本語 한국어 A😀B"),"accept mixed script UTF-16");
    auto original=model.text(); auto emoji=original.find(u"😀");
    model.select(emoji+1,emoji+1);
    check(model.caret()==emoji,"caret never splits a surrogate pair");
    check(!model.replace(emoji+1,emoji+2,u"x"),"partial surrogate replacement rejected");
    model.select(emoji+2,emoji+2); check(model.erase(false),"delete emoji");
    check(model.text()==original.substr(0,emoji)+original.substr(emoji+2),"backspace removes whole scalar");
    check(model.undo() && model.text()==original,"undo restores text");
    check(model.caret()==emoji+2,"undo restores caret");
    check(model.redo(),"redo deletion");
    check(!model.set_text(std::u16string(1,char16_t(0xd800))),"unpaired high surrogate rejected");
    check(!model.set_text(std::u16string(1,char16_t(0xdc00))),"unpaired low surrogate rejected");
    check(model.set_text(u"Ae\u0301👩‍💻Z"),"accept combining and ZWJ sequence");
    // Model is layout-neutral. These are the corresponding shaped cluster
    // boundaries; native integration obtains them from DirectWrite.
    model.set_clusters({0,1,3,8,9}); model.select(8,8); model.move(false,false);
    check(model.caret()==3,"cluster-aware arrows preserve ZWJ emoji");
    model.move(false,false); check(model.caret()==1,"cluster-aware arrows preserve combining marks");
    model.select(0,3); check(model.insert(u"한"),"replace selected cluster span");
    check(model.undo() && model.anchor()==0 && model.caret()==3,"undo selection restored");
    check(model.set_text(u"a"),"reset for composition"); model.select(1,1);
    model.begin_composition(); check(model.insert(u"n"),"provisional input");
    check(model.replace(1,2,u"ni"),"composition update");
    check(model.replace(1,3,u"你"),"composition commit"); model.end_composition();
    check(model.text()==u"a你","composition result");
    check(model.undo() && model.text()==u"a","whole composition is one undo step");
    check(model.redo() && model.text()==u"a你","whole composition redo");
    check(!model.set_text(std::u16string(EditorModel::maximum_units+1,u'a')),"bounded canonical text");
    check(model.set_text(u""),"clear editor");
    for (int i=0;i<150;++i) check(model.insert(u"x"),"history fill");
    int count=0; while(model.undo()) ++count;
    check(count==100,"undo history bounded to 100 steps");
}
void coordinates() {
    ProjectiveMapping mapping{{1.1,.07,250,-.08,.9,120,.0001,-.00015,1}};
    for (double x:{0.,200.,1000.}) for (double y:{0.,350.,700.}) {
        double px{},py{},rx{},ry{};
        check(mapping.project(x,y,px,py) && mapping.unproject(px,py,rx,ry),"projection round trip succeeds");
        check(std::abs(rx-x)<1e-8 && std::abs(ry-y)<1e-8,"drawing and pointer projection share coordinates");
    }
    ProjectiveMapping singular{{0,0,0,0,0,0,0,0,0}}; double x{},y{};
    check(!singular.project(1,1,x,y) && !singular.unproject(1,1,x,y),"invalid transforms cannot hit editor");
}
#ifdef _WIN32
void notification_delta(const endfield::platform::ProjectedEditor& editor,
    endfield::platform::EditorAccessibilityNotifications before,
    std::uint64_t text,std::uint64_t selection,std::uint64_t value) {
    const auto after=editor.accessibility_notifications();
    check(after.text-before.text==text,"TextPattern change notification count follows actual text edits");
    check(after.selection-before.selection==selection,"selection notification count follows actual selection/caret changes");
    check(after.value-before.value==value,"ValuePattern property notification count follows actual value changes");
}
POINT text_point(HWND owner,ITextProvider* provider,const wchar_t* query,double fraction=.25) {
    Microsoft::WRL::ComPtr<ITextRangeProvider> document,found;
    check(SUCCEEDED(provider->get_DocumentRange(&document)),"synthetic pointer fixture document range");
    BSTR text=SysAllocString(query);
    const HRESULT status=document->FindText(text,FALSE,FALSE,&found);SysFreeString(text);
    check(SUCCEEDED(status) && found,"synthetic pointer fixture text span");
    SAFEARRAY* rectangles{};
    check(SUCCEEDED(found->GetBoundingRectangles(&rectangles)) && rectangles,"pointer fixture uses projected UIA screen geometry");
    LONG low{},high{};SafeArrayGetLBound(rectangles,1,&low);SafeArrayGetUBound(rectangles,1,&high);
    check(high-low+1>=4,"pointer fixture span visible");
    double values[4]{};
    for(LONG at=0;at<4;++at) {LONG index=low+at;SafeArrayGetElement(rectangles,&index,&values[at]);}
    SafeArrayDestroy(rectangles);
    // Use the leading quarter so even a one-cluster span hits its leading ACP.
    POINT point{static_cast<LONG>(std::lround(values[0]+values[2]*fraction)),
                static_cast<LONG>(std::lround(values[1]+values[3]*.5))};
    check(ScreenToClient(owner,&point)!=FALSE,"pointer fixture screen/client mapping");return point;
}
void pointer_message(endfield::platform::ProjectedEditor& editor,UINT message,POINT point) {
    LRESULT result{};
    check(editor.handle_message(message,MK_LBUTTON,MAKELPARAM(point.x,point.y),result),"synthetic editor pointer message consumed");
}
void native_text_store() {
    // Isolated window/model only. This does not read user files, install an IME,
    // synthesize user keyboard events, or claim that live composition passed.
    HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    check(SUCCEEDED(apartment),"STA text services apartment");
    HWND owner=CreateWindowExW(0,L"STATIC",L"Synthetic projected editor test",WS_OVERLAPPEDWINDOW,
        0,0,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(owner!=nullptr,"isolated hidden HWND");
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(factory.GetAddressOf()))),"native DirectWrite factory");
    {
        endfield::platform::ProjectedEditor editor;
        int invalidations=0;
        check(SUCCEEDED(editor.initialize(owner,factory.Get(),[&] { ++invalidations; })),"native TSF context creation");
        check(SUCCEEDED(editor.tsf_status()),"TSF status reports initialized context");
        editor.set_rectangle(D2D1::RectF(90,120,640,360));
        editor.set_projection({{1.1,.02,30,-.04,.95,10,.0002,.0001,1}});
        check(editor.set_text(u"中文 日本語 한국어 e\u0301 👩‍💻"),"native mixed-script text layout");
        check(editor.model().text()==u"中文 日本語 한국어 e\u0301 👩‍💻","layout preserves canonical UTF-16");
        const auto combining=editor.model().text().find(u"e\u0301");
        check(editor.model().navigation_boundary(combining,true)==combining+2,"native DirectWrite cluster navigation preserves combining accent");
        check(invalidations>0,"text changes schedule artwork invalidation");
        check(!editor.font_inventory().empty(),"native installed-font inventory available");
        check(GetWindow(owner,GW_CHILD)==nullptr,"projected editor creates no flat child Edit HWND");
        check(editor.set_text(u"ab"),"synthetic notification fixture");
        auto notifications=editor.accessibility_notifications();editor.select_range(2,2);
        notification_delta(editor,notifications,0,1,0);
        notifications=editor.accessibility_notifications();editor.select_range(2,2);
        notification_delta(editor,notifications,0,0,0);
        editor.focus(true);LRESULT result{};
        notifications=editor.accessibility_notifications();
        check(editor.handle_message(WM_CHAR,u'c',0,result) && editor.model().text()==u"abc","synthetic ordinary character insertion");
        notification_delta(editor,notifications,1,1,1);
        notifications=editor.accessibility_notifications();
        check(editor.handle_message(WM_KEYDOWN,VK_BACK,0,result) && editor.model().text()==u"ab","synthetic ordinary backspace");
        notification_delta(editor,notifications,1,1,1);
        notifications=editor.accessibility_notifications();
        check(editor.handle_message(WM_KEYDOWN,VK_LEFT,0,result),"synthetic selection-only arrow");
        notification_delta(editor,notifications,0,1,0);
        notifications=editor.accessibility_notifications();check(editor.set_text(u"ab"),"same value can reset editor state");
        notification_delta(editor,notifications,0,1,0);
        notifications=editor.accessibility_notifications();
        check(editor.handle_message(WM_KEYDOWN,VK_BACK,0,result),"empty leading backspace consumed");
        notification_delta(editor,notifications,0,0,0);
        std::u16string document;
        for (unsigned row=0;row<100;++row) document+=u"中文 日本語 한국어 e\u0301 😀\n";
        check(editor.set_text(document),"long synthetic document");
        const auto text_revision=editor.model().revision();
        editor.select_range(document.size(),document.size());
        check(editor.scroll_offset()>0,"caret navigation reveals a line below viewport");
        auto artwork=editor.artwork_revision(); editor.scroll_to(0);
        check(editor.scroll_offset()==0 && editor.artwork_revision()>artwork,"wheel viewport invalidates only editor artwork");
        check(editor.model().revision()==text_revision,"scrolling cannot mutate or relayout canonical text");
        editor.scroll_to(-100); check(editor.scroll_offset()==0,"negative scroll clamps");
        editor.scroll_to(1e9f); check(editor.scroll_offset()>0 && editor.scroll_offset()<1e9f,"scroll bounded by content height");
        editor.scroll_to(0); editor.set_accessible_name(L"Synthetic notes");
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> accessible;
        check(SUCCEEDED(editor.get_accessibility_provider(IID_PPV_ARGS(&accessible))),"native UIA provider available");
        VARIANT name; VariantInit(&name);
        check(SUCCEEDED(accessible->GetPropertyValue(UIA_NamePropertyId,&name)) && name.vt==VT_BSTR &&
            std::wstring(name.bstrVal)==L"Synthetic notes","UIA accessible name reflects selected editor"); VariantClear(&name);
        Microsoft::WRL::ComPtr<ITextProvider> text_provider;
        check(SUCCEEDED(accessible.As(&text_provider)),"native TextPattern provider");
        Microsoft::WRL::ComPtr<ITextRangeProvider> full_range;
        check(SUCCEEDED(text_provider->get_DocumentRange(&full_range)),"UIA document range");
        BSTR text{}; check(SUCCEEDED(full_range->GetText(-1,&text)),"UIA document text");
        check(std::u16string(reinterpret_cast<const char16_t*>(text),SysStringLen(text))==document,"UIA preserves UTF-16 canonical text"); SysFreeString(text);
        BSTR query=SysAllocString(L"日本語"); Microsoft::WRL::ComPtr<ITextRangeProvider> found;
        check(SUCCEEDED(full_range->FindText(query,FALSE,FALSE,&found)) && found,"UIA find mixed-script phrase"); SysFreeString(query);
        check(SUCCEEDED(found->Select()) && editor.model().end()-editor.model().begin()==3,"UIA selection controls same native text model");
        SAFEARRAY* bounds{};
        check(SUCCEEDED(found->GetBoundingRectangles(&bounds)) && bounds,"UIA projected visible text bounds");
        LONG low{},high{}; SafeArrayGetLBound(bounds,1,&low); SafeArrayGetUBound(bounds,1,&high);
        check(high-low+1>=4,"UIA returns projected screen rectangle"); SafeArrayDestroy(bounds);
        Microsoft::WRL::ComPtr<IValueProvider> value_provider; check(SUCCEEDED(accessible.As(&value_provider)),"native ValuePattern provider");
        notifications=editor.accessibility_notifications();
        check(SUCCEEDED(value_provider->SetValue(L"Accessible 😀 text")),"UIA edit operation");
        check(editor.model().text()==u"Accessible 😀 text","UIA edits same canonical model");
        notification_delta(editor,notifications,1,1,1);

        const std::u16string pointer_text=u"alpha cafe\u0301 😀 omega\r\nsecond line\n";
        check(editor.set_text(pointer_text),"synthetic word/paragraph fixture");
        const auto alpha=text_point(owner,text_provider.Get(),L"alpha");
        const auto omega=text_point(owner,text_provider.Get(),L"omega");
        const auto cafe=text_point(owner,text_provider.Get(),L"cafe\u0301");
        const auto second=text_point(owner,text_provider.Get(),L"second");
        pointer_message(editor,WM_LBUTTONDBLCLK,alpha);
        check(editor.model().begin()==0 && editor.model().end()==5,"double click selects a word without trailing whitespace");
        pointer_message(editor,WM_MOUSEMOVE,omega);
        check(editor.model().begin()==0 && editor.model().end()==pointer_text.find(u"\r"),"word dragging expands by complete source words");
        pointer_message(editor,WM_LBUTTONUP,omega);
        check(!editor.pointer_tracking(),"synthetic word drag releases pointer scope");
        pointer_message(editor,WM_LBUTTONDBLCLK,cafe);
        check(editor.model().begin()==pointer_text.find(u"cafe\u0301") && editor.model().end()==pointer_text.find(u"cafe\u0301")+5,
            "double click preserves the DirectWrite combining cluster in a word");
        pointer_message(editor,WM_LBUTTONUP,cafe);
        pointer_message(editor,WM_LBUTTONDOWN,cafe);
        check(editor.model().begin()==0 && editor.model().end()==pointer_text.find(u"\r")+2,"third click selects the complete CRLF paragraph");
        pointer_message(editor,WM_MOUSEMOVE,second);
        check(editor.model().begin()==0 && editor.model().end()==pointer_text.size(),"paragraph dragging expands by complete paragraphs");
        pointer_message(editor,WM_LBUTTONUP,second);
        const auto emoji=text_point(owner,text_provider.Get(),L"😀");
        pointer_message(editor,WM_LBUTTONDBLCLK,emoji);
        check(editor.model().begin()==pointer_text.find(u"😀") && editor.model().end()==pointer_text.find(u"😀")+2,
            "double click preserves the complete emoji surrogate pair");
        pointer_message(editor,WM_LBUTTONUP,emoji);

        check(editor.set_text(u"q"),"one-unit word analyzer fixture");
        const auto single=text_point(owner,text_provider.Get(),L"q",.75);
        pointer_message(editor,WM_LBUTTONDBLCLK,single);
        check(editor.model().begin()==0 && editor.model().end()==1,"trailing-half word click supports a one-unit document and terminal item");
        pointer_message(editor,WM_LBUTTONUP,single);
        check(editor.set_text(u"first\u2029second"),"Unicode paragraph separator fixture");
        const auto first=text_point(owner,text_provider.Get(),L"first");
        pointer_message(editor,WM_LBUTTONDBLCLK,first);pointer_message(editor,WM_LBUTTONUP,first);
        pointer_message(editor,WM_LBUTTONDOWN,first);
        check(editor.model().begin()==0 && editor.model().end()==6,"third click honors the Unicode paragraph separator");
        pointer_message(editor,WM_LBUTTONUP,first);
        std::u16string mixed=u"target ";
        for(unsigned item=0;item<120;++item) mixed+=u"中 Α ";
        check(editor.set_text(mixed),"multi-item native word analyzer fixture");
        const auto target=text_point(owner,text_provider.Get(),L"target");
        pointer_message(editor,WM_LBUTTONDBLCLK,target);
        check(editor.model().begin()==0 && editor.model().end()==6,"word analyzer grows its bounded script-item buffer for mixed scripts");
        pointer_message(editor,WM_LBUTTONUP,target);
        editor.set_rectangle(D2D1::RectF(90,120,190,360));
        const std::u16string wrapped=u"wrapped paragraph continues beyond a visual line\nnext";
        check(editor.set_text(wrapped),"soft-wrapped paragraph fixture");
        const auto wrap=text_point(owner,text_provider.Get(),L"wrapped");
        pointer_message(editor,WM_LBUTTONDBLCLK,wrap);pointer_message(editor,WM_LBUTTONUP,wrap);
        pointer_message(editor,WM_LBUTTONDOWN,wrap);
        check(editor.model().begin()==0 && editor.model().end()==wrapped.find(u'\n')+1,"paragraph selection crosses visual wrapping to the real separator");
        pointer_message(editor,WM_LBUTTONUP,wrap);
        editor.shutdown();
        text=nullptr; check(full_range->GetText(-1,&text)==UIA_E_ELEMENTNOTAVAILABLE,"retained UIA ranges detach safely after editor closes");
    }
    DestroyWindow(owner); factory.Reset(); CoUninitialize();
}
#endif
}
int main() {
    try { text_model(); coordinates();
#ifdef _WIN32
        native_text_store();
#endif
        std::cout << "PASS: " << checks << " checks; synthetic UTF-16, history, composition grouping, projected coordinates, native TSF context, DirectWrite layout, viewport scrolling, pointer selection and UIA text/value notification contracts (live CJK IME/Narrator unverified)\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
