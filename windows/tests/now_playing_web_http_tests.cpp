// The WinHTTP transport of Now Playing downloads against an in-process HTTP/1.1
// server on 127.0.0.1 (the test's own socket; nothing leaves the machine):
// request line and headers, the source acceptance rules (status, media type,
// Content-Length), the streamed size caps, redirects never followed, ONE
// kept-alive session whose pooled connection serves consecutive lookups, the
// session lifetime (open while retained, closed once released and idle),
// cancellation and owner-thread delivery through NowPlayingWeb. The production
// options are only constructed: https only, so the plain-http test URLs are
// refused before any socket is opened.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include "native/now_playing_web.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace n=endfield::native;namespace m=endfield::modules;using Kind=n::NowPlayingWeb::Kind;
namespace {
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}

struct Reply {
    int status{200};std::string media{"application/json"};std::string body;
    bool length{true};       // Content-Length (else Connection: close after the body)
    std::string location;    // a Location header (redirect replies)
    bool hang{};             // answer nothing until the client goes away
};
struct Seen {std::string method,target,agent,referer,cookie,encoding;};
std::string lowered(std::string v){for(auto&c:v)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return v;}

class Server {
public:
    Server(){
        WSADATA data{};check(WSAStartup(MAKEWORD(2,2),&data)==0,"Winsock starts");
        listener_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(listener_!=INVALID_SOCKET,"Loopback listener socket");
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
        check(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof address)==0&&listen(listener_,16)==0,"The listener binds 127.0.0.1 only");
        int size=sizeof address;getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&size);port_=ntohs(address.sin_port);
        acceptor_=std::thread([this]{run();});
    }
    ~Server(){
        closesocket(listener_);acceptor_.join();
        {std::lock_guard lock(mutex_);for(auto&[socket,open]:sockets_)if(open)shutdown(socket,SD_BOTH);}
        for(auto&worker:workers_)worker.join();
        WSACleanup();
    }
    std::uint16_t port()const noexcept{return port_;}
    void reply(std::string path,Reply r){std::lock_guard lock(mutex_);replies_[std::move(path)]=std::move(r);}
    std::vector<Seen>seen(){std::lock_guard lock(mutex_);return seen_;}
    unsigned accepted()const noexcept{return accepted_.load();}
    unsigned closedByClient()const noexcept{return closed_.load();}
    bool waitClosed(unsigned count,int milliseconds){
        for(int k=0;k<milliseconds/10&&closed_.load()<count;++k)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return closed_.load()>=count;
    }
private:
    void run(){
        for(;;){
            const SOCKET s=accept(listener_,nullptr,nullptr);
            if(s==INVALID_SOCKET)return;
            ++accepted_;
            std::lock_guard lock(mutex_);
            sockets_.emplace_back(s,true);
            workers_.emplace_back([this,s]{serve(s);});
        }
    }
    void finish(SOCKET s){
        std::lock_guard lock(mutex_);
        for(auto&[socket,open]:sockets_)if(socket==s&&open){closesocket(s);open=false;}
    }
    static bool sendAll(SOCKET s,const std::string&data){
        std::size_t sent{};
        while(sent<data.size()){const int n=send(s,data.data()+sent,static_cast<int>(std::min<std::size_t>(data.size()-sent,1<<20)),0);if(n<=0)return false;sent+=static_cast<std::size_t>(n);}
        return true;
    }
    void serve(SOCKET s){
        const DWORD timeout=20000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof timeout);
        std::string buffer;
        for(;;){
            std::size_t end;
            while((end=buffer.find("\r\n\r\n"))==std::string::npos){
                char chunk[4096];const int count=recv(s,chunk,sizeof chunk,0);
                if(count<=0){++closed_;finish(s);return;}
                buffer.append(chunk,static_cast<std::size_t>(count));
            }
            const std::string head=buffer.substr(0,end);buffer.erase(0,end+4);
            Seen seen;
            const auto lineEnd=head.find("\r\n");const auto line=head.substr(0,lineEnd);
            const auto space=line.find(' '),second=line.find(' ',space+1);
            seen.method=line.substr(0,space);seen.target=line.substr(space+1,second-space-1);
            for(std::size_t at=lineEnd;at!=std::string::npos&&at+2<head.size();){
                const auto next=head.find("\r\n",at+2);const auto field=head.substr(at+2,next==std::string::npos?std::string::npos:next-at-2);at=next;
                const auto colon=field.find(':');if(colon==std::string::npos)continue;
                auto name=lowered(field.substr(0,colon));auto value=field.substr(colon+1);while(!value.empty()&&value.front()==' ')value.erase(0,1);
                if(name=="user-agent")seen.agent=value;else if(name=="referer")seen.referer=value;else if(name=="cookie")seen.cookie=value;else if(name=="accept-encoding")seen.encoding=value;
            }
            Reply r;
            {
                std::lock_guard lock(mutex_);
                seen_.push_back(seen);
                const auto path=seen.target.substr(0,seen.target.find('?'));
                if(const auto found=replies_.find(path);found!=replies_.end())r=found->second;else r.status=404;
            }
            if(r.hang){
                // Hold the request until the client closes the connection.
                char c;while(recv(s,&c,1,0)>0){}
                ++closed_;finish(s);return;
            }
            std::string out="HTTP/1.1 "+std::to_string(r.status)+" Synthetic\r\n";
            if(!r.media.empty())out+="Content-Type: "+r.media+"\r\n";
            if(!r.location.empty())out+="Location: "+r.location+"\r\n";
            out+=r.length?"Content-Length: "+std::to_string(r.body.size())+"\r\n":std::string("Connection: close\r\n");
            out+="\r\n";out+=r.body;
            if(!sendAll(s,out)||!r.length){shutdown(s,SD_SEND);char c;while(recv(s,&c,1,0)>0){}++closed_;finish(s);return;}
        }
    }
    SOCKET listener_{INVALID_SOCKET};std::uint16_t port_{};std::thread acceptor_;
    std::mutex mutex_;std::vector<std::pair<SOCKET,bool>>sockets_;std::vector<std::thread>workers_;
    std::map<std::string,Reply>replies_;std::vector<Seen>seen_;std::atomic<unsigned>accepted_{},closed_{};
};

