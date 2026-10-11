#include "tools/shelf_preview.hpp"
#include "modules/module_strings.hpp"
#include "core/source_color.hpp"
#include "native/shelf_icon_provider.hpp"
#include "native/shelf_file_picker.hpp"
#include "native/shelf_properties.hpp"
#include "native/shelf_automation.hpp"
#include "app/utility_executor.hpp"
#include "core/migration/mac_import_relink.hpp"
#include "native/file_shelf_files.hpp"
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <iomanip>
#include <utility>
#include <ole2.h>
#include <shlobj.h>
#include <wrl/client.h>

namespace endfield::tools {
namespace {
namespace gpu=native;namespace mod=modules;namespace data=ehud::data;
using Matrix=core::Matrix4;using Json=data::Json;
void need(bool value,const char*why){if(!value)throw std::runtime_error(why);}
bool contains(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
Json emptyPlane(){return Json::Object{{"bounds",Json::Array{0,0,400,334}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
std::string byteLabel(std::int64_t bytes){
    // Decimal file sizes, matching the source formatter's file-count style.
    constexpr std::array units{"bytes","KB","MB","GB","TB","PB","EB"};
    double count=double(bytes);std::size_t unit{};while(count>=1000&&unit+1<units.size()){count/=1000;++unit;}
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(unit&&count<10?1:0)<<count<<' '<<units[unit];return out.str();
}
std::vector<mod::FileShelfItem> snapshot(const data::FileShelfStore&store){
    std::vector<mod::FileShelfItem> result;result.reserve(store.items().size());
    for(const auto&i:store.items())result.push_back({i.id,i.name,i.windowsPath,i.typeDescription,i.byteCount,i.isDirectory,i.availabilityError});return result;
}
mod::ShelfPresentationStyle style(){auto s=mod::shelfPresentationStyle(core::Language::simplifiedChinese);s.accent={250./255,212./255,31./255,1};s.errorColor=mod::ShelfColor{1,.27,.23,1};return s;}
constexpr UINT iconMessage=WM_APP+187;
constexpr UINT pickerMessage=WM_APP+188;
// noticeMessage lParam of a UI Automation invocation (drop notices carry 0):
// the host refreshes artwork and the automation tree after it.
constexpr LPARAM contentNotice=1;
namespace mig=ehud::migration;
bool sameID(std::string_view a,std::string_view b){return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](char x,char y){return (x>='a'&&x<='z'?char(x-32):x)==(y>='a'&&y<='z'?char(y-32):y);});}
std::atomic<UINT_PTR> nextGeneration{1};
// Windows-only text (no Mac source pair exists): English, Simplified Chinese,
// Traditional Chinese, Japanese, Korean; grammar mirrors the source strings.
struct TextRow {std::string_view en,zhHans,zhHant,ja,ko;};
constexpr std::array<TextRow,37> windowsText{{
    {"Reveal in File Explorer: ","在文件资源管理器中显示：","在檔案總管中顯示：","エクスプローラーで表示：","파일 탐색기에서 보기: "},
    {"Properties: ","属性：","內容：","プロパティ：","속성: "},
    {"The file chooser could not open.","无法打开文件选择窗口。","無法開啟檔案選擇視窗。","ファイル選択ウインドウを開けませんでした。","파일 선택 창을 열 수 없습니다."},
    {"This item isn't a file on disk. Save it to a folder first, then add it.","此项目不是磁盘上的文件。请先将其保存到文件夹，再添加。","此項目不是磁碟上的檔案。請先將其儲存到資料夾，再新增。","この項目はディスク上のファイルではありません。フォルダに保存してから追加してください。","이 항목은 디스크에 있는 파일이 아닙니다. 먼저 폴더에 저장한 다음 추가하세요."},
    {"Choose a file or folder that is available on this PC.","请选择此电脑上可访问的文件或文件夹。","請選擇此電腦上可存取的檔案或資料夾。","このPCでアクセスできるファイルまたはフォルダを選択してください。","이 PC에서 사용할 수 있는 파일이나 폴더를 선택하세요."},
    {"the original may have been deleted, or it is not accessible.","原始项目可能已被删除或无法访问。","原始項目可能已被刪除或無法存取。","元の項目が削除されたか、アクセスできない可能性があります。","원본 항목이 삭제되었거나 액세스할 수 없습니다."},
    {"The shelf has too many references to save.","暂存架引用过多，无法保存。","暫存架引用過多，無法儲存。","シェルフの参照が多すぎて保存できません。","보관함 참조가 너무 많아 저장할 수 없습니다."},
    {"The saved shelf changed outside this window. Restart EndfieldHUD before making changes.","暂存架数据已在外部更改，请重新启动 EndfieldHUD 后再编辑。","暫存架資料已在外部更改，請重新啟動 EndfieldHUD 後再編輯。","保存済みのシェルフが外部で変更されました。EndfieldHUDを再起動してから編集してください。","다른 창이나 앱에서 보관함이 변경되었습니다. EndfieldHUD를 재시작한 후 변경하세요."},
    {"This file shelf was saved by a newer version of EndfieldHUD.","此暂存架由较新版本的 EndfieldHUD 保存。","此暫存架由較新版本的 EndfieldHUD 儲存。","このファイルシェルフは新しいバージョンのEndfieldHUDで保存されています。","이 파일 보관함은 더 최신 버전의 EndfieldHUD에서 저장되었습니다."},
    {"Unable to show the file in File Explorer","无法在文件资源管理器中显示文件","無法在檔案總管中顯示檔案","エクスプローラーでファイルを表示できません","파일 탐색기에서 파일을 표시할 수 없습니다"},
    // HUDFileShelfInteraction.layoutAccessibility help with the Windows keys and shell.
    {"Scroll to browse. Arrow keys select; Shift-click or Shift-arrow extends the selection. Space shows properties. Ctrl+R reveals in File Explorer. Delete removes only shelf references.",
     "滚动浏览，方向键选择，Shift 点击或 Shift 加方向键多选。空格键显示属性，Ctrl+R 在文件资源管理器中显示，Delete 仅移除暂存架引用。",
     "捲動瀏覽，方向鍵選擇，Shift 點擊或 Shift 加方向鍵擴展選取範圍。空白鍵顯示內容，Ctrl+R 在檔案總管中顯示，Delete 僅移除暫存架引用。",
     "スクロールで閲覧できます。矢印キーで選択し、ShiftクリックまたはShift＋矢印キーで選択範囲を広げます。Spaceでプロパティを表示、Ctrl+Rでエクスプローラーに表示、Deleteでシェルフの参照だけを削除します。",
     "스크롤하여 둘러보세요. 방향키로 선택하고 Shift-클릭 또는 Shift-방향키로 선택 범위를 확장합니다. Space로 속성을 보고, Ctrl+R로 파일 탐색기에서 확인하세요. Delete는 보관함의 파일 참조만 제거합니다."},
    // Mac-imported shelf items (Migration/relink.json), same wording as the import card.
    {"Needs relinking","需要重新关联","需要重新連結","再リンクが必要","다시 연결 필요"},
    {"Relink “{0}”","重新关联“{0}”","重新連結「{0}」","「{0}」を再リンク","‘{0}’ 다시 연결"},
    {"Relink","重新关联","重新連結","再リンク","다시 연결"},
    {"Relink: ","重新关联：","重新連結：","再リンク：","다시 연결: "},
    {"Imported from macOS. Choose this item on this PC to relink it; nothing is matched automatically.","从 macOS 导入。请在此电脑上选择该项目以重新关联；不会自动匹配。",
     "從 macOS 匯入。請在此電腦上選擇該項目以重新連結；不會自動比對。","macOS から読み込まれました。このPCで項目を選択して再リンクしてください。自動では照合されません。",
     "macOS에서 가져왔습니다. 이 PC에서 항목을 선택해 다시 연결하세요. 자동으로 일치시키지 않습니다."},
    {"Choose exactly one item to relink.","请只选择一个项目进行重新关联。","請只選擇一個項目進行重新連結。","再リンクする項目を1つだけ選択してください。","다시 연결할 항목을 하나만 선택하세요."},
    // The properties card (Windows Explorer's property names in each language).
    {"Properties","属性","內容","プロパティ","속성"},
    {"Name","名称","名稱","名前","이름"},
    {"Type","类型","類型","種類","유형"},
    {"Size","大小","大小","サイズ","크기"},
    {"Contains","包含","包含","内容","포함"},
    {"Location","位置","位置","場所","위치"},
    {"Created","创建时间","建立時間","作成日時","만든 날짜"},
    {"Modified","修改时间","修改時間","更新日時","수정한 날짜"},
    {"Dimensions","尺寸","尺寸","大きさ","해상도"},
    {"Duration","时长","長度","長さ","길이"},
    {"{0} bytes","{0} 字节","{0} 位元組","{0} バイト","{0}바이트"},
    {"{0} items","{0} 项","{0} 個項目","{0} 項目","{0}개 항목"},
    {"1 item","1 项","1 個項目","1 項目","1개 항목"},
    {"Symbolic link","符号链接","符號連結","シンボリック リンク","기호화된 링크"},
    {"Open","打开","開啟","開く","열기"},
    {"Reveal in File Explorer","在文件资源管理器中显示","在檔案總管中顯示","エクスプローラーで表示","파일 탐색기에서 보기"},
    {"Close","关闭","關閉","閉じる","닫기"},
    {"Esc or Space closes. Enter opens the item. Ctrl+R reveals it in File Explorer. Arrow keys show the next item.",
     "Esc 或空格键关闭，Enter 打开项目，Ctrl+R 在文件资源管理器中显示，方向键显示其他项目。",
     "Esc 或空白鍵關閉，Enter 開啟項目，Ctrl+R 在檔案總管中顯示，方向鍵顯示其他項目。",
     "EscまたはSpaceで閉じ、Enterで項目を開き、Ctrl+Rでエクスプローラーに表示します。矢印キーで他の項目を表示します。",
     "Esc 또는 Space로 닫고, Enter로 항목을 열고, Ctrl+R로 파일 탐색기에서 봅니다. 방향키로 다른 항목을 표시합니다."},
    {"Some properties of this item could not be read.","无法读取此项目的部分属性。","無法讀取此項目的部分內容。","この項目の一部のプロパティを読み取れませんでした。","이 항목의 일부 속성을 읽을 수 없습니다."},
    {"Unable to open the file","无法打开文件","無法開啟檔案","ファイルを開けません","파일을 열 수 없습니다"},
}};
static_assert(windowsText.size()==std::size_t(ShelfText::openFailed)+1);
std::string_view pick(const TextRow&row,core::Language language){
    switch(language){case core::Language::simplifiedChinese:return row.zhHans;case core::Language::traditionalChinese:return row.zhHant;
    case core::Language::japanese:return row.ja;case core::Language::korean:return row.ko;default:return row.en;}
}
const wchar_t* localeName(core::Language language){
    switch(language){case core::Language::english:return L"en-US";case core::Language::simplifiedChinese:return L"zh-CN";case core::Language::traditionalChinese:return L"zh-TW";
    case core::Language::japanese:return L"ja-JP";case core::Language::korean:return L"ko-KR";default:return LOCALE_NAME_USER_DEFAULT;}
}
std::string narrow(std::wstring_view value){
    if(value.empty())return {};const auto count=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);if(count<=0)return {};
    std::string out(std::size_t(count),'\0');if(WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),out.data(),count,nullptr,nullptr)!=count)return {};return out;
}
std::wstring widen(std::string_view value){
    if(value.empty())return {};const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);if(count<=0)return {};
    std::wstring out(std::size_t(count),L'\0');if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),count)!=count)return {};return out;
}
// A FILETIME (UTC ticks) as the HUD language's long date and time, in the
// time zone rules of that date (SystemTimeToTzSpecificLocalTime).
std::string dateText(std::int64_t ticks,core::Language language){
    const auto value=static_cast<std::uint64_t>(ticks);const FILETIME utc{DWORD(value&0xFFFFFFFFu),DWORD(value>>32)};SYSTEMTIME universal{},time{};
    if(ticks<=0||!FileTimeToSystemTime(&utc,&universal)||!SystemTimeToTzSpecificLocalTime(nullptr,&universal,&time))return {};
    std::array<wchar_t,128>date{},clock{};const auto*name=localeName(language);
    if(!GetDateFormatEx(name,DATE_LONGDATE,&time,nullptr,date.data(),int(date.size()),nullptr)||!GetTimeFormatEx(name,TIME_NOSECONDS,&time,nullptr,clock.data(),int(clock.size())))return {};
    return narrow(date.data())+" "+narrow(clock.data());
}
// FileShelfStrings for Windows: the source strings with the platform names
// replaced (File Explorer for Finder, Preview for Quick Look).
mod::FileShelfStrings windowsStrings(core::Language language){
    auto strings=mod::fileShelfStrings(language);strings.revealPrefix=shelfText(ShelfText::revealPrefix,language);strings.previewPrefix=shelfText(ShelfText::previewPrefix,language);return strings;
}
std::string lowered(std::string_view value){std::string out(value);for(auto&c:out)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return out;}
bool contains(std::string_view text,std::string_view part){return lowered(text).find(lowered(part))!=std::string::npos;}
}
std::string shelfText(ShelfText value,core::Language language){return std::string(pick(windowsText[std::size_t(value)],language));}
std::string shelfErrorText(const std::exception&error,mod::FileShelfState::Failure failure,core::Language language){
    using F=mod::FileShelfState::Failure;using C=data::StoreErrorCode;const std::string_view message=error.what();
    const auto persistence=[&]{return core::localized("The file shelf could not be saved or opened: ","无法保存或打开暂存架：",language)+std::string(message);};
    const auto unavailable=[&]{return core::localized("This item is unavailable: ","此项目不可用：",language)+shelfText(ShelfText::unavailableReason,language);};
    if(contains(message,"no longer on the shelf"))return core::localized("This item is no longer on the shelf.","此项目已不在暂存架中。",language);
    if(contains(message,"different item occupies"))return core::localized("The original item is unavailable. A different item now occupies its old location.","原始项目不可用，其他项目已占用了原来的位置。",language);
    const auto*store=dynamic_cast<const data::StoreError*>(&error);
    if(!store)return persistence();
    switch(store->code()){
    case C::changedOnDisk:return shelfText(ShelfText::changedOnDisk,language);
    case C::newerVersion:return shelfText(ShelfText::newerVersion,language);
    case C::tooLarge:if(contains(message,"archive byte bound"))return shelfText(ShelfText::tooManyReferences,language);return failure==F::importing?shelfText(ShelfText::chooseAvailable,language):unavailable();
    case C::unavailable:case C::invalid:
        if(failure==F::importing)return shelfText(ShelfText::chooseAvailable,language);
        if(failure==F::action||failure==F::refreshing)return unavailable();
        return persistence();
    }
    return persistence();
}
std::optional<std::string> shelfKnownMessage(std::string_view message,core::Language language){
    // Mac importPasteboard ignores a clipboard without file URLs silently.
    if(message=="Drop source did not provide filesystem paths")return std::string{};
    if(message=="Shelf references could not be committed"||message=="Shelf drop failed")return shelfErrorText(std::runtime_error(std::string(message)),mod::FileShelfState::Failure::changing,language);
    if(contains(message,"no longer on the shelf")||contains(message,"different item occupies"))return shelfErrorText(std::runtime_error(std::string(message)),mod::FileShelfState::Failure::action,language);
    for(const auto prefix:{"Cannot acquire shelf file metadata","Shelf reference ","This shelf provider does not support","Filesystem does not provide","Original shelf item","Outgoing shelf reference changed","Preview requires","Original preview reference"})
        if(message.starts_with(prefix))return shelfErrorText(data::StoreError(data::StoreErrorCode::unavailable,std::string(message)),mod::FileShelfState::Failure::action,language);
    return std::nullopt;
}
struct ShelfPreview::Impl {
    struct DropState {
        HWND hwnd{};core::Projection projection;double scale{1};bool enabled{},noticeExternal{};
        std::shared_ptr<const gpu::ShelfDropTarget::Callbacks> external;
        // Commit can reenter a route transition. Its deferred notice still
        // belongs to the callback snapshot that completed the transaction.
        std::optional<bool> committedExternal;
        // Names of references a Shelf-route drop added (shelfAdded records).
        std::vector<std::string> addedNames;
    };
    struct Selection {std::string id;double fromY{},fromZ{},toY{},toZ{},start{};};
    HWND hwnd;gpu::LayerRasterizer&raster;const gpu::NativeShelfAssets&assets;ShelfPreviewOptions options;
    // Bookmark-equivalent resolution and Shell type names (FileShelfStore.swift).
    gpu::NativeFileShelfFiles files{{"文件","文件夹"},{true,true}};
    std::shared_ptr<data::FileShelfStore>store;std::unique_ptr<mod::FileShelfState>state;
    mod::ShelfPresentation presentation;mod::ShelfPresentationStyle appearance=style();
    gpu::LayerImageSource memoryImages;std::shared_ptr<const gpu::LayerMemoryImage>emptyIcon;
    std::unique_ptr<gpu::NativeShelfIconProvider>icons;std::unique_ptr<gpu::NativeShelfScene>scene;
    gpu::LayerScene geometry;std::unique_ptr<gpu::NativeModuleSurface>surface;
    gpu::NativeModuleRegistration registration{"shelf.registration"};
    std::shared_ptr<DropState>dropState=std::make_shared<DropState>();std::unique_ptr<gpu::ShelfDropTarget>drop;
    std::unique_ptr<gpu::NativeShelfFilePicker>picker;
    std::function<void(mod::EventKind,mod::EventMetadata)>recordEvent;
    // Mac-imported items awaiting relink (Migration/relink.json, unresolved
    // fileShelf.item rows without a Windows record) and the one whose picker
    // is open. The slot gives the relinked record the imported identifier and
    // date (FileShelfStore::Creation). The ledger is read and written on the
    // utility worker; rows whose dismissal is still queued stay hidden, and
    // only the latest read is applied.
    mig::ShelfRelinkSlot relinkSlot;std::vector<mig::MacRelinkItem>pendingRelinks;std::optional<mig::MacRelinkItem>relinkTarget;
    std::set<std::string,std::less<>>dismissing;std::uint64_t relinkReads{};
    // The properties card (replaces the Quick Look preview): the open card's
    // item, its utility read and the values that arrived; the content shown
    // (kept while a closed card fades out) and whether it takes input.
    struct Card {std::string itemID;std::uint64_t request{};std::optional<gpu::ShelfProperties>values;bool failed{};double start{};};
    std::optional<Card>card;std::optional<mod::ShelfPropertiesContent>cardContent;std::optional<double>cardClosing;bool cardInteractive{};
    std::optional<std::string>cardRequested;std::uint64_t cardRequests{},propertyReads{};float cardOpacity{1};
    // The shared utility executor (setUtility), else a private one-worker
    // executor notified through workMessage. Completions check `alive`.
    app::UtilityExecutor*utility{};std::unique_ptr<app::UtilityExecutor>ownUtility;app::UtilityExecutor::Route sharedRoute{},ownRoute{};
    std::shared_ptr<bool>alive=std::make_shared<bool>(true);
    // UI Automation peers (Mac HUDShelfActionButton), created on demand.
    std::unique_ptr<gpu::NativeShelfAutomation>automation;IRawElementProviderFragmentRoot*automationRoot{};bool contentPosted{};
    std::vector<gpu::NativeShelfImage>images;std::vector<gpu::ShelfIconRequest>requests;
    std::vector<Selection>selectionTracks;std::vector<gpu::ShelfSelectionPose>selectionPoses;
    std::map<std::string,gpu::ShelfIconRequest,std::less<>>requestVersions;
    std::vector<gpu::LayerCompositionEntry>composed;std::optional<ShelfPreviewAction>action;
    std::optional<std::pair<std::string,core::Point>>dragCandidate;
    app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;
    core::Language language{core::Language::simplifiedChinese};
    bool active{},acceptsInput{},pressed{},nativeDrag{},dirty{true},released{};
    std::uint64_t imageRevision{},iconRevision{};UINT_PTR generation{nextGeneration++};
    double time{},toolbarStart{-1},dropStart{-1},revealStart{-1},revealDirection{1};
    unsigned eventDepth{};
    struct Event {
        Impl&i;Event(Impl&v,double requested):i(v){need(std::isfinite(requested),"Shelf owner requires finite time");if(!i.eventDepth)i.time=std::max(i.time,requested);++i.eventDepth;}~Event(){--i.eventDepth;}
    };
    Impl(HWND h,gpu::LayerRasterizer&r,const gpu::NativeShelfAssets&a,ShelfPreviewOptions o):hwnd(h),raster(r),assets(a),options(std::move(o)),geometry(r){
        if(options.persistentDataRoot){
            // Production: the existing versioned root, opened as it is.
            need(options.newDataRoot.is_absolute(),"Shelf requires an absolute data root");
            data::detail::validateRoot(options.newDataRoot);std::error_code e;std::filesystem::create_directories(options.newDataRoot,e);need(!e,"Create Shelf data root");
        }else{
            need(options.newDataRoot.is_absolute()&&!std::filesystem::exists(options.newDataRoot),"Shelf preview requires a NEW absolute data root");
            data::detail::validateRoot(options.newDataRoot);need(std::filesystem::create_directory(options.newDataRoot),"Create isolated Shelf fixture data");
        }
        // The import ledger is read on the utility worker at the first
        // activation or refresh, never here on the HUD thread.
        store=std::make_shared<data::FileShelfStore>(options.newDataRoot,files.platform(),relinkSlot.creation());
        // The Windows references followed by the Mac-imported items awaiting
        // relink; removing or clearing those dismisses their ledger rows.
        mod::FileShelfState::Store persistence{[this]{return items();},[this]{store->refresh();readRelinks();},[s=store](auto paths){return s->add(paths);},
            [this](auto id){removeReference(id);},[this]{clearReferences();}};
        mod::FileShelfState::PlatformActions actions;
        // Add files is never a relink.
        actions.chooseFiles=[this]{relinkTarget.reset();request({ShelfPreviewAction::Kind::choose,{}});};
        // The properties card opens after the state's callback returned.
        actions.preview=[this](auto id){cardRequested=std::string(id);};
        actions.reveal=[this](auto id){request({ShelfPreviewAction::Kind::reveal,std::string(id)});};actions.formatFileSize=byteLabel;
        actions.describeError=[this](const std::exception&error,mod::FileShelfState::Failure failure){return shelfErrorText(error,failure,language);};
        recordEvent=std::move(options.recordEvent);auto strings=windowsStrings(language);
        state=std::make_unique<mod::FileShelfState>(items(),std::move(persistence),std::move(actions),std::move(strings));
        const std::array<std::uint8_t,4> transparent{};emptyIcon=memoryImages.publish("shelf.pending-icon",1,1,1,transparent);
        gpu::LayerRasterOptions ro;ro.pixelsPerPoint=2;ro.assetRoot=assets.root();ro.memoryImages=&memoryImages;
        scene=std::make_unique<gpu::NativeShelfScene>(presentation,raster,ro);
        geometry.load(emptyPlane(),ro);surface=std::make_unique<gpu::NativeModuleSurface>(geometry,core::Module::fileShelf);
        if(options.nativeIcons)icons=std::make_unique<gpu::NativeShelfIconProvider>(memoryImages,files.platform().resolve,gpu::ShelfIconRoute{hwnd,iconMessage,generation});
        picker=std::make_unique<gpu::NativeShelfFilePicker>(gpu::ShelfPickerRoute{hwnd,pickerMessage,generation},gpu::ShelfPickerLabels{"添加到文件暂存架","添加引用","添加选中的文件与文件夹"});
        dropState->hwnd=hwnd;
        if(options.registerDropTarget){
            auto shared=dropState;auto retainedStore=store;
            drop=std::make_unique<gpu::ShelfDropTarget>(gpu::ShelfTransferRoute{hwnd,noticeMessage,generation},gpu::ShelfDropTarget::Callbacks{
                [shared](POINTL p){const auto route=shared->external;shared->noticeExternal=bool(route);if(route)return route->acceptsPoint(p);if(!shared->enabled)return false;POINT point{p.x,p.y};if(!ScreenToClient(shared->hwnd,&point))return false;const auto local=shared->projection.unproject({double(point.x),double(point.y)});return local&&contains(mod::FileShelfState::bounds(),*local);},
                [shared,retainedStore](auto paths){const auto route=shared->external;shared->noticeExternal=bool(route);
                    if(route){const bool committed=route->commit(paths);if(committed)shared->committedExternal=true;return committed;}
                    std::set<std::string,std::less<>>before;for(const auto&item:retainedStore->items())before.insert(item.id);
                    retainedStore->add(paths);shared->committedExternal=false;
                    for(const auto&item:retainedStore->items())if(!before.contains(item.id))shared->addedNames.push_back(item.name);return true;},
                // Drag feedback belongs to an external route (the HUD's area owners); the Shelf's own drops read no payload while hovering.
                [shared](std::span<const std::string> paths,std::optional<POINTL> p){const auto route=shared->external;return !route||!route->hover||route->hover(paths,p);},
                [shared]{const auto route=shared->external;return route&&route->hover;}});
            need(SUCCEEDED(drop->registerTarget()),"Register Windows Shelf file drop target");drop->setEnabled(false);
            // Incoming drags keep their Shell drag image across the HUD.
            drop->setDragImages(options.nativeIcons);
        }
        images.reserve(10);requests.reserve(8);selectionTracks.reserve(16);selectionPoses.reserve(8);composed.reserve(12);
    }
    ~Impl(){*alive=false;if(utility&&sharedRoute)utility->invalidate(sharedRoute);ownUtility.reset();automation.reset();dropState->enabled=false;dropState->external.reset();if(drop)drop->stop();if(icons)icons->stop();}
    // ---- Utility work (properties reads, import-ledger writes) ----
    // Work receives value-owned inputs only. Each completion runs on the HUD
    // thread (the shared executor's drain, attributed to the submitting
    // owner, or this owner's workMessage) and then asks the host for a frame.
    bool submit(app::UtilityExecutor::Work work,std::function<void(std::exception_ptr)>done){
        if(released)return false;
        app::UtilityExecutor*executor=utility;app::UtilityExecutor::Route*route=&sharedRoute;
        if(!executor){if(!ownUtility){const auto window=hwnd;const auto token=generation;ownUtility=std::make_unique<app::UtilityExecutor>([window,token]{PostMessageW(window,workMessage,token,0);},8);ownRoute=ownUtility->makeRoute();}executor=ownUtility.get();route=&ownRoute;}
        if(!*route)*route=executor->makeRoute();
        const bool shared=executor==utility;
        return executor->submit(*route,std::move(work),[this,shared,alive=alive,done=std::move(done)](std::exception_ptr error){
            if(!*alive)return;try{done(error);}catch(const std::exception&failure){try{state->showError(shelfErrorText(failure,mod::FileShelfState::Failure::changing,language));changed();}catch(...){}}catch(...){}
            if(shared&&!contentPosted)contentPosted=PostMessageW(hwnd,noticeMessage,generation,contentNotice)!=FALSE;});
    }
    bool request(ShelfPreviewAction value){if(action||nativeDrag)return false;action=std::move(value);need(PostMessageW(hwnd,actionMessage,generation,0)!=FALSE,"Queue Shelf user action");return true;}
    // ---- Mac-imported references (import ledger) ----
    const mig::MacRelinkItem*pending(std::string_view id)const{for(const auto&item:pendingRelinks)if(sameID(item.recordID,id))return &item;return nullptr;}
    bool onShelf(std::string_view id)const{const auto&rows=store->items();return std::any_of(rows.begin(),rows.end(),[&](const auto&row){return sameID(row.id,id);});}
    // Reads the ledger (activation and refresh) through the import ledger API
    // on the utility worker: a row whose record already exists here (the app
    // stopped between the record and the ledger write) is resolved with that
    // record's path first (reconcileMacRelinks); no file is ever looked up by
    // path or name. Only the latest read is applied.
    void readRelinks(){
        std::vector<std::pair<std::string,std::string>>rows;rows.reserve(store->items().size());for(const auto&row:store->items())rows.emplace_back(row.id,row.windowsPath);
        auto result=std::make_shared<std::vector<mig::MacRelinkItem>>();const auto read=++relinkReads;
        const bool queued=submit([root=options.newDataRoot,rows=std::move(rows),result]{
                (void)mig::reconcileMacRelinks(root,mig::relinkShelfItem,[&](const mig::MacRelinkItem&item){
                    const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto&value){return sameID(value.first,item.recordID);});
                    return row==rows.end()?mig::RelinkRecordCheck{}:mig::RelinkRecordCheck{mig::RelinkRecordState::relinked,row->second};});
                *result=mig::MacRelinkLedger(root).unresolved(mig::relinkShelfItem);},
            [this,read,result](std::exception_ptr error){
                if(read!=relinkReads)return; // a newer read follows
                std::vector<mig::MacRelinkItem>open;if(!error)open=std::move(*result); // an unreadable ledger lists nothing; the import card keeps it
                std::erase_if(open,[&](const auto&item){return onShelf(item.recordID)||dismissing.contains(item.recordID)||!item.macReference["createdAt"].isNumber();});
                if(open==pendingRelinks)return;
                pendingRelinks=std::move(open);if(relinkTarget&&!pending(relinkTarget->recordID))relinkTarget.reset();
                state->refreshFromStore();changed();});
        (void)queued; // a full queue keeps the current list; the next activation reads again
    }
    // Dismisses the rows of records the user removed (dismissMacRelink by
    // store and record id); they stay hidden until the write finished.
    void dismissRelinks(std::vector<std::string>recordIDs){
        if(recordIDs.empty())return;for(const auto&id:recordIDs)dismissing.insert(id);
        const auto clear=[this,recordIDs]{for(const auto&id:recordIDs)dismissing.erase(id);};
        if(!submit([root=options.newDataRoot,recordIDs]{for(const auto&id:recordIDs)(void)mig::dismissMacRelink(root,{mig::relinkShelfItem,id});},
            [this,clear](std::exception_ptr error){clear();if(error)std::rethrow_exception(error);})){clear();throw std::runtime_error("Shelf references could not be committed");}
    }
    // The card of an imported item: the source record's metadata, unavailable.
    mod::FileShelfItem relinkItem(const mig::MacRelinkItem&item)const{
        const auto&reference=item.macReference;mod::FileShelfItem value;value.id=item.recordID;
        value.name=reference["name"].isString()&&!reference["name"].string().empty()?reference["name"].string():!item.displayName.empty()?item.displayName:std::string("Item");
        value.lastKnownPath=item.lastKnownPath;value.typeDescription=reference["typeDescription"].isString()?reference["typeDescription"].string():std::string{};
        if(reference["byteCount"].isNumber()&&reference["byteCount"].number()>=0)value.byteCount=reference["byteCount"].integer();
        value.isDirectory=reference["isDirectory"].isBool()?reference["isDirectory"].boolean():item.kind=="folder";
        value.availabilityError=shelfText(ShelfText::relinkNeeded,language);return value;
    }
    std::vector<mod::FileShelfItem>items()const{
        auto out=snapshot(*store);out.reserve(out.size()+pendingRelinks.size());
        for(const auto&item:pendingRelinks)if(!onShelf(item.recordID))out.push_back(relinkItem(item));return out;
    }
    // FileShelfState store callbacks (references only, never files).
    void removeReference(std::string_view id){
        if(const auto*item=pending(id)){std::vector<std::string>ids{item->recordID};std::erase_if(pendingRelinks,[&](const auto&row){return sameID(row.recordID,ids.front());});dismissRelinks(std::move(ids));return;}
        store->remove(id);
    }
    void clearReferences(){
        store->clear();std::vector<std::string>ids;for(const auto&item:pendingRelinks)ids.push_back(item.recordID);
        pendingRelinks.clear();relinkTarget.reset();dismissRelinks(std::move(ids));
    }
    // Opens the picker for exactly this imported item (through the host's
    // choose action, which releases pointer capture first).
    void requestRelink(std::string_view id){
        const auto*item=pending(id);if(!item)return;state->selectItem(item->recordID,{});
        auto target=*item;relinkTarget=std::move(target);if(!request({ShelfPreviewAction::Kind::choose,{}}))relinkTarget.reset();
    }
    // The imported card under a canvas point, outside its card buttons.
    std::optional<std::string>relinkAt(core::Point point)const{
        const auto range=state->visibleRange();
        for(auto n=range.begin;n<range.end;++n){const auto&item=state->items()[n];if(!pending(item.id))continue;const auto placed=state->card(item.id);if(!placed||!contains(placed->clipped,point))continue;
            for(const auto&button:state->cardActions(item.id))if(contains(button.rect,point))return std::nullopt;return item.id;}
        return std::nullopt;
    }
    // ---- UI Automation ----
    std::optional<core::Rect>screenRect(core::Rect rect)const{
        if(!pose||!acceptsInput)return std::nullopt;double left{},top{},right{},bottom{};bool first=true;
        for(const auto corner:{core::Point{rect.x,rect.y},core::Point{rect.x+rect.width,rect.y},core::Point{rect.x,rect.y+rect.height},core::Point{rect.x+rect.width,rect.y+rect.height}}){
            const auto p=projection.project(corner);if(!p||!std::isfinite(p->x)||!std::isfinite(p->y))return std::nullopt;
            if(first){left=right=p->x;top=bottom=p->y;first=false;}else{left=std::min(left,p->x);right=std::max(right,p->x);top=std::min(top,p->y);bottom=std::max(bottom,p->y);}
        }
        POINT origin{};if(!ClientToScreen(hwnd,&origin))return std::nullopt;
        return core::Rect{left+origin.x,top+origin.y,right-left,bottom-top};
    }
    std::vector<gpu::ShelfAutomationControl>automationControls()const{
        std::vector<gpu::ShelfAutomationControl>out;if(!pose||!acceptsInput)return out;
        // The open properties card is modal: the card itself (its rows as the
        // read-only value) and its close/open/reveal plates, nothing beneath.
        if(card&&cardInteractive&&cardContent){
            const auto layout=mod::shelfPropertiesLayout(cardContent->rows.size());std::string summary;
            for(const auto&row:cardContent->rows){if(!summary.empty())summary+="; ";summary+=row.label+": "+row.value;}
            const auto help=shelfText(ShelfText::propertiesHelp,language);
            out.push_back({"shelf.properties:card",cardContent->heading+": "+(cardContent->rows.empty()?std::string{}:cardContent->rows.front().value),help,summary,layout.frame,true});
            const std::array<std::string,3>labels{cardContent->close,cardContent->open,cardContent->reveal};
            for(std::size_t n=0;n<layout.plates.size();++n)out.push_back({std::string(mod::shelfPropertiesActions[n]),labels[n],help,{},layout.plates[n],false});
            return out;
        }
        const auto help=state->error()?*state->error():shelfText(ShelfText::accessibilityHelp,language);const auto selected=core::localized("Selected","已选择",language);
        const auto actions=state->accessibleActions();out.reserve(actions.size()+pendingRelinks.size());
        for(const auto&action:actions){
            const bool plate=action.id.starts_with("shelf:")&&action.id.ends_with(":select");
            const auto id=plate?std::string_view(action.id).substr(6,action.id.size()-6-7):std::string_view{};
            out.push_back({action.id,action.label,help,plate&&state->selectedIDs().contains(id)?selected:std::string{},action.rect,plate});
            // The Windows-only relink action of an imported card (no artwork).
            if(plate)if(const auto*item=pending(id))out.push_back({"shelf:"+std::string(id)+":relink",shelfText(ShelfText::relinkAction,language)+relinkItem(*item).name,shelfText(ShelfText::relinkHelp,language),{},action.rect,false});
        }
        return out;
    }
    bool performAction(std::string_view action){
        if(nativeDrag)return false;
        if(action.starts_with("shelf.properties:")){const bool done=card&&cardInteractive&&cardAction(action);changed();return done;}
        if(action.starts_with("shelf:")&&action.ends_with(":relink")&&action.size()>13){requestRelink(action.substr(6,action.size()-13));changed();return true;}
        state->perform(action);changed();return true;
    }
    const data::ShelfRecord*record(std::string_view id)const{const auto&rows=store->items();const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto&i){return sameID(i.id,id);});return found==rows.end()?nullptr:&*found;}
    std::optional<std::string_view>hitAction(core::Point point)const{
        // Nested card controls win over the containing selection plate.
        const auto actions=presentation.actions();for(auto it=actions.rbegin();it!=actions.rend();++it)if(contains(it->rect,point))return it->id;return {};
    }
    static double progress(double t,double start,double duration){return core::CubicTiming{.16,.78,.25,1}.value(std::clamp((t-start)/duration,0.,1.));}
    std::pair<double,double> selectedPose(std::string_view id,bool target)const{
        const auto found=std::find_if(selectionTracks.begin(),selectionTracks.end(),[&](const auto&s){return s.id==id;});
        if(found==selectionTracks.end())return {target?-1.5:0,target?5.:0};const double p=progress(time,found->start,.18);return {found->fromY+(found->toY-found->fromY)*p,found->fromZ+(found->toZ-found->fromZ)*p};
    }
    void changed(){
        dirty=true;
        for(const auto&e:state->takeEvents())switch(e.kind){
            case mod::FileShelfState::EventKind::settle:selectionTracks.clear();toolbarStart=dropStart=revealStart=-1;break;
            case mod::FileShelfState::EventKind::selection:
                for(const auto&id:e.values){const auto placed=state->card(id);if(!placed)continue;auto found=std::find_if(selectionTracks.begin(),selectionTracks.end(),[&](const auto&s){return s.id==id;});
                    const auto before=found==selectionTracks.end()?std::pair{placed->selected?0.:-1.5,placed->selected?0.:5.}:selectedPose(id,placed->selected);
                    Selection track{id,before.first,before.second,placed->selectionY,placed->selectionZ,e.animated?time:time-.18};if(found!=selectionTracks.end())*found=std::move(track);else selectionTracks.push_back(std::move(track));}break;
            case mod::FileShelfState::EventKind::toolbar:toolbarStart=e.animated?time:-1;break;
            case mod::FileShelfState::EventKind::dropTrace:dropStart=e.animated?time:-1;break;
            case mod::FileShelfState::EventKind::collectionReveal:revealStart=e.animated?time:-1;revealDirection=e.direction;break;
            // SystemHUDView: onItemsAdded / onItemRemoved / onShelfCleared.
            case mod::FileShelfState::EventKind::itemsAdded:for(const auto&name:e.values)emit(mod::EventKind::shelfAdded,{{"filename",name}});break;
            case mod::FileShelfState::EventKind::itemRemoved:for(const auto&name:e.values)emit(mod::EventKind::shelfRemoved,{{"filename",name}});break;
            case mod::FileShelfState::EventKind::shelfCleared:emit(mod::EventKind::shelfCleared,{{"count",std::to_string(e.count)}});break;
            default:break;
        }
        if(auto id=std::exchange(cardRequested,{}))toggleCard(*id);
        if(card&&(!record(card->itemID)||record(card->itemID)->availabilityError))closeCard(true);
    }
    void emit(mod::EventKind kind,mod::EventMetadata metadata)const{if(recordEvent)recordEvent(kind,std::move(metadata));}
    // ---- The properties card ----
    // The containing folder, shortened in the middle to the value column
    // (measured with the value's own font): "C:\Users\…\Project\Assets".
    std::string location(const std::string&path){
        const auto slash=path.find_last_of('\\');auto folder=slash==std::string::npos?path:path.substr(0,slash);if(folder.size()==2&&folder[1]==':')folder+='\\';
        const auto width=[&](const std::string&text){return raster.measureSourceText("shelf.properties.location",mod::shelfPropertiesValueText(text,appearance.contentsScale),100000).width;};
        if(folder.empty()||width(folder)<=mod::shelfPropertiesValueWidth)return folder;
        const auto root=gpu::shelfPathVolumeRoot(folder);const std::string prefix=root.empty()?std::string{}:root;
        // Code-point boundaries of the remainder; keep the longest tail that fits.
        std::vector<std::size_t>starts;for(std::size_t n=prefix.size();n<folder.size();++n)if((static_cast<unsigned char>(folder[n])&0xC0)!=0x80)starts.push_back(n);
        std::size_t low=0,high=starts.size();
        while(low<high){const auto mid=(low+high)/2;if(width(prefix+"…"+folder.substr(starts[mid]))<=mod::shelfPropertiesValueWidth)high=mid;else low=mid+1;}
        return low<starts.size()?prefix+"…"+folder.substr(starts[low]):prefix+"…";
    }
    mod::ShelfPropertiesContent cardText(const Card&value){
        const auto*row=record(value.itemID);mod::ShelfPropertiesContent out;out.itemID=value.itemID;
        out.heading=shelfText(ShelfText::properties,language);out.close=shelfText(ShelfText::close,language);out.open=shelfText(ShelfText::open,language);out.reveal=shelfText(ShelfText::reveal,language);
        if(!row)return out;const auto*values=value.values?&*value.values:nullptr;const auto strings=state->strings();
        const auto add=[&](ShelfText label,std::string text){if(!text.empty())out.rows.push_back({shelfText(label,language),std::move(text)});};
        const auto render=[&](ShelfText pattern,std::string argument){const std::array<std::string_view,1>arguments{argument};return core::renderTranslation(shelfText(pattern,language),arguments).value_or(argument);};
        const bool directory=values?values->directory:row->isDirectory,link=values&&values->symbolicLink;
        add(ShelfText::propertyName,row->name);
        auto type=directory?strings.folder:!row->typeDescription.empty()?row->typeDescription:mod::FileShelfState::typeLabel({row->id,row->name,row->windowsPath,row->typeDescription,row->byteCount,row->isDirectory,{}},strings);
        if(link)type+=" · "+shelfText(ShelfText::propertyLink,language);add(ShelfText::propertyType,std::move(type));
        if(directory){if(values&&values->folderItems)add(ShelfText::propertyContains,*values->folderItems==1?shelfText(ShelfText::propertyItem,language):render(ShelfText::propertyItems,gpu::shelfGroupedCount(*values->folderItems)));}
        else if(const auto bytes=values?values->byteCount:row->byteCount;bytes&&!link){const auto exact=render(ShelfText::propertyBytes,gpu::shelfGroupedCount(std::uint64_t(*bytes)));add(ShelfText::propertySize,*bytes<1000?exact:byteLabel(*bytes)+" ("+exact+")");}
        add(ShelfText::propertyLocation,location(values?values->windowsPath:row->windowsPath));
        // Times arrive from the worker; "…" keeps the rows (and the card's
        // height) while they load, "—" when they could not be read.
        const auto when=[&](ShelfText label,const std::optional<std::int64_t>&ticks){auto text=ticks?dateText(*ticks,language):std::string{};add(label,!text.empty()?text:std::string(values||value.failed?"—":"…"));};
        when(ShelfText::propertyCreated,values?values->created:std::nullopt);when(ShelfText::propertyModified,values?values->modified:std::nullopt);
        if(values&&values->dimensions)add(ShelfText::propertyDimensions,gpu::shelfGroupedCount((*values->dimensions)[0])+" × "+gpu::shelfGroupedCount((*values->dimensions)[1]));
        if(values&&values->duration)add(ShelfText::propertyDuration,gpu::shelfDurationText(*values->duration));
        return out;
    }
    // The card's content and whether it takes input; the presentation (and
    // the scene) take it with the next frame's content(), never in between.
    void syncCard(){
        if(card)cardContent=cardText(*card);cardInteractive=card.has_value();dirty=true;
    }
    // The open card's plate under a canvas point (its own geometry: valid
    // before the next frame publishes the card).
    std::optional<std::string_view>cardPlate(core::Point point)const{
        if(!cardContent)return std::nullopt;const auto layout=mod::shelfPropertiesLayout(cardContent->rows.size());
        for(std::size_t n=0;n<layout.plates.size();++n)if(contains(layout.plates[n],point))return mod::shelfPropertiesActions[n];return std::nullopt;
    }
    void readCard(){
        const auto*row=record(card->itemID);if(!row){card->failed=true;return;}
        auto result=std::make_shared<std::optional<gpu::ShelfProperties>>();const auto request=card->request=++cardRequests;++propertyReads;
        if(!submit([copy=*row,resolve=files.platform().resolve,result]{const auto lease=resolve(copy);*result=gpu::readShelfProperties(lease);},
            [this,request,result](std::exception_ptr error){
                if(!card||card->request!=request)return;
                // The original is gone (or no longer the referenced object):
                // the card closes with the Shelf's localized reason.
                if(error){std::string text=shelfText(ShelfText::propertiesUnavailable,language);
                    try{std::rethrow_exception(error);}catch(const data::StoreError&failure){text=shelfErrorText(failure,mod::FileShelfState::Failure::action,language);}catch(...){}
                    closeCard(true);state->showError(std::move(text));changed();return;}
                if(*result)card->values=std::move(**result);else card->failed=true;
                syncCard();}))card->failed=true;
    }
    // Opens (fading in) or switches the card to an available item; an
    // unavailable or imported card has no properties (Mac: no Quick Look).
    void showCard(std::string_view id){
        const auto*row=record(id);if(!row||row->availabilityError)return;
        const double start=card?card->start:cardClosing?time-.16*double(cardOpacity):time;cardClosing.reset();
        card=Card{std::string(id),0,{},false,start};readCard();syncCard();
    }
    void closeCard(bool animated){
        if(!card)return;card.reset();cardRequested.reset();
        if(animated&&active&&pose){cardClosing=time;syncCard();}else{cardClosing.reset();cardContent.reset();syncCard();}
    }
    void toggleCard(std::string_view id){if(card&&card->itemID==id)closeCard(true);else showCard(id);}
    // Mac NotesRetainedMenu: an item performs and the menu dismisses.
    bool cardAction(std::string_view id){
        if(!card)return false;const auto item=card->itemID;
        if(id==mod::shelfPropertiesClose){closeCard(true);return true;}
        if(id==mod::shelfPropertiesOpen||id==mod::shelfPropertiesReveal){closeCard(true);request({id==mod::shelfPropertiesOpen?ShelfPreviewAction::Kind::open:ShelfPreviewAction::Kind::reveal,item});return true;}
        return id=="shelf.properties:card";
    }
    // Quick Look follows the selection: the card shows the newly selected
    // available item, and closes when its item is gone or unavailable.
    void followCard(){
        if(!card)return;const auto*row=record(card->itemID);if(!row||row->availabilityError){closeCard(false);return;}
        if(const auto&selected=state->selectedID();selected&&!sameID(*selected,card->itemID))if(const auto*next=record(*selected);next&&!next->availabilityError)showCard(*selected);
    }
    float cardFade()const{if(card)return float(std::clamp((time-card->start)/.16,0.,1.));if(cardClosing)return float(1-std::clamp((time-*cardClosing)/.16,0.,1.));return 1.f;}
    // Mac beginOutgoingDrag frames: side = clamp(projected card height * 0.7,
    // 36, 72) points, each later item 5 points up and right, at most five.
    void dragImage(gpu::ShelfDragTransfer&transfer,std::span<const std::string>ids){
        if(!options.nativeIcons||ids.empty())return;
        std::vector<std::shared_ptr<const gpu::LayerMemoryImage>>pixels;std::vector<gpu::ShelfDragIcon>icons;
        for(const auto&id:ids){if(icons.size()==gpu::shelfDragImageMaximumFrames)break;const auto found=requestVersions.find(id);if(found==requestVersions.end())continue;
            auto image=memoryImages.acquire(found->second.imageKey,found->second.revision);if(!image||image->width()<2||image->height()<2)continue;
            icons.push_back({image->width(),image->height(),image->premultipliedBGRA()});pixels.push_back(std::move(image));}
        if(icons.empty())return;
        double cardPoints=52;
        if(const auto placed=state->card(ids.front());placed&&pose){const auto top=projection.project({placed->full.x,placed->full.y}),bottom=projection.project({placed->full.x,placed->full.y+placed->full.height});
            if(top&&bottom&&metrics.scale>0)cardPoints=std::hypot(bottom->x-top->x,bottom->y-top->y)/metrics.scale;}
        const double scale=metrics.scale>0?metrics.scale:1;
        const auto side=unsigned(std::lround(std::clamp(cardPoints*.7,36.,72.)*scale)),offset=unsigned(std::lround(5*scale));
        // Without the Shell helper the drag keeps the system cursor only.
        (void)transfer.setDragImage(gpu::composeShelfDragImage(icons,std::min(side,gpu::shelfDragImageMaximumSide),std::min(offset,side)));
    }
    void drainDrop(){
        if(!drop)return;auto changes=drop->takeChanges(generation);bool changedState{};
        if(changes.committed){const bool external=dropState->committedExternal.value_or(dropState->noticeExternal);dropState->committedExternal.reset();
            if(!external){state->refreshFromStore();std::vector<std::string>ids;for(const auto&file:store->items())if(std::find(changes.paths.begin(),changes.paths.end(),file.windowsPath)!=changes.paths.end())ids.push_back(file.id);state->revealItems(ids);revealStart=time;revealDirection=1;changedState=true;
                for(auto&name:std::exchange(dropState->addedNames,{}))emit(mod::EventKind::shelfAdded,{{"filename",std::move(name)}});}}
        if(!dropState->noticeExternal){if(changes.feedbackChanged){state->setDropTarget(changes.hovering);changedState=true;}
            if(changes.error){auto text=changes.errorCode?shelfErrorText(data::StoreError(*changes.errorCode,*changes.error),mod::FileShelfState::Failure::importing,language):shelfKnownMessage(*changes.error,language).value_or(*changes.error);
                if(!text.empty()){state->showError(std::move(text));changedState=true;}}
            if(changes.virtualFiles){state->showError(shelfText(ShelfText::virtualFiles,language));changedState=true;}}
        if(changedState)changed();
    }
    void content(){
        if(!dirty)return;presentation.update(*state,appearance);presentation.setProperties(cardContent,cardInteractive);requests.clear();images.clear();
        for(const auto&d:presentation.chromeImages())images.push_back(assets.image(d));
        for(const auto&shown:presentation.cards())for(const auto&d:shown.images){
            gpu::ShelfIconRequest req;
            if(const auto*file=record(d.itemID))req={"shelf.icon."+file->id,0,file->id,file->windowsPath,file->identity,file->isDirectory,bool(file->availabilityError)};
            else{
                // An imported item awaiting relink: the type icon for its name
                // only (no file access); a synthetic local name carries the extension.
                const auto*item=pending(d.itemID);need(item!=nullptr,"Shelf descriptor has no committed reference");const auto row=relinkItem(*item);
                std::string extension;if(const auto dot=row.name.find_last_of('.');!row.isDirectory&&dot!=std::string::npos&&dot+1<row.name.size()&&row.name.size()-dot<=16&&
                    std::all_of(row.name.begin()+std::ptrdiff_t(dot)+1,row.name.end(),[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');}))extension=row.name.substr(dot);
                req={"shelf.icon."+item->recordID,0,item->recordID,"C:\\relink"+extension,{},row.isDirectory,true};
            }
            const auto old=requestVersions.find(req.itemID);if(old!=requestVersions.end()){req.revision=old->second.revision;if(req!=old->second)req.revision=++iconRevision;}else req.revision=++iconRevision;
            requestVersions[req.itemID]=req;requests.push_back(req);auto pixels=memoryImages.acquire(req.imageKey,req.revision);if(!pixels)pixels=emptyIcon;
            images.push_back({d,Json::Object{{"memoryImage",pixels->key()},{"revision",std::int64_t(pixels->revision())}}});
        }
        std::erase_if(requestVersions,[&](const auto&item){return !record(item.first)&&!pending(item.first);});
        if(icons&&active)icons->setVisible(requests);
        scene->syncContent(images,++imageRevision);dirty=false;
    }
    std::optional<core::Point>local(core::Point logical)const{return acceptsInput?projection.unproject({logical.x*metrics.scale,logical.y*metrics.scale}):std::nullopt;}
};
ShelfPreview::ShelfPreview(HWND h,native::LayerRasterizer&r,const native::NativeShelfAssets&a,ShelfPreviewOptions o):impl_(std::make_unique<Impl>(h,r,a,std::move(o))){}
ShelfPreview::~ShelfPreview()=default;
void ShelfPreview::resize(const app::ClientMetrics&m){impl_->metrics=m;}
void ShelfPreview::setLanguage(core::Language language){auto&i=*impl_;if(language==i.language)return;
    // Platform actions only: retain source grammar but name the actual Windows
    // shell (File Explorer for Finder, Preview for Quick Look).
    auto strings=windowsStrings(language);auto style=mod::shelfPresentationStyle(language,i.appearance);
    const auto add=core::localized("Add references","添加引用",language);gpu::ShelfPickerLabels labels{core::localized("Add to Temporary File Shelf","添加到文件暂存架",language),add,add};
    i.state->setStrings(std::move(strings));i.picker->setLabels(std::move(labels));i.appearance=std::move(style);i.language=language;i.dirty=true;
    if(i.card)i.syncCard();
}
void ShelfPreview::setAppearance(bool dark,mod::ShelfColor accent){auto value=impl_->appearance;value.dark=dark;value.accent=accent;value.lightDropColor.reset();setAppearance(std::move(value));}
void ShelfPreview::setAppearance(mod::ShelfPresentationStyle value){auto&i=*impl_;
    // Depot rasters are the prepared source 2x variants. HUD scale is a numeric
    // module transform, independent of this source backing density.
    need(value.contentsScale==2,"Shelf packaged artwork requires its original 2x backing");
    for(const auto*color:{&value.accent})for(double v:*color)need(std::isfinite(v)&&v>=0&&v<=1,"Invalid Shelf accent");
    for(const auto*color:{&value.errorColor,&value.lightDropColor})if(*color)for(double v:**color)need(std::isfinite(v)&&v>=0&&v<=1,"Invalid Shelf platform color");
    if(!value.lightDropColor){const auto blend=core::source::sourceBlackBlend({value.accent[0],value.accent[1],value.accent[2]},.4);value.lightDropColor=mod::ShelfColor{blend[0],blend[1],blend[2],value.accent[3]};}
    value=mod::shelfPresentationStyle(i.language,std::move(value));if(value==i.appearance)return;i.appearance=std::move(value);i.dirty=true;
}
const mod::ShelfPresentationStyle&ShelfPreview::appearance()const{return impl_->appearance;}
void ShelfPreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){
    auto&i=*impl_;const Impl::Event event(i,t);t=i.time;
    const auto*shown=sample.current.module==core::Module::fileShelf?&sample.current:sample.incoming&&sample.incoming->module==core::Module::fileShelf?&*sample.incoming:nullptr;
    const bool active=shown&&sample.requested==core::Module::fileShelf;
    if(active!=i.active){i.active=active;if(active)i.state->activate();else{i.closeCard(false);i.state->deactivate();i.dragCandidate.reset();i.pressed=false;if(i.icons)i.icons->hide();}i.changed();}
    i.acceptsInput=active&&sample.acceptsModuleInput&&!i.nativeDrag;i.dropState->enabled=i.acceptsInput;if(i.drop)i.drop->setEnabled(bool(i.dropState->external)||i.acceptsInput);
    if(!shown){i.pose.reset();i.registration.update({});return;}
    // A closed properties card leaves once its fade-out finished.
    if(i.cardClosing&&t-*i.cardClosing>=.16){i.cardClosing.reset();i.cardContent.reset();i.syncCard();}
    i.content();i.surface->update(center,settings,*shown,opacity);i.pose=i.surface->pose();const auto&p=*i.pose;
    const auto camera=gpu::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight)*Matrix::scale(i.metrics.scale,i.metrics.scale);
    i.projection=core::Projection::viewport(camera*p.contentWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);i.dropState->projection=i.projection;
    i.selectionPoses.clear();for(const auto&card:i.presentation.cards()){const auto s=i.selectedPose(card.itemID,i.state->selectedIDs().contains(card.itemID));i.selectionPoses.push_back({card.itemID,s.first,s.second});}
    std::erase_if(i.selectionTracks,[&](const auto&s){return t-s.start>=.18||!i.state->card(s.id);});
    gpu::NativeShelfPose pose;pose.contentWorld=p.contentWorld;pose.opacity=p.opacity;pose.time=t;pose.selections=i.selectionPoses;pose.ownerMasks=std::span(&p.hostClip,1);pose.moduleShutter=p.shutter;
    if(i.toolbarStart>=0){const double v=1-Impl::progress(t,i.toolbarStart,.18);pose.toolbarY=6*v;pose.toolbarZ=-6*v;}
    if(i.dropStart>=0)pose.dropStrokeEnd=core::CubicTiming{0,0,.58,1}.value(std::clamp((t-i.dropStart)/.20,0.,1.));
    // HUDSubsectionTransition.reveal: the authored shutter (gpu::shelfRevealShutter).
    if(i.revealStart>=0&&t-i.revealStart<.26)pose.reveal=gpu::ShelfRevealSample{i.revealDirection,t-i.revealStart};
    // NotesRetainedMenu: the card fades in and out over .16 s (linear opacity).
    i.cardOpacity=i.cardFade();pose.propertiesOpacity=i.cardOpacity;
    i.scene->updatePose(pose);i.registration.update(p.registration);
}
bool ShelfPreview::requiresFrames(double t)const{const auto&i=*impl_;if(!i.pose)return false;t=std::max(t,i.time);return i.scene->requiresFrames(t)||!i.selectionTracks.empty()||(i.toolbarStart>=0&&t-i.toolbarStart<.18)||(i.dropStart>=0&&t-i.dropStart<.20)||(i.revealStart>=0&&t-i.revealStart<.26)||
    i.cardClosing.has_value()||(i.card&&(t-i.card->start<.16||i.cardOpacity<1));}
