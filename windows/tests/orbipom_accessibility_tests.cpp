// Minigame accessibility names against the unchanged Mac sources
// (tools/orbipom_accessibility_reference.py -> orbipom-accessibility-source.json).
// Portable; an injected synthetic runtime only, no VM, window or game data.
#include "modules/orbipom_accessibility.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
namespace {
using namespace endfield;namespace m=modules;using J=ehud::data::Json;using L=core::Language;unsigned checks{};
void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
constexpr std::array<std::pair<L,const char*>,5>languages{{{L::english,"english"},{L::simplifiedChinese,"simplifiedChinese"},{L::traditionalChinese,"traditionalChinese"},{L::japanese,"japanese"},{L::korean,"korean"}}};
struct Fake final:m::OrbiPomRuntimePort {
    m::OrbiPomSnapshot value;std::optional<std::string>failure;
    const m::OrbiPomSnapshot&snapshot()const noexcept override{return value;}const std::optional<std::string>&error()const noexcept override{return failure;}
    void start(std::optional<std::uint32_t>)override{value={};value.state="playing";}void advance(double)override{}void move(core::Point)override{}void pointerUp(core::Point)override{}
    bool drop()override{return true;}bool activate(m::OrbiPomSkill s)override{value.skill=s;value.skillPhase="selecting";return true;}void cancelSkill()override{value.skill.reset();value.skillPhase.reset();}
    void pause(bool p)override{value.paused=p;}void highScore(std::int64_t)override{}
};
std::string expected(const J&fixture,std::string_view id,const m::OrbiPomSnapshot&s,const char*language){
    if(!fixture["overrides"][std::string(id)].isNull())return fixture["overrides"][std::string(id)][language].string();
    if(id=="start")return fixture["start"][s.state=="idle"?"idle":"other"][language].string();
    const auto&skill=fixture["skills"][std::string(id)];check(!skill.isNull(),"Every exposed minigame action has a Mac accessibility name");
    const auto suffix=skill["suffix"].isNumber()?std::to_string(skill["suffix"].integer()):std::to_string(s.swapCharge)+"/6";
    return skill["title"][language].string()+" "+suffix;
}
void compare(const J&fixture,const m::OrbiPomSession&session,const m::OrbiPomState&state,std::span<const char* const>ids,const char*why){
    for(const auto&[language,key]:languages){const auto ax=m::orbiPomAccessibility(session,state,language);const auto actions=state.actions();
        check(!ax.rules&&ax.buttons.size()==ids.size()&&actions.count==ids.size(),why);
        for(std::size_t n=0;n<ids.size();++n){const auto&b=ax.buttons[n];const auto&hit=actions.items[n];
            check(b.id==ids[n]&&b.action==hit.action&&b.rect==hit.rect&&b.enabled==hit.enabled,"Accessible buttons keep the source id, action, rectangle and enabled state in paint order");
            check(b.label==expected(fixture,b.id,session.snapshot(),key),"Accessible minigame buttons read the Mac AX label in all five languages");}}
}
void run(const J&fixture){
    check(fixture["authority"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1"&&fixture["rules"]["paragraphs"].array().size()==9,"Fixture belongs to the pinned Mac source");
    Fake*fake{};m::OrbiPomSession session(0,{[&]{auto p=std::make_unique<Fake>();fake=p.get();return p;},{},{}});m::OrbiPomState state(session);
    check(m::orbiPomAccessibility(session,state,L::english)==m::OrbiPomAccessibility{},"Inactive minigame exposes no accessible controls, like the hidden Mac AX buttons");
    state.setPresented(true);state.setActive(true);
    {constexpr std::array ids{"start","clear","wind","shake","swap","rules"};compare(fixture,session,state,ids,"Idle board exposes Start, the four skills and Rules");}
    check(state.perform(m::OrbiPomAction::start)&&fake&&session.snapshot().isPlaying(),"Synthetic runtime starts");
    fake->value.energy=3;fake->value.swapCharge=4;
    {constexpr std::array ids{"pause","restart","clear","wind","shake","swap","rules"};compare(fixture,session,state,ids,"Running board exposes pause/restart, skills with their source suffixes and Rules");}
    check(state.perform(m::OrbiPomAction::clear)&&session.snapshot().skill,"Synthetic skill selection");
    {constexpr std::array ids{"pause","restart","clear","wind","shake","swap","cancelSkill","rules"};compare(fixture,session,state,ids,"Selected skill adds its Cancel control");}
    state.perform(m::OrbiPomAction::cancelSkill);state.perform(m::OrbiPomAction::restart);
    {constexpr std::array ids{"cancelRestart","confirmRestart","clear","wind","shake","swap","rules"};compare(fixture,session,state,ids,"Restart confirmation exposes Cancel/Confirm with disabled skills and Rules");
     const auto ax=m::orbiPomAccessibility(session,state,L::english);check(!ax.buttons[2].enabled&&!ax.buttons.back().enabled,"Confirmation disables skills and Rules like the source canvas");}
    state.perform(m::OrbiPomAction::cancelRestart);fake->value.state="over";
    {constexpr std::array ids{"start","clear","wind","shake","swap","rules"};compare(fixture,session,state,ids,"Finished game offers Play again");}
    state.setRulesPresented(true);
    for(const auto&[language,key]:languages){const auto ax=m::orbiPomAccessibility(session,state,language);std::string value;for(std::size_t n=0;n<9;++n){if(n)value+='\n';value+=fixture["rules"]["paragraphs"].array()[n][key].string();}
        check(ax.buttons.empty()&&ax.rules&&ax.rulesLabel==fixture["rules"]["label"][key].string()&&ax.rulesValue==value&&ax.rulesCloseLabel==fixture["rules"]["close"][key].string(),"Open rules hide the board buttons and expose the Mac rules label, joined paragraphs and Close");
        const auto&r=fixture["rules"]["closeRect"].array();check(ax.rulesClose==core::Rect{r[0].number(),r[1].number(),r[2].number(),r[3].number()},"Rules Close item keeps its source menu-local frame");}
    state.setActive(false);check(m::orbiPomAccessibility(session,state,L::english)==m::OrbiPomAccessibility{},"Leaving the module hides every accessible control");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass orbipom-accessibility-source.json");std::ifstream file(argv[1],std::ios::binary);check(bool(file),"Mac minigame accessibility fixture opens");const std::string bytes(std::istreambuf_iterator<char>(file),{});run(J::parse(bytes,1024*1024));std::cout<<"PASS "<<checks<<" minigame accessibility checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
