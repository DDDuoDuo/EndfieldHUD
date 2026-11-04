#include "native/clipboard_provider.hpp"
#include <algorithm>
#include <atomic>
#include <map>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include "native/activity_catalog.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <icu.h>
#else
#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#endif

namespace endfield::native {
namespace {
using Json=ehud::data::Json;
void need(bool condition,const char*message){if(!condition)throw std::invalid_argument(message);}
std::uint32_t scalar(std::u16string_view s,std::size_t&at){const auto first=s[at++];if(first<0xd800||first>0xdbff)return first;const auto second=s[at++];return 0x10000u+(std::uint32_t(first)-0xd800u)*1024u+second-0xdc00u;}
std::string utf8(std::u16string_view s){std::string out;out.reserve(s.size());for(std::size_t n=0;n<s.size();){const auto c=scalar(s,n);if(c<0x80)out.push_back(char(c));else if(c<0x800){out.push_back(char(0xc0|(c>>6)));out.push_back(char(0x80|(c&63)));}else if(c<0x10000){out.push_back(char(0xe0|(c>>12)));out.push_back(char(0x80|((c>>6)&63)));out.push_back(char(0x80|(c&63)));}else{out.push_back(char(0xf0|(c>>18)));out.push_back(char(0x80|((c>>12)&63)));out.push_back(char(0x80|((c>>6)&63)));out.push_back(char(0x80|(c&63)));}}return out;}
std::u16string_view filename(std::u16string_view path){while(path.size()>1&&(path.back()==u'/'||path.back()==u'\\'))path.remove_suffix(1);const auto at=path.find_last_of(u"/\\");return at==std::u16string_view::npos?path:path.substr(at+1);}
}
struct NativeClipboardProvider::Impl {
    std::thread::id owner=std::this_thread::get_id();std::shared_ptr<const ClipboardProviderSource>source;
    struct CachedRow {modules::ClipboardRecord record;std::uint64_t seen{};};
    LayerImageSource images;std::map<std::uint64_t,CachedRow>rows;std::string prefix;UBreakIterator*iterator{};
    std::u16string flattened;ClipboardProviderStats counts;
    explicit Impl(ClipboardProviderSource s):source(std::make_shared<const ClipboardProviderSource>(std::move(s))){
        need(bool(source->history),"Clipboard provider requires existing history view");static std::atomic<std::uint64_t>identity{};prefix="clipboard-provider-"+std::to_string(++identity)+"/";
        UErrorCode error=U_ZERO_ERROR;iterator=ubrk_open(UBRK_CHARACTER,"",nullptr,0,&error);if(U_FAILURE(error)||!iterator){if(iterator)ubrk_close(iterator);throw std::runtime_error("Cannot initialize Clipboard grapheme iterator");}
    }
    ~Impl(){if(iterator)ubrk_close(iterator);}
    void thread()const{if(owner!=std::this_thread::get_id())throw std::logic_error("Clipboard provider belongs to its creating thread");}
    std::string key(std::uint64_t id)const{return prefix+std::to_string(id);}
    void text(std::u16string_view value){UErrorCode error=U_ZERO_ERROR;ubrk_setText(iterator,reinterpret_cast<const UChar*>(value.data()),static_cast<std::int32_t>(value.size()),&error);if(U_FAILURE(error))throw std::runtime_error("Cannot segment Clipboard preview");}
    // String.count of the whole text (ClipboardDetails.characters, :231).
    std::int64_t characters(std::u16string_view value){text(value);ubrk_first(iterator);std::int64_t count{};while(ubrk_next(iterator)!=UBRK_DONE)++count;return count;}
    std::string compact(std::u16string_view input){
        // ClipboardStore.make: split(Character.isWhitespace), join one space,
        // then prefix89+ellipsis iff flattened extended-grapheme count >90.
        // Swift Character.isWhitespace examines its first scalar (including
        // space+combining mark); control characters other than whitespace stay.
        need(input.size()<=ClipboardHistory::maximum_text_bytes&&ClipboardHistory::valid_utf16(input),"Invalid bounded Clipboard preview UTF16");
        flattened.clear();text(input);bool separate{};auto first=ubrk_first(iterator);
        for(auto end=ubrk_next(iterator);end!=UBRK_DONE;first=end,end=ubrk_next(iterator)){
            need(first>=0&&end>first&&std::size_t(end)<=input.size(),"Invalid Clipboard grapheme boundary");const auto cluster=input.substr(std::size_t(first),std::size_t(end-first));std::size_t at{};
            if(u_isUWhiteSpace(static_cast<UChar32>(scalar(cluster,at)))){separate=!flattened.empty();continue;}
            if(separate)flattened.push_back(u' ');separate=false;flattened.append(cluster);
        }
        text(flattened);ubrk_first(iterator);std::int32_t prefix89{};unsigned count{};for(auto end=ubrk_next(iterator);end!=UBRK_DONE;end=ubrk_next(iterator)){++count;if(count==89)prefix89=end;if(count==91)break;}
        if(count>90)return utf8(std::u16string_view(flattened).substr(0,std::size_t(prefix89)))+"…";return utf8(flattened);
    }
    // ClipboardStore.read / make (:182-233) as the canvas reads the record.
    modules::ClipboardRecord record(const ClipboardItem&item){modules::ClipboardRecord r;r.id=item.id;r.pinned=item.pinned;r.createdAt=item.origin.created;
        const auto&p=item.payload;auto&d=r.details;
        if(item.origin.source&&!item.origin.source->empty())d.source=utf8(*item.origin.source);
        switch(p.kind){
        case ClipboardKind::text:r.kind=modules::ClipboardRecordKind::text;r.preview=compact(p.text);d.characters=characters(p.text);break;
        case ClipboardKind::url:r.kind=modules::ClipboardRecordKind::url;r.preview=compact(p.text);break;
        case ClipboardKind::files:{r.kind=modules::ClipboardRecordKind::files;std::u16string names;for(std::size_t n=0;n<std::min<std::size_t>(3,p.files.size());++n){if(n)names+=u", ";names+=filename(p.files[n]);}
            if(p.files.size()>3){names+=u" (+";for(auto c:std::to_string(p.files.size()-3))names.push_back(char16_t(c));names+=u')';}r.preview=compact(names);
            d.fileCount=std::int64_t(p.files.size());if(item.origin.fileBytes)d.byteCount=std::int64_t(std::min<std::uint64_t>(*item.origin.fileBytes,std::uint64_t(INT64_MAX)));break;}
        case ClipboardKind::image:
            r.kind=modules::ClipboardRecordKind::image;
            // The Mac names PNG and TIFF pasteboard data; a Windows DIB is a bitmap ("BMP").
            d.format=p.image_format==ClipboardImageFormat::png?"PNG":p.image_format==ClipboardImageFormat::tiff?"TIFF":"BMP";
            // No preview: the canvas titles image records "Image" and never
            // reads the Mac's "Image · W × H" preview.
            if(p.decodedImage){const auto&t=*p.decodedImage;
                d.pixelWidth=t.sourceWidth;d.pixelHeight=t.sourceHeight;r.thumbnail=modules::design::ImageSource{modules::design::ImageSource::Kind::memory,key(item.id),{},1};}
            break;
        }
        ++counts.previewBuilds;return r;
    }
    // The 96 px thumbnail at {key, 1} while its record lives (the Mac keeps
    // ClipboardItem.thumbnail with the record).
    void publish(const ClipboardItem&item){
        if(item.payload.kind!=ClipboardKind::image||!item.payload.decodedImage)return;const auto k=key(item.id);if(images.acquire(k,1))return;
        const auto&t=*item.payload.decodedImage;(void)images.publish(k,1,t.width,t.height,t.rgba);++counts.imagePreparations;
    }
    modules::ClipboardSnapshot snapshot(){thread();++counts.snapshots;const auto current=source;if(!current)return {};const auto&history=current->history();modules::ClipboardSnapshot out;out.capacity=history.capacity();if(current->status)out.status=current->status();out.items.reserve(history.items().size());
        for(const auto&item:history.items()){auto found=rows.find(item.id);if(found==rows.end())found=rows.emplace(item.id,CachedRow{record(item),counts.snapshots}).first;found->second.seen=counts.snapshots;publish(item);auto value=found->second.record;value.pinned=item.pinned;out.items.push_back(std::move(value));}
        for(auto it=rows.begin();it!=rows.end();){if(it->second.seen==counts.snapshots){++it;continue;}(void)images.retire(key(it->first));it=rows.erase(it);}return out;
    }
};
NativeClipboardProvider::NativeClipboardProvider(ClipboardProviderSource source):impl_(std::make_shared<Impl>(std::move(source))){}
NativeClipboardProvider::~NativeClipboardProvider()=default;
modules::ClipboardActions NativeClipboardProvider::actions()const{impl_->thread();const std::weak_ptr<Impl>weak=impl_;modules::ClipboardActions out;
    out.snapshot=[weak]{if(auto i=weak.lock())return i->snapshot();return modules::ClipboardSnapshot{};};
    out.copy=[weak](auto id){if(auto i=weak.lock()){i->thread();const auto s=i->source;const auto*item=s?s->history().find(id):nullptr;if(!item||!s->copy)return false;const auto kind=item->payload.kind;if(!s->copy(id))return false;if(s->copied)s->copied(kind);return true;}return false;};
    out.togglePin=[weak](auto id){if(auto i=weak.lock()){i->thread();const auto s=i->source;if(s&&s->pin){const auto*item=s->history().find(id);if(item)return s->pin(id,!item->pinned);}}return false;};
    out.remove=[weak](auto id){if(auto i=weak.lock()){i->thread();const auto s=i->source;return s&&s->history().find(id)&&s->remove&&s->remove(id);}return false;};
    out.clearUnpinned=[weak]{if(auto i=weak.lock()){i->thread();const auto s=i->source;return s&&s->clearUnpinned&&s->clearUnpinned();}return false;};
    return out;
}
LayerImageSource&NativeClipboardProvider::images()noexcept{return impl_->images;}
void NativeClipboardProvider::clearImages(){impl_->thread();impl_->images.clear();}
void NativeClipboardProvider::close(){impl_->thread();impl_->source.reset();impl_->rows.clear();impl_->images.clear();}
ClipboardProviderStats NativeClipboardProvider::stats()const{impl_->thread();auto s=impl_->counts;s.metadataRows=impl_->rows.size();s.images=impl_->images.stats();return s;}
std::optional<std::string>clipboardStatusText(std::int32_t status,core::Language language){
    if(status>=0||status==clipboard_status::pending)return {};
    switch(status){
    case clipboard_status::cacheFull:return core::localized("The cache is full of pinned items. Unpin or remove an item to capture more.","缓存已被固定项目占满，请取消固定或移除项目后继续记录。",language);
    case clipboard_status::textTooLarge:return core::localized("Text over 1 MB was skipped.","已跳过超过 1 MB 的文本。",language);
    case clipboard_status::imageTooLarge:return core::localized("An image over 64 MB was skipped.","已跳过超过 64 MB 的图像。",language);
    case clipboard_status::imageDimensionsTooLarge:return core::localized("An image over 100 megapixels was skipped.","已跳过超过 1 亿像素的图像。",language);
    case clipboard_status::tooManyFiles:return core::localized("This file selection is too large to cache.","此文件选择过大，无法缓存。",language);
    default:break;
    }
    // Windows-only state (the Mac pasteboard is never "busy"); not in the
    // shared source catalog, so its five translations live here.
    switch(language){
    case core::Language::simplifiedChinese:return std::string("剪贴板不可用");
    case core::Language::traditionalChinese:return std::string("剪貼簿無法使用");
    case core::Language::japanese:return std::string("クリップボードを利用できません");
    case core::Language::korean:return std::string("클립보드를 사용할 수 없습니다");
    default:return std::string("Clipboard unavailable");
    }
}
std::string_view clipboardEventKind(ClipboardKind kind)noexcept{switch(kind){case ClipboardKind::url:return "url";case ClipboardKind::files:return "files";case ClipboardKind::image:return "image";default:return "text";}}
std::array<std::uint8_t,4>NativeClipboardProvider::unicodeVersion()noexcept{UVersionInfo v{};u_getUnicodeVersion(v);return {v[0],v[1],v[2],v[3]};}
#ifdef _WIN32
ClipboardProviderSource clipboardProviderSource(SystemServices&services,std::function<std::optional<std::string>(std::int32_t)>status,std::function<void(ClipboardKind)>copied){ClipboardProviderSource out;out.copied=std::move(copied);
    out.history=[&services]() -> const ClipboardHistory& {return services.clipboard();};out.copy=[&services](auto id){return SUCCEEDED(services.copy_clipboard_item(id));};out.remove=[&services](auto id){return services.erase_clipboard_item(id);};out.pin=[&services](auto id,bool pinned){return services.pin_clipboard_item(id,pinned);};
    out.clearUnpinned=[&services]{const auto count=services.clipboard().items().size();services.clear_clipboard(true);return count!=services.clipboard().items().size();};
    if(status)out.status=[&services,format=std::move(status)]{return format(static_cast<std::int32_t>(services.clipboard_status()));};return out;
}
namespace {
std::u16string wide(std::string_view value){std::u16string out;if(value.empty()||value.size()>4096)return out;const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
    if(count<=0)return out;out.resize(std::size_t(count));MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),reinterpret_cast<wchar_t*>(out.data()),count);return out;}
