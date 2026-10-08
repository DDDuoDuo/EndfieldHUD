#include "native/shelf_file_picker.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace endfield::native {
bool validShelfPickerSelection(std::span<const std::string>paths)noexcept{
    if(paths.empty()||paths.size()>shelfPickerMaximumItems)return false;
    std::size_t bytes{};for(const auto&path:paths){
        if(!ehud::data::validWindowsFilePath(path)||path.size()>shelfPickerMaximumSelectionBytes-bytes)return false;
        bytes+=path.size();
    }return true;
}
}
#ifdef _WIN32
#include "native/file_shelf_files.hpp"
#include <atomic>
#include <shobjidl.h>
#include <shlobj.h>
#include <wrl/client.h>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
struct Failure {HRESULT result;};
void checked(HRESULT result){if(FAILED(result))throw Failure{result};}
template<class F>HRESULT protect(F&&body)noexcept{try{return body();}catch(const Failure&e){return e.result;}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(const std::invalid_argument&){return E_INVALIDARG;}catch(...){return E_FAIL;}}
std::wstring wide(std::string_view value){
    need(!value.empty()&&value.size()<=32768&&ehud::data::Json::validUtf8(value)&&value.find('\0')==std::string_view::npos,"Invalid native Shelf dialog text");
    const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(size<=0)throw Failure{HRESULT_FROM_WIN32(GetLastError())};std::wstring out(static_cast<std::size_t>(size),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),size)!=size)throw Failure{HRESULT_FROM_WIN32(GetLastError())};return out;
}
std::string pathText(PCWSTR value){
    need(value!=nullptr,"Shell returned no filesystem path");std::size_t units{};while(units<=32768&&value[units])++units;need(units>0&&units<=32768,"Shell filesystem path exceeds its explicit bound");
    const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,static_cast<int>(units),nullptr,0,nullptr,nullptr);
    if(size<=0)throw Failure{HRESULT_FROM_WIN32(GetLastError())};std::string out(static_cast<std::size_t>(size),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,static_cast<int>(units),out.data(),size,nullptr,nullptr)!=size)throw Failure{HRESULT_FROM_WIN32(GetLastError())};
    need(ehud::data::validWindowsFilePath(out),"Picker returned an unsupported filesystem path");return out;
}
void labels(const ShelfPickerLabels&value){for(const auto*token:{&value.title,&value.addReferences,&value.addSelection})need(!token->empty()&&token->size()<=4096&&ehud::data::Json::validUtf8(*token)&&token->find('\0')==std::string::npos,"Invalid Shelf picker localization");}
void route(const ShelfPickerRoute&value){
    if(!value.owner){need(!value.message&&!value.generation,"Empty Shelf picker route must have no message or generation");return;}
    DWORD process{};const auto thread=GetWindowThreadProcessId(value.owner,&process);
    need(IsWindow(value.owner)&&thread==GetCurrentThreadId()&&process==GetCurrentProcessId()&&value.message>=WM_APP&&value.message<=0xbfff&&value.generation,"Shelf picker requires a live owner-thread window/private generation route");
}
struct TaskString {PWSTR value{};~TaskString(){CoTaskMemFree(value);}};
HRESULT collect(IShellItemArray*array,std::vector<std::string>&out)noexcept{return protect([&]{
    need(array!=nullptr,"Picker returned no selection");DWORD count{};checked(array->GetCount(&count));need(count>0&&count<=shelfPickerMaximumItems,"Picker selection exceeds the item bound");
    std::vector<std::string>next;next.reserve(count);std::size_t bytes{};
    for(DWORD index=0;index<count;++index){ComPtr<IShellItem>item;checked(array->GetItemAt(index,&item));SFGAOF attributes{};checked(item->GetAttributes(SFGAO_FILESYSTEM,&attributes));need((attributes&SFGAO_FILESYSTEM)!=0,"Shelf accepts filesystem references only");
        TaskString path;checked(item->GetDisplayName(SIGDN_FILESYSPATH,&path.value));auto token=pathText(path.value);
        need(token.size()<=shelfPickerMaximumSelectionBytes-bytes,"Picker selection exceeds its aggregate path bound");bytes+=token.size();next.push_back(std::move(token));}
    out.swap(next);return S_OK;
});}
constexpr DWORD addSelectionControl=0x4568;
struct NativeDialogState {ComPtr<IFileOpenDialog>dialog;std::vector<std::string>picked;bool custom{},canceled{};};
class DialogEvents final:public IFileDialogEvents,public IFileDialogControlEvents {
    std::atomic<ULONG>references_{1};std::weak_ptr<NativeDialogState>state_;
public:
    explicit DialogEvents(const std::shared_ptr<NativeDialogState>&state):state_(state){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid==IID_IUnknown||iid==IID_IFileDialogEvents)*out=static_cast<IFileDialogEvents*>(this);else if(iid==IID_IFileDialogControlEvents)*out=static_cast<IFileDialogControlEvents*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto remaining=--references_;if(!remaining)delete this;return remaining;}
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog*)override{const auto state=state_.lock();return state&&!state->canceled?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog*,IShellItem*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog*,IShellItem*,FDE_SHAREVIOLATION_RESPONSE*out)override{if(!out)return E_POINTER;*out=FDESVR_DEFAULT;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog*,IShellItem*,FDE_OVERWRITE_RESPONSE*out)override{if(!out)return E_POINTER;*out=FDEOR_DEFAULT;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnItemSelected(IFileDialogCustomize*,DWORD,DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnButtonClicked(IFileDialogCustomize*,DWORD id)override{
        return protect([&]{if(id!=addSelectionControl)return S_OK;auto state=state_.lock();if(!state||state->canceled)return E_ABORT;auto dialog=state->dialog;if(!dialog)return E_ABORT;
        ComPtr<IShellItemArray>items;const auto hr=dialog->GetSelectedItems(&items);if(FAILED(hr))return hr;if(state->canceled)return E_ABORT;
        std::vector<std::string>picked;const auto converted=collect(items.Get(),picked);if(FAILED(converted))return converted;if(state->canceled)return E_ABORT;
        state->picked.swap(picked);state->custom=true;return dialog->Close(S_OK);});
    }
    HRESULT STDMETHODCALLTYPE OnCheckButtonToggled(IFileDialogCustomize*,DWORD,BOOL)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnControlActivating(IFileDialogCustomize*,DWORD)override{return S_OK;}
};
class NativeDialog final:public ShelfPickerDialog {
    std::shared_ptr<NativeDialogState>state_{std::make_shared<NativeDialogState>()};ComPtr<DialogEvents>events_;DWORD advice_{};bool advised_{};
public:
    ~NativeDialog()override{state_->canceled=true;if(advised_&&state_->dialog)state_->dialog->Unadvise(advice_);events_.Reset();state_->dialog.Reset();}
    HRESULT configure(const ShelfPickerLabels&text)override{return protect([&]{
        labels(text);APTTYPE apartment{};APTTYPEQUALIFIER qualifier{};checked(CoGetApartmentType(&apartment,&qualifier));need(apartment==APTTYPE_STA||apartment==APTTYPE_MAINSTA,"Native Shelf picker must use the owner's existing STA apartment");
        checked(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&state_->dialog)));
        FILEOPENDIALOGOPTIONS options{};checked(state_->dialog->GetOptions(&options));
        options|=FOS_FORCEFILESYSTEM|FOS_ALLOWMULTISELECT|FOS_PATHMUSTEXIST|FOS_FILEMUSTEXIST|FOS_NODEREFERENCELINKS|FOS_DONTADDTORECENT;
        options&=~(FOS_PICKFOLDERS|FOS_ALLNONSTORAGEITEMS|FOS_STRICTFILETYPES);checked(state_->dialog->SetOptions(options));
        const auto title=wide(text.title),prompt=wide(text.addReferences),selection=wide(text.addSelection);checked(state_->dialog->SetTitle(title.c_str()));checked(state_->dialog->SetOkButtonLabel(prompt.c_str()));
        ComPtr<IFileDialogCustomize>custom;checked(state_->dialog.As(&custom));checked(custom->AddPushButton(addSelectionControl,selection.c_str()));
        events_.Attach(new DialogEvents(state_));checked(state_->dialog->Advise(events_.Get(),&advice_));advised_=true;return S_OK;
    });}
    HRESULT show(HWND owner)override{return state_->dialog?state_->dialog->Show(owner):E_UNEXPECTED;}
    HRESULT selection(std::vector<std::string>&out)override{
        if(state_->canceled)return HRESULT_FROM_WIN32(ERROR_CANCELLED);if(state_->custom){out=std::move(state_->picked);return S_OK;}
        if(!state_->dialog)return E_UNEXPECTED;ComPtr<IShellItemArray>items;const auto result=state_->dialog->GetResults(&items);if(FAILED(result))return result;return collect(items.Get(),out);
    }
    void cancel()noexcept override{state_->canceled=true;auto dialog=state_->dialog;if(dialog)dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));}
};
std::shared_ptr<ShelfPickerDialog>nativeDialog(){return std::make_shared<NativeDialog>();}
HRESULT shellReveal(const ehud::data::ShelfFileAccess&lease)noexcept{return protect([&]{
    const auto name=wide(lease.metadata().windowsPath);PIDLIST_ABSOLUTE raw{};checked(SHParseDisplayName(name.c_str(),nullptr,&raw,0,nullptr));
    struct PIDL {PIDLIST_ABSOLUTE value;~PIDL(){CoTaskMemFree(value);}}pidl{raw};need(pidl.value!=nullptr,"Cannot resolve a Shelf Explorer selection");return SHOpenFolderAndSelectItems(pidl.value,0,nullptr,0);
});}
}
struct NativeShelfFilePicker::State {
    const DWORD thread{GetCurrentThreadId()};ShelfPickerRoute route;ShelfPickerLabels labels;Factory factory;
    std::shared_ptr<ShelfPickerDialog>dialog;std::optional<ShelfPickerCompletion>completion;
    std::uint64_t token{1};bool alive{true},queued{},presenting{};ShelfPickerStats counts;
    State(ShelfPickerRoute r,ShelfPickerLabels l,Factory f):route(r),labels(std::move(l)),factory(std::move(f)){}
    void onThread()const{need(thread==GetCurrentThreadId(),"Shelf picker belongs to its creating UI thread");}
    bool current(std::uint64_t value)const noexcept{return alive&&presenting&&token==value;}
    bool post(ShelfPickerNotice value)noexcept{if(!alive||!route.owner)return false;if(!PostMessageW(route.owner,route.message,route.generation,static_cast<LPARAM>(value)))return false;++counts.notices;return true;}
    void cancel()noexcept{if(token<std::numeric_limits<std::uint64_t>::max())++token;queued=false;presenting=false;completion.reset();auto owned=std::exchange(dialog,{});if(owned)owned->cancel();++counts.cancellations;}
};
NativeShelfFilePicker::NativeShelfFilePicker(ShelfPickerRoute value,ShelfPickerLabels text,Factory factory){route(value);labels(text);if(!factory)factory=nativeDialog;state_=std::make_shared<State>(value,std::move(text),std::move(factory));}
NativeShelfFilePicker::~NativeShelfFilePicker(){auto state=state_;if(GetCurrentThreadId()!=state->thread)std::terminate();state->alive=false;state->route={};state->cancel();}
bool NativeShelfFilePicker::request(){auto state=state_;state->onThread();need(state->alive&&state->route.owner,"Shelf picker needs an active owner route");if(state->queued||state->presenting||state->completion)return false;
    route(state->route);need(state->token<std::numeric_limits<std::uint64_t>::max(),"Shelf picker generation exhausted");++state->token;state->queued=true;
    if(!state->post(ShelfPickerNotice::showRequested)){state->queued=false;throw std::runtime_error("Cannot queue the native Shelf picker");}++state->counts.requests;return true;
}
bool NativeShelfFilePicker::handleMessage(UINT_PTR generation,LPARAM notice){auto state=state_;state->onThread();if(!state->alive||generation!=state->route.generation||!state->route.owner)return false;
    if(notice==static_cast<LPARAM>(ShelfPickerNotice::completed))return true;
    if(notice!=static_cast<LPARAM>(ShelfPickerNotice::showRequested)||!state->queued||state->presenting)return false;
    state->queued=false;state->presenting=true;const auto token=state->token;ShelfPickerCompletion result;
    result.result=protect([&]{auto dialog=state->factory();if(!state->current(token))return E_ABORT;need(bool(dialog),"Shelf picker factory returned no dialog");state->dialog=dialog;
        auto hr=dialog->configure(state->labels);if(!state->current(token))return E_ABORT;if(FAILED(hr))return hr;
        ++state->counts.shows;hr=dialog->show(state->route.owner);if(!state->current(token))return E_ABORT;if(FAILED(hr))return hr;
        hr=dialog->selection(result.paths);if(!state->current(token))return E_ABORT;if(FAILED(hr))return hr;need(validShelfPickerSelection(result.paths),"Native Shelf picker returned an invalid complete path selection");return S_OK;});
    if(!state->current(token))return true;
    if(FAILED(result.result))result.paths.clear();state->presenting=false;auto dialog=std::exchange(state->dialog,{});dialog.reset();
    if(!state->alive||state->token!=token)return true;state->completion=std::move(result);state->post(ShelfPickerNotice::completed);return true;
}
std::optional<ShelfPickerCompletion>NativeShelfFilePicker::drain(UINT_PTR generation){auto state=state_;state->onThread();if(!state->alive||generation!=state->route.generation)return {};return std::exchange(state->completion,{});}
void NativeShelfFilePicker::cancel(){auto state=state_;state->onThread();state->cancel();}
void NativeShelfFilePicker::setRoute(ShelfPickerRoute value){route(value);auto state=state_;state->onThread();state->cancel();if(state->alive)state->route=value;}
ShelfPickerStats NativeShelfFilePicker::stats()const{auto state=state_;state->onThread();auto stats=state->counts;stats.queued=state->queued;stats.presenting=state->presenting;stats.hasCompletion=bool(state->completion);return stats;}
HRESULT revealShelfReference(ehud::data::ShelfFileAccess lease,ShelfRevealResolver resolver,ShelfRevealOperation operation)noexcept{return protect([&]{
    need(lease.open()&&ehud::data::validWindowsFilePath(lease.metadata().windowsPath),"Explorer handoff needs a validated Shelf reference lease");
    ehud::data::ShelfRecord record;record.windowsPath=lease.metadata().windowsPath;record.identity=lease.metadata().identity;
    if(!resolver)resolver=NativeFileShelfFiles{}.platform().resolve;auto current=resolver(record);need(current.open()&&record.identity.matches(current.metadata().identity)&&ehud::data::validWindowsFilePath(current.metadata().windowsPath),"Shelf Explorer reference identity changed");
    if(!operation)operation=shellReveal;return operation(current);
});}
} // namespace endfield::native
#endif
