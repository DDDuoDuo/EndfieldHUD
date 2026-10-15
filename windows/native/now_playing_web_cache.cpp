#include "native/now_playing_web_cache.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace endfield::native {
namespace {
namespace fs=std::filesystem;
constexpr std::string_view magic="EHNPCACHE1\n";
constexpr std::string_view extension=".np";
char kindCode(NowPlayingWeb::Kind kind)noexcept{return kind==NowPlayingWeb::Kind::lyrics?'l':'a';}
std::uint64_t keyOf(const NowPlayingWeb::Request&request)noexcept{
    // FNV-1a over the kind and the exact URL; the URL is also stored in the
    // file, so a collision reads as a miss.
    std::uint64_t hash=14695981039346656037ull;
    const auto mix=[&](unsigned char c){hash^=c;hash*=1099511628211ull;};
    mix(static_cast<unsigned char>(kindCode(request.kind)));
    for(const char c:request.url)mix(static_cast<unsigned char>(c));
    return hash;
}
std::string hexName(std::uint64_t key){
    static constexpr char digits[]="0123456789abcdef";
    std::string out(16,'0');
    for(int k=15;k>=0;--k){out[static_cast<std::size_t>(k)]=digits[key&15];key>>=4;}
    return out+std::string(extension);
}
std::optional<std::uint64_t>parseName(const fs::path&path){
    if(path.extension().string()!=extension)return std::nullopt;
    const auto stem=path.stem().string();
    if(stem.size()!=16)return std::nullopt;
    std::uint64_t key{};
    for(const char c:stem){
        key<<=4;
        if(c>='0'&&c<='9')key|=static_cast<std::uint64_t>(c-'0');
        else if(c>='a'&&c<='f')key|=static_cast<std::uint64_t>(c-'a'+10);
        else return std::nullopt;
    }
    return key;
}
std::string header(const NowPlayingWeb::Request&request){
    std::string out(magic);
    out+=kindCode(request.kind);
    out+='\n';
    out+=request.url;
    out+='\n';
    return out;
}
// A plain directory (never a link or junction to somewhere else).
bool plainDirectory(const fs::path&path){
    std::error_code error;
    const auto status=fs::symlink_status(path,error);
    return !error&&fs::is_directory(status)&&!fs::is_symlink(status);
}
}

