#include "native/system_services.hpp"
#include <array>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace endfield::native;
namespace {
int checks{};
void check(bool condition, const char* label) {
    ++checks; if (!condition) throw std::runtime_error(label);
}
ClipboardPayload text(std::u16string value) { ClipboardPayload result; result.text=std::move(value); return result; }
void little32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i=0; i<4; ++i) bytes[at+i]=static_cast<std::uint8_t>(value>>(i*8));
}
void big32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for(unsigned i=0;i<4;++i)bytes[at+i]=static_cast<std::uint8_t>(value>>(24-i*8));
}
std::vector<std::uint8_t> dib(std::uint32_t width=2, std::uint32_t height=2, std::uint32_t header=40) {
    std::vector<std::uint8_t> bytes(header+std::size_t(width)*height*4);
    little32(bytes,0,header);little32(bytes,4,width);little32(bytes,8,height);
    bytes[12]=1;bytes[14]=32;little32(bytes,20,width*height*4);
    return bytes;
}
std::vector<std::uint8_t> png() {
    // Fixed, valid 1x1 RGBA PNG. Tests only invoke the structural validator,
    // not a native decoder, clipboard, capture API, audio endpoint, or HWND.
    constexpr std::array<std::uint8_t,68> fixture{
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,
        0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,165,246,69,64,0,0,0,0,73,69,78,68,174,66,96,130};
    return {fixture.begin(),fixture.end()};
}
std::vector<std::uint8_t> tiff(){std::vector<std::uint8_t>b(38);b[0]='I';b[1]='I';b[2]=42;b[4]=8;b[8]=2;b[10]=0;b[11]=1;b[12]=4;b[14]=1;b[18]=1;b[22]=1;b[23]=1;b[24]=4;b[26]=1;b[30]=1;return b;}
struct CaptureBlock {std::mutex mutex;std::condition_variable cv;bool entered{},released{};void wait(){std::unique_lock lock(mutex);entered=true;cv.notify_all();cv.wait(lock,[&]{return released;});}void await(){std::unique_lock lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}))throw std::runtime_error("Owned capture decoder timeout");}void release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}};
struct CaptureRelease {std::shared_ptr<CaptureBlock>block;~CaptureRelease(){block->release();}};
ClipboardImageResult thumbnail(const ClipboardEncodedImage&i){auto t=std::make_shared<ClipboardImageThumbnail>();t->encoded=i;t->sourceWidth=t->sourceHeight=t->width=t->height=1;t->rgba={12,24,36,255};return {std::move(t),ClipboardImageError::none,0};}
void capture(){
    auto metadata=tiff();check(ClipboardHistory::valid_image(ClipboardImageFormat::tiff,metadata),"TIFF metadata shares source pixel and byte bounds");little32(metadata,18,100000);little32(metadata,30,100000);check(!ClipboardHistory::valid_image(ClipboardImageFormat::tiff,metadata),"Huge TIFF cannot enter structural history by bypassing PNG bounds");
    using State=ClipboardCaptureState;using Read=ClipboardReadState;std::atomic<unsigned>wakes{},decodeCalls{};endfield::app::UtilityExecutor executor([&]{++wakes;});ClipboardHistory history(8);std::uint32_t sequence=100;const auto owner=std::this_thread::get_id();bool ownerReads=true,workerDecodes=true;std::vector<unsigned>ranks;std::vector<State>notices;unsigned mode{};bool busyOnce{};std::shared_ptr<CaptureBlock>blocked;
    auto reader=[&](std::uint32_t expected,unsigned first){ownerReads=ownerReads&&std::this_thread::get_id()==owner;ranks.push_back(first);ClipboardReadResult r;r.sequence=sequence;if(expected!=sequence){r.state=Read::stale;return r;}if(busyOnce){busyOnce=false;r.state=Read::busy;r.nativeError=-1;return r;}if(mode==1){r.state=Read::payload;r.payload=text(u"new text");return r;}if(mode==2){r.state=Read::payload;r.payload.kind=ClipboardKind::files;r.payload.files={u"C:\\owned\\item.txt"};return r;}if(mode==3){r.state=Read::excluded;return r;}if(mode==4){r.state=Read::empty;return r;}if(mode==5){r.state=Read::invalid;return r;}if(first>=ClipboardCapture::imageFormats.size()){r.state=Read::payload;r.payload=text(u"fallback text");return r;}r.state=Read::payload;r.nextImage=first+1;r.payload.kind=ClipboardKind::image;r.payload.image_format=ClipboardCapture::imageFormats[first];r.payload.image=first==0?png():first==1?tiff():dib(2,2,first==2?124:40);return r;};
    auto decoder=[&](const ClipboardEncodedImage&i){workerDecodes=workerDecodes&&std::this_thread::get_id()!=owner;++decodeCalls;if(blocked)blocked->wait();if(i.format==ClipboardEncodedFormat::png)return ClipboardImageResult{{},ClipboardImageError::decode,-1};return thumbnail(i);};
    ClipboardCapture capture(history,executor,reader,[&]{return sequence;},[&](auto state,auto){check(std::this_thread::get_id()==owner,"Clipboard completion notification runs on owner");notices.push_back(state);},decoder);
    check(capture.state()==State::idle&&!executor.stats().started,"Constructed capture reads nothing and starts no worker");check(capture.begin(sequence)==State::pending&&history.items().empty(),"Undecoded PNG never enters history");executor.waitIdle();executor.drain();check(ranks==std::vector<unsigned>({0,1})&&history.items().empty(),"Failed PNG rereads same sequence and falls back to source TIFF");executor.waitIdle();executor.drain();check(capture.state()==State::changed&&history.items().size()==1&&history.items().front().payload.image_format==ClipboardImageFormat::tiff,"Successful TIFF thumbnail gates image acceptance");const auto&payload=history.items().front().payload;check(payload.image.empty()&&payload.decodedImage&&payload.image_bytes().data()==payload.decodedImage->encoded.bytes->data()&&history.retained_bytes()==payload.image_bytes().size(),"History and copyback share original encoded allocation without RGBA budget inflation");ClipboardPayload legacy;legacy.kind=ClipboardKind::image;legacy.image_format=ClipboardImageFormat::tiff;legacy.image=tiff();check(payload==legacy,"Shared and legacy encoded payload equality compares original bytes");check(ownerReads&&workerDecodes,"No native-reader callback runs on decode worker");
    const auto reads=ranks.size();const auto jobs=decodeCalls.load();for(unsigned n=0;n<100;++n)check(!capture.queueCapacityAvailable(),"No idle image jobs");check(ranks.size()==reads&&decodeCalls==jobs,"Capture has no polling reads or decode churn");
    blocked=std::make_shared<CaptureBlock>();CaptureRelease unblock{blocked};++sequence;capture.begin(sequence);blocked->await();mode=1;++sequence;capture.begin(sequence);check(history.items().front().payload.text==u"new text","New text immediately supersedes pending image");blocked->release();executor.waitIdle();executor.drain();check(history.items().size()==2&&history.items().front().payload.text==u"new text","Stale decode cannot insert over newer text");blocked.reset();
    ++sequence;mode=2;capture.begin(sequence);check(history.items().front().payload.kind==ClipboardKind::files,"Files retain first priority without image work");for(const auto m:{3u,4u,5u}){mode=m;++sequence;const auto before=history.items().size();capture.begin(sequence);check(history.items().size()==before,"Excluded empty or invalid native content never mutates history");}
    mode=0;busyOnce=true;++sequence;check(capture.begin(sequence)==State::busy,"Busy first read does not acknowledge or decode");check(capture.retryBusy()&&capture.state()==State::pending,"One explicit owner retry resumes same captured generation");executor.waitIdle();executor.drain();executor.waitIdle();executor.drain();
    mode=0;++sequence;const auto rankStart=ranks.size();capture.begin(sequence);busyOnce=true;executor.waitIdle();executor.drain();check(capture.state()==State::busy&&ranks.back()==1,"Busy fallback keeps the failed PNG behind it");check(capture.retryBusy(),"Existing owner retry resumes busy fallback");executor.waitIdle();executor.drain();check(std::vector<unsigned>(ranks.begin()+static_cast<std::ptrdiff_t>(rankStart),ranks.end())==std::vector<unsigned>({0,1,1}),"Busy TIFF retry never restarts PNG or changes sequence");
    ++sequence;capture.begin(sequence);executor.waitIdle();const auto before=history.items().size();++sequence;executor.drain();check(capture.state()==State::stale&&history.items().size()==before,"OS sequence changed before delivery forbids fallback or insertion");
    ClipboardHistory fallback;std::vector<ClipboardEncodedFormat>formats;ClipboardCapture allBad(fallback,executor,reader,[&]{return sequence;},[](auto,auto){},[&](const auto&i){formats.push_back(i.format);return ClipboardImageResult{{},ClipboardImageError::decode,-1};});mode=0;++sequence;allBad.begin(sequence);for(unsigned i=0;i<4;++i){executor.waitIdle();executor.drain();}check(formats==std::vector<ClipboardEncodedFormat>({ClipboardEncodedFormat::png,ClipboardEncodedFormat::tiff,ClipboardEncodedFormat::dibV5,ClipboardEncodedFormat::dib}),"Exact same-sequence source PNG TIFF then Windows DIBV5 DIB order");check(fallback.items().size()==1&&fallback.items().front().payload.text==u"fallback text","Corrupt image formats can fall back to same-sequence text");
    auto block=std::make_shared<CaptureBlock>();CaptureRelease release{block};unsigned late{};ClipboardHistory untouched;auto disposable=std::make_unique<ClipboardCapture>(untouched,executor,reader,[&]{return sequence;},[&](auto,auto){++late;},[block](const auto&i){block->wait();return thumbnail(i);});++sequence;disposable->begin(sequence);block->await();const auto initial=late;disposable.reset();block->release();executor.waitIdle();executor.drain();check(late==initial&&untouched.items().empty(),"Capture destruction cancels route before borrowed history/executor teardown");
}
void battery() {
    auto value=decode_power_status({});
    check(value.available&&!value.present&&!value.percent&&!value.ac_connected&&!value.charging,"unknown native fields remain unknown");
    value=decode_power_status({0,1,37,1200,4000});
    check(value.present==true&&value.ac_connected==false&&value.charging==false&&value.percent==37u,"battery offline");
    check(value.remaining_seconds==1200u&&value.full_seconds==4000u&&value.fully_charged==false,"power duration and full state");
    value=decode_power_status({1,8,73});
    check(value.charging==true&&value.ac_connected==true&&value.percent==73u,"charge signal");
    value=decode_power_status({0,8,73});
    check(value.charging==false,"offline AC must override stale charging flag");
    value=decode_power_status({255,8,73});
    check(value.charging==true&&value.ac_connected==true,"charging infers AC only when native AC is unknown");
    value=decode_power_status({1,1,100});
    check(value.charging==false&&value.fully_charged==true,"plugged full battery is not charging");
    value=decode_power_status({1,128,50,123,400});
    check(value.present==false&&value.charging==false&&!value.percent&&!value.remaining_seconds&&!value.full_seconds,"no battery strips battery-only data");
    value=decode_power_status({1,255,255});
    check(!value.charging&&!value.present&&!value.percent&&!value.fully_charged,"unknown flag must not fabricate a mode");
    value=decode_power_status({0,1,101});check(!value.percent,"invalid percentage ignored");
}
void notification() {
    NotificationBatch batch;
    check(!batch.request(0),"zero event does not post");
    check(batch.request(1),"first event posts");
    check(!batch.request(2)&&!batch.request(1),"duplicate callbacks coalesce");
    check(batch.drain()==3&&batch.drain()==0,"one drain preserves every event bit");
    check(batch.request(4)&&batch.drain()==4,"next batch posts again");
    batch.cancel();check(!batch.active()&&!batch.request(8)&&batch.drain()==0,"terminal cancel ignores stale callbacks");
    NotificationBatch concurrent;std::atomic<unsigned> posts{};
    std::vector<std::thread> workers;
    for(unsigned i=0;i<8;++i)workers.emplace_back([&,i]{for(unsigned n=0;n<1000;++n)if(concurrent.request(1u<<i))++posts;});
    for(auto& worker:workers)worker.join();
    check(posts==1&&concurrent.drain()==255,"cross-thread callbacks post one batch without losing bits");
    ClipboardSequence sequence;sequence.begin(42);
    check(!sequence.should_capture(42)&&!sequence.should_capture(0),"no initial or inaccessible clipboard capture");
    check(sequence.should_capture(43),"new clipboard change eligible");sequence.acknowledge(43);
    check(!sequence.should_capture(43),"acknowledge prevents observer reentry");sequence.copied(44);
    check(!sequence.should_capture(44)&&sequence.should_capture(45),"self-copy suppressed, later external copy accepted");
    sequence.begin(std::numeric_limits<std::uint32_t>::max());check(sequence.should_capture(1),"sequence wrap safe");
    check(!excludes_clipboard({}),"no marker permits capture");
    check(excludes_clipboard({true,false,{}}),"sensitive marker excludes every payload");
    check(excludes_clipboard({false,true,{}}),"malformed privacy metadata fails closed");
    check(excludes_clipboard({false,false,0})&&!excludes_clipboard({false,false,1}),"Windows history opt-out honored");
}
void clipboard() {
    ClipboardHistory history(2);
    check(history.items().empty()&&history.capacity()==2&&history.retained_bytes()==0,"session history starts empty");
    check(history.ingest(text(u"private"),true)==ClipboardInsertResult::excluded&&history.items().empty(),"exclude before payload insertion");
    check(history.ingest(text(u"first"))==ClipboardInsertResult::inserted,"first insert");const auto first=history.items()[0].id;
    check(history.pin(first,true),"pin item");
    check(history.ingest(text(u"second"))==ClipboardInsertResult::inserted,"second insert");const auto second=history.items()[0].id;
    check(history.ingest(text(u"first"))==ClipboardInsertResult::moved&&history.items()[0].id==first&&history.items()[0].pinned,"duplicate moves newest and keeps identity/pin");
    check(history.ingest(text(u"first"))==ClipboardInsertResult::duplicate,"newest duplicate is no-op");
    check(history.ingest(text(u"third"))==ClipboardInsertResult::inserted&&!history.find(second)&&history.find(first),"eviction protects older pinned item");
    check(history.retained_bytes()==20,"retain budget tracks UTF-16 storage");
    const auto third=history.items()[0].id;history.pin(third,true);
    check(history.ingest(text(u"fourth"))==ClipboardInsertResult::full&&history.items().size()==2,"pinned full history never loses data");
    check(!history.set_capacity(1)&&history.capacity()==2,"cannot reduce below pinned count");
    history.clear();check(history.items().size()==2,"ordinary clear keeps pins");
    history.pin(third,false);check(history.set_capacity(1)&&!history.find(third)&&history.find(first),"capacity evicts unpinned only");
    history.clear(false);check(history.items().empty()&&history.retained_bytes()==0,"explicit clear all releases payloads");
    check(!history.erase(first)&&!history.pin(first,true),"missing item mutation is no-op");
    check(!history.set_capacity(0)&&!history.set_capacity(ClipboardHistory::maximum_capacity+1),"bounded capacity");
    check(!ClipboardHistory::valid(text(u"")),"empty text rejected");
    check(!ClipboardHistory::valid(text(std::u16string{0xd800})),"unpaired surrogate rejected");
    check(!ClipboardHistory::valid(text(std::u16string{u'a',0,u'b'})),"embedded nul rejected");
    check(ClipboardHistory::utf8_bytes(u"A中😀")==8,"correct UTF-8 byte accounting");
    check(ClipboardHistory::valid(text(std::u16string(ClipboardHistory::maximum_text_bytes,u'A'))),"one MiB ASCII remains accepted as Mac");
    check(!ClipboardHistory::valid(text(std::u16string(ClipboardHistory::maximum_text_bytes+1,u'A'))),"text size limit");
    check(!ClipboardHistory::valid(text(std::u16string(ClipboardHistory::maximum_text_bytes/3+1,u'中'))),"UTF-8 size limit covers non-ASCII");
    check(ClipboardHistory::url_text(u"https://endfield.example/path")&&ClipboardHistory::url_text(u"HTTPS://endfield.example"),"web URL classification");
    check(ClipboardHistory::url_text(u"mailto:a@example.com")&&ClipboardHistory::url_text(u"file:///C:/file.txt"),"non-web URL classification");
    check(!ClipboardHistory::url_text(u"https:///bad")&&!ClipboardHistory::url_text(u"https://a b")&&!ClipboardHistory::url_text(u"javascript:alert(1)"),"invalid/unsupported URL stays text");
    auto url=text(u"https://endfield.example");url.kind=ClipboardKind::url;check(ClipboardHistory::valid(url),"URL payload accepted");
    url.text=u"not a URL";check(!ClipboardHistory::valid(url),"mislabeled URL rejected");
    ClipboardPayload files;files.kind=ClipboardKind::files;files.files={u"C:\\test\\example.txt",u"\\\\server\\share\\file.png"};
    check(ClipboardHistory::valid(files),"drive and UNC references supported without file access");
    files.files={u"relative.txt"};check(!ClipboardHistory::valid(files),"relative file rejected");
    files.files={u"\\\\server\\share"};check(!ClipboardHistory::valid(files),"UNC reference needs a file");
    files.files={u"\\\\.\\device"};check(!ClipboardHistory::valid(files),"device path rejected");
    files.files=std::vector<std::u16string>(513,u"C:\\f");check(!ClipboardHistory::valid(files),"file-count limit");
    auto picture=ClipboardPayload{};picture.kind=ClipboardKind::image;picture.image=dib();
    check(ClipboardHistory::valid(picture),"packed DIB accepted");
    picture.image.pop_back();check(!ClipboardHistory::valid(picture),"truncated pixels rejected");
    picture.image=dib();little32(picture.image,4,0xffffffff);check(!ClipboardHistory::valid(picture),"negative DIB width rejected");
    picture.image=dib();little32(picture.image,8,0x80000000);check(!ClipboardHistory::valid(picture),"minimum signed height rejected without overflow");
    picture.image=dib();little32(picture.image,16,1);check(!ClipboardHistory::valid(picture),"unbounded compressed DIB not accepted");
    picture.image=dib(2,2,124);picture.image_format=ClipboardImageFormat::dib_v5;check(ClipboardHistory::valid(picture),"DIB V5 alpha bytes retained");
    little32(picture.image,112,124);little32(picture.image,116,1000);check(!ClipboardHistory::valid(picture),"out-of-range color profile rejected");
    picture.image=png();picture.image_format=ClipboardImageFormat::png;check(ClipboardHistory::valid(picture),"PNG bounds accepted");
    picture.image.push_back(0);check(!ClipboardHistory::valid(picture),"trailing PNG bytes rejected");
    picture.image=png();picture.image[16]=0xff;check(!ClipboardHistory::valid(picture),"oversized decoded PNG dimensions rejected");
    picture.image=png();picture.image[37]=0xff;check(!ClipboardHistory::valid(picture),"chunk length overflow rejected");
    picture.image=png();picture.text=u"extra";check(!ClipboardHistory::valid(picture),"mixed payloads rejected");
    AudioSnapshot audio;check(audio.paused&&!audio.available&&!audio.volume&&!audio.muted,"audio model has no fabricated device state");
}
void clipboard_image_limits(){
    using H=ClipboardHistory;
    check(H::maximum_image_bytes==64*1'048'576&&H::maximum_retained_bytes==128*1'048'576&&H::maximum_thumbnail_dimension==96,"Source encoded/history/thumbnail limits remain independent");
    for(const auto dimensions:std::array<std::array<std::uint32_t,2>,7>{{{100000,1000},{1000,100000},{10000,10000},{16385,1},{1,16385},{4097,4097},{1,1}}}){
        const auto w=dimensions[0],h=dimensions[1];
        check(H::valid_image_metadata(w,h,H::maximum_image_bytes),"Exact source dimensions and64MiB encoded boundary accepted without allocating image pixels");
        // Only the IHDR metadata is varied. This is a structural-parser fixture,
        // deliberately never submitted to a codec as a complete large image.
        auto bytes=png();big32(bytes,16,w);big32(bytes,20,h);
        check(H::valid_image(ClipboardImageFormat::png,bytes),"PNG preflight uses source dimensions rather than a full-RGBA memory estimate");
    }
    for(const auto dimensions:std::array<std::array<std::uint32_t,2>,8>{{{100001,1},{1,100001},{10000,10001},{100000,1001},{0,1},{1,0},{0xffffffffu,0xffffffffu},{0xffffffffu,1}}}){
        const auto w=dimensions[0],h=dimensions[1];check(!H::valid_image_metadata(w,h,68),"Out-of-source dimension or product limit rejects without overflow");
        auto bytes=png();big32(bytes,16,w);big32(bytes,20,h);check(!H::valid_image(ClipboardImageFormat::png,bytes),"PNG extent rejection agrees with shared metadata contract");
    }
    check(!H::valid_image_metadata(1,1,0)&&!H::valid_image_metadata(1,1,H::maximum_image_bytes+1),"Empty and oversized encoded payloads rejected with no giant allocation");
    // Thin, real packed DIB payloads prove both signed orientations use the
    // source axis bound while actual stride/pixel availability remains required.
    for(const auto header:{40u,124u}){
        auto bytes=dib(100000,1,header);const auto format=header==124?ClipboardImageFormat::dib_v5:ClipboardImageFormat::dib;
        check(H::valid_image(format,bytes),"100000-wide packed DIB remains within the encoded budget");little32(bytes,8,0xffffffffu);
        check(H::valid_image(format,bytes),"Top-down DIB uses absolute height safely");bytes.pop_back();check(!H::valid_image(format,bytes),"Larger allowed dimensions never bypass actual DIB byte availability");
        bytes=dib(100001,1,header);check(!H::valid_image(format,bytes),"Actual payload does not override source axis rejection");
    }
    auto header=dib();little32(header,4,10000);little32(header,8,10000);check(!H::valid_image(ClipboardImageFormat::dib,header),"Valid100MP metadata alone cannot stand in for missing packed DIB pixels");
    ClipboardPayload payload;payload.kind=ClipboardKind::image;payload.image_format=ClipboardImageFormat::png;payload.image=png();big32(payload.image,16,10000);big32(payload.image,20,10000);
    ClipboardHistory history(2);check(history.ingest(payload)==ClipboardInsertResult::inserted&&history.retained_bytes()==payload.image.size(),"History charges encoded bytes, never hypothetical100MP decoded RGBA");
    const auto id=history.items()[0].id;history.pin(id,true);check(history.ingest(payload)==ClipboardInsertResult::duplicate&&history.items()[0].pinned,"Expanded source acceptance preserves duplicate/pin identity");
    big32(payload.image,20,10001);check(history.ingest(payload)==ClipboardInsertResult::invalid&&history.items().size()==1&&history.find(id)&&history.retained_bytes()==68,"Over-limit metadata cannot mutate existing retained history");
    history.clear(false);check(history.retained_bytes()==0,"Encoded image storage releases normally");
}
void stereo(){
    check(!audio_stereo_balance(0,0)&&!audio_stereo_balance(-1,.5f)&&!audio_stereo_balance(.5f,std::numeric_limits<float>::quiet_NaN()),"Unknown/zero stereo state never invents balance");
    for(unsigned n=0;n<=1000;++n){const auto balance=float(n)/500-1;const auto levels=audio_stereo_levels(.8f,balance);const auto restored=audio_stereo_balance(levels[0],levels[1]);check(restored&&std::abs(*restored-balance)<2e-7f&&std::max(levels[0],levels[1])==.8f,"Source stereo math preserves peak across entire balance range");}
    std::array<float,2>levels{.4f,.8f};std::vector<unsigned>writes;AudioStereoAccess access{[&](unsigned n,float&v){v=levels[n];return 0;},[&](unsigned n,float v){writes.push_back(n);levels[n]=v;return 0;}};
    check(apply_audio_stereo_balance(-1,access)==0&&levels==std::array{.8f,0.f},"Injected stereo transaction applies full left pan");
    levels={.4f,.8f};writes.clear();access.write=[&](unsigned n,float v){writes.push_back(n);levels[n]=v;return writes.size()==2?-42:0;};check(apply_audio_stereo_balance(0,access)==-42&&levels==std::array{.4f,.8f}&&writes==std::vector<unsigned>{0,1,1,0},"Partially failing second channel restores every attempted original in reverse order");
    levels={.4f,.8f};writes.clear();unsigned reads{};access.read=[&](unsigned n,float&v){v=++reads==3?std::numeric_limits<float>::infinity():levels[n];return 0;};access.write=[&](unsigned n,float v){writes.push_back(n);levels[n]=v;return 0;};check(apply_audio_stereo_balance(.2f,access)<0&&levels==std::array{.4f,.8f}&&writes==std::vector<unsigned>{0,0},"Invalid native readback rolls back before writing the next channel");
    writes.clear();check(apply_audio_stereo_balance(2,access)<0&&writes.empty(),"Invalid balance rejects before touching injected hardware");
}
}
int main() {
    try {battery();notification();clipboard();clipboard_image_limits();capture();stereo();std::cout<<checks<<" synthetic system-service checks passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