struct Outcome {bool done{};m::NowPlayingBody body;std::thread::id thread;unsigned calls{};};
struct Signal {
    std::mutex mutex;std::condition_variable changed;Outcome outcome;
    void set(m::NowPlayingBody body){{std::lock_guard lock(mutex);++outcome.calls;outcome.done=true;outcome.body=std::move(body);outcome.thread=std::this_thread::get_id();}changed.notify_all();}
    bool wait(double seconds){std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::duration<double>(seconds),[&]{return outcome.done;});}
    Outcome snapshot(){std::lock_guard lock(mutex);return outcome;}
};
std::string text(const m::NowPlayingBody&body){return body?std::string(body->begin(),body->end()):std::string("<null>");}

struct Client {
    Server&server;std::unique_ptr<n::NowPlayingHttp>http;
    explicit Client(Server&s):server(s){
        n::NowPlayingHttp::Options o;o.secure=false;o.directOnly=true;const auto port=s.port();
        o.endpoint=[port](std::wstring_view)->std::optional<std::pair<std::wstring,std::uint16_t>>{return std::pair<std::wstring,std::uint16_t>(L"127.0.0.1",port);};
        http=std::make_unique<n::NowPlayingHttp>(std::move(o));http->setRetained(true);
    }
    std::pair<std::shared_ptr<n::NowPlayingWeb::Task>,std::shared_ptr<Signal>>start(std::string url,Kind kind){
        auto signal=std::make_shared<Signal>();
        auto task=http->transport()({std::move(url),kind},[signal](m::NowPlayingBody body){signal->set(std::move(body));});
        return {task,signal};
    }
    Outcome fetch(std::string url,Kind kind=Kind::lyrics){
        auto [task,signal]=start(std::move(url),kind);
        check(bool(task),"The transport starts an allowed request");
        check(signal->wait(15),"The request completes within its bound");
        return signal->snapshot();
    }
};
const std::string lrclib="http://lrclib.net/api/get?track_name=Synthetic%20Song&artist_name=Example%20Artist";
const std::string catalog="http://music.163.com/api/song/lyric?id=1901371647&lv=-1&kv=-1&tv=-1";
const std::string cover="http://p1.music.126.net/AbC==/1099.jpg?param=500y500";

