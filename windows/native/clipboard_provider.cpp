#include "native/clipboard_provider.hpp"
#include <algorithm>
#include <atomic>
#include <map>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
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
    struct CachedRow {ClipboardRow row;std::uint64_t seen{};};
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
    ClipboardRow row(const ClipboardItem&item){ClipboardRow result;result.id=item.id;result.kind=item.payload.kind;result.pinned=item.pinned;
        const auto&p=item.payload;
        if(p.kind==ClipboardKind::text||p.kind==ClipboardKind::url)result.preview=compact(p.text);
        else if(p.kind==ClipboardKind::files){std::u16string names;for(std::size_t n=0;n<std::min<std::size_t>(3,p.files.size());++n){if(n)names+=u", ";names+=filename(p.files[n]);}if(p.files.size()>3){names+=u" (+";for(auto c:std::to_string(p.files.size()-3))names.push_back(char16_t(c));names+=u')';}result.preview=compact(names);}
        else if(p.decodedImage){const auto&t=*p.decodedImage;result.preview="Image · "+std::to_string(t.sourceWidth)+" × "+std::to_string(t.sourceHeight);result.thumbnail=Json::Object{{"memoryImage",key(item.id)},{"revision",1}};}
        else result.preview="Image"; // legacy synthetic history lacks validated pixels; no invented thumbnail
        ++counts.previewBuilds;return result;
    }
    ClipboardSnapshot snapshot(){thread();++counts.snapshots;const auto current=source;if(!current)return {};const auto&history=current->history();ClipboardSnapshot out;out.capacity=history.capacity();if(current->status)out.status=current->status();out.rows.reserve(history.items().size());
        for(const auto&item:history.items()){auto found=rows.find(item.id);if(found==rows.end())found=rows.emplace(item.id,CachedRow{row(item),counts.snapshots}).first;found->second.seen=counts.snapshots;auto value=found->second.row;value.pinned=item.pinned;out.rows.push_back(std::move(value));}
        std::erase_if(rows,[&](const auto&entry){return entry.second.seen!=counts.snapshots;});return out;
    }
    void prepare(std::span<const std::uint64_t>ids){thread();need(ids.size()<=maximumPreparedRows,"Clipboard prepares only current and incoming visible rows");for(auto id:ids)need(id!=0,"Clipboard image row identity must be nonzero");const auto current=source;if(!current)return;
        const auto&history=current->history();std::array<std::shared_ptr<const LayerMemoryImage>,maximumPreparedRows>held;std::array<std::string,maximumPreparedRows>keys;
        // Touch every retained requested key before publishing new ones, so
        // bounded LRU pressure cannot evict a current/outgoing row's thumbnail.
        for(std::size_t n=0;n<ids.size();++n){keys[n]=key(ids[n]);held[n]=images.acquire(keys[n],1);}
        for(std::size_t n=0;n<ids.size();++n){if(held[n])continue;const auto*item=history.find(ids[n]);if(!item||!item->payload.decodedImage)continue;const auto&t=*item->payload.decodedImage;
            if(auto existing=images.acquire(keys[n],1)){held[n]=std::move(existing);continue;}held[n]=images.publish(keys[n],1,t.width,t.height,t.rgba);++counts.imagePreparations;
        }
    }
};
NativeClipboardProvider::NativeClipboardProvider(ClipboardProviderSource source):impl_(std::make_shared<Impl>(std::move(source))){}
NativeClipboardProvider::~NativeClipboardProvider()=default;
ClipboardActions NativeClipboardProvider::actions()const{impl_->thread();const std::weak_ptr<Impl>weak=impl_;ClipboardActions out;
    out.snapshot=[weak]{if(auto i=weak.lock())return i->snapshot();return ClipboardSnapshot{};};
    out.copy=[weak](auto id){if(auto i=weak.lock()){i->thread();const auto s=i->source;return s&&s->history().find(id)&&s->copy&&s->copy(id);}return false;};
    out.togglePin=[weak](auto id){if(auto i=weak.lock()){i->thread();const auto s=i->source;if(s&&s->pin){const auto*item=s->history().find(id);if(item)return s->pin(id,!item->pinned);}}return false;};
    out.remove=[weak](auto id){if(auto i=weak.lock()){i->thread();const auto s=i->source;return s&&s->history().find(id)&&s->remove&&s->remove(id);}return false;};
    out.clearUnpinned=[weak]{if(auto i=weak.lock()){i->thread();const auto s=i->source;return s&&s->clearUnpinned&&s->clearUnpinned();}return false;};
    out.prepareImageRows=[weak](auto ids){if(auto i=weak.lock())i->prepare(ids);};return out;
}
LayerImageSource&NativeClipboardProvider::images()noexcept{return impl_->images;}
void NativeClipboardProvider::clearImages(){impl_->thread();impl_->images.clear();}
void NativeClipboardProvider::close(){impl_->thread();impl_->source.reset();impl_->rows.clear();impl_->images.clear();}
ClipboardProviderStats NativeClipboardProvider::stats()const{impl_->thread();auto s=impl_->counts;s.metadataRows=impl_->rows.size();s.images=impl_->images.stats();return s;}
std::array<std::uint8_t,4>NativeClipboardProvider::unicodeVersion()noexcept{UVersionInfo v{};u_getUnicodeVersion(v);return {v[0],v[1],v[2],v[3]};}
#ifdef _WIN32
ClipboardProviderSource clipboardProviderSource(SystemServices&services,std::function<std::optional<std::string>(std::int32_t)>status){ClipboardProviderSource out;
    out.history=[&services]() -> const ClipboardHistory& {return services.clipboard();};out.copy=[&services](auto id){return SUCCEEDED(services.copy_clipboard_item(id));};out.remove=[&services](auto id){return services.erase_clipboard_item(id);};out.pin=[&services](auto id,bool pinned){return services.pin_clipboard_item(id,pinned);};
    out.clearUnpinned=[&services]{const auto count=services.clipboard().items().size();services.clear_clipboard(true);return count!=services.clipboard().items().size();};
    if(status)out.status=[&services,format=std::move(status)]{return format(static_cast<std::int32_t>(services.clipboard_status()));};return out;
}
#endif
}
