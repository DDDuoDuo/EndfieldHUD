#include "native/hypergryph_account_transport.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>

namespace endfield::native {
namespace h=modules::hypergryph;
namespace {
std::wstring wide(std::string_view text) {
    if(text.empty()) return {};
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(size<=0) throw std::invalid_argument("Invalid UTF-8 request text");
    std::wstring out(static_cast<std::size_t>(size),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),size);
    return out;
}
std::string lowerASCII(std::string s) {for(auto& c:s) if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a');return s;}
}
AccountResponseHead classifyAccountResponseHead(int status,std::optional<unsigned long long> length,std::size_t maximum,bool authentication,
                                                std::optional<std::string> mime,bool image) {
    AccountResponseHead head;
    if(status==301||status==302||status==303||status==307||status==308) {head.failure=h::Error{h::ErrorKind::unsafeRedirect};return head;}
    const bool success=status>=200&&status<=299;
    if(!success&&!(authentication&&(status==401||status==403))) {head.failure=h::Error{h::ErrorKind::http,status};return head;}
    if(length&&*length>maximum) {head.failure=h::Error{h::ErrorKind::responseTooLarge};return head;}
    if(image&&mime) {
        const auto m=lowerASCII(*mime);
        if(!m.empty()&&m.rfind("image/",0)!=0&&m!="application/octet-stream") {head.failure=h::Error{h::ErrorKind::invalidResponse};return head;}
    }
    head.readBody=true;return head;
}

struct WinHttpAccountTransport::Impl:std::enable_shared_from_this<Impl> {
    // Shared with WinHTTP callback threads.
    struct Shared {
        std::mutex mutex;std::deque<std::pair<std::uint64_t,h::HttpOutcome>> done;std::function<void()> notify;
        void complete(std::uint64_t id,h::HttpOutcome outcome) {
            std::function<void()> wake;
            {std::lock_guard lock(mutex);const bool first=done.empty();done.emplace_back(id,std::move(outcome));if(first) wake=notify;}
            if(wake) wake();
        }
    };
    struct Context {
        std::shared_ptr<Shared> shared;std::uint64_t id{};std::size_t maximum{};bool authentication{},image{};
        std::mutex mutex;HINTERNET connect{},request{};bool finished{},closed{};int status{};std::string body;
        // One read buffer per request, owned by the context (which WinHTTP's
        // callback holder keeps alive until HANDLE_CLOSING), so a read pending
        // during cancellation never leaks or touches freed memory.
        static constexpr DWORD readChunk=64*1024;
        std::unique_ptr<char[]> buffer;
        void finish(h::HttpOutcome outcome) {
            {std::lock_guard lock(mutex);if(finished) return;finished=true;}
            shared->complete(id,std::move(outcome));close();
        }
        void close() {
            HINTERNET r{},c{};
            {std::lock_guard lock(mutex);if(closed) return;closed=true;r=request;c=connect;}
            if(r) WinHttpCloseHandle(r);
            if(c) WinHttpCloseHandle(c);
        }
    };
    class Handle final:public h::AccountRequest {
    public:
        Handle(std::weak_ptr<Impl> owner,std::shared_ptr<Context> context):owner_(std::move(owner)),context_(std::move(context)) {}
        void cancel() override {
            if(auto owner=owner_.lock()) {owner->pending.erase(context_->id);owner->started.erase(context_->id);}
            {std::lock_guard lock(context_->mutex);context_->finished=true;}
            context_->close();
        }
    private:
        std::weak_ptr<Impl> owner_;std::shared_ptr<Context> context_;
    };
    Options options;HINTERNET session{};std::shared_ptr<Shared> shared=std::make_shared<Shared>();
    std::map<std::uint64_t,std::function<void(h::HttpOutcome)>> pending;std::map<std::uint64_t,std::pair<double,std::weak_ptr<Context>>> started;
    std::deque<std::function<void()>> posted;std::uint64_t next{};

