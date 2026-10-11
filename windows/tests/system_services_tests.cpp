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
    // Fixed, valid 1x1 RGBA PNG (every chunk CRC is correct, so the real WIC
    // thumbnail job of the synthetic-clipboard service run accepts it).
    constexpr std::array<std::uint8_t,68> fixture{
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,
        0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};
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
void interrupted_capture(){
    // ClipboardSequence.reopen: an acknowledged change that never reached
    // history is readable again, the app's own copy stays excluded.
    ClipboardSequence sequence;sequence.begin(10);
    check(sequence.should_capture(11),"A new change is eligible");sequence.acknowledge(11);
    check(!sequence.should_capture(11),"An acknowledged change is not read twice");sequence.reopen();
    check(sequence.should_capture(11)&&sequence.should_capture(12),"An interrupted capture is read again");
    sequence.copied(13);sequence.reopen();check(!sequence.should_capture(13)&&sequence.should_capture(14),"Reopening never re-reads the app's own copy");
    // Header-only dimension preflight (Mac imageDimensionsTooLarge).
    auto large=png();big32(large,16,100001);ClipboardEncodedImage image{ClipboardEncodedFormat::png,std::make_shared<const std::vector<std::uint8_t>>(large)};
    check(clipboardEncodedImageTooLarge(image),"A PNG header beyond 100,000 px is too large");
    image.bytes=std::make_shared<const std::vector<std::uint8_t>>(png());check(!clipboardEncodedImageTooLarge(image),"A small PNG is not too large");
    auto tall=dib(1,1,40);little32(tall,4,20000);little32(tall,8,static_cast<std::uint32_t>(-20000));image={ClipboardEncodedFormat::dib,std::make_shared<const std::vector<std::uint8_t>>(tall)};
    check(clipboardEncodedImageTooLarge(image),"A top-down DIB of 400 MP is too large from its header");
    big32(large,16,0);image={ClipboardEncodedFormat::png,std::make_shared<const std::vector<std::uint8_t>>(large)};check(!clipboardEncodedImageTooLarge(image),"A zero dimension is invalid, not too large");
    // ClipboardCapture reports it without trying another format.
    endfield::app::UtilityExecutor executor([]{});ClipboardHistory history;std::vector<unsigned>ranks;std::vector<std::pair<ClipboardCaptureState,std::int32_t>>states;unsigned decodes{};
    ClipboardCapture capture(history,executor,[&](std::uint32_t expected,unsigned first){ranks.push_back(first);ClipboardReadResult r;r.sequence=expected;r.state=ClipboardReadState::payload;r.nextImage=first+1;r.payload.kind=ClipboardKind::image;r.payload.image_format=ClipboardCapture::imageFormats[first];auto bytes=png();big32(bytes,16,100001);r.payload.image=std::move(bytes);return r;},
        []{return 5u;},[&](auto state,auto native){states.emplace_back(state,native);},[&](const auto&){++decodes;return ClipboardImageResult{};});
    check(capture.begin(5)==ClipboardCaptureState::invalid&&ranks==std::vector<unsigned>{0}&&decodes==0&&!states.empty()&&states.back().second==static_cast<std::int32_t>(0x80070216u),
        "An oversized PNG header reports ERROR_ARITHMETIC_OVERFLOW once, decodes nothing and tries no fallback");
}
}
#ifdef _WIN32
#include <map>
#include <shlobj_core.h> // DROPFILES
namespace {
// Synthetic session clipboard: tests never read, write or listen to the
// user's clipboard. Sequence numbers advance on every content change.
struct SyntheticClipboard final : ClipboardBackend {
    std::map<std::wstring,UINT>names;UINT next{0xc100};
    std::map<UINT,std::vector<std::uint8_t>>content;DWORD current{500};
    bool busy{},listening{},opened{};unsigned opens{},writes{};
    std::vector<std::vector<UINT>>written;
    UINT register_format(const wchar_t*name)override{const auto[it,inserted]=names.try_emplace(name,next);if(inserted)++next;return it->second;}
    bool listen(HWND)override{listening=true;return true;}
    void unlisten(HWND)noexcept override{listening=false;}
    DWORD sequence()override{return current;}
    HRESULT open(HWND)override{++opens;if(busy)return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);if(opened)return E_UNEXPECTED;opened=true;return S_OK;}
    void close()noexcept override{opened=false;}
    bool available(UINT format)override{return opened&&format&&content.contains(format);}
    ClipboardFormatData data(UINT format)override{const auto it=content.find(format);if(!opened||it==content.end()||it->second.empty())return {};return {it->second.data(),it->second.size()};}
    ClipboardWriteResult write(HWND,std::span<const ClipboardFormatWrite>formats)override{ClipboardWriteResult r;if(busy){r.status=HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);return r;}
        r.opened=true;content.clear();std::vector<UINT>list;for(const auto&f:formats){content[f.format].assign(f.bytes.begin(),f.bytes.end());list.push_back(f.format);}written.push_back(std::move(list));++writes;r.sequence=++current;return r;}
    UINT id(const wchar_t*name)const{return names.at(name);}
    // Another application copies these formats.
    void copy(std::vector<std::pair<UINT,std::vector<std::uint8_t>>>formats){content.clear();for(auto&[format,bytes]:formats)content[format]=std::move(bytes);++current;}
};
std::vector<std::uint8_t> wide(std::u16string_view text){std::vector<std::uint8_t>b((text.size()+1)*2);std::memcpy(b.data(),text.data(),text.size()*2);return b;}
std::vector<std::uint8_t> narrow(std::string_view text){std::vector<std::uint8_t>b(text.begin(),text.end());b.push_back(0);return b;}
std::vector<std::uint8_t> drop(const std::vector<std::u16string>&files){std::size_t units=1;for(const auto&f:files)units+=f.size()+1;std::vector<std::uint8_t>b(sizeof(DROPFILES)+units*2);DROPFILES h{};h.pFiles=sizeof(DROPFILES);h.fWide=TRUE;std::memcpy(b.data(),&h,sizeof(h));std::size_t at=sizeof(h);for(const auto&f:files){std::memcpy(b.data()+at,f.data(),f.size()*2);at+=(f.size()+1)*2;}return b;}
void drops(){
    std::vector<std::u16string>files;
    check(parse_clipboard_drop({},files)==HRESULT_FROM_WIN32(ERROR_INVALID_DATA),"No drop data is invalid");
    auto bytes=drop({u"C:\\one.txt",u"\\\\server\\share\\two.png"});check(parse_clipboard_drop({bytes.data(),bytes.size()},files)==S_OK&&files==std::vector<std::u16string>{u"C:\\one.txt",u"\\\\server\\share\\two.png"},"A wide CF_HDROP list is read in order");
    std::vector<std::uint8_t>ansi(sizeof(DROPFILES));DROPFILES h{};h.pFiles=sizeof(DROPFILES);std::memcpy(ansi.data(),&h,sizeof(h));for(const char c:std::string_view("C:\\a.txt\0C:\\b.txt\0\0",19))ansi.push_back(static_cast<std::uint8_t>(c));
    check(parse_clipboard_drop({ansi.data(),ansi.size()},files)==S_OK&&files==std::vector<std::u16string>{u"C:\\a.txt",u"C:\\b.txt"},"An ANSI CF_HDROP list is converted");
    bytes=drop({});check(parse_clipboard_drop({bytes.data(),bytes.size()},files)==HRESULT_FROM_WIN32(ERROR_INVALID_DATA)&&files.empty(),"An empty drop is invalid");
    bytes=drop({u"C:\\x"});bytes.resize(bytes.size()-2);check(parse_clipboard_drop({bytes.data(),bytes.size()},files)==HRESULT_FROM_WIN32(ERROR_INVALID_DATA),"A list without its final terminator is invalid");
    bytes=drop(std::vector<std::u16string>(513,u"C:\\f"));check(parse_clipboard_drop({bytes.data(),bytes.size()},files)==HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES),"More than 512 files is Mac tooManyFiles");
    bytes=drop(std::vector<std::u16string>(512,u"C:\\f"));check(parse_clipboard_drop({bytes.data(),bytes.size()},files)==S_OK&&files.size()==512,"512 files are accepted");
    bytes=drop(std::vector<std::u16string>(40,u"C:\\"+std::u16string(30000,u'x')));check(parse_clipboard_drop({bytes.data(),bytes.size()},files)==HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES),"An aggregate path list over 1 MB is Mac tooManyFiles");
    bytes=drop({u"C:\\"+std::u16string(32768,u'x')});check(parse_clipboard_drop({bytes.data(),bytes.size()},files)==HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW),"A path over 32767 units overflows");
    bytes=drop({u"C:\\x"});h.pFiles=static_cast<DWORD>(bytes.size());std::memcpy(bytes.data(),&h,sizeof(h));check(parse_clipboard_drop({bytes.data(),bytes.size()},files)==HRESULT_FROM_WIN32(ERROR_INVALID_DATA),"A list offset outside the block is invalid");
}
constexpr UINT notice=WM_APP+7;
struct Harness {
    SystemServices*services{};unsigned delivered{},timers{};
    static LRESULT CALLBACK proc(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto*self=reinterpret_cast<Harness*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(self&&message==WM_TIMER)++self->timers;
        if(self&&self->services&&self->services->handle_message(message,w,l)){if(message==notice)++self->delivered;return 0;}
        return DefWindowProcW(hwnd,message,w,l);
    }
    HWND hwnd{};ATOM atom{};
    Harness(){WNDCLASSW c{};c.lpfnWndProc=proc;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldHUD.SystemServicesTests";atom=RegisterClassW(&c);
        hwnd=CreateWindowExW(0,c.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,c.hInstance,nullptr);if(!hwnd)throw std::runtime_error("Message-only test window");SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));}
    ~Harness(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(L"EndfieldHUD.SystemServicesTests",GetModuleHandleW(nullptr));}
    unsigned queued()const{MSG m;unsigned n{};std::vector<MSG>held;while(PeekMessageW(&m,hwnd,notice,notice,PM_REMOVE)){++n;held.push_back(m);}for(const auto&x:held)PostMessageW(x.hwnd,x.message,x.wParam,x.lParam);return n;}
    void pump(){MSG m;while(PeekMessageW(&m,hwnd,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
};
void services(){
    Harness window;auto board=std::make_shared<SyntheticClipboard>();PowerStatus power{1,8,50,3600,7200};unsigned powerReads{},batteryChanges{},clipboardChanges{};
    endfield::app::UtilityExecutor executor([]{});
    SystemServices services;window.services=&services;
    struct Detach{Harness&w;~Detach(){w.services=nullptr;}}detach{window}; // before the services are destroyed
    SystemServicesOptions options;options.clipboard=board;options.power=[&](PowerStatus&out){++powerReads;out=power;return true;};
    check(services.start(window.hwnd,notice,[&](ServiceChange change){(change==ServiceChange::battery?batteryChanges:clipboardChanges)++;},false,&executor,options)==S_OK,"Services start over the synthetic clipboard and power source");
    check(board->listening&&powerReads==1&&batteryChanges==1&&services.battery().percent==50u&&services.battery().charging==true&&board->opens==0,"Start reads power once and nothing from the clipboard");
    const auto url=board->id(L"UniformResourceLocatorW"),ansiURL=board->id(L"UniformResourceLocator"),pngFormat=board->id(L"PNG"),owner=board->id(L"EndfieldHUD.Windows.ClipboardOwner.v1");
    const auto change=[&]{check(services.handle_message(WM_CLIPBOARDUPDATE,0,0),"Clipboard updates are recognized");window.pump();};
    const auto drain=[&]{for(unsigned n=0;n<4;++n){executor.waitIdle();executor.drain();services.clipboard_queue_capacity_available();}window.pump();};
    const auto front=[&]()->const ClipboardPayload&{check(!services.clipboard().items().empty(),"History has an item");return services.clipboard().items().front().payload;};

    // WM_POWERBROADCAST storm: one posted notice and one read per batch, no timer.
    board->copy({{CF_UNICODETEXT,wide(u"storm text")}});power.battery_percent=51;
    for(unsigned n=0;n<10000;++n)check(services.handle_message(WM_POWERBROADCAST,n%3==0?PBT_POWERSETTINGCHANGE:n%3==1?PBT_APMPOWERSTATUSCHANGE:PBT_APMRESUMEAUTOMATIC,0),"Power broadcasts are recognized");
    for(unsigned n=0;n<1000;++n)services.handle_message(WM_CLIPBOARDUPDATE,0,0);
    check(window.queued()==1,"A storm of 11,000 power and clipboard messages posts one notice");
    window.pump();
    check(window.delivered==1&&powerReads==2&&batteryChanges==2&&services.battery().percent==51u,"The batch reads power once and reports one change");
    check(board->opens==1&&services.clipboard().items().size()==1&&front().text==u"storm text","The batch reads the clipboard once");
    for(unsigned n=0;n<1000;++n)services.handle_message(WM_POWERBROADCAST,PBT_APMSUSPEND,0);
    window.pump();check(window.delivered==1&&powerReads==2,"Suspend broadcasts post nothing");
    for(unsigned n=0;n<100;++n)services.handle_message(WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,0);window.pump();
    check(window.delivered==2&&powerReads==3&&batteryChanges==2,"An unchanged reading after a later batch reports nothing");
    check(MsgWaitForMultipleObjects(0,nullptr,FALSE,120,QS_ALLINPUT)==WAIT_TIMEOUT&&window.timers==0,"No timer or follow-up message exists after the batches");

    // Suspension: changes are ignored, then the latest is captured once.
    services.set_clipboard_suspended(true);const auto opens=board->opens;const auto size=services.clipboard().items().size();
    for(const auto*text:{u"one",u"two",u"three"}){board->copy({{CF_UNICODETEXT,wide(text)}});change();}
    check(services.clipboard_suspended()&&board->opens==opens&&services.clipboard().items().size()==size&&services.refresh_clipboard()==S_FALSE,"Nothing is read while suspended, explicit refreshes included");
    services.set_clipboard_suspended(false);
    check(board->opens==opens+1&&services.clipboard().items().size()==size+1&&front().text==u"three","Resuming captures only the latest change, once");
    change();check(board->opens==opens+1,"The caught-up change is not read again");

    // An image still validating when capture is suspended is read again on resume.
    board->copy({{pngFormat,png()}});change();check(services.clipboard_status()==E_PENDING,"An image capture is validating");
    services.set_clipboard_suspended(true);drain();
    check(front().kind!=ClipboardKind::image,"The interrupted image never enters history while suspended");
    services.set_clipboard_suspended(false);drain();
    check(front().kind==ClipboardKind::image&&front().image_format==ClipboardImageFormat::png&&services.clipboard_status()==S_OK,"Resuming reads the interrupted image again");

    // Explicit URL type (Mac .URL before .string) and link copy-back.
    board->copy({{url,wide(u"https://例子.com/路径")},{CF_UNICODETEXT,wide(u"例子 link")}});change();
    check(front().kind==ClipboardKind::url&&front().url==u"https://xn--fsqu00a.com/%E8%B7%AF%E5%BE%84"&&front().text==u"例子 link","UniformResourceLocatorW is the link, CF_UNICODETEXT its original string");
    const auto linkID=services.clipboard().items().front().id;
    board->copy({{url,wide(u"x-endfield:open")}});change();
    check(front().kind==ClipboardKind::url&&front().url==u"x-endfield:open"&&front().text==u"x-endfield:open","An explicit URL of any scheme without a string is its own text");
    board->copy({{url,wide(u"not a url")},{CF_UNICODETEXT,wide(u"https://example.com")}});change();
    check(front().kind==ClipboardKind::url&&front().url==u"https://example.com"&&front().text==u"https://example.com","An unparsable URL type falls back to the string");
    board->copy({{ansiURL,narrow("https://ansi.example/path")}});change();
    check(front().kind==ClipboardKind::url&&front().url==u"https://ansi.example/path","The ANSI URL type is read when the wide one is absent");
    board->copy({{CF_UNICODETEXT,wide(u"  https://example.com/a\n")}});change();
    check(front().kind==ClipboardKind::url&&front().url==u"https://example.com/a"&&front().text==u"  https://example.com/a\n","A link string is trimmed for its URL and kept as typed");
    board->copy({{CF_UNICODETEXT,wide(u"plain words")}});change();check(front().kind==ClipboardKind::text&&front().url.empty(),"Other strings stay text");
    const auto textID=services.clipboard().items().front().id;
    check(services.copy_clipboard_item(linkID)==S_OK&&board->written.back()==std::vector<UINT>{url,CF_UNICODETEXT,owner},"A link is written back as the URL type plus the string");
    check(board->content[url]==wide(u"https://xn--fsqu00a.com/%E8%B7%AF%E5%BE%84")&&board->content[CF_UNICODETEXT]==wide(u"例子 link"),"The URL is absoluteString and the string is the original");
    const auto afterCopy=board->opens;change();check(board->opens==afterCopy&&services.clipboard().items().front().id==textID,"The app's own copy-back is never captured");
    check(services.copy_clipboard_item(textID)==S_OK&&board->written.back()==std::vector<UINT>{CF_UNICODETEXT,owner},"Text is written back as the string only");

    // Distinct capture statuses (ClipboardStore.CaptureError).
    const auto items=services.clipboard().items().size();
    board->copy({{CF_UNICODETEXT,wide(std::u16string(ClipboardHistory::maximum_text_bytes+1,u'a'))}});change();
    check(services.clipboard_status()==HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),"Text over 1 MB is textTooLarge");
    board->copy({{CF_UNICODETEXT,wide(std::u16string(ClipboardHistory::maximum_text_bytes/2,u'中'))}});change();
    check(services.clipboard_status()==HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),"Text over 1 MB of UTF-8 is textTooLarge");
    board->copy({{url,wide(u"https://example.com/"+std::u16string(ClipboardHistory::maximum_text_bytes,u'a'))}});change();
    check(services.clipboard_status()==HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),"A URL type over 1 MB is textTooLarge");
    board->copy({{CF_HDROP,drop(std::vector<std::u16string>(513,u"C:\\f"))}});change();
    check(services.clipboard_status()==HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES),"More than 512 files is tooManyFiles");
    auto huge=png();big32(huge,16,100001);board->copy({{pngFormat,huge}});change();
    check(services.clipboard_status()==static_cast<HRESULT>(0x80070216),"A PNG beyond 100,000 px is imageDimensionsTooLarge");
    board->copy({{pngFormat,std::vector<std::uint8_t>(ClipboardHistory::maximum_image_bytes+1,1)}});change();
    check(services.clipboard_status()==HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW),"An image over 64 MB is imageTooLarge");
    board->content.clear();
    check(services.clipboard().items().size()==items,"Rejected captures never change history");
    board->copy({{CF_UNICODETEXT,wide(u"secret")},{board->id(L"ExcludeClipboardContentFromMonitorProcessing"),{}}});change();
    check(services.clipboard().items().size()==items&&front().text!=u"secret","Excluded content is never captured");
    // A busy clipboard: one coalesced retry, then the explicit refresh.
    board->busy=true;const auto busyOpens=board->opens;board->copy({{CF_UNICODETEXT,wide(u"after busy")}});change();window.pump();
    check(board->opens==busyOpens+2&&services.clipboard_status()==HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED),"A busy clipboard is retried once and reported");
    window.pump();check(board->opens==busyOpens+2,"No further retry loop runs");
    board->busy=false;check(services.refresh_clipboard()==S_OK&&front().text==u"after busy"&&services.clipboard_status()==S_OK,"An explicit refresh captures the released change");
    services.stop();check(!board->listening&&!services.running(),"Stop removes the listener");
}
}
#endif
int main() {
    try {battery();notification();clipboard();clipboard_image_limits();capture();stereo();interrupted_capture();
#ifdef _WIN32
        drops();services();
#endif
        std::cout<<checks<<" synthetic system-service checks passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
