// Source NowPlayingFixtureBackend port: explicit synthetic player for
// diagnostics/module coverage. No real session, player, audio or network.
#include "native/now_playing_fixture.hpp"
#include "tools/now_playing_session.hpp"
#include <iostream>
#include <stdexcept>
namespace n=endfield::native;namespace m=endfield::modules;namespace t=endfield::tools;using Q=endfield::app::UtilityExecutor;
namespace {
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}
}
int main(){
    try{
        const auto cover=n::nowPlayingFixtureCover();
        check(cover&&cover->size()==54+96*96*4&&(*cover)[0]=='B'&&(*cover)[1]=='M'&&cover==n::nowPlayingFixtureCover(),"Synthetic 96x96 cover is one immutable bounded payload");
        check((*cover)[54+(40*96+5)*4+2]==217&&(*cover)[54+(40*96+60)*4+2]==23,"Cover keeps the source gold bar and dark field");
#ifdef _WIN32
        const auto decoded=n::decodeNowPlayingImage(*cover);check(decoded.image&&decoded.image->width==96&&decoded.image->height==96,"WIC decodes the synthetic cover");
#endif
        Q q{[]{}};double now=5;n::NativeNowPlayingService service(q,n::nowPlayingFixtureProvider([&]{return now;}),[]{},[&]{return now;});
        t::NowPlayingSession owner(service,q,{},[]{},[](std::span<const std::uint8_t>b){auto f=std::make_shared<n::NotesImageFrame>();f->width=f->height=1;f->straightRGBA={b[0],0,0,255};return n::NowPlayingImageResult{f};});
        const auto flush=[&]{for(int k=0;k<6;++k){q.waitIdle();q.drain();owner.utilityCompleted(now);}};
        owner.setActive(true,now);flush();
        const auto&input=owner.presentation().input();
        check(input.track&&input.track->title=="EndfieldHUD Fixture 1"&&input.track->artist=="Local verification"&&input.track->album=="Now Playing","Source fixture metadata");
        check(input.track->duration==210.&&input.track->position==42.&&!input.track->isPlaying&&!input.failed,"Source fixture timeline starts paused at 42 of 210 seconds");
        check(input.lyrics&&input.lyrics->lines().size()==3&&owner.presentation().lyricRows()[1]=="Current","Source fixture timed lyrics reach the rows");
        check(owner.artwork()&&owner.artwork()->straightRGBA[0]=='B'&&input.coverAvailable,"Fixture cover passes through the artwork owner");
        check(owner.dispatch({m::NowPlayingIntentKind::next,input.session},now),"Fixture accepts next");flush();
        check(owner.presentation().input().track->title=="EndfieldHUD Fixture 2"&&owner.presentation().input().track->position==0.,"Next advances the synthetic track to 0 s");
        check(owner.dispatch({m::NowPlayingIntentKind::playPause,owner.presentation().input().session},now),"Fixture accepts play");flush();now=15;owner.deadline(now);
        check(owner.presentation().input().track->isPlaying,"Play toggles the synthetic player");
        check(owner.dispatch({m::NowPlayingIntentKind::seek,owner.presentation().input().session,300},now),"Fixture accepts seek");flush();
        check(owner.presentation().input().track->position==210.,"Seek clamps to the fixture duration");
        owner.setActive(false,now);now=20;owner.setActive(true,now);flush();
        check(owner.presentation().input().track->title=="EndfieldHUD Fixture 2","Fixture player state survives conceal/reopen like the controller-owned source fixture");
        owner.setActive(false,now);q.shutdown();
        std::cout<<"Now Playing fixture: "<<checks<<" checks passed (synthetic player only)\n";return 0;
    }catch(const std::exception&e){std::cerr<<"Now Playing fixture failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