void requests(){
    Server server;Client client(server);
    server.reply("/api/get",{200,"application/json",R"({"syncedLyrics":"[00:01]x"})"});
    server.reply("/api/song/lyric",{200,"application/json",R"({"code":200})"});
    const auto caller=std::this_thread::get_id();
    const auto lyrics=client.fetch(lrclib);
    check(lyrics.calls==1&&text(lyrics.body)==R"({"syncedLyrics":"[00:01]x"})","An accepted JSON body is delivered once, byte for byte");
    const auto seen=server.seen();
    check(seen.size()==1&&seen[0].method=="GET"&&seen[0].target=="/api/get?track_name=Synthetic%20Song&artist_name=Example%20Artist",
          "Exactly one GET of the exact source path and query (no re-escaping)");
    check(seen[0].agent=="EndfieldHUD/1.0"&&seen[0].referer.empty()&&seen[0].cookie.empty(),"The source User-Agent, no Referer and no cookie for LRCLIB");
    check(lowered(seen[0].encoding).find("gzip")!=std::string::npos,"Compressed answers are accepted and decoded by WinHTTP");
    check(lyrics.thread!=caller,"The completion runs on WinHTTP's threads, never the caller's");
    client.fetch(catalog);
    check(server.seen()[1].referer=="https://music.163.com/","The NetEase catalog request carries the source Referer");
    check(n::nowPlayingWebReferer("https://MUSIC.163.com/api/x")=="https://music.163.com/"&&!n::nowPlayingWebReferer("https://music.163.com.example/x")&&
          !n::nowPlayingWebReferer("https://lrclib.net/api/get")&&!n::nowPlayingWebReferer("music.163.com/x"),"The Referer rule matches the catalog host exactly");
    check(n::nowPlayingWebMediaType(" image/jpeg ; q=1")=="image/jpeg"&&n::nowPlayingWebMediaType("text/plain")=="text/plain","Media type parameters are ignored");
}
void acceptance(){
    Server server;Client client(server);
    const auto with=[&](Reply r,Kind kind=Kind::lyrics){server.reply(kind==Kind::lyrics?"/api/get":"/AbC==/1099.jpg",std::move(r));return client.fetch(kind==Kind::lyrics?lrclib:cover,kind).body;};
    check(text(with({200,"text/plain; charset=utf-8","[00:01]x"}))=="[00:01]x","text/plain (with parameters) is an accepted lyric media type");
    const auto empty=with({200,"application/json",""});check(empty&&empty->empty(),"An empty accepted lyric body is delivered empty (source Data())");
    check(!with({200,"text/html","<html>"}),"Other lyric media types are refused");
    check(!with({404,"application/json","{}"}),"Lyric status must be exactly 200 (404)");
    check(!with({201,"application/json","{}"}),"Lyric status must be exactly 200 (201)");
    server.reply("/elsewhere",{200,"application/json","{}"});
    check(!with({302,"application/json","{}",true,"/elsewhere"}),"A redirect answer is refused");
    bool followed{};for(const auto&s:server.seen())followed=followed||s.target=="/elsewhere";
    check(!followed,"A redirect is never followed");
    const std::string limit(1024*1024,'a');
    check(with({200,"application/json",limit})->size()==limit.size(),"A lyric body of exactly 1 MiB is accepted");
    check(!with({200,"application/json",limit+"a"}),"A declared lyric body above 1 MiB is refused before reading");
    check(!with({200,"application/json",limit+"a",false}),"Without a length the streamed 1 MiB cap still holds");
    check(text(with({200,"image/jpeg","\xff\xd8\xff"},Kind::artwork))=="\xff\xd8\xff","An allow-listed cover image is delivered");
    check(text(with({206,"IMAGE/PNG","png"},Kind::artwork))=="png","Any 2xx image/* cover is accepted (case-insensitive media type)");
    check(!with({204,"image/jpeg",""},Kind::artwork),"An empty cover is no cover");
    check(!with({200,"application/octet-stream","x"},Kind::artwork),"Non-image cover media types are refused");
    check(!with({404,"image/jpeg","x"},Kind::artwork),"A cover status must be 2xx");
    const std::string eight(8*1024*1024,'b');
    check(with({200,"image/jpeg",eight},Kind::artwork)->size()==eight.size(),"A cover of exactly 8 MiB is accepted");
    check(!with({200,"image/jpeg",eight+"b",false},Kind::artwork),"A cover above 8 MiB is refused while streaming");
}
// ONE kept-alive session: consecutive lookups share the pooled connection
// instead of a new connection (and, over https, a new TLS handshake) each.
void session(){
    Server server;Client client(server);
    server.reply("/api/get",{200,"application/json","{}"});
    server.reply("/api/song/lyric",{200,"application/json","{}"});
    server.reply("/AbC==/1099.jpg",{200,"image/jpeg","jpg"});
    client.fetch(lrclib);client.fetch(catalog);client.fetch(cover,Kind::artwork);client.fetch(lrclib);
    auto stats=client.http->stats();
    check(server.seen().size()==4&&server.accepted()==1,"Four consecutive lookups reuse one kept-alive connection");
    check(stats.sessionsOpened==1&&stats.requests==4&&stats.open&&stats.retained&&stats.inFlight==0,"One session serves every request while retained");
    client.http->setRetained(false);stats=client.http->stats();
    check(!stats.open&&stats.sessionsClosed==1&&stats.inFlight==0,"Released and idle, the session closes");
    {
        const auto begin=std::chrono::steady_clock::now();const bool closed=server.waitClosed(1,30000);
        std::cout<<"session release: the pooled connection closed after "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<" s\n";
        check(closed,"Its pooled idle connection closes within WinHTTP's idle period");
    }
    client.fetch(lrclib);stats=client.http->stats();
    check(stats.sessionsOpened==2&&!stats.open&&server.accepted()==2,"A request after the release opens a session once and closes it when done");
    client.http->setRetained(true);
    server.reply("/api/get",{200,"application/json","{}",true,{},true});
    auto [task,signal]=client.start(lrclib,Kind::lyrics);
    for(int k=0;k<500&&server.seen().size()<6;++k)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    client.http->setRetained(false);
    check(client.http->stats().open&&client.http->stats().inFlight==1,"A release waits for the request in flight");
    task->cancel();
    check(signal->wait(15)&&!signal->snapshot().body&&!client.http->stats().open,"The session closes once that request ends");
}
void cancellation(){
    Server server;Client client(server);
    server.reply("/api/get",{200,"application/json","{}",true,{},true});
    auto [task,signal]=client.start(lrclib,Kind::lyrics);
    for(int k=0;k<500&&server.seen().empty();++k)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(server.seen().size()==1,"The held request reached the server");
    const auto started=std::chrono::steady_clock::now();task->cancel();
    check(signal->wait(15),"Cancellation completes");
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    const auto outcome=signal->snapshot();
    check(!outcome.body&&outcome.calls==1&&elapsed<5,"Cancellation completes at once, exactly once, with no body");
    check(server.waitClosed(1,5000),"The cancelled request's connection is closed");
    task->cancel();Sleep(50);check(signal->snapshot().calls==1,"Repeated cancellation never completes twice");
    server.reply("/api/get",{200,"application/json","{}"});
    auto [early,earlySignal]=client.start(lrclib,Kind::lyrics);early->cancel();
    check(earlySignal->wait(15)&&!earlySignal->snapshot().body&&earlySignal->snapshot().calls==1,"Cancellation right after the start delivers no body");
}
void owner(){
    Server server;Client client(server);
    server.reply("/api/get",{200,"application/json",R"({"ok":1})"});
    auto wake=std::make_shared<Signal>();const auto origin=std::chrono::steady_clock::now();
    const auto clock=[origin]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-origin).count();};
    // NowPlayingWeb accepts only the https allow-list: route its https URL to
    // the loopback endpoint through the plain-http test session.
    const auto transport=client.http->transport();
    n::NowPlayingWeb web([transport](const n::NowPlayingWeb::Request&r,n::NowPlayingWeb::Done d){
        auto request=r;request.url.replace(0,5,"http");return transport(request,std::move(d));},[wake]{wake->set(nullptr);},clock);
    std::optional<std::string>delivered;std::thread::id thread;
    web.fetch({"https://lrclib.net/api/get?track_name=Synthetic%20Song&artist_name=Example%20Artist",Kind::lyrics},[&](m::NowPlayingBody body){delivered=text(body);thread=std::this_thread::get_id();});
    check(!delivered&&web.nextWakeTime().has_value(),"Nothing is delivered before the owner drains; the 10 s bound is exported");
    check(wake->wait(15),"The transport completion posts an owner wake");
    check(!delivered&&web.drain()&&delivered==std::string(R"({"ok":1})")&&thread==std::this_thread::get_id(),"The owner drain delivers the body on the owner thread");
    check(!web.nextWakeTime()&&web.stats().completed==1&&web.stats().inFlight==0,"Completed work leaves no deadline");
    server.reply("/api/get",{200,"application/json","{}",true,{},true});
    bool ran{};const auto cancel=web.fetch({"https://lrclib.net/api/get?track_name=Late&artist_name=A",Kind::lyrics},[&](m::NowPlayingBody){ran=true;});
    cancel();
    check(web.stats().cancelled==1&&web.stats().inFlight==0&&!web.nextWakeTime(),"Owner cancellation retires the request and its deadline at once");
    Sleep(200);web.drain();
    check(!ran,"A cancelled owner request never delivers a late transport completion");
}
void production(){
    n::NowPlayingHttp http;
    const auto stats=http.stats();
    check(!stats.open&&stats.sessionsOpened==0,"Constructing the production session performs no IO");
    check(!http.transport()({lrclib,Kind::lyrics},[](m::NowPlayingBody){})&&!http.stats().open,"Production refuses plain http before opening anything");
    check(bool(n::windowsNowPlayingTransport()),"The probe's retained transport is available without IO");
}
}
int main(){
    try{
        requests();acceptance();session();cancellation();owner();production();
        std::cout<<"Now Playing WinHTTP transport: "<<checks<<" checks passed (loopback server only)\n";return 0;
    }catch(const std::exception&e){std::cerr<<"Now Playing WinHTTP transport failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
