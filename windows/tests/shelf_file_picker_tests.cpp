#include "native/shelf_file_picker.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <objbase.h>
#endif

namespace n=endfield::native;namespace d=ehud::data;
namespace {
unsigned checks{};void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&&body,const char*message){bool rejected{};try{body();}catch(const std::exception&){rejected=true;}check(rejected,message);}
void selections(){
    std::vector<std::string>mixed{"C:\\explicit-synthetic\\file.txt","C:\\explicit-synthetic\\folder","C:\\explicit-synthetic\\alias.lnk"};
    check(n::validShelfPickerSelection(mixed),"Mixed file/folder/link tokens remain references");
    check(!n::validShelfPickerSelection({}),"Empty success selection rejects");
    mixed.push_back("relative.txt");check(!n::validShelfPickerSelection(mixed),"Invalid last token rejects whole batch");mixed.pop_back();
    mixed.push_back("\\\\synthetic-server\\synthetic-share\\file.txt");check(n::validShelfPickerSelection(mixed),"Explicit filesystem UNC token accepted without IO");
    mixed.back()="C:\\folder\\..\\file.txt";check(!n::validShelfPickerSelection(mixed),"Dot traversal rejects");
    mixed.back()="C:\\folder\\file.txt:stream";check(!n::validShelfPickerSelection(mixed),"Alternate stream rejects");
    mixed.back()="C:\\folder\\bad\xff";check(!n::validShelfPickerSelection(mixed),"Invalid UTF8 rejects");
    std::vector<std::string>many(n::shelfPickerMaximumItems+1,"C:\\synthetic\\file");check(!n::validShelfPickerSelection(many),"Selection item bound is explicit, never truncate");
    const std::string longPath="C:\\"+std::string(32765,'a');std::vector<std::string>budget(128,longPath);check(n::validShelfPickerSelection(budget),"Exact aggregate byte boundary accepted");budget.push_back("C:\\a");check(!n::validShelfPickerSelection(budget),"Aggregate path budget rejects atomically");
}
}
#ifdef _WIN32
namespace {
struct COM {COM(){check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Owned test thread STA initialized");}~COM(){CoUninitialize();}};
struct Window {
    HWND value{};static constexpr UINT notice=WM_APP+251;
    Window(){value=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Owned hidden Shelf picker fixture",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(value&&!IsWindowVisible(value),"Synthetic picker owner is hidden");}
    ~Window(){if(value)DestroyWindow(value);}
    n::ShelfPickerRoute route(UINT_PTR generation)const{return {value,notice,generation};}
    MSG next(){MSG message{};check(PeekMessageW(&message,value,notice,notice,PM_REMOVE)!=FALSE,"Private picker notice queued exactly on owned HWND");return message;}
    void discard(){MSG message{};while(PeekMessageW(&message,value,notice,notice,PM_REMOVE))check(message.wParam!=0,"No raw callback pointer in route");}
};
struct FakeDialog final:n::ShelfPickerDialog {
    n::ShelfPickerLabels labels;std::vector<std::string>paths{"C:\\explicit-synthetic\\file.txt","C:\\explicit-synthetic\\folder","C:\\explicit-synthetic\\alias.lnk"};
    HRESULT configured{S_OK},shown{S_OK},selected{S_OK};unsigned configureCalls{},showCalls{},selectCalls{},cancelCalls{};HWND owner{};
    std::function<void()>onConfigure,onShow,onSelection,onCancel;
    HRESULT configure(const n::ShelfPickerLabels&value)override{++configureCalls;labels=value;if(onConfigure)onConfigure();return configured;}
    HRESULT show(HWND value)override{++showCalls;owner=value;if(onShow)onShow();return shown;}
    HRESULT selection(std::vector<std::string>&out)override{++selectCalls;if(onSelection)onSelection();out=paths;return selected;}
    void cancel()noexcept override{++cancelCalls;if(onCancel)try{onCancel();}catch(...) {}}
};
void dispatch(n::NativeShelfFilePicker&picker,const MSG&message){check(picker.handleMessage(message.wParam,message.lParam),"Owner handles queued request outside state callback");}
void deferredAndReferences(){
    Window window;auto dialog=std::make_shared<FakeDialog>();unsigned factories{};bool stateCallback{};
    n::ShelfPickerLabels labels{"添加到文件暂存架","添加引用","添加所选引用"};
    n::NativeShelfFilePicker picker(window.route(11),labels,[&]{check(!stateCallback,"Native modal operation cannot run under state/store busy callback");++factories;return dialog;});
    stateCallback=true;check(picker.request(),"Chooser request enqueues without opening a dialog");check(factories==0&&dialog->showCalls==0,"No native UI inside synchronous source callback");stateCallback=false;
    check(!picker.request(),"Repeated chooser request coalesces");check(!picker.drain(11),"No completion before owner handles request");auto request=window.next();check(request.wParam==11&&request.lParam==static_cast<LPARAM>(n::ShelfPickerNotice::showRequested),"Request notice contains only route generation/action");
    check(!picker.handleMessage(10,request.lParam)&&factories==0,"Stale owner generation cannot start chooser");dispatch(picker,request);
    check(dialog->configureCalls==1&&dialog->showCalls==1&&dialog->selectCalls==1&&dialog->owner==window.value,"Single dialog supplies complete native selection");check(dialog->labels.title==labels.title&&dialog->labels.addReferences==labels.addReferences&&dialog->labels.addSelection==labels.addSelection,"Owner localization passed intact");
    auto completion=window.next();check(completion.lParam==static_cast<LPARAM>(n::ShelfPickerNotice::completed),"Completion is a queued generation notice, not callback");dispatch(picker,completion);check(!picker.drain(10),"Stale drain cannot consume paths");auto result=picker.drain(11);check(result&&SUCCEEDED(result->result)&&result->paths==dialog->paths,"Files/folders/links returned intact, without content access");check(!picker.drain(11),"Completion delivered at most once");
    const n::ShelfPickerLabels nextLabels{"New title","New add","New selection"};picker.setLabels(nextLabels);check(dialog->configureCalls==1&&dialog->cancelCalls==0,"Preference change never reconstructs or cancels native picker");check(picker.request(),"Next localized picker request accepted");dispatch(picker,window.next());check(dialog->labels.title==nextLabels.title&&dialog->configureCalls==2,"Next dialog receives new preference labels");dispatch(picker,window.next());check(picker.drain(11).has_value(),"Localized chooser completion delivered");rejects([&]{picker.setLabels({"", "valid", "valid"});},"Invalid picker labels reject before request");
    const auto root=std::filesystem::temp_directory_path()/("endfield-picker-store-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));check(std::filesystem::create_directory(root),"Own new temporary store root");struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}}cleanup{root};
    unsigned acquired{},released{};auto acquire=[&](std::string_view path){++acquired;d::ShelfFileMetadata metadata;metadata.windowsPath=path;metadata.name=metadata.windowsPath.substr(metadata.windowsPath.find_last_of('\\')+1);metadata.isDirectory=metadata.name=="folder";metadata.kind=metadata.isDirectory?d::ShelfFileKind::directory:d::ShelfFileKind::regular;metadata.typeDescription=metadata.isDirectory?"Folder":"File";
        metadata.identity.volumeSerial=1;metadata.identity.objectID[0]=static_cast<std::uint8_t>(metadata.isDirectory?2u:(metadata.name=="alias.lnk"?3u:1u));return d::ShelfFileAccess{std::move(metadata),[&]{++released;}};};
    d::FileShelfStore store(root,{acquire,[&](const d::ShelfRecord&record){return acquire(record.windowsPath);}});
    check(store.add(result->paths)==3&&store.items().size()==3,"Complete result commits reference metadata once through actual store");check(acquired==released,"Import scopes balanced before returning to renderer owner");
    d::FileShelfStore restored(root,{acquire,[&](const d::ShelfRecord&record){return acquire(record.windowsPath);}});check(restored.items().size()==3&&restored.items()[1].isDirectory,"Temporary archive preserves native reference types");check(!IsWindowVisible(window.value),"Fake dialog fixture never makes UI visible");
}
void cancellationAndFailure(){
    Window window;auto dialog=std::make_shared<FakeDialog>();n::NativeShelfFilePicker picker(window.route(21),{},[&]{return dialog;});
    picker.request();const auto stale=window.next();picker.cancel();check(!picker.handleMessage(stale.wParam,stale.lParam)&&dialog->showCalls==0,"Canceled queued action cannot open a chooser");
    dialog->shown=HRESULT_FROM_WIN32(ERROR_CANCELLED);picker.request();dispatch(picker,window.next());dispatch(picker,window.next());auto canceled=picker.drain(21);check(canceled&&canceled->canceled()&&canceled->paths.empty()&&dialog->selectCalls==0,"User cancel never returns/imports paths");
    dialog->shown=S_OK;dialog->paths.push_back("relative.txt");picker.request();dispatch(picker,window.next());dispatch(picker,window.next());auto bad=picker.drain(21);check(bad&&FAILED(bad->result)&&bad->paths.empty(),"Invalid late selection discards entire dialog result");dialog->paths.pop_back();
    dialog->onSelection=[&]{picker.cancel();};picker.request();dispatch(picker,window.next());check(!picker.drain(21)&&!picker.stats().hasCompletion,"Cancel reentry during selection suppresses stale completion");dialog->onSelection={};
    picker.request();const auto old=window.next();picker.setRoute(window.route(22));picker.request();const auto next=window.next();check(!picker.handleMessage(old.wParam,old.lParam),"Old HWND route generation cannot open replacement dialog");dispatch(picker,next);dispatch(picker,window.next());check(picker.drain(22).has_value(),"New route receives only its current result");
    rejects([&]{picker.setRoute({window.value,WM_USER,22});},"Nonprivate route rejected");rejects([&]{n::NativeShelfFilePicker invalid(window.route(30),{"","Add","Selection"});},"Empty localization rejects before UI");
    n::NativeShelfFilePicker failing(window.route(31),{},[]{throw std::runtime_error("Synthetic factory failure");return std::shared_ptr<n::ShelfPickerDialog>{};});failing.request();dispatch(failing,window.next());dispatch(failing,window.next());const auto failed=failing.drain(31);check(failed&&FAILED(failed->result)&&failed->paths.empty(),"Factory exceptions become bounded failed result");window.discard();
}
void destructionReentry(){
    Window window;auto dialog=std::make_shared<FakeDialog>();auto picker=std::make_unique<n::NativeShelfFilePicker>(window.route(41),n::ShelfPickerLabels{},[&]{return dialog;});
    picker->request();const auto request=window.next();auto* borrowed=picker.get();dialog->onShow=[&]{picker.reset();};check(borrowed->handleMessage(request.wParam,request.lParam),"Facade may die during native nested modal call");check(!picker&&dialog->cancelCalls==1&&dialog->selectCalls==0,"Teardown cancels and suppresses selection after modal reentry");MSG notice{};check(!PeekMessageW(&notice,window.value,Window::notice,Window::notice,PM_REMOVE),"Destroyed owner route receives no completion notice");
    dialog->onShow={};dialog->onConfigure=[&]{picker.reset();};picker=std::make_unique<n::NativeShelfFilePicker>(window.route(42),n::ShelfPickerLabels{},[&]{return dialog;});picker->request();const auto configured=window.next();borrowed=picker.get();const auto shows=dialog->showCalls;check(borrowed->handleMessage(configured.wParam,configured.lParam)&&!picker&&dialog->showCalls==shows,"Configure reentry cannot use a detached owner HWND");
}
d::ShelfFileAccess lease(unsigned identity,unsigned&released){d::ShelfFileMetadata metadata;metadata.windowsPath="C:\\explicit-synthetic\\owned.txt";metadata.name="owned.txt";metadata.identity.volumeSerial=1;metadata.identity.objectID[0]=static_cast<std::uint8_t>(identity);return {std::move(metadata),[&]{++released;}};}
void revealIdentity(){
    unsigned released{},calls{};auto result=n::revealShelfReference(lease(1,released),[&](const d::ShelfRecord&record){check(record.identity.objectID[0]==1,"Reveal independently verifies original object identity");return lease(1,released);},[&](const d::ShelfFileAccess&current){++calls;check(current.open()&&released==0,"Both leases span the Explorer handoff");return S_OK;});
    check(SUCCEEDED(result)&&calls==1&&released==2,"Reveal balances both leases without copying/opening content");
    released=calls=0;result=n::revealShelfReference(lease(1,released),[&](const d::ShelfRecord&){return lease(2,released);},[&](const d::ShelfFileAccess&){++calls;return S_OK;});check(FAILED(result)&&calls==0&&released==2,"Replaced file identity never reaches Shell");
    released=calls=0;result=n::revealShelfReference(lease(1,released),[&](const d::ShelfRecord&){return lease(1,released);},[&](const d::ShelfFileAccess&)->HRESULT{++calls;throw std::runtime_error("Synthetic handoff failure");});check(FAILED(result)&&calls==1&&released==2,"External handoff failure cannot leak references");
    check(FAILED(n::revealShelfReference({}, {}, {})),"Closed lease cannot implicitly access any user path");
}
}
int main(){try{selections();COM com;deferredAndReferences();cancellationAndFailure();destructionReentry();revealIdentity();std::cout<<"PASS "<<checks<<" Shelf picker/reference handoff checks\n";return 0;}catch(const std::exception&error){std::cerr<<"FAIL after "<<checks<<": "<<error.what()<<'\n';return 1;}}
#else
int main(){try{selections();std::cout<<"PASS "<<checks<<" portable Shelf selection checks; native dialog/reveal fixtures not executed\n";return 0;}catch(const std::exception&error){std::cerr<<"FAIL after "<<checks<<": "<<error.what()<<'\n';return 1;}}
#endif