bool ShelfPreview::covers(core::Point p)const{const auto q=impl_->local(p);return q&&contains(mod::FileShelfState::bounds(),*q);}
bool ShelfPreview::pointer(const app::PointerEvent&e,double t){
    auto&i=*impl_;const Impl::Event event(i,t);const auto q=i.local({e.x,e.y});
    if(e.kind==app::PointerKind::captureLost||e.kind==app::PointerKind::leave){i.dragCandidate.reset();i.pressed=false;if(i.pose)i.scene->setFeedback({},false,false,i.time);return false;}
    if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool owned=i.pressed||i.dragCandidate.has_value();i.dragCandidate.reset();i.pressed=false;if(owned){i.state->finishPointerSelection();i.changed();}if(i.pose)i.scene->setFeedback(q?i.hitAction(*q):std::nullopt,false,false,i.time);return owned;}
    if(!i.acceptsInput)return false;
    // The properties card is modal (HUDArchiveInteraction.mouseDown with a
    // secondary menu): a press on it performs its plate, anywhere else it
    // only dismisses the card; nothing beneath scrolls, drags or selects.
    if(i.card&&i.cardInteractive){
        if(e.kind==app::PointerKind::move){if(i.pose)i.scene->setFeedback(q?i.cardPlate(*q):std::nullopt,i.pressed,false,i.time);return q&&contains(mod::FileShelfState::bounds(),*q);}
        if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left)return false;
        const auto inside=q&&i.cardContent&&contains(mod::shelfPropertiesLayout(i.cardContent->rows.size()).frame,*q);
        if(inside){if(const auto plate=i.cardPlate(*q))i.cardAction(*plate);}else i.closeCard(true);
        i.changed();return true;
    }
    // The second click of a double-click that dismissed the card stays with it.
    if(i.cardClosing&&e.kind==app::PointerKind::doubleClick&&e.button==app::PointerButton::left)return true;
    if(e.kind==app::PointerKind::move){if(i.dragCandidate&&q&&std::hypot(q->x-i.dragCandidate->second.x,q->y-i.dragCandidate->second.y)>=5){const auto id=i.dragCandidate->first;i.dragCandidate.reset();i.state->beginSelectionDrag();i.request({ShelfPreviewAction::Kind::drag,id});}
        if(i.pose)i.scene->setFeedback(q?i.hitAction(*q):std::nullopt,i.pressed,false,i.time);return q&&contains(mod::FileShelfState::bounds(),*q);}
    if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left||!q||!contains(mod::FileShelfState::bounds(),*q))return false;
    i.pressed=true;if(e.kind==app::PointerKind::down)if(const auto id=i.state->itemAt(*q))i.dragCandidate=std::pair{std::string(*id),*q};
    // A double-click on an imported card asks for its file on this PC (an
    // available card would preview; an unavailable Mac card does nothing).
    const auto relinkID=e.kind==app::PointerKind::doubleClick?i.relinkAt(*q):std::nullopt;
    i.state->mouseDown(*q,e.kind==app::PointerKind::doubleClick?2:1,{bool(e.modifiers&MK_SHIFT),bool(e.modifiers&MK_CONTROL)});
    if(relinkID)i.requestRelink(*relinkID);i.changed();return true;
}
bool ShelfPreview::wheel(const app::WheelEvent&e,double t){auto&i=*impl_;const Impl::Event event(i,t);const auto q=i.local({e.x,e.y});if(!q||!contains(mod::FileShelfState::bounds(),*q))return false;if(i.card&&i.cardInteractive)return true;if(e.horizontal||!std::isfinite(e.steps)||!e.linesPerStep)return true;const double travel=e.linesPerStep==UINT32_MAX?208:12.*e.linesPerStep;i.state->scroll(*q,-e.steps*travel);i.changed();return true;}
bool ShelfPreview::key(const app::KeyEvent&e,double t){auto&i=*impl_;const Impl::Event event(i,t);if(!i.acceptsInput||e.kind!=app::KeyKind::down)return false;const bool shift=GetKeyState(VK_SHIFT)<0,control=GetKeyState(VK_CONTROL)<0;
    // The open properties card: Esc or Space closes it (Space toggles like
    // Quick Look), Enter opens the item, Ctrl+R reveals it; arrows select as
    // usual and the card follows the selection.
    if(i.card&&i.cardInteractive)switch(e.value){
        case VK_ESCAPE:case VK_SPACE:i.closeCard(true);i.changed();return true;
        case VK_RETURN:i.cardAction(mod::shelfPropertiesOpen);i.changed();return true;
        case 'R':if(control){i.cardAction(mod::shelfPropertiesReveal);i.changed();return true;}break;
        case VK_LEFT:case VK_RIGHT:case VK_UP:case VK_DOWN:i.state->selectNext(e.value==VK_LEFT?-1:e.value==VK_RIGHT?1:e.value==VK_UP?-2:2,shift);i.changed();i.followCard();i.changed();return true;
        default:break;
    }
    switch(e.value){case VK_LEFT:i.state->selectNext(-1,shift);break;case VK_RIGHT:i.state->selectNext(1,shift);break;case VK_UP:i.state->selectNext(-2,shift);break;case VK_DOWN:i.state->selectNext(2,shift);break;
    case VK_PRIOR:i.state->scrollBy(-208);break;case VK_NEXT:i.state->scrollBy(208);break;case VK_HOME:i.state->scrollBy(-i.state->maximumOffset());break;case VK_END:i.state->scrollBy(i.state->maximumOffset());break;
    case VK_DELETE:case VK_BACK:i.state->deleteSelection();break;
    case VK_SPACE:if(const auto&selected=i.state->selectedID();selected&&i.pending(*selected))i.requestRelink(*selected);else i.state->previewSelection();break;
    case VK_ESCAPE:if(!i.state->confirmingClear())return false;i.state->perform("shelf:cancelClear");break;
    case 'V':if(!control)return false;i.request({ShelfPreviewAction::Kind::paste,{}});break;
    case 'R':if(!control)return false;i.state->revealSelection();break;default:return false;}i.changed();return true;
}
bool ShelfPreview::message(const app::NativeMessage&m,double t){auto&i=*impl_;if(m.wParam!=i.generation)return false;
    // The native modal pump must not borrow this owner's event instant or state
    // mutation guard. Nested animation/input callbacks get their current times.
    if(m.message==pickerMessage){i.picker->handleMessage(m.wParam,m.lParam);if(auto result=i.picker->drain(m.wParam)){auto target=std::exchange(i.relinkTarget,{});
        if(SUCCEEDED(result->result)&&!result->paths.empty()){if(target)relink(target->recordID,result->paths,t);else importFiles(result->paths,t);}
        else if(!result->canceled())showError(shelfText(ShelfText::pickerFailed,i.language),t);}return true;}
    const Impl::Event event(i,t);
    if(m.message==noticeMessage&&m.lParam==contentNotice){i.contentPosted=false;return true;}
    if(m.message==workMessage){if(i.ownUtility)i.ownUtility->drain();return true;}
    if(m.message==noticeMessage&&i.drop){i.drainDrop();return true;}
    if(m.message==iconMessage&&i.icons){if(!i.icons->drain(m.wParam).empty())i.dirty=true;return true;}return false;
}
bool ShelfPreview::pointerLocked()const{return impl_->dragCandidate.has_value()||impl_->nativeDrag;}
bool ShelfPreview::preservesFocusOnLoss()const{const auto&i=*impl_;const auto picker=i.picker->stats();return i.nativeDrag||picker.queued||picker.presenting||i.state->dropTarget();}
ShelfPanelState ShelfPreview::panels()const{const auto&i=*impl_;const auto picker=i.picker->stats();
    return {picker.queued||picker.presenting,i.card&&i.cardInteractive,i.card&&i.card->values.has_value(),i.card?i.card->itemID:std::string{},i.propertyReads};}
