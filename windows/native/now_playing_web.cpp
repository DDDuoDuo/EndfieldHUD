#include "native/now_playing_web.hpp"
#include "native/now_playing_web_cache.hpp"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace endfield::native {
namespace {
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
std::string lower(std::string_view value){std::string out(value);for(auto&c:out)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return out;}
}
std::size_t nowPlayingWebLimit(NowPlayingWeb::Kind kind)noexcept{
    return kind==NowPlayingWeb::Kind::lyrics?modules::nowPlayingMaximumLyricsResponseBytes:modules::nowPlayingMaximumArtworkBytes;
}
bool nowPlayingWebAccepts(NowPlayingWeb::Kind kind,int status,std::string_view mediaType,std::optional<std::uint64_t>length)noexcept{
    if(length&&*length>nowPlayingWebLimit(kind))return false;
    const auto media=lower(mediaType);
    if(kind==NowPlayingWeb::Kind::lyrics)return status==200&&(media=="application/json"||media=="text/plain");
    return status>=200&&status<=299&&media.size()>6&&media.compare(0,6,"image/")==0;
}
std::optional<std::string>nowPlayingWebReferer(std::string_view url){
    // Exactly the catalog host (source NowPlayingCatalog sets this Referer).
    const auto value=lower(url);
    std::string_view rest(value);
    if(rest.rfind("https://",0)==0)rest.remove_prefix(8);
    else if(rest.rfind("http://",0)==0)rest.remove_prefix(7);
    else return std::nullopt;
    const auto end=rest.find_first_of("/?#:");
    if(rest.substr(0,end)=="music.163.com")return std::string("https://music.163.com/");
    return std::nullopt;
}
std::string nowPlayingWebMediaType(std::string_view value){
    const auto end=value.find(';');
    auto media=value.substr(0,end);
    while(!media.empty()&&(media.front()==' '||media.front()=='\t'))media.remove_prefix(1);
    while(!media.empty()&&(media.back()==' '||media.back()=='\t'))media.remove_suffix(1);
    return std::string(media);
}

