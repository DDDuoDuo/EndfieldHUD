// The Windows.Web.Http transport of Now Playing downloads, driven through an
// in-process IHttpFilter: request method/URI/headers, the source acceptance
// rules (status, media type, Content-Length), the streamed size cap,
// cancellation, the thread-pool contract and owner-thread delivery through
// NowPlayingWeb. The production protocol filter is only constructed and
// inspected. No socket is opened and no network, cookie store or cache is used.
#include "native/now_playing_web_winrt.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

namespace n=endfield::native;namespace m=endfield::modules;
namespace http=winrt::Windows::Web::Http;namespace filters=http::Filters;
namespace foundation=winrt::Windows::Foundation;namespace streams=winrt::Windows::Storage::Streams;
namespace {
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}
using Kind=n::NowPlayingWeb::Kind;

struct Reply {
    std::uint32_t status{200};std::wstring media{L"application/json"},charset;
    std::optional<std::uint64_t>length;std::string body;bool hang{};
};
struct Seen {
    std::mutex mutex;std::vector<std::wstring>uris,methods,agents,referers;std::vector<std::thread::id>threads;unsigned cancels{};
    std::size_t requests(){std::lock_guard lock(mutex);return uris.size();}
};
// Answers every request from memory. A hanging reply waits for cancellation
// (or a generous bound) and never touches a socket.
struct Filter:winrt::implements<Filter,filters::IHttpFilter,foundation::IClosable> {
    Reply reply;std::shared_ptr<Seen>seen;winrt::handle released{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    Filter(Reply r,std::shared_ptr<Seen>s):reply(std::move(r)),seen(std::move(s)){}
    foundation::IAsyncOperationWithProgress<http::HttpResponseMessage,http::HttpProgress>SendRequestAsync(http::HttpRequestMessage request){
        auto self=get_strong();
        {std::lock_guard lock(seen->mutex);
            seen->uris.emplace_back(request.RequestUri().AbsoluteUri());seen->methods.emplace_back(request.Method().Method());
            seen->agents.emplace_back(request.Headers().UserAgent().ToString());
            const auto referer=request.Headers().Referer();seen->referers.emplace_back(referer?std::wstring(referer.AbsoluteUri()):std::wstring());
            seen->threads.push_back(std::this_thread::get_id());}
        if(reply.hang){
            auto token=co_await winrt::get_cancellation_token();
            token.callback([self]{{std::lock_guard lock(self->seen->mutex);++self->seen->cancels;}SetEvent(self->released.get());});
            co_await winrt::resume_on_signal(released.get(),std::chrono::seconds(20));
        }
        http::HttpResponseMessage response(static_cast<http::HttpStatusCode>(reply.status));
        streams::Buffer buffer(static_cast<std::uint32_t>(std::max<std::size_t>(1,reply.body.size())));
        if(!reply.body.empty())std::memcpy(buffer.data(),reply.body.data(),reply.body.size());
        buffer.Length(static_cast<std::uint32_t>(reply.body.size()));
        http::HttpBufferContent content(buffer);
        if(!reply.media.empty()){http::Headers::HttpMediaTypeHeaderValue type{winrt::hstring(reply.media)};if(!reply.charset.empty())type.CharSet(winrt::hstring(reply.charset));content.Headers().ContentType(type);}
        if(reply.length)content.Headers().ContentLength(*reply.length);
        response.Content(content);response.RequestMessage(request);
        co_return response;
    }
    void Close(){}
};
struct Outcome {bool done{};m::NowPlayingBody body;std::thread::id thread;unsigned calls{};};
// Shared with transport callbacks so a late callback never touches a
// destroyed test frame.
struct Signal {
    winrt::handle event{CreateEventW(nullptr,FALSE,FALSE,nullptr)};std::mutex mutex;Outcome outcome;unsigned posts{};
    void post(){{std::lock_guard lock(mutex);++posts;}SetEvent(event.get());}
    bool wait(double seconds){return WaitForSingleObject(event.get(),static_cast<DWORD>(seconds*1000))==WAIT_OBJECT_0;}
    Outcome snapshot(){std::lock_guard lock(mutex);return outcome;}
};
struct Run {
    std::shared_ptr<Seen>seen=std::make_shared<Seen>();std::shared_ptr<Signal>signal=std::make_shared<Signal>();
    std::shared_ptr<n::NowPlayingWeb::Task>task;
    Run(Reply reply,n::NowPlayingWeb::Request request){
        const auto filter=winrt::make<Filter>(std::move(reply),seen);
        const auto transport=n::windowsNowPlayingTransport([filter]{return filter;});
        task=transport(request,[signal=signal](m::NowPlayingBody body){
            {std::lock_guard lock(signal->mutex);++signal->outcome.calls;signal->outcome.done=true;signal->outcome.body=std::move(body);signal->outcome.thread=std::this_thread::get_id();}
            SetEvent(signal->event.get());});
        check(bool(task),"Transport accepts an allowed request");
    }
    Outcome wait(double seconds=10){check(signal->wait(seconds),"Transport completes within its bound");return signal->snapshot();}
};
std::string bytes(const m::NowPlayingBody&body){return body?std::string(body->begin(),body->end()):std::string("<null>");}
Outcome fetch(Reply reply,std::string url,Kind kind=Kind::lyrics){Run run(std::move(reply),{std::move(url),kind});return run.wait();}
const std::string lrclib="https://lrclib.net/api/get?track_name=Synthetic%20Song&artist_name=Example%20Artist";
const std::string netease="https://music.163.com/api/song/lyric?id=1901371647&lv=-1&kv=-1&tv=-1";
const std::string cover="https://p1.music.126.net/AbC==/1099.jpg?param=600y600";

void protocolFilter(){
    const auto filter=n::nowPlayingProtocolFilter().as<filters::HttpBaseProtocolFilter>();
    check(!filter.AllowAutoRedirect()&&!filter.AllowUI(),"Production filter refuses redirects and never shows UI");
    check(filter.CookieUsageBehavior()==filters::HttpCookieUsageBehavior::NoCookies,"Production filter neither sends nor stores cookies");
    check(filter.CacheControl().ReadBehavior()==filters::HttpCacheReadBehavior::NoCache&&filter.CacheControl().WriteBehavior()==filters::HttpCacheWriteBehavior::NoCache,"Production filter neither reads nor writes the HTTP cache");
    bool refused{};try{(void)n::windowsNowPlayingTransport(std::function<filters::IHttpFilter()>{});}catch(const std::invalid_argument&){refused=true;}
    check(refused,"A transport needs a protocol filter factory");
}
void requests(){
    const auto caller=std::this_thread::get_id();
    Run run({200,L"application/json",{},{},R"({"syncedLyrics":"[00:01]x"})"},{lrclib,Kind::lyrics});const auto lyrics=run.wait();
    check(lyrics.calls==1&&bytes(lyrics.body)==R"({"syncedLyrics":"[00:01]x"})","Accepted JSON body is delivered once, byte for byte");
    check(run.seen->requests()==1&&run.seen->methods[0]==L"GET"&&run.seen->uris[0]==std::wstring(winrt::to_hstring(lrclib)),"Exactly one GET of the exact source URL");
    check(run.seen->agents[0]==L"EndfieldHUD/1.0"&&run.seen->referers[0].empty(),"LRCLIB request carries the source User-Agent and no Referer");
    check(run.seen->threads[0]!=caller&&lyrics.thread!=caller,"Request and completion run on the OS thread pool, never the caller's thread");
    Run catalog({200,L"application/json",{},{},R"({"code":200})"},{netease,Kind::lyrics});catalog.wait();
    check(catalog.seen->referers[0]==L"https://music.163.com/"&&catalog.seen->agents[0]==L"EndfieldHUD/1.0","NetEase request carries the source Referer");
}
void acceptance(){
    check(bytes(fetch({200,L"text/plain",L"utf-8",{},"[00:01]x"},lrclib).body)=="[00:01]x","text/plain (with parameters) is an accepted lyric media type");
    const auto empty=fetch({200,L"application/json",{},{},""},lrclib).body;check(empty&&empty->empty(),"An empty accepted lyric body is delivered empty (source Data())");
    check(!fetch({200,L"text/html",{},{},"<html>"},lrclib).body,"Other lyric media types are refused");
    check(!fetch({404,L"application/json",{},{},"{}"},lrclib).body,"Lyric status must be exactly 200 (404)");
    check(!fetch({201,L"application/json",{},{},"{}"},lrclib).body,"Lyric status must be exactly 200 (201)");
    check(!fetch({302,L"application/json",{},{},"{}"},lrclib).body,"A redirect response is never followed or accepted");
    check(!fetch({200,L"application/json",{},std::uint64_t(1024*1024+1),"{}"},lrclib).body,"Declared lyric Content-Length above 1 MiB is refused before reading");
    const std::string limit(1024*1024,'a');
    check(fetch({200,L"application/json",{},{},limit},lrclib).body->size()==limit.size(),"A lyric body of exactly 1 MiB is accepted");
    check(!fetch({200,L"application/json",{},{},limit+"a"},lrclib).body,"A lyric body above 1 MiB is refused");
    check(!fetch({200,L"application/json",{},std::uint64_t(16),limit+"a"},lrclib).body,"The streamed cap holds even when Content-Length understates the body");
    check(bytes(fetch({200,L"image/jpeg",{},{},"\xff\xd8\xff"},cover,Kind::artwork).body)=="\xff\xd8\xff","Allow-listed cover image is delivered");
    check(bytes(fetch({206,L"IMAGE/PNG",{},{},"png"},cover,Kind::artwork).body)=="png","Any 2xx image/* cover is accepted (case-insensitive media type)");
    check(!fetch({204,L"image/jpeg",{},{},""},cover,Kind::artwork).body,"An empty cover is no cover");
    check(!fetch({200,L"application/octet-stream",{},{},"x"},cover,Kind::artwork).body,"Non-image cover media types are refused");
    check(!fetch({404,L"image/jpeg",{},{},"x"},cover,Kind::artwork).body,"Cover status must be 2xx");
    const std::string eight(8*1024*1024,'b');
    check(fetch({200,L"image/jpeg",{},{},eight},cover,Kind::artwork).body->size()==eight.size(),"A cover of exactly 8 MiB is accepted");
    check(!fetch({200,L"image/jpeg",{},{},eight+"b"},cover,Kind::artwork).body,"A cover above 8 MiB is refused");
}
void cancellation(){
    Run hanging({200,L"application/json",{},{},"{}",true},{lrclib,Kind::lyrics});
    for(int n=0;n<1000&&!hanging.seen->requests();++n)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(hanging.seen->requests()==1,"The hanging request reached the filter");
    const auto started=std::chrono::steady_clock::now();hanging.task->cancel();
    const auto result=hanging.wait(15);const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    check(!result.body&&result.calls==1,"Cancellation completes exactly once with no body");
    unsigned observed{};{std::lock_guard lock(hanging.seen->mutex);observed=hanging.seen->cancels;}
    std::cout<<"cancel: completed after "<<elapsed<<" s; filter observed cancellation "<<observed<<" time(s)\n";
    hanging.task->cancel();Sleep(50);check(hanging.signal->snapshot().calls==1,"Repeated cancellation never completes twice");
    Run early({200,L"application/json",{},{},"{}"},{lrclib,Kind::lyrics});early.task->cancel();
    check(!early.wait().body,"Cancellation before the request starts delivers no body");
}
void owner(){
    // NowPlayingWeb over the WinRT transport: completions are posted to one
    // owner wake and delivered only inside drain() on the owner thread.
    const auto seen=std::make_shared<Seen>();const auto filter=winrt::make<Filter>(Reply{200,L"application/json",{},{},R"({"ok":1})"},seen);
    const auto wake=std::make_shared<Signal>();const auto origin=std::chrono::steady_clock::now();
    const auto clock=[origin]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-origin).count();};
    n::NowPlayingWeb web(n::windowsNowPlayingTransport([filter]{return filter;}),[wake]{wake->post();},clock);
    std::optional<std::string>delivered;std::thread::id thread;
    web.fetch({lrclib,Kind::lyrics},[&](m::NowPlayingBody body){delivered=bytes(body);thread=std::this_thread::get_id();});
    check(!delivered&&web.nextWakeTime().has_value(),"Nothing is delivered before the owner drains; the 10 s bound is exported");
    check(wake->wait(10),"Transport completion posts an owner wake");
    check(!delivered&&web.drain()&&delivered==std::string(R"({"ok":1})")&&thread==std::this_thread::get_id(),"Owner drain delivers the body on the owner thread");
    check(!web.nextWakeTime()&&web.stats().completed==1&&web.stats().inFlight==0&&wake->snapshot().calls==0,"Completed work leaves no deadline");
    { std::lock_guard lock(wake->mutex);check(wake->posts==1,"One completion posts exactly one coalesced wake"); }
    const auto blocked=winrt::make<Filter>(Reply{200,L"application/json",{},{},"{}",true},seen);
    const auto late=std::make_shared<Signal>();
    n::NowPlayingWeb slow(n::windowsNowPlayingTransport([blocked]{return blocked;}),[late]{late->post();},clock);
    bool ran{};const auto cancel=slow.fetch({lrclib,Kind::lyrics},[&](m::NowPlayingBody){ran=true;});cancel();
    check(slow.stats().cancelled==1&&slow.stats().inFlight==0&&!slow.nextWakeTime(),"Owner cancellation retires the request and its deadline at once");
    late->wait(25);slow.drain();
    check(!ran,"A cancelled owner request never delivers a late transport completion");
}
}
int main(){
    try{
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        protocolFilter();requests();acceptance();cancellation();owner();
        std::cout<<"Now Playing WinRT transport: "<<checks<<" checks passed (in-process filter, no network)\n";return 0;
    }catch(const winrt::hresult_error&e){std::cerr<<"Now Playing WinRT transport failed after "<<checks<<": HRESULT "<<std::hex<<static_cast<std::uint32_t>(e.code().value)<<" "<<winrt::to_string(e.message())<<'\n';return 1;}
    catch(const std::exception&e){std::cerr<<"Now Playing WinRT transport failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