std::optional<ShelfPreviewAction>ShelfPreview::takeAction(){return std::exchange(impl_->action,{});}
bool ShelfPreview::importFiles(std::span<const std::string>paths,double t){auto&i=*impl_;const Impl::Event event(i,t);const bool value=i.state->importFiles(paths);i.changed();return value;}
bool ShelfPreview::paste(double t){auto&i=*impl_;
    Microsoft::WRL::ComPtr<IDataObject>source;if(FAILED(OleGetClipboard(&source))||!source)return false;
    FORMATETC files{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
    if(SUCCEEDED(source->QueryGetData(&files))){std::vector<std::string>paths;try{paths=gpu::readShelfTransferPaths(*source.Get());}catch(const std::exception&error){source.Reset();showError(error,t);return false;}
        source.Reset();return importFiles(paths,t);}
    for(const auto*name:{L"FileGroupDescriptorW",L"FileGroupDescriptor"}){FORMATETC descriptor{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(name)),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
        if(descriptor.cfFormat&&SUCCEEDED(source->QueryGetData(&descriptor))){source.Reset();showError(shelfText(ShelfText::virtualFiles,i.language),t);return false;}}
    return false;
}
void ShelfPreview::showError(std::string message,double t){auto&i=*impl_;if(auto known=shelfKnownMessage(message,i.language)){if(known->empty())return;message=std::move(*known);}
    const Impl::Event event(i,t);i.state->showError(std::move(message));i.changed();}
void ShelfPreview::showError(const std::exception&error,double t){auto&i=*impl_;showError(shelfErrorText(error,mod::FileShelfState::Failure::action,i.language),t);}
void ShelfPreview::requestFiles(){auto&i=*impl_;
    if(i.relinkTarget&&!i.pending(i.relinkTarget->recordID))i.relinkTarget.reset();
    if(i.relinkTarget){
        // One replacement for one imported item: any single file, or (for a
        // Mac folder) a single item through the mixed file/folder dialog.
        const auto row=i.relinkItem(*i.relinkTarget);const std::array<std::string_view,1>argument{row.name};const auto accept=shelfText(ShelfText::relinkAccept,i.language);
        auto title=core::renderTranslation(shelfText(ShelfText::relinkTitle,i.language),argument).value_or(accept);
        i.picker->setLabels({std::move(title),accept,accept,row.isDirectory?gpu::ShelfPickerMode::mixedReferences:gpu::ShelfPickerMode::singleFile});
    }else{const auto add=core::localized("Add references","添加引用",i.language);i.picker->setLabels({core::localized("Add to Temporary File Shelf","添加到文件暂存架",i.language),add,add});}
    i.picker->request();}
void ShelfPreview::requestPreview(std::string_view id){auto&i=*impl_;i.toggleCard(id);}
void ShelfPreview::setUtility(app::UtilityExecutor*value){auto&i=*impl_;if(value==i.utility)return;if(i.utility&&i.sharedRoute)i.utility->invalidate(i.sharedRoute,false);i.utility=value;i.sharedRoute=0;}
std::optional<mod::ShelfPropertiesContent>ShelfPreview::properties()const{const auto&i=*impl_;return i.card?i.cardContent:std::nullopt;}
void ShelfPreview::refresh(double t){auto&i=*impl_;const Impl::Event event(i,t);
    try{i.store->refresh();}catch(const std::exception&error){i.state->showError(shelfErrorText(error,mod::FileShelfState::Failure::refreshing,i.language));}
    i.readRelinks();i.state->refreshFromStore();i.changed();}
bool ShelfPreview::perform(std::string_view action,double t){auto&i=*impl_;const Impl::Event event(i,t);return i.performAction(action);}
bool ShelfPreview::relink(std::string_view id,std::span<const std::string>paths,double t){auto&i=*impl_;const Impl::Event event(i,t);
    const auto*found=i.pending(id);if(!found){i.state->showError(shelfErrorText(std::runtime_error("Item is no longer on the shelf"),mod::FileShelfState::Failure::action,i.language));i.changed();return false;}
    if(paths.size()!=1){i.state->showError(shelfText(ShelfText::relinkChooseOne,i.language));i.changed();return false;}
    const auto item=*found;std::size_t added{};
    // The chosen object becomes a reference with the imported identifier and
    // date; a duplicate of an existing reference adds nothing.
    try{i.relinkSlot.arm(item);added=i.store->add(paths);i.relinkSlot.disarm();}
    catch(const std::exception&error){i.relinkSlot.disarm();i.state->showError(shelfErrorText(error,mod::FileShelfState::Failure::importing,i.language));i.changed();return false;}
    std::string chosen(paths.front());std::replace(chosen.begin(),chosen.end(),'/','\\');
    if(const auto*row=i.record(item.recordID))chosen=row->windowsPath;
    // The ledger after the record, on the utility worker (resolveMacRelink by
    // store and record id); a lost write is repaired by the next reconcile.
    std::erase_if(i.pendingRelinks,[&](const auto&row){return sameID(row.recordID,item.recordID);});if(i.relinkTarget&&sameID(i.relinkTarget->recordID,item.recordID))i.relinkTarget.reset();
    if(!i.submit([root=i.options.newDataRoot,recordID=item.recordID,chosen]{(void)mig::resolveMacRelink(root,{mig::relinkShelfItem,recordID},chosen);},
        [](std::exception_ptr error){if(error)std::rethrow_exception(error);}))i.state->showError(shelfErrorText(std::runtime_error("Shelf references could not be committed"),mod::FileShelfState::Failure::changing,i.language));
    i.state->refreshFromStore();
    if(added)if(const auto*row=i.record(item.recordID)){const std::array<std::string,1>ids{row->id};i.state->revealItems(ids);i.emit(mod::EventKind::shelfAdded,{{"filename",row->name}});}
    i.changed();return true;}
std::size_t ShelfPreview::pendingRelinks()const{return impl_->pendingRelinks.size();}
bool ShelfPreview::automationRefresh(IRawElementProviderFragmentRoot*root){auto&i=*impl_;
    if(!i.automation||i.automationRoot!=root){
        if(i.automation)i.automation->disconnect();i.automationRoot=root;
        gpu::ShelfAutomationHost host{root,[this]{return impl_->automationControls();},[this](core::Rect rect){return impl_->screenRect(rect);},
            [this](std::string_view id){auto&self=*impl_;if(!self.acceptsInput)return false;const std::string action(id);const bool done=self.performAction(action);
                // The host refreshes artwork and this tree after the notice.
                if(!self.contentPosted)self.contentPosted=PostMessageW(self.hwnd,noticeMessage,self.generation,contentNotice)!=FALSE;return done;}};
        i.automation=std::make_unique<gpu::NativeShelfAutomation>(std::move(host));
    }
    return i.automation->refresh();}
std::size_t ShelfPreview::automationCount()const{return impl_->automation?impl_->automation->size():0;}
long ShelfPreview::automationChild(std::size_t index,IRawElementProviderFragment**out)const{if(!impl_->automation){if(out)*out=nullptr;return E_INVALIDARG;}return impl_->automation->child(index,out);}
long ShelfPreview::automationFromPoint(double x,double y,IRawElementProviderFragment**out)const{if(!impl_->automation){if(out)*out=nullptr;return S_OK;}return impl_->automation->fromPoint(x,y,out);}
void ShelfPreview::automationDisconnect()noexcept{if(impl_->automation)impl_->automation->disconnect();}
void ShelfPreview::setEventRecorder(std::function<void(modules::EventKind,modules::EventMetadata)>value){impl_->recordEvent=std::move(value);}
// The properties card lives in the HUD: no popup window filters keys.
bool ShelfPreview::filterKey(const app::NativeMessage&){return false;}
void ShelfPreview::cancelPanels(){impl_->relinkTarget.reset();impl_->picker->cancel();impl_->closeCard(false);}
void ShelfPreview::cancelInteraction(){auto&i=*impl_;i.dragCandidate.reset();i.pressed=false;i.action.reset();}
void ShelfPreview::setNativeDragActive(bool value){impl_->nativeDrag=value;cancelInteraction();}
void ShelfPreview::setExternalDropCallbacks(std::optional<gpu::ShelfDropTarget::Callbacks> callbacks,double t){
    auto&i=*impl_;const Impl::Event event(i,t);need(!i.released,"Shelf drop route is released");
    if(callbacks)need(bool(callbacks->acceptsPoint)&&bool(callbacks->commit),"Incomplete external file-drop callbacks");
    if(!callbacks&&!i.dropState->external)return;
    need(i.drop!=nullptr,"Shelf file-drop registration is disabled");
    auto next=callbacks?std::make_shared<const gpu::ShelfDropTarget::Callbacks>(std::move(*callbacks)):nullptr;
    i.drop->setEnabled(false);i.drainDrop();
    i.dropState->external=std::move(next);
    // A route can change while COM prevents notice draining. Remove an old
    // Shelf hover now; later commits retain their own origin independently.
    if(i.state->dropTarget()){i.state->setDropTarget(false);i.changed();}
    i.drop->setEnabled(bool(i.dropState->external)||i.acceptsInput);
}
IDropTarget*ShelfPreview::fileDropTarget()const noexcept{return impl_->drop?impl_->drop->dropTarget():nullptr;}
data::ShelfFileAccess ShelfPreview::access(std::string_view id){return impl_->store->access(id);}
std::unique_ptr<gpu::ShelfDragTransfer>ShelfPreview::prepareDrag(std::string_view id){auto&i=*impl_;auto ids=i.state->dragSelection(id);
    auto transfer=std::make_unique<gpu::ShelfDragTransfer>(i.store->prepareCopy(ids),i.files.platform().resolve);i.dragImage(*transfer,ids);return transfer;}
const mod::FileShelfState&ShelfPreview::state()const{return *impl_->state;}
void ShelfPreview::upload(gpu::Renderer&r){auto&i=*impl_;if(!i.pose)return;i.registration.uploadGeometry(r);i.scene->uploadAnimations(r);}
std::span<const gpu::LayerCompositionEntry>ShelfPreview::entries(){auto&i=*impl_;i.composed.clear();if(i.pose){for(const auto&e:i.scene->entries())i.composed.push_back(e);i.composed.push_back({&i.geometry,i.registration.draws()});}return i.composed;}
void ShelfPreview::collected(gpu::Renderer&r){need(impl_->scene->collectRetired(r),"Shelf retired artwork must be detached after publication");}
void ShelfPreview::release(gpu::Renderer&r){auto&i=*impl_;cancelPanels();automationDisconnect();i.dropState->enabled=false;i.dropState->external.reset();if(i.drop)i.drop->stop();if(i.icons)i.icons->stop();need(i.scene->releaseResources(r)&&i.registration.releaseResources(r)&&i.geometry.releaseResources(r),"Shelf resources remain published during teardown");i.released=true;}
}