struct NowPlayingWeb::Impl:std::enable_shared_from_this<Impl>{
    // A request is first looked up in the disk cache (loading) when the cache
    // lists it, else (or after a miss) sent through the transport.
    struct Pending {
        std::uint64_t id{};Request request;std::shared_ptr<Task>task;std::function<void()>cancelLoad;
        std::function<void(modules::NowPlayingBody)>completion;double deadline{};bool loading{};
        void stop(){auto load=std::move(cancelLoad);cancelLoad=nullptr;if(load)load();if(task)task->cancel();}
    };
    struct Finished {std::uint64_t id{};modules::NowPlayingBody body;bool cached{};};
    Transport transport;std::function<void()>notify;std::function<double()>now;double timeout;std::shared_ptr<NowPlayingWebCache>cache;
    const std::thread::id owner=std::this_thread::get_id();
    std::vector<Pending>pending;std::uint64_t next{1};Stats counts;bool alive{true};
    std::mutex mutex;std::vector<Finished>finished;bool notified{};
    Impl(Transport t,std::function<void()>n,std::function<double()>c,double bound,std::shared_ptr<NowPlayingWebCache>store)
        :transport(std::move(t)),notify(std::move(n)),now(std::move(c)),timeout(bound),cache(std::move(store)){
        need(bool(transport)&&bool(notify)&&bool(now)&&std::isfinite(timeout)&&timeout>0&&timeout<=60,"Now Playing web needs transport, owner wake, clock and bounded timeout");
        pending.reserve(maximumInFlight);
    }
    void check()const{if(owner!=std::this_thread::get_id())throw std::logic_error("Now Playing web belongs to its owner thread");}
    // Any thread. Coalesces wakes exactly like the service's message slot.
    void post(std::uint64_t id,modules::NowPlayingBody body,bool cached=false){
        bool wake{};{std::lock_guard lock(mutex);finished.push_back({id,std::move(body),cached});wake=!notified;notified=true;}
        if(wake)try{notify();}catch(...){std::lock_guard lock(mutex);notified=false;}
    }
    Pending*find(std::uint64_t id){
        const auto found=std::find_if(pending.begin(),pending.end(),[&](const auto&p){return p.id==id;});
        return found==pending.end()?nullptr:&*found;
    }
    std::optional<Pending>take(std::uint64_t id){
        const auto found=std::find_if(pending.begin(),pending.end(),[&](const auto&p){return p.id==id;});
        if(found==pending.end())return {};auto value=std::move(*found);pending.erase(found);return value;
    }
    void cancel(std::uint64_t id){if(auto value=take(id)){++counts.cancelled;value->stop();}}
    // Owner thread: the network request of a pending entry.
    bool startNetwork(Pending&p){
        const std::weak_ptr<Impl>weak=weak_from_this();const auto id=p.id;
        std::shared_ptr<Task>task;
        try{task=transport(p.request,[weak,id](modules::NowPlayingBody body){if(auto self=weak.lock())self->post(id,std::move(body));});}catch(...){task.reset();}
        if(!task)return false;
        ++counts.started;p.task=std::move(task);return true;
    }
    // Owner thread (executor drain, or synchronously from a full queue).
    void cached(std::uint64_t id,modules::NowPlayingBody body){
        auto*p=find(id);if(!p||!p->loading)return; // cancelled or timed out meanwhile
        p->loading=false;p->cancelLoad=nullptr;
        if(body){++counts.cacheHits;post(id,std::move(body),true);return;}
        ++counts.cacheMisses;
        if(!startNetwork(*p))post(id,nullptr);
    }
    static std::function<void()>fetch(const std::shared_ptr<Impl>&i,Request request,std::function<void(modules::NowPlayingBody)>completion){
        i->check();need(bool(completion),"Now Playing web fetch needs a completion");
        const bool allowed=request.kind==Kind::lyrics?modules::nowPlayingLyricsURLAllowed(request.url):modules::nowPlayingArtworkURLAllowed(request.url);
        if(!i->alive||!allowed||i->pending.size()>=maximumInFlight){++i->counts.rejected;completion(nullptr);return []{};}
        const auto time=i->now();need(std::isfinite(time),"Now Playing web clock must be finite");
        const auto id=i->next++;const std::weak_ptr<Impl>weak=i;
        Pending entry{id,std::move(request),nullptr,nullptr,std::move(completion),time+i->timeout,false};
        const auto cancel=[weak,id]{if(auto self=weak.lock()){self->check();self->cancel(id);}};
        if(i->cache&&i->cache->contains(entry.request)){
            entry.loading=true;const auto key=entry.request;
            i->pending.push_back(std::move(entry));
            auto stop=i->cache->load(key,[weak,id](modules::NowPlayingBody body){if(auto self=weak.lock())self->cached(id,std::move(body));});
            if(auto*p=i->find(id);p&&p->loading)p->cancelLoad=std::move(stop);
            return cancel;
        }
        if(!i->startNetwork(entry)){++i->counts.rejected;entry.completion(nullptr);return []{};}
        i->pending.push_back(std::move(entry));
        return cancel;
    }
};
NowPlayingWeb::NowPlayingWeb(Transport transport,std::function<void()>notify,std::function<double()>now,double timeout,std::shared_ptr<NowPlayingWebCache>cache)
    :impl_(std::make_shared<Impl>(std::move(transport),std::move(notify),std::move(now),timeout,std::move(cache))){}
