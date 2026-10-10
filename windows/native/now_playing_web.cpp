#include "native/now_playing_web.hpp"
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

struct NowPlayingWeb::Impl:std::enable_shared_from_this<Impl>{
    struct Pending {std::uint64_t id{};Kind kind{};std::shared_ptr<Task>task;std::function<void(modules::NowPlayingBody)>completion;double deadline{};};
    Transport transport;std::function<void()>notify;std::function<double()>now;double timeout;const std::thread::id owner=std::this_thread::get_id();
    std::vector<Pending>pending;std::uint64_t next{1};Stats counts;bool alive{true};
    std::mutex mutex;std::vector<std::pair<std::uint64_t,modules::NowPlayingBody>>finished;bool notified{};
    Impl(Transport t,std::function<void()>n,std::function<double()>c,double bound):transport(std::move(t)),notify(std::move(n)),now(std::move(c)),timeout(bound){
        need(bool(transport)&&bool(notify)&&bool(now)&&std::isfinite(timeout)&&timeout>0&&timeout<=60,"Now Playing web needs transport, owner wake, clock and bounded timeout");
        pending.reserve(maximumInFlight);
    }
    void check()const{if(owner!=std::this_thread::get_id())throw std::logic_error("Now Playing web belongs to its owner thread");}
    // Any thread. Coalesces wakes exactly like the service's message slot.
    void post(std::uint64_t id,modules::NowPlayingBody body){
        bool wake{};{std::lock_guard lock(mutex);finished.emplace_back(id,std::move(body));wake=!notified;notified=true;}
        if(wake)try{notify();}catch(...){std::lock_guard lock(mutex);notified=false;}
    }
    std::optional<Pending>take(std::uint64_t id){
        const auto found=std::find_if(pending.begin(),pending.end(),[&](const auto&p){return p.id==id;});
        if(found==pending.end())return {};auto value=std::move(*found);pending.erase(found);return value;
    }
    void cancel(std::uint64_t id){if(auto value=take(id)){++counts.cancelled;if(value->task)value->task->cancel();}}
    static std::function<void()>fetch(const std::shared_ptr<Impl>&i,Request request,std::function<void(modules::NowPlayingBody)>completion){
        i->check();need(bool(completion),"Now Playing web fetch needs a completion");
        const bool allowed=request.kind==Kind::lyrics?modules::nowPlayingLyricsURLAllowed(request.url):modules::nowPlayingArtworkURLAllowed(request.url);
        if(!i->alive||!allowed||i->pending.size()>=maximumInFlight){++i->counts.rejected;completion(nullptr);return []{};}
        const auto time=i->now();need(std::isfinite(time),"Now Playing web clock must be finite");
        const auto id=i->next++;const std::weak_ptr<Impl>weak=i;
        std::shared_ptr<Task>task;
        try{task=i->transport(request,[weak,id](modules::NowPlayingBody body){if(auto self=weak.lock())self->post(id,std::move(body));});}catch(...){task.reset();}
        if(!task){++i->counts.rejected;completion(nullptr);return []{};}
        ++i->counts.started;i->pending.push_back({id,request.kind,std::move(task),std::move(completion),time+i->timeout});
        return [weak,id]{if(auto self=weak.lock()){self->check();self->cancel(id);}};
    }
};
NowPlayingWeb::NowPlayingWeb(Transport transport,std::function<void()>notify,std::function<double()>now,double timeout)
    :impl_(std::make_shared<Impl>(std::move(transport),std::move(notify),std::move(now),timeout)){}
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
    auto i=impl_;i->check();std::vector<std::pair<std::uint64_t,modules::NowPlayingBody>>done;
    {std::lock_guard lock(i->mutex);done.swap(i->finished);i->notified=false;}
    bool ran{};
    for(auto&[id,body]:done){
        auto value=i->take(id);if(!value)continue; // cancelled or timed out: never delivered
        if(body&&body->size()>nowPlayingWebLimit(value->kind))body.reset();
        if(body)++i->counts.completed;else ++i->counts.failed;
        ran=true;value->completion(std::move(body));
    }
    const auto time=i->now();
    for(;;){
        const auto expired=std::find_if(i->pending.begin(),i->pending.end(),[&](const auto&p){return p.deadline<=time;});
        if(expired==i->pending.end())break;
        auto value=std::move(*expired);i->pending.erase(expired);++i->counts.timedOut;
        if(value.task)value.task->cancel();ran=true;value.completion(nullptr);
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
    for(auto&value:values){++i->counts.cancelled;if(value.task)value.task->cancel();}
}
NowPlayingWeb::Stats NowPlayingWeb::stats()const{impl_->check();auto s=impl_->counts;s.inFlight=impl_->pending.size();return s;}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "native/now_playing_web_winrt.hpp"
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Web.Http.Headers.h>

