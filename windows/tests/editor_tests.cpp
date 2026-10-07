#include "../platform/projected_editor.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <wrl/client.h>
#include <objbase.h>
#endif

using endfield::platform::EditorModel;
using endfield::platform::ProjectiveMapping;
namespace {
void check(bool expression, const char* description) {
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
        check(invalidations>0,"text changes schedule artwork invalidation");
        check(!editor.font_inventory().empty(),"native installed-font inventory available");
        check(GetWindow(owner,GW_CHILD)==nullptr,"projected editor creates no flat child Edit HWND");
        editor.shutdown();
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
        std::cout << "PASS: synthetic UTF-16, history, composition grouping, projected coordinates, native TSF context and DirectWrite layout (live CJK IME unverified)\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