NowPlayingWeb::~NowPlayingWeb(){impl_->alive=false;cancelAll();}
std::function<void()>NowPlayingWeb::fetch(Request request,std::function<void(modules::NowPlayingBody)>completion){return Impl::fetch(impl_,std::move(request),std::move(completion));}
modules::NowPlayingFetch NowPlayingWeb::lyricsFetch(){
    const std::weak_ptr<Impl>weak=impl_;
    return [weak](const std::string&url,std::function<void(modules::NowPlayingBody)>completion)->std::function<void()>{
        if(const auto self=weak.lock())return Impl::fetch(self,{url,Kind::lyrics},std::move(completion));
        completion(nullptr);return []{};
    };
}
bool NowPlayingWeb::drain(){
    auto i=impl_;i->check();std::vector<Impl::Finished>done;
    {std::lock_guard lock(i->mutex);done.swap(i->finished);i->notified=false;}
    bool ran{};
    for(auto&f:done){
        auto value=i->take(f.id);if(!value)continue; // cancelled or timed out: never delivered
        auto body=std::move(f.body);
        if(body&&body->size()>nowPlayingWebLimit(value->request.kind))body.reset();
        if(body)++i->counts.completed;else ++i->counts.failed;
        // Only a body the transport accepted is kept on disk.
        if(body&&!f.cached&&i->cache){i->cache->store(value->request,body);++i->counts.cacheStores;}
        ran=true;value->completion(std::move(body));
    }
    const auto time=i->now();
    for(;;){
        const auto expired=std::find_if(i->pending.begin(),i->pending.end(),[&](const auto&p){return p.deadline<=time;});
        if(expired==i->pending.end())break;
        auto value=std::move(*expired);i->pending.erase(expired);++i->counts.timedOut;
        value.stop();ran=true;value.completion(nullptr);
    }
    return ran;
}
std::optional<double>NowPlayingWeb::nextWakeTime()const{
    impl_->check();std::optional<double>out;
    for(const auto&p:impl_->pending)out=out?std::min(*out,p.deadline):p.deadline;
    return out;
}
void NowPlayingWeb::cancelAll(){
    auto i=impl_;i->check();auto values=std::move(i->pending);i->pending.clear();
    for(auto&value:values){++i->counts.cancelled;value.stop();}
}
void NowPlayingWeb::prepare(){auto i=impl_;i->check();if(i->cache)i->cache->prepare();}
NowPlayingWeb::Stats NowPlayingWeb::stats()const{impl_->check();auto s=impl_->counts;s.inFlight=impl_->pending.size();return s;}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <cwchar>
#include <memory>

namespace endfield::native {
namespace {
std::wstring widen(std::string_view text){
    if(text.empty())return {};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(count<=0)return {};
    std::wstring out(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),count);
    return out;
}
std::string narrow(const wchar_t*text){
    std::string out;
    for(const wchar_t*p=text;p&&*p;++p)out.push_back(*p<128?static_cast<char>(*p):'?');
    return out;
}
void closeHandle(HINTERNET handle)noexcept{if(handle)WinHttpCloseHandle(handle);}
}

struct NowPlayingHttp::Impl:std::enable_shared_from_this<Impl>{
    struct Connection{std::wstring host;INTERNET_PORT port{};HINTERNET handle{};};
    // One request: the WinHTTP context holds it until HANDLE_CLOSING.
    struct Call final:NowPlayingWeb::Task{
        static constexpr DWORD chunk=64*1024;
        std::weak_ptr<Impl>owner;NowPlayingWeb::Kind kind{};std::size_t limit{};
        std::mutex mutex;NowPlayingWeb::Done done;HINTERNET handle{};bool finished{};
        std::shared_ptr<std::vector<std::uint8_t>>body;std::unique_ptr<std::uint8_t[]>buffer;
        void cancel()noexcept override{finish(nullptr);}
        // At most once: the handle first (a fully read response returns its
        // connection to the session's pool before anyone starts the next
        // lookup), then the session's in-flight count (a released session
        // closes), then the completion.
        void finish(modules::NowPlayingBody result)noexcept{
            NowPlayingWeb::Done callback;HINTERNET closing{};
            {std::lock_guard lock(mutex);if(finished)return;finished=true;callback=std::move(done);done=nullptr;closing=handle;handle=nullptr;}
            closeHandle(closing);
            if(const auto session=owner.lock())session->ended();
            if(callback)try{callback(std::move(result));}catch(...){}
        }
        bool live(){std::lock_guard lock(mutex);return !finished;}
        void complete()noexcept;
        // HANDLE_CLOSING: the handle is already going away (never closed twice).
        void closed()noexcept{{std::lock_guard lock(mutex);handle=nullptr;}finish(nullptr);}
    };
    Options options;
    mutable std::mutex mutex;HINTERNET session{};std::vector<Connection>connections;std::size_t inFlight{};bool retained{};Stats counts;
    explicit Impl(Options o):options(std::move(o)){connections.reserve(8);}
    ~Impl(){closeAll();}

