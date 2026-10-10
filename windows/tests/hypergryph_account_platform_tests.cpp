// Windows account services: DPAPI vault, owner-only cache file and the WinHTTP
// transport's gating, owner-thread delivery, cancellation and deadlines. All
// files live in a fresh temporary directory; the only socket attempt is to
// 127.0.0.1 on a closed port with proxy discovery disabled. No real account,
// token, profile, Keychain item or external host is touched.
#include "native/hypergryph_account_services.hpp"
#include "native/hypergryph_account_transport.hpp"
#include "native/hypergryph_account_vault.hpp"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

namespace h=endfield::modules::hypergryph;
namespace n=endfield::native;
namespace {
std::size_t checks{};
void check(bool value,const std::string& why) {++checks;if(!value) throw std::runtime_error(why);}
template<class F> bool throws(F f) {try {f();} catch(const std::exception&) {return true;}return false;}
struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        wchar_t base[MAX_PATH+1]{};check(GetTempPathW(MAX_PATH,base)>0,"Temporary directory");
        std::random_device random;path=std::filesystem::path(base)/(L"EndfieldHUD-account-test-"+std::to_wstring(random())+L"-"+std::to_wstring(GetCurrentProcessId()));
        path=path.lexically_normal();
    }
    ~TempDirectory() {std::error_code e;std::filesystem::remove_all(path,e);}
};
void vault() {
    TempDirectory temp;const auto directory=temp.path/L"Account";
    n::DpapiAccountVault vault(directory);
    check(!vault.load(h::Region::mainland)&&!vault.load(h::Region::global),"Empty vault has no credentials and needs no directory");
    // Long scoped credentials exceed Credential Manager's 2,560-byte blob limit.
    const h::Credentials longValue{std::string(4096,'C'),std::string(4096,'T'),std::string(4096,'D')};
    const h::Credentials legacy{"SYNTHETIC-CRED-2","synthetic-token-2",std::nullopt};
    vault.save(longValue,h::Region::mainland);vault.save(legacy,h::Region::global);
    check(vault.load(h::Region::mainland)==longValue,"4 KiB credential parts round-trip without truncation");
    check(vault.load(h::Region::global)==legacy,"Regions are separate; a missing device context stays missing");
    const auto blob=n::readBoundedFile(vault.path(h::Region::mainland),n::DpapiAccountVault::maximumBlobBytes);
    check(blob&&blob->find("CCCCCCCC")==std::string::npos&&blob->find("TTTTTTTT")==std::string::npos,"Stored blob never contains plaintext");
    check(n::ownerOnly(vault.path(h::Region::mainland))&&n::ownerOnly(directory),"Credential file and directory are owner-only with a protected DACL");
    check(!n::ownerOnly(temp.path),"Only the Account directory is owner-only; a created parent keeps inherited security");
    // Entropy binds a blob to its region: a copied record cannot be read as another region.
    std::filesystem::copy_file(vault.path(h::Region::mainland),vault.path(h::Region::global),std::filesystem::copy_options::overwrite_existing);
    check(throws([&]{(void)vault.load(h::Region::global);}),"A blob moved to another region is rejected");
    vault.remove(h::Region::global);vault.remove(h::Region::global);
    check(!vault.load(h::Region::global)&&vault.load(h::Region::mainland)==longValue,"Disconnect removes only that region; removing twice is harmless");
    n::writeOwnerOnlyFile(vault.path(h::Region::global),"not a vault record");
    check(throws([&]{(void)vault.load(h::Region::global);}),"Unrecognized records fail closed");
    check(throws([]{n::DpapiAccountVault relative(std::filesystem::path(L"relative"));}),"Vault requires an explicit absolute directory");
    n::OwnerOnlyAccountCacheFile cache(directory/L"profile-cache.json");
    check(!cache.exists()&&!cache.read(),"Missing cache reads as absent");
    cache.write("{\"version\":1}");
    check(cache.exists()&&cache.read()==std::optional<std::string>("{\"version\":1}")&&n::ownerOnly(directory/L"profile-cache.json"),"Cache file is atomic and owner-only (Mac 0600)");
    check(throws([&]{cache.write(std::string(h::maximumCacheBytes+1,' '));})&&cache.read()==std::optional<std::string>("{\"version\":1}"),"Oversized cache is refused and the old bytes stay");
    std::size_t leftovers{};for(const auto& entry:std::filesystem::directory_iterator(directory)) if(entry.path().extension()==L".tmp") ++leftovers;
    check(leftovers==0,"Atomic writes leave no temporary files");
}
void heads() {
    using n::classifyAccountResponseHead;constexpr std::size_t api=h::maximumResponseBytes;
    check(classifyAccountResponseHead(200,std::nullopt,api,true,std::nullopt,false).readBody,"2xx reads body");
    check(classifyAccountResponseHead(401,100,api,true,std::nullopt,false).readBody&&classifyAccountResponseHead(403,100,api,true,std::nullopt,false).readBody,"401/403 bodies carry the provider code");
    check(classifyAccountResponseHead(500,std::nullopt,api,true,std::nullopt,false).failure==h::Error{h::ErrorKind::http,500},"Other statuses fail as http(status)");
    for(const int code:{301,302,303,307,308}) check(classifyAccountResponseHead(code,0,api,true,std::nullopt,false).failure==h::Error{h::ErrorKind::unsafeRedirect},"Every redirect is unsafe");
    check(classifyAccountResponseHead(200,api+1,api,true,std::nullopt,false).failure==h::Error{h::ErrorKind::responseTooLarge},"Content-Length above 8 MiB is refused before reading");
    check(classifyAccountResponseHead(401,0,4*1024*1024,false,std::string("image/png"),true).failure==h::Error{h::ErrorKind::http,401},"Avatar requests accept only 2xx");
    check(classifyAccountResponseHead(200,0,4*1024*1024,false,std::string("text/html"),true).failure.has_value()&&
          classifyAccountResponseHead(200,0,4*1024*1024,false,std::string("application/octet-stream"),true).readBody&&
          classifyAccountResponseHead(200,0,4*1024*1024,false,std::string(""),true).readBody,"Avatar MIME gate matches the Mac loader");
}
void transport() {
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(event!=nullptr,"Owner wake event");
    double clock=1000;
    n::WinHttpAccountTransport::Options options;options.notify=[event]{SetEvent(event);};options.now=[&clock]{return clock;};options.port=1;options.directOnly=true;
    {
        n::WinHttpAccountTransport transport(options);
        h::HttpRequest request;request.host="127.0.0.1";request.path="/api/v1/game/player/binding";
        request.headers={{"Accept","application/json"},{"cred","SYNTHETIC-CRED"}};
        std::optional<h::HttpOutcome> result;bool synchronous=true;
        auto handle=transport.start(request,[&](h::HttpOutcome outcome){result=std::move(outcome);});synchronous=result.has_value();
        check(!synchronous&&transport.activeRequests()==1&&transport.nextDeadline()==1020,"Request is asynchronous with a 20 s resource deadline");
        for(int i=0;i<200&&!result;++i) {WaitForSingleObject(event,100);transport.drain();}
        check(result&&result->failure&&result->failure->kind==h::ErrorKind::transport,"Refused loopback connection completes on the owner drain as a transport failure");
        check(transport.activeRequests()==0&&!transport.nextDeadline(),"Completed requests leave no deadline");
        // Cancellation suppresses the completion.
        bool called=false;auto cancelled=transport.start(request,[&](h::HttpOutcome){called=true;});cancelled->cancel();
        for(int i=0;i<10;++i) {WaitForSingleObject(event,50);transport.drain();}
        check(!called&&transport.activeRequests()==0,"Cancelled request never reaches its completion");
        // Resource deadline expires overdue requests without any timer.
        std::optional<h::HttpOutcome> late;auto overdue=transport.start(request,[&](h::HttpOutcome o){late=std::move(o);});
        check(!transport.deadline(1019.9),"Deadline is not early");
        clock=1020;check(transport.deadline(clock),"Overdue request expires at its deadline");
        transport.drain();check(late&&late->failure&&late->failure->kind==h::ErrorKind::transport,"Expired request reports transport failure");
        int posted{};transport.post([&]{++posted;});check(posted==0,"Posted callbacks never run synchronously");
        transport.drain();check(posted==1,"Posted callbacks run on drain");
        check(throws([&]{transport.startImage("http://assets.skland.com/a.png",1024,[](h::HttpOutcome){});}),"Avatar fetch requires HTTPS");
        auto pendingImage=transport.startImage("https://127.0.0.1/a.png",1024,[](h::HttpOutcome){});
        // Destroying the transport with work in flight is safe.
    }
    CloseHandle(event);
}
// Account client over the real transport: invalid credentials never hit WinHTTP.
void clientPlumbing() {
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);double clock=0;
    n::WinHttpAccountTransport::Options options;options.notify=[event]{SetEvent(event);};options.now=[&clock]{return clock;};options.port=1;options.directOnly=true;
    n::WinHttpAccountTransport transport(options);
    h::HypergryphAccountClient client(transport,[]{return 812692800.0;});
    std::vector<std::string> results;
    client.bindings(h::Credentials{"bad cred","t",std::nullopt},h::Region::mainland,[&](h::Outcome<std::vector<h::Role>> r){results.push_back(std::string(h::errorName(r.error().kind)));});
    check(transport.activeRequests()==0&&results.empty(),"Invalid credentials start no request");
    transport.drain();check(results==std::vector<std::string>{"invalidCredentials"},"Failure arrives through the owner queue");
    CloseHandle(event);
}
}
std::vector<std::uint8_t> syntheticPNG(unsigned width,unsigned height) {
    using Microsoft::WRL::ComPtr;ComPtr<IWICImagingFactory> wic;check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.GetAddressOf()))),"WIC");
    std::vector<std::uint8_t> pixels(std::size_t(width)*height*4);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {auto* p=&pixels[(std::size_t(y)*width+x)*4];p[0]=std::uint8_t(x);p[1]=std::uint8_t(y);p[2]=128;p[3]=255;}
    ComPtr<IWICBitmap> bitmap;check(SUCCEEDED(wic->CreateBitmapFromMemory(width,height,GUID_WICPixelFormat32bppBGRA,width*4,static_cast<UINT>(pixels.size()),pixels.data(),bitmap.GetAddressOf())),"Synthetic bitmap");
    ComPtr<IStream> memory;check(SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,memory.GetAddressOf())),"Synthetic stream");
    ComPtr<IWICBitmapEncoder> encoder;wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.GetAddressOf());encoder->Initialize(memory.Get(),WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;encoder->CreateNewFrame(frame.GetAddressOf(),nullptr);frame->Initialize(nullptr);frame->SetSize(width,height);
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;frame->SetPixelFormat(&format);check(SUCCEEDED(frame->WriteSource(bitmap.Get(),nullptr)),"Synthetic PNG pixels");frame->Commit();encoder->Commit();
    STATSTG stat{};memory->Stat(&stat,STATFLAG_NONAME);std::vector<std::uint8_t> out(static_cast<std::size_t>(stat.cbSize.QuadPart));LARGE_INTEGER zero{};memory->Seek(zero,STREAM_SEEK_SET,nullptr);ULONG read{};memory->Read(out.data(),static_cast<ULONG>(out.size()),&read);
    return out;
}
std::pair<unsigned,unsigned> pngSize(const std::vector<std::uint8_t>& png) {
    check(png.size()>24&&png[0]==0x89&&png[1]=='P'&&png[2]=='N'&&png[3]=='G',"Output is PNG");
    const auto be=[&](std::size_t at){return (unsigned(png[at])<<24)|(unsigned(png[at+1])<<16)|(unsigned(png[at+2])<<8)|png[at+3];};return {be(16),be(20)};
}
void avatar() {
    check(pngSize(n::downsampleAvatar(syntheticPNG(1024,600)))==std::pair{512u,300u},"Avatar is downsampled to the 512 px source thumbnail bound");
    check(pngSize(n::downsampleAvatar(syntheticPNG(100,80)))==std::pair{100u,80u},"Small avatars keep their size (no upscaling)");
    check(throws([]{const std::vector<std::uint8_t> junk(64,7);(void)n::downsampleAvatar(junk);})&&throws([]{(void)n::downsampleAvatar({});}),"Undecodable avatars are rejected");
    check(throws([]{(void)n::downsampleAvatar(syntheticPNG(8193,1));}),"Avatars beyond 8192 px are rejected before decoding pixels");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);double clock=0;
    n::WinHttpAccountTransport::Options options;options.notify=[event]{SetEvent(event);};options.now=[&]{return clock;};options.directOnly=true;options.port=1;
    n::WinHttpAccountTransport transport(options);endfield::app::UtilityExecutor executor([event]{SetEvent(event);});
    {
        n::WinHttpAvatarLoader loader(transport,executor);int calls{};std::optional<std::vector<std::uint8_t>> result{std::vector<std::uint8_t>{1}};
        loader.load("https://evil.example/a.png",[&](std::optional<std::vector<std::uint8_t>> r){++calls;result=std::move(r);});
        check(transport.activeRequests()==0&&calls==0,"Non-allowlisted avatar host starts no request");
        transport.drain();check(calls==1&&!result,"Rejected avatar reports unavailable on the owner queue");
        auto first=loader.load("https://evil.example/a.png",[&](auto){++calls;});first->cancel();transport.drain();check(calls==1,"Cancelled avatar load never completes");
    }
    executor.shutdown();
    // Unavailable login presenter: Connect reports the Mac failure without a browser.
    n::UnavailableLoginPresenter presenter([&](std::function<void()> f){transport.post(std::move(f));});
    std::optional<h::LoginFailure> failure;presenter.present(h::Region::global,[&](auto,h::LoginFailure f){failure=f;});
    check(!failure,"Login result is never synchronous");transport.drain();check(failure==h::LoginFailure::pageUnavailable&&!presenter.isPresenting(),"No WebView2 build reports unavailable");
    const auto runtime=n::webView2RuntimeVersion();std::wcout<<L"WebView2 Evergreen runtime: "<<(runtime?*runtime:L"not installed")<<L"\n";
    CloseHandle(event);
}
void queuedCache() {
    TempDirectory temp;HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(event!=nullptr,"Utility wake event");
    endfield::app::UtilityExecutor executor([event]{SetEvent(event);});int failures{};
    const auto settle=[&](n::QueuedAccountCacheFile& c){for(int i=0;i<200&&!c.idle();++i) {WaitForSingleObject(event,20);executor.drain();}};
    {
        const auto path=temp.path/L"Account"/L"profile-cache.json";
        n::QueuedAccountCacheFile cache(path,executor,[&]{++failures;});
        check(!cache.exists()&&!cache.read()&&cache.idle(),"Missing queued cache reads as absent");
        cache.write("{\"version\":1,\"n\":1}");cache.write("{\"version\":1,\"n\":2}");cache.write("{\"version\":1,\"n\":3}");
        check(cache.writesStarted()==1&&!cache.idle(),"The owner thread only queues: one write in flight, the latest bytes pending");
        settle(cache);
        check(cache.idle()&&cache.writesStarted()==2&&cache.read()==std::optional<std::string>("{\"version\":1,\"n\":3}")&&n::ownerOnly(path)&&failures==0,
              "Writes coalesce to two atomic owner-only replacements ending with the latest bytes");
        check(throws([&]{cache.write(std::string(h::maximumCacheBytes+1,' '));})&&cache.idle(),"Oversized cache bytes are refused on the owner thread");
        // A failing write reports once and is not retried automatically.
        std::filesystem::create_directories(temp.path/L"Blocked");std::ofstream(temp.path/L"Blocked"/L"Account")<<"not a directory";
        n::QueuedAccountCacheFile blocked(temp.path/L"Blocked"/L"Account"/L"profile-cache.json",executor,[&]{++failures;});
        blocked.write("{\"version\":1}");
        for(int i=0;i<200&&failures==0;++i) {WaitForSingleObject(event,20);executor.drain();}
        for(int i=0;i<10;++i) {WaitForSingleObject(event,20);executor.drain();}
        check(failures==1&&blocked.writesStarted()==1&&!blocked.idle(),"A failed write reports once, keeps its bytes and starts no retry loop");
        check(!blocked.flush(),"Flush reports a persistent failure");
        std::filesystem::remove(temp.path/L"Blocked"/L"Account");
        check(blocked.flush()&&blocked.idle()&&blocked.read()==std::optional<std::string>("{\"version\":1}"),"Flush writes the kept bytes once the path is usable");
        cache.write("{\"version\":1,\"n\":5}");cache.write("{\"version\":1,\"n\":6}");
        check(cache.flush()&&cache.read()==std::optional<std::string>("{\"version\":1,\"n\":6}"),"Flush waits for the accepted write and writes the newest bytes");
        settle(cache);check(cache.idle()&&cache.read()==std::optional<std::string>("{\"version\":1,\"n\":6}")&&failures==1,"The superseded completion never restores older bytes");
    }
    executor.shutdown();CloseHandle(event);
}
void services() {
    TempDirectory temp;HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    endfield::app::UtilityExecutor executor([event]{SetEvent(event);});double clock=0;std::vector<std::string> events;int changes{};
    {
        n::HypergryphAccountServicesOptions options;options.accountDirectory=temp.path/L"Account";options.notify=[event]{SetEvent(event);};options.hostClock=[&]{return clock;};
        options.onEvent=[&](std::string_view e){events.emplace_back(e);};options.changed=[&]{++changes;};
        n::HypergryphAccountServices bundle(executor,options);auto& c=bundle.controller();
        check(c.cacheWritesAllowed()&&!c.record().linked&&c.gaugeValue({true,1500,0}).value=="25 / 25","Fresh Windows install starts unlinked with Work Mode minutes");
        c.setVisible(true,true);h::AccountAction connect;connect.kind=h::AccountAction::Kind::connect;connect.region=h::AccountPresentation::RegionChoice::china;c.perform(connect);
        bundle.drain();
        check(c.presentation().statusMessage=="Sign-in could not finish. Please try again."&&!c.isPresentingLogin(),"Without WebView2 the official login reports a clear failure");
        h::AccountAction header;header.kind=h::AccountAction::Kind::selectHeaderMode;header.headerMode=h::AccountPresentation::HeaderMode::arknights;c.perform(header);
        check(events==std::vector<std::string>{"settings"},"Preference change records one settings event");
        for(int i=0;i<50&&!std::filesystem::exists(temp.path/L"Account"/L"profile-cache.json");++i) {WaitForSingleObject(event,20);executor.drain();bundle.drain();}
        check(std::filesystem::exists(temp.path/L"Account"/L"profile-cache.json")&&n::ownerOnly(temp.path/L"Account"/L"profile-cache.json"),"Preference cache is written owner-only under the app Account directory");
        c.disconnect();for(int i=0;i<100&&c.hasActiveRequest();++i) {WaitForSingleObject(event,20);executor.drain();bundle.drain();}
        check(!c.hasActiveRequest()&&events.back()=="unlinked","Disconnect completes through the shared utility queue");
        check(!bundle.nextDeadline(),"Idle services hold no request deadline");
    }
    executor.shutdown();CloseHandle(event);
}
int main() {
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {vault();heads();transport();clientPlumbing();avatar();queuedCache();services();if(SUCCEEDED(hr)) CoUninitialize();std::cout<<"PASS "<<checks<<" Windows account platform checks\n";return 0;}
    catch(const std::exception& e) {if(SUCCEEDED(hr)) CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