struct NowPlayingWebCache::Impl:std::enable_shared_from_this<Impl>{
    struct Entry{std::uint64_t key{};std::uintmax_t size{};std::uint64_t used{};};
    app::UtilityExecutor&executor;Options options;app::UtilityExecutor::Route route{};
    mutable std::mutex mutex;std::vector<Entry>index;std::uint64_t clock{};Stats counts;bool indexed{},scanning{};
    Impl(app::UtilityExecutor&e,Options o):executor(e),options(std::move(o)){
        if(options.directory.empty()||!options.maximumEntries||!options.maximumBytes)throw std::invalid_argument("The Now Playing disk cache needs a directory and bounds");
        index.reserve(std::min<std::size_t>(options.maximumEntries,256));
    }
    // Under the mutex: the entries beyond the bounds, least recently used first.
    std::vector<std::uint64_t>overflow(){
        std::vector<std::uint64_t>out;
        std::uintmax_t total{};for(const auto&e:index)total+=e.size;
        std::sort(index.begin(),index.end(),[](const Entry&a,const Entry&b){return a.used<b.used;});
        std::size_t drop{};
        while(drop<index.size()&&(index.size()-drop>options.maximumEntries||total>options.maximumBytes)){total-=index[drop].size;out.push_back(index[drop].key);++drop;}
        index.erase(index.begin(),index.begin()+static_cast<std::ptrdiff_t>(drop));
        counts.evictions+=out.size();
        return out;
    }
    void remove(const std::vector<std::uint64_t>&keys){
        for(const auto key:keys){std::error_code error;fs::remove(options.directory/hexName(key),error);}
    }
    // Worker: one directory scan builds the index (oldest first by file time).
    void scan(){
        struct Found{std::uint64_t key;std::uintmax_t size;fs::file_time_type time;};
        std::vector<Found>found;
        if(plainDirectory(options.directory)){
            std::error_code error;
            for(fs::directory_iterator it(options.directory,error),end;!error&&it!=end;it.increment(error)){
                std::error_code status;
                const auto kind=it->symlink_status(status);
                if(status||!fs::is_regular_file(kind))continue;
                const auto path=it->path();
                if(path.extension()==".tmp"){std::error_code ignored;fs::remove(path,ignored);continue;} // an interrupted write
                const auto key=parseName(path);if(!key)continue;
                std::error_code size,time;
                const auto bytes=it->file_size(size);const auto written=it->last_write_time(time);
                if(size||time)continue;
                found.push_back({*key,bytes,written});
                if(found.size()>4096)break; // bounded scan of a foreign directory
            }
        }
        std::sort(found.begin(),found.end(),[](const Found&a,const Found&b){return a.time<b.time;});
        std::vector<std::uint64_t>evicted;
        {
            std::lock_guard lock(mutex);
            for(const auto&f:found)if(std::none_of(index.begin(),index.end(),[&](const Entry&e){return e.key==f.key;}))index.push_back({f.key,f.size,++clock});
            evicted=overflow();
            indexed=true;scanning=false;counts.indexed=true;
        }
        remove(evicted);
    }
    void forget(std::uint64_t key){
        std::lock_guard lock(mutex);
        std::erase_if(index,[&](const Entry&e){return e.key==key;});
    }
    // Worker: the entry's body, or null (then it is forgotten and removed).
    modules::NowPlayingBody read(const NowPlayingWeb::Request&request){
        const auto key=keyOf(request);const auto path=options.directory/hexName(key);
        const auto expected=header(request);const auto limit=nowPlayingWebLimit(request.kind);
        modules::NowPlayingBody body;
        {
            std::error_code error;
            const auto status=fs::symlink_status(path,error);
            const auto size=!error&&fs::is_regular_file(status)?fs::file_size(path,error):0;
            if(!error&&size>expected.size()&&size-expected.size()<=limit){
                std::ifstream in(path,std::ios::binary);
                std::string head(expected.size(),'\0');
                if(in.read(head.data(),static_cast<std::streamsize>(head.size()))&&head==expected){
                    auto bytes=std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(size-expected.size()));
                    if(in.read(reinterpret_cast<char*>(bytes->data()),static_cast<std::streamsize>(bytes->size()))&&in.peek()==std::char_traits<char>::eof())body=std::move(bytes);
                }
            }
        }
        if(!body){
            forget(key);
            std::error_code ignored;fs::remove(path,ignored);
            std::lock_guard lock(mutex);++counts.misses;
            return nullptr;
        }
        {
            std::error_code ignored;fs::last_write_time(path,fs::file_time_type::clock::now(),ignored); // recently used across launches
            std::lock_guard lock(mutex);
            for(auto&e:index)if(e.key==key)e.used=++clock;
            ++counts.hits;
        }
        return body;
    }
    // Worker: atomic replace through a temporary file, then the bounds.
    void write(const NowPlayingWeb::Request&request,const modules::NowPlayingBody&body){
        const auto key=keyOf(request);const auto path=options.directory/hexName(key);
        auto temporary=path;temporary+=".tmp";
        bool written{};
        {
            std::error_code error;
            fs::create_directories(options.directory,error);
            if(!error&&plainDirectory(options.directory)){
                {
                    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
                    const auto head=header(request);
                    out.write(head.data(),static_cast<std::streamsize>(head.size()));
                    out.write(reinterpret_cast<const char*>(body->data()),static_cast<std::streamsize>(body->size()));
                    out.flush();
                    written=static_cast<bool>(out);
                }
                if(written){fs::rename(temporary,path,error);written=!error;}
                if(!written){std::error_code ignored;fs::remove(temporary,ignored);}
            }
        }
        std::vector<std::uint64_t>evicted;
        {
            std::lock_guard lock(mutex);
            if(!written){++counts.failures;return;}
            std::erase_if(index,[&](const Entry&e){return e.key==key;});
            index.push_back({key,body->size()+header(request).size(),++clock});
            ++counts.stores;
            evicted=overflow();
        }
        // The new entry is the most recently used: never among the evicted.
        remove(evicted);
    }
};