    static void CALLBACK callback(HINTERNET handle,DWORD_PTR value,DWORD status,LPVOID info,DWORD length){
        if(!value)return;
        auto*holder=reinterpret_cast<std::shared_ptr<Call>*>(value);
        if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){
            const auto call=std::move(*holder);delete holder;
            if(call)call->closed(); // no completion after the handle went away
            return;
        }
        const auto call=*holder;
        if(!call||!call->live())return;
        switch(status){
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            if(!WinHttpReceiveResponse(handle,nullptr))call->finish(nullptr);
            break;
        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:{
            DWORD code{},size=sizeof code;
            if(!WinHttpQueryHeaders(handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&size,WINHTTP_NO_HEADER_INDEX)){call->finish(nullptr);break;}
            std::optional<std::uint64_t>declared;
            {
                wchar_t buffer[32]{};DWORD bytes=sizeof buffer;
                if(WinHttpQueryHeaders(handle,WINHTTP_QUERY_CONTENT_LENGTH,WINHTTP_HEADER_NAME_BY_INDEX,buffer,&bytes,WINHTTP_NO_HEADER_INDEX)){
                    wchar_t*end{};const auto v=wcstoull(buffer,&end,10);if(end!=buffer)declared=v;
                }
            }
            std::string media;
            {
                wchar_t buffer[256]{};DWORD bytes=sizeof buffer;
                if(WinHttpQueryHeaders(handle,WINHTTP_QUERY_CONTENT_TYPE,WINHTTP_HEADER_NAME_BY_INDEX,buffer,&bytes,WINHTTP_NO_HEADER_INDEX))media=nowPlayingWebMediaType(narrow(buffer));
            }
            if(!nowPlayingWebAccepts(call->kind,static_cast<int>(code),media,declared)){call->finish(nullptr);break;}
            call->body=std::make_shared<std::vector<std::uint8_t>>();
            call->body->reserve(static_cast<std::size_t>(std::min<std::uint64_t>(declared.value_or(Call::chunk),call->limit)));
            if(!WinHttpQueryDataAvailable(handle,nullptr))call->finish(nullptr);
            break;
        }
        case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:{
            const DWORD available=info?*static_cast<DWORD*>(info):0;
            if(!available){call->complete();break;}
            if(!call->buffer)call->buffer=std::make_unique<std::uint8_t[]>(Call::chunk);
            if(!WinHttpReadData(handle,call->buffer.get(),std::min(available,Call::chunk),nullptr))call->finish(nullptr);
            break;
        }
        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:{
            if(!length){call->complete();break;}
            // The streamed bound holds even when Content-Length understated it.
            if(length>call->limit-call->body->size()){call->finish(nullptr);break;}
            const auto*bytes=static_cast<const std::uint8_t*>(info);
            call->body->insert(call->body->end(),bytes,bytes+length);
            if(!WinHttpQueryDataAvailable(handle,nullptr))call->finish(nullptr);
            break;
        }
        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            call->finish(nullptr);
            break;
        default:break;
        }
    }
    HINTERNET openSession(){
        const DWORD access=options.directOnly?WINHTTP_ACCESS_TYPE_NO_PROXY:WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
        HINTERNET handle=WinHttpOpen(options.agent.c_str(),access,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC);
        if(!handle&&!options.directOnly)handle=WinHttpOpen(options.agent.c_str(),WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC);
        if(!handle)return nullptr;
        DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols|=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        if(!WinHttpSetOption(handle,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof protocols)){
            protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;WinHttpSetOption(handle,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof protocols);
        }
        if(WinHttpSetStatusCallback(handle,callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK){
            WinHttpCloseHandle(handle);return nullptr;
        }
        // The source 10 s resource bound (NowPlayingWeb also expires it).
        WinHttpSetTimeouts(handle,10000,10000,10000,10000);
        return handle;
    }
    // Takes the session and its connections out (under the mutex) for closing.
    std::vector<HINTERNET>detach(){
        std::vector<HINTERNET>out;
        for(const auto&c:connections)out.push_back(c.handle);
        connections.clear();
        if(session){out.push_back(session);session=nullptr;++counts.sessionsClosed;}
        return out;
    }
    void closeAll(){
        std::vector<HINTERNET>handles;
        {std::lock_guard lock(mutex);handles=detach();}
        for(const auto h:handles)closeHandle(h);
    }
    void ended(){
        std::vector<HINTERNET>handles;
        {std::lock_guard lock(mutex);if(inFlight)--inFlight;if(!retained&&!inFlight)handles=detach();}
        for(const auto h:handles)closeHandle(h);
    }
    std::shared_ptr<NowPlayingWeb::Task>start(const NowPlayingWeb::Request&request,NowPlayingWeb::Done done){
        if(!done)return nullptr;
        auto url=widen(request.url);if(url.empty())return nullptr;
        URL_COMPONENTS parts{};parts.dwStructSize=sizeof parts;parts.dwSchemeLength=parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=static_cast<DWORD>(-1);
        if(!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&parts))return nullptr;
        const bool https=parts.nScheme==INTERNET_SCHEME_HTTPS;
        if(options.secure?!https:(parts.nScheme!=INTERNET_SCHEME_HTTP&&!https))return nullptr;
        if(parts.dwUserNameLength||parts.dwPasswordLength)return nullptr;
        std::wstring host(parts.lpszHostName,parts.dwHostNameLength);INTERNET_PORT port=parts.nPort;
        std::wstring target(parts.lpszUrlPath?std::wstring(parts.lpszUrlPath,parts.dwUrlPathLength):std::wstring());
        if(target.empty())target=L"/";
        if(parts.lpszExtraInfo)target.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
        // A fragment never reaches the wire (the allow-lists refuse them anyway).
        if(const auto hash=target.find(L'#');hash!=std::wstring::npos)target.resize(hash);
        if(options.endpoint)if(const auto endpoint=options.endpoint(host)){host=endpoint->first;port=endpoint->second;}
        auto call=std::make_shared<Call>();
        call->owner=weak_from_this();call->kind=request.kind;call->limit=nowPlayingWebLimit(request.kind);call->done=std::move(done);
        HINTERNET handle{};
        {
            std::lock_guard lock(mutex);
            if(!session){session=openSession();if(!session)return nullptr;++counts.sessionsOpened;}
            auto connection=std::find_if(connections.begin(),connections.end(),[&](const Connection&c){return c.port==port&&_wcsicmp(c.host.c_str(),host.c_str())==0;});
            if(connection==connections.end()){
                const HINTERNET connect=WinHttpConnect(session,host.c_str(),port,0);
                if(!connect)return nullptr;
                if(connections.size()>=8){WinHttpCloseHandle(connections.front().handle);connections.erase(connections.begin());}
                connections.push_back({host,port,connect});connection=connections.end()-1;++counts.connects;
            }
            const DWORD flags=(https?WINHTTP_FLAG_SECURE:0)|WINHTTP_FLAG_ESCAPE_DISABLE|WINHTTP_FLAG_ESCAPE_DISABLE_QUERY;
            handle=WinHttpOpenRequest(connection->handle,L"GET",target.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,flags);
            if(!handle)return nullptr;
            ++inFlight;++counts.requests;
        }
        DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(handle,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof redirect);
        DWORD disabled=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_AUTHENTICATION;WinHttpSetOption(handle,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof disabled);
        DWORD logon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;WinHttpSetOption(handle,WINHTTP_OPTION_AUTOLOGON_POLICY,&logon,sizeof logon);
        DWORD decompression=WINHTTP_DECOMPRESSION_FLAG_ALL;WinHttpSetOption(handle,WINHTTP_OPTION_DECOMPRESSION,&decompression,sizeof decompression);
#ifdef WINHTTP_PROTOCOL_FLAG_HTTP2
        DWORD protocol=WINHTTP_PROTOCOL_FLAG_HTTP2;WinHttpSetOption(handle,WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL,&protocol,sizeof protocol);
#endif
        WinHttpSetTimeouts(handle,10000,10000,10000,10000);
        std::wstring headers;
        if(const auto referer=nowPlayingWebReferer(request.url))headers=L"Referer: "+widen(*referer)+L"\r\n";
        // The handle owns this holder from here: HANDLE_CLOSING deletes it.
        auto*holder=new std::shared_ptr<Call>(call);
        if(!WinHttpSetOption(handle,WINHTTP_OPTION_CONTEXT_VALUE,&holder,sizeof holder)){
            delete holder;
            {std::lock_guard lock(call->mutex);call->done=nullptr;call->finished=true;}
            WinHttpCloseHandle(handle);ended();return nullptr;
        }
        {std::lock_guard lock(call->mutex);call->handle=handle;}
        if(!WinHttpSendRequest(handle,headers.empty()?WINHTTP_NO_ADDITIONAL_HEADERS:headers.c_str(),headers.empty()?0:static_cast<DWORD>(-1L),
               WINHTTP_NO_REQUEST_DATA,0,0,reinterpret_cast<DWORD_PTR>(holder))){
            // Not started: the caller completes it (no Done from here).
            {std::lock_guard lock(call->mutex);call->done=nullptr;}
            call->finish(nullptr);
            return nullptr;
        }
        return call;
    }
};
// End of the body: the accepted bytes (an empty cover is no cover; an empty
// lyric document is delivered empty, as the source Data()).
void NowPlayingHttp::Impl::Call::complete()noexcept{
    modules::NowPlayingBody result;
    if(body&&(kind==NowPlayingWeb::Kind::lyrics||!body->empty()))result=body;
    finish(std::move(result));
}