    static void CALLBACK callback(HINTERNET handle,DWORD_PTR value,DWORD status,LPVOID info,DWORD length) {
        if(!value) return;
        auto* holder=reinterpret_cast<std::shared_ptr<Context>*>(value);auto context=*holder;
        if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {if(handle==context->request) delete holder;return;}
        if(handle!=context->request) return;
        {std::lock_guard lock(context->mutex);if(context->finished) return;}
        switch(status) {
            case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
                if(!WinHttpReceiveResponse(handle,nullptr)) context->finish({h::Error{h::ErrorKind::transport}});
                break;
            case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: {
                DWORD code{},size=sizeof code;
                if(!WinHttpQueryHeaders(handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&size,WINHTTP_NO_HEADER_INDEX)) {
                    context->finish({h::Error{h::ErrorKind::invalidResponse}});break;
                }
                std::optional<unsigned long long> contentLength;
                {
                    wchar_t buffer[32]{};DWORD bytes=sizeof buffer;
                    if(WinHttpQueryHeaders(handle,WINHTTP_QUERY_CONTENT_LENGTH,WINHTTP_HEADER_NAME_BY_INDEX,buffer,&bytes,WINHTTP_NO_HEADER_INDEX)) {
                        wchar_t* end{};const auto v=wcstoull(buffer,&end,10);if(end!=buffer) contentLength=v;
                    }
                }
                std::optional<std::string> mime;
                if(context->image) {
                    wchar_t buffer[256]{};DWORD bytes=sizeof buffer;
                    if(WinHttpQueryHeaders(handle,WINHTTP_QUERY_CONTENT_TYPE,WINHTTP_HEADER_NAME_BY_INDEX,buffer,&bytes,WINHTTP_NO_HEADER_INDEX)) {
                        std::string text;for(const wchar_t* p=buffer;*p&&*p!=L';';++p) text.push_back(*p<128?static_cast<char>(*p):'?');
                        while(!text.empty()&&text.back()==' ') text.pop_back();mime=text;
                    } else mime=std::string();
                }
                const auto head=classifyAccountResponseHead(static_cast<int>(code),contentLength,context->maximum,context->authentication,mime,context->image);
                if(head.failure) {context->finish({head.failure});break;}
                // The status is retained for interpretResponse (401/403 handling).
                {std::lock_guard lock(context->mutex);context->body.clear();context->status=static_cast<int>(code);}
                if(!WinHttpQueryDataAvailable(handle,nullptr)) context->finish({h::Error{h::ErrorKind::transport}});
                break;
            }
            case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE: {
                const DWORD available=*static_cast<DWORD*>(info);
                if(available==0) {
                    h::HttpOutcome outcome;{std::lock_guard lock(context->mutex);outcome.status=context->status;outcome.body=std::move(context->body);}
                    context->finish(std::move(outcome));break;
                }
                std::size_t size{};{std::lock_guard lock(context->mutex);size=context->body.size();}
                if(available>context->maximum-std::min(size,context->maximum)) {context->finish({h::Error{h::ErrorKind::responseTooLarge}});break;}
                if(!context->buffer) context->buffer=std::make_unique<char[]>(Context::readChunk);
                if(!WinHttpReadData(handle,context->buffer.get(),std::min(available,Context::readChunk),nullptr)) context->finish({h::Error{h::ErrorKind::transport}});
                break;
            }
            case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: {
                // DATA_AVAILABLE already bounded this read against the body cap.
                if(length) {std::lock_guard lock(context->mutex);context->body.append(static_cast<const char*>(info),length);}
                if(length==0) {
                    h::HttpOutcome outcome;{std::lock_guard lock(context->mutex);outcome.status=context->status;outcome.body=std::move(context->body);}
                    context->finish(std::move(outcome));
                } else if(!WinHttpQueryDataAvailable(handle,nullptr)) context->finish({h::Error{h::ErrorKind::transport}});
                break;
            }
            case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR: {
                const auto* result=static_cast<WINHTTP_ASYNC_RESULT*>(info);
                context->finish({h::Error{result&&result->dwError==ERROR_WINHTTP_OPERATION_CANCELLED?h::ErrorKind::cancelled:h::ErrorKind::transport}});
                break;
            }
            case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE: break; // followed by REQUEST_ERROR; default TLS validation only
            default: break;
        }
    }
    explicit Impl(Options o):options(std::move(o)) {
        if(!options.now) throw std::invalid_argument("WinHTTP account transport requires the host clock");
        shared->notify=options.notify;
        if(options.directOnly) session=WinHttpOpen(options.userAgent.c_str(),WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC);
        else session=WinHttpOpen(options.userAgent.c_str(),WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC);
        if(!session&&!options.directOnly) session=WinHttpOpen(options.userAgent.c_str(),WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC);
        if(!session) throw std::runtime_error("WinHTTP session is unavailable");
        DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols|=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        if(!WinHttpSetOption(session,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof protocols)) {
            protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;WinHttpSetOption(session,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof protocols);
        }
        if(WinHttpSetStatusCallback(session,callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK) {
            WinHttpCloseHandle(session);throw std::runtime_error("WinHTTP callback could not be installed");
        }
    }
    ~Impl() {
        {std::lock_guard lock(shared->mutex);shared->notify={};}
        for(auto& [id,entry]:started) if(auto context=entry.second.lock()) {{std::lock_guard lock(context->mutex);context->finished=true;}context->close();}
        pending.clear();started.clear();
        if(session) WinHttpCloseHandle(session);
    }
    h::RequestHandle open(const std::string& host,const std::string& pathAndQuery,const std::vector<std::pair<std::string,std::string>>& headers,
                          std::size_t maximum,bool authentication,bool image,double timeout,std::function<void(h::HttpOutcome)> completion) {
        auto context=std::make_shared<Context>();context->shared=shared;context->id=++next;context->maximum=maximum;context->authentication=authentication;context->image=image;
        const auto fail=[&](h::ErrorKind kind){const auto id=context->id;pending[id]=std::move(completion);shared->complete(id,{h::Error{kind}});return std::make_shared<Handle>(weak_from_this(),context);};
        context->connect=WinHttpConnect(session,wide(host).c_str(),options.port,0);
        if(!context->connect) return fail(h::ErrorKind::transport);
        context->request=WinHttpOpenRequest(context->connect,L"GET",wide(pathAndQuery).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE|WINHTTP_FLAG_ESCAPE_DISABLE|WINHTTP_FLAG_REFRESH);
        if(!context->request) {WinHttpCloseHandle(context->connect);context->connect=nullptr;return fail(h::ErrorKind::transport);}
        DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(context->request,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof redirect);
        DWORD disabled=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_AUTHENTICATION;WinHttpSetOption(context->request,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof disabled);
        DWORD logon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;WinHttpSetOption(context->request,WINHTTP_OPTION_AUTOLOGON_POLICY,&logon,sizeof logon);
#ifdef WINHTTP_OPTION_DECOMPRESSION
        DWORD decompression=WINHTTP_DECOMPRESSION_FLAG_ALL;WinHttpSetOption(context->request,WINHTTP_OPTION_DECOMPRESSION,&decompression,sizeof decompression);
#endif
        const int ms=static_cast<int>(timeout*1000);WinHttpSetTimeouts(context->request,ms,ms,ms,ms);
        std::wstring headerText;
        for(const auto& [name,value]:headers) headerText+=wide(name)+L": "+wide(value)+L"\r\n";
        auto* holder=new std::shared_ptr<Context>(context);
        if(!WinHttpSetOption(context->request,WINHTTP_OPTION_CONTEXT_VALUE,&holder,sizeof holder)) {
            delete holder;context->close();return fail(h::ErrorKind::transport);
        }
        const auto id=context->id;pending[id]=std::move(completion);started[id]={options.now(),context};
        if(!WinHttpSendRequest(context->request,headerText.empty()?WINHTTP_NO_ADDITIONAL_HEADERS:headerText.c_str(),headerText.empty()?0:static_cast<DWORD>(-1L),
                               WINHTTP_NO_REQUEST_DATA,0,0,reinterpret_cast<DWORD_PTR>(holder))) {
            started.erase(id);context->finish({h::Error{h::ErrorKind::transport}});
        }
        for(auto& c:headerText) c=0;
        return std::make_shared<Handle>(weak_from_this(),context);
    }
};

WinHttpAccountTransport::WinHttpAccountTransport(Options options):impl_(std::make_shared<Impl>(std::move(options))) {}
WinHttpAccountTransport::~WinHttpAccountTransport()=default;
h::RequestHandle WinHttpAccountTransport::start(h::HttpRequest request,std::function<void(h::HttpOutcome)> completion) {
    if(request.host.empty()||request.path.empty()||request.path.front()!='/') throw std::invalid_argument("Account transport needs an HTTPS host and absolute path");
    const auto target=request.path+(request.query.empty()?"":"?"+request.query);
    return impl_->open(request.host,target,request.headers,h::maximumResponseBytes,true,false,impl_->options.requestTimeout,std::move(completion));
}
h::RequestHandle WinHttpAccountTransport::startImage(const std::string& url,std::size_t maximum,std::function<void(h::HttpOutcome)> completion) {
    const auto normalized=h::httpsURL(url);if(!normalized) throw std::invalid_argument("Avatar requests require an HTTPS URL");
    const auto rest=std::string_view(*normalized).substr(8);const auto slash=std::min(rest.find_first_of("/?#"),rest.size());
    const auto authority=std::string(rest.substr(0,slash));const auto host=authority.substr(0,authority.find(':'));
    std::string target(rest.substr(slash));if(const auto hash=target.find('#');hash!=std::string::npos) target.resize(hash);
    if(target.empty()||target.front()=='?') target="/"+target;
    return impl_->open(host,target,{{"Accept","image/*"}},maximum,false,true,impl_->options.imageTimeout,std::move(completion));
}
void WinHttpAccountTransport::post(std::function<void()> callback) {
    const bool first=impl_->posted.empty();impl_->posted.push_back(std::move(callback));
    if(first&&impl_->options.notify) impl_->options.notify();
}
std::size_t WinHttpAccountTransport::drain() {
    auto& i=*impl_;std::size_t count{};auto keep=impl_;
    while(true) {
        std::deque<std::pair<std::uint64_t,h::HttpOutcome>> ready;
        {std::lock_guard lock(i.shared->mutex);ready.swap(i.shared->done);}
        std::deque<std::function<void()>> callbacks;callbacks.swap(i.posted);
        if(ready.empty()&&callbacks.empty()) break;
        for(auto& [id,outcome]:ready) {
            i.started.erase(id);const auto found=i.pending.find(id);if(found==i.pending.end()) continue;
            auto completion=std::move(found->second);i.pending.erase(found);completion(std::move(outcome));++count;
        }
        for(auto& callback:callbacks) {callback();++count;}
    }
    return count;
}
std::optional<double> WinHttpAccountTransport::nextDeadline() const {
    std::optional<double> next;
    for(const auto& [id,entry]:impl_->started) {const auto at=entry.first+impl_->options.resourceTimeout;next=next?std::min(*next,at):at;}
    return next;
}
bool WinHttpAccountTransport::deadline(double now) {
    auto& i=*impl_;std::vector<std::uint64_t> overdue;
    for(const auto& [id,entry]:i.started) if(now>=entry.first+i.options.resourceTimeout) overdue.push_back(id);
    for(const auto id:overdue) {
        auto context=i.started[id].second.lock();i.started.erase(id);
        if(context) {{std::lock_guard lock(context->mutex);if(context->finished) continue;context->finished=true;}context->close();}
        i.shared->complete(id,{h::Error{h::ErrorKind::transport}});
    }
    return !overdue.empty();
}
std::size_t WinHttpAccountTransport::activeRequests() const {return impl_->pending.size();}
}
#endif
