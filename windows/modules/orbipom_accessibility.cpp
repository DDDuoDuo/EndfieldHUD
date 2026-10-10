#include "modules/orbipom_accessibility.hpp"

namespace endfield::modules {
std::string_view orbiPomActionID(OrbiPomAction action)noexcept{
    switch(action){
    case OrbiPomAction::start:return "start";case OrbiPomAction::pause:return "pause";case OrbiPomAction::restart:return "restart";
    case OrbiPomAction::cancelRestart:return "cancelRestart";case OrbiPomAction::confirmRestart:return "confirmRestart";
    case OrbiPomAction::cancelSkill:return "cancelSkill";case OrbiPomAction::clear:return "clear";case OrbiPomAction::wind:return "wind";
    case OrbiPomAction::shake:return "shake";case OrbiPomAction::swap:return "swap";case OrbiPomAction::rules:return "rules";
    }
    return {};
}
OrbiPomAccessibility orbiPomAccessibility(const OrbiPomSession&session,const OrbiPomState&state,core::Language language){
    OrbiPomAccessibility out;if(!state.active())return out;
    const auto L=[&](std::string_view english,std::string_view simplified){return core::localized(english,simplified,language);};
    if(state.rulesPresented()){
        out.rules=true;out.rulesLabel=L("Rules","游戏规则");out.rulesCloseLabel=L("Close","关闭");
        const auto paragraphs=orbiPomRuleParagraphs(language);for(std::size_t n=0;n<paragraphs.size();++n){if(n)out.rulesValue+='\n';out.rulesValue+=paragraphs[n];}
        return out;
    }
    const auto&s=session.snapshot();const auto actions=state.actions();out.buttons.reserve(actions.count);
    for(std::size_t n=0;n<actions.count;++n){const auto&hit=actions.items[n];std::string label;
        switch(hit.action){
        case OrbiPomAction::start:label=s.state=="idle"?L("Start","开始"):L("Play again","再来一局");break;
        case OrbiPomAction::pause:label=L("Pause / resume","暂停 / 继续");break;
        case OrbiPomAction::restart:label=L("Restart","重新开始");break;
        case OrbiPomAction::cancelRestart:case OrbiPomAction::cancelSkill:label=L("Cancel","取消");break;
        case OrbiPomAction::confirmRestart:label=L("Confirm","确认");break;
        case OrbiPomAction::rules:label=L("Rules","游戏规则");break;
        case OrbiPomAction::clear:label=L("Eliminate","消除")+" "+std::to_string(orbiPomEnergyCost(OrbiPomSkill::clear));break;
        case OrbiPomAction::wind:label=L("Wind","风场")+" "+std::to_string(orbiPomEnergyCost(OrbiPomSkill::wind));break;
        case OrbiPomAction::shake:label=L("Shake","震动")+" "+std::to_string(orbiPomEnergyCost(OrbiPomSkill::shake));break;
        case OrbiPomAction::swap:label=L("Swap","交换")+" "+std::to_string(s.swapCharge)+"/6";break;
        }
        out.buttons.push_back({std::string(orbiPomActionID(hit.action)),std::move(label),hit.rect,hit.enabled,hit.action});
    }
    return out;
}
}