NowPlayingHttp::NowPlayingHttp():NowPlayingHttp(Options{}){}
NowPlayingHttp::NowPlayingHttp(Options options):impl_(std::make_shared<Impl>(std::move(options))){}
NowPlayingHttp::~NowPlayingHttp(){impl_->closeAll();}
NowPlayingWeb::Transport NowPlayingHttp::transport(){
    const std::weak_ptr<Impl>weak=impl_;
    return [weak](const NowPlayingWeb::Request&request,NowPlayingWeb::Done done)->std::shared_ptr<NowPlayingWeb::Task>{
        const auto self=weak.lock();
        return self?self->start(request,std::move(done)):nullptr;
    };
}
void NowPlayingHttp::setRetained(bool value){
    auto i=impl_;std::vector<HINTERNET>handles;
    {std::lock_guard lock(i->mutex);i->retained=value;if(!value&&!i->inFlight)handles=i->detach();}
    for(const auto h:handles)closeHandle(h);
}
NowPlayingHttp::Stats NowPlayingHttp::stats()const{
    std::lock_guard lock(impl_->mutex);
    auto s=impl_->counts;s.inFlight=impl_->inFlight;s.open=impl_->session!=nullptr;s.retained=impl_->retained;
    return s;
}
NowPlayingWeb::Transport windowsNowPlayingTransport(){
    auto http=std::make_shared<NowPlayingHttp>();
    http->setRetained(true);
    auto inner=http->transport();
    // The transport keeps its session alive for its own lifetime.
    return [http,inner](const NowPlayingWeb::Request&request,NowPlayingWeb::Done done){return inner(request,std::move(done));};
}
}
#endif
