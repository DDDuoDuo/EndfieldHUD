#include "native/hypergryph_account_services.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <cmath>
#include <ctime>
#include <stdexcept>

namespace endfield::native {
namespace h=modules::hypergryph;
h::LocalDateFields localDateFields(h::Time time) {
    const double unix=h::toUnix(time);if(!std::isfinite(unix)) return {};
    const auto seconds=static_cast<std::time_t>(std::floor(unix));std::tm local{};
    if(localtime_s(&local,&seconds)!=0) return {};
    return {local.tm_mon+1,local.tm_mday,local.tm_hour,local.tm_min,local.tm_sec};
}
h::Time referenceNow() {
    const auto now=std::chrono::system_clock::now().time_since_epoch();
    return h::fromUnix(std::chrono::duration<double>(now).count());
}
std::optional<std::wstring> webView2RuntimeVersion() {
    static const wchar_t* key=L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    static const wchar_t* userKey=L"Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    for(const auto& [root,path]:{std::pair{HKEY_LOCAL_MACHINE,key},std::pair{HKEY_CURRENT_USER,userKey}}) {
        wchar_t buffer[64]{};DWORD bytes=sizeof buffer;
        if(RegGetValueW(root,path,L"pv",RRF_RT_REG_SZ,nullptr,buffer,&bytes)==ERROR_SUCCESS&&buffer[0]&&std::wstring(buffer)!=L"0.0.0.0") return std::wstring(buffer);
    }
    return std::nullopt;
}
UnavailableLoginPresenter::UnavailableLoginPresenter(std::function<void(std::function<void()>)> post):post_(std::move(post)) {
    if(!post_) throw std::invalid_argument("Login presenter needs the owner queue");
}
void UnavailableLoginPresenter::present(h::Region,std::function<void(std::optional<h::LoginResult>,h::LoginFailure)> completion) {
    post_([completion]{completion(std::nullopt,h::LoginFailure::pageUnavailable);});
}
UtilityAccountWorkQueue::UtilityAccountWorkQueue(app::UtilityExecutor& e,std::function<void(std::function<void()>)> post):executor_(e),route_(e.makeRoute()),post_(std::move(post)) {}
UtilityAccountWorkQueue::~UtilityAccountWorkQueue() {executor_.invalidate(route_,false);}
void UtilityAccountWorkQueue::run(std::function<void()> work,std::function<void(std::exception_ptr)> done) {
    auto completion=done;
    if(!executor_.submit(route_,std::move(work),std::move(done)))
        post_([completion]{completion(std::make_exception_ptr(std::runtime_error("The shared utility queue is full")));});
}
struct HypergryphAccountServices::Impl {
    HypergryphAccountServicesOptions options;
    std::unique_ptr<WinHttpAccountTransport> transport;std::unique_ptr<h::HypergryphAccountClient> client;
    std::unique_ptr<DpapiAccountVault> vault;std::unique_ptr<QueuedAccountCacheFile> cache;
    std::unique_ptr<UtilityAccountWorkQueue> queue;std::unique_ptr<WinHttpAvatarLoader> avatar;
    std::unique_ptr<UnavailableLoginPresenter> unavailable;std::unique_ptr<h::HypergryphAccountController> controller;
};
HypergryphAccountServices::HypergryphAccountServices(app::UtilityExecutor& executor,HypergryphAccountServicesOptions options):impl_(std::make_unique<Impl>()) {
    auto& i=*impl_;i.options=std::move(options);
    if(!i.options.hostClock||!i.options.wallClock) throw std::invalid_argument("Account services need the host and wall clocks");
    WinHttpAccountTransport::Options t;t.notify=i.options.notify;t.now=i.options.hostClock;
    i.transport=std::make_unique<WinHttpAccountTransport>(std::move(t));
    i.client=std::make_unique<h::HypergryphAccountClient>(*i.transport,i.options.wallClock);
    i.vault=std::make_unique<DpapiAccountVault>(i.options.accountDirectory);
    // Cache writes leave the UI thread; a failed write surfaces the Mac status text.
    i.cache=std::make_unique<QueuedAccountCacheFile>(i.options.accountDirectory/L"profile-cache.json",executor,
        [impl=impl_.get()]{if(impl->controller) impl->controller->cacheWriteFailed();});
    auto post=poster();
    i.queue=std::make_unique<UtilityAccountWorkQueue>(executor,post);
    i.avatar=std::make_unique<WinHttpAvatarLoader>(*i.transport,executor);
    if(!i.options.login) i.unavailable=std::make_unique<UnavailableLoginPresenter>(post);
    h::AccountControllerOptions c;c.now=i.options.wallClock;c.localDate=i.options.localDate;c.language=i.options.language;
    c.onEvent=i.options.onEvent;c.changed=i.options.changed;c.initialAvatarFilename=i.options.initialAvatarFilename;
    i.controller=std::make_unique<h::HypergryphAccountController>(*i.client,*i.vault,*i.queue,i.cache.get(),i.options.profile,
        i.options.login?i.options.login:i.unavailable.get(),i.avatar.get(),std::move(c));
}
HypergryphAccountServices::~HypergryphAccountServices() {
    auto& i=*impl_;i.controller.reset();i.avatar.reset();i.queue.reset();i.client.reset();i.transport.reset();
}
h::HypergryphAccountController& HypergryphAccountServices::controller() noexcept {return *impl_->controller;}
WinHttpAccountTransport& HypergryphAccountServices::transport() noexcept {return *impl_->transport;}
std::size_t HypergryphAccountServices::drain() {return impl_->transport->drain();}
bool HypergryphAccountServices::flush() {return impl_->cache->flush();}
std::optional<double> HypergryphAccountServices::nextDeadline() const {return impl_->transport->nextDeadline();}
bool HypergryphAccountServices::deadline(double t) {const bool expired=impl_->transport->deadline(t);if(expired) impl_->transport->drain();return expired;}
std::function<void(std::function<void()>)> HypergryphAccountServices::poster() {
    auto* transport=impl_->transport.get();
    return [transport](std::function<void()> f){transport->post(std::move(f));};
}
}
#endif