// NSRunningApplication.localizedName of a window's process; none for this process.
std::optional<std::u16string> applicationName(HWND window){
    DWORD pid{};if(!window||!GetWindowThreadProcessId(window,&pid)||!pid||pid==GetCurrentProcessId())return std::nullopt;
    const HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return std::nullopt;
    std::wstring path(32768,L'\0');DWORD size=DWORD(path.size());const bool read=QueryFullProcessImageNameW(process,0,path.data(),&size)!=FALSE;CloseHandle(process);
    if(!read||!size)return std::nullopt;path.resize(size);
    const auto slash=path.find_last_of(L"\\/");std::wstring stem=slash==std::wstring::npos?path:path.substr(slash+1);if(const auto dot=stem.rfind(L'.');dot!=std::wstring::npos&&dot>0)stem.resize(dot);
    const auto name=activityDisplayName(windowsActivityFileDescription(path),utf8(std::u16string_view(reinterpret_cast<const char16_t*>(stem.data()),stem.size())));
    if(name.empty())return std::nullopt;return wide(name);
}
}
ClipboardOrigin windowsClipboardOrigin(const ClipboardPayload&payload){ClipboardOrigin out;
    // The clipboard's owner window stands for the declared source
    // (org.nspasteboard.source); else the foreground app, never this one.
    out.source=applicationName(GetClipboardOwner());if(!out.source)out.source=applicationName(GetForegroundWindow());
    if(payload.kind==ClipboardKind::files&&payload.files.size()<=64){std::uint64_t total{};bool regular=true;
        for(const auto&file:payload.files){WIN32_FILE_ATTRIBUTE_DATA data{};
            if(!GetFileAttributesExW(reinterpret_cast<const wchar_t*>(file.c_str()),GetFileExInfoStandard,&data)||(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE))){regular=false;break;}
            total+=(std::uint64_t(data.nFileSizeHigh)<<32)|data.nFileSizeLow;}
        if(regular)out.fileBytes=total;}
    return out;
}
#endif
}