namespace endfield::native {
namespace {
namespace http=winrt::Windows::Web::Http;
namespace filters=winrt::Windows::Web::Http::Filters;
namespace foundation=winrt::Windows::Foundation;
namespace streams=winrt::Windows::Storage::Streams;
// The OS thread pool joins the implicit MTA; keep it alive for every request.
struct MtaLease {
    CO_MTA_USAGE_COOKIE cookie{};HRESULT status=CoIncrementMTAUsage(&cookie);
    ~MtaLease(){if(SUCCEEDED(status)&&cookie)CoDecrementMTAUsage(cookie);}
    MtaLease()=default;MtaLease(const MtaLease&)=delete;MtaLease&operator=(const MtaLease&)=delete;
};
struct HttpTask final:NowPlayingWeb::Task {
    std::shared_ptr<MtaLease>mta;NowPlayingWeb::Done done;std::mutex mutex;foundation::IAsyncInfo operation{nullptr};bool cancelled{};std::atomic<bool>finished{};
    void cancel()noexcept override{
        foundation::IAsyncInfo current{nullptr};{std::lock_guard lock(mutex);cancelled=true;current=operation;}
        if(current)try{current.Cancel();}catch(...){}
    }
    void track(const foundation::IAsyncInfo&value){
        bool stop{};{std::lock_guard lock(mutex);stop=cancelled;if(!stop)operation=value;}
        if(stop){try{value.Cancel();}catch(...){}throw winrt::hresult_canceled();}
    }
    void untrack(){std::lock_guard lock(mutex);operation=nullptr;}
    bool isCancelled(){std::lock_guard lock(mutex);return cancelled;}
    void complete(modules::NowPlayingBody body){if(finished.exchange(true))return;auto callback=std::move(done);done=nullptr;if(callback)try{callback(std::move(body));}catch(...){}}
};
using FilterFactory=std::function<filters::IHttpFilter()>;
winrt::fire_and_forget download(std::shared_ptr<HttpTask>task,NowPlayingWeb::Request request,FilterFactory factory){
    modules::NowPlayingBody result;
    try{
        // Never run WinRT networking on the caller's (UI/STA) thread.
        co_await winrt::resume_background();
        if(task->isCancelled())throw winrt::hresult_canceled();
        const auto filter=factory();if(!filter)throw winrt::hresult_error(E_POINTER);
        http::HttpClient client(filter);
        const foundation::Uri uri(winrt::to_hstring(request.url));
        http::HttpRequestMessage message(http::HttpMethod::Get(),uri);
        message.Headers().UserAgent().TryParseAdd(L"EndfieldHUD/1.0");
        if(uri.Host()==L"music.163.com")message.Headers().Referer(foundation::Uri(L"https://music.163.com/"));
        auto send=client.SendRequestAsync(message,http::HttpCompletionOption::ResponseHeadersRead);
        task->track(send.as<foundation::IAsyncInfo>());const auto response=co_await send;task->untrack();
        const auto content=response.Content();if(!content)throw winrt::hresult_error(E_FAIL);
        const auto headers=content.Headers();std::string media;if(const auto type=headers.ContentType())media=winrt::to_string(type.MediaType());
        std::optional<std::uint64_t>length;if(const auto value=headers.ContentLength())length=value.Value();
        if(!nowPlayingWebAccepts(request.kind,static_cast<int>(response.StatusCode()),media,length))throw winrt::hresult_error(E_FAIL);
        auto open=content.ReadAsInputStreamAsync();task->track(open.as<foundation::IAsyncInfo>());const auto stream=co_await open;task->untrack();
        const auto limit=nowPlayingWebLimit(request.kind);auto bytes=std::make_shared<std::vector<std::uint8_t>>();
        bytes->reserve(static_cast<std::size_t>(std::min<std::uint64_t>(length.value_or(65536),limit)));
        streams::Buffer buffer(65536);
        for(;;){
            auto read=stream.ReadAsync(buffer,buffer.Capacity(),streams::InputStreamOptions::Partial);
            task->track(read.as<foundation::IAsyncInfo>());const auto chunk=co_await read;task->untrack();
            const auto count=chunk.Length();if(!count)break;
            if(count>limit-bytes->size())throw winrt::hresult_error(E_FAIL); // streamed bound, never buffered past it
            bytes->insert(bytes->end(),chunk.data(),chunk.data()+count);
        }
        stream.Close();
        if(request.kind==NowPlayingWeb::Kind::lyrics||!bytes->empty())result=std::move(bytes);
    }catch(...){result.reset();}
    task->untrack();task->complete(std::move(result));
}
}
filters::IHttpFilter nowPlayingProtocolFilter(){
    filters::HttpBaseProtocolFilter filter;
    filter.AllowAutoRedirect(false);filter.AllowUI(false);
    filter.CacheControl().ReadBehavior(filters::HttpCacheReadBehavior::NoCache);
    filter.CacheControl().WriteBehavior(filters::HttpCacheWriteBehavior::NoCache);
    filter.CookieUsageBehavior(filters::HttpCookieUsageBehavior::NoCookies);
    return filter;
}
NowPlayingWeb::Transport windowsNowPlayingTransport(FilterFactory factory){
    need(bool(factory),"Now Playing transport needs a protocol filter factory");
    return [factory=std::move(factory)](const NowPlayingWeb::Request&request,NowPlayingWeb::Done done)->std::shared_ptr<NowPlayingWeb::Task>{
        auto task=std::make_shared<HttpTask>();task->mta=std::make_shared<MtaLease>();
        if(FAILED(task->mta->status))return nullptr;
        task->done=std::move(done);download(task,request,factory);return task;
    };
}
NowPlayingWeb::Transport windowsNowPlayingTransport(){return windowsNowPlayingTransport(nowPlayingProtocolFilter);}
}
#endif