NowPlayingWebCache::NowPlayingWebCache(app::UtilityExecutor&executor,Options options):impl_(std::make_shared<Impl>(executor,std::move(options))){
    impl_->route=executor.makeRoute();
}
NowPlayingWebCache::~NowPlayingWebCache(){
    // Queued reads and writes are discarded; a running one finishes on its
    // own reference to the state.
    try{impl_->executor.invalidate(impl_->route);}catch(...){}
}
void NowPlayingWebCache::prepare(){
    auto i=impl_;
    {std::lock_guard lock(i->mutex);if(i->indexed||i->scanning)return;i->scanning=true;}
    bool queued{};
    try{queued=i->executor.submit(i->route,[i]{i->scan();},[](std::exception_ptr){});}catch(const std::logic_error&){queued=false;}
    if(!queued){std::lock_guard lock(i->mutex);i->scanning=false;++i->counts.backpressure;} // a later prepare() retries
}
bool NowPlayingWebCache::contains(const NowPlayingWeb::Request&request)const{
    const auto key=keyOf(request);
    std::lock_guard lock(impl_->mutex);
    return impl_->indexed&&std::any_of(impl_->index.begin(),impl_->index.end(),[&](const Impl::Entry&e){return e.key==key;});
}
std::function<void()>NowPlayingWebCache::load(const NowPlayingWeb::Request&request,std::function<void(modules::NowPlayingBody)>done){
    if(!done)throw std::invalid_argument("A Now Playing cache read needs a completion");
    struct State{std::function<void(modules::NowPlayingBody)>done;modules::NowPlayingBody body;bool cancelled{};};
    auto state=std::make_shared<State>();state->done=std::move(done);
    auto i=impl_;bool queued{};
    try{
        queued=i->executor.submit(i->route,[i,state,request]{state->body=i->read(request);},[state](std::exception_ptr){
            if(state->cancelled)return;
            auto callback=std::move(state->done);state->done=nullptr;
            if(callback)callback(std::move(state->body));
        });
    }catch(const std::logic_error&){queued=false;}
    if(!queued){
        {std::lock_guard lock(i->mutex);++i->counts.backpressure;}
        auto callback=std::move(state->done);state->done=nullptr;
        callback(nullptr);
        return []{};
    }
    return [state]{state->cancelled=true;state->done=nullptr;};
}
void NowPlayingWebCache::store(const NowPlayingWeb::Request&request,modules::NowPlayingBody body){
    if(!body||body->empty()||body->size()>nowPlayingWebLimit(request.kind))return;
    auto i=impl_;bool queued{};
    try{queued=i->executor.submit(i->route,[i,request,body]{i->write(request,body);},[](std::exception_ptr){});}catch(const std::logic_error&){queued=false;}
    if(!queued){std::lock_guard lock(i->mutex);++i->counts.backpressure;}
}
NowPlayingWebCache::Stats NowPlayingWebCache::stats()const{
    std::lock_guard lock(impl_->mutex);
    auto s=impl_->counts;s.entries=impl_->index.size();s.bytes=0;
    for(const auto&e:impl_->index)s.bytes+=e.size;
    s.indexed=impl_->indexed;
    return s;
}
std::string NowPlayingWebCache::fileName(const NowPlayingWeb::Request&request){return hexName(keyOf(request));}
}
