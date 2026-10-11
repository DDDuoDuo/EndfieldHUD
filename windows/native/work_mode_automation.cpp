#include "native/work_mode_automation.hpp"
#ifdef _WIN32
#include "core/localization.hpp"

namespace endfield::native {
std::vector<ProfileAutomationControl>workModeAutomationControls(const modules::WorkModePresentation&state,core::Language language){
    const auto actions=state.actions();
    std::vector<ProfileAutomationControl>out;out.reserve(actions.size());
    const auto status=state.accessibilityStatus()+core::localized(". Space starts, pauses or resumes.","。空格键开始、暂停或继续。",language);
    for(const auto&a:actions)out.push_back({a.id,a.label,a.id=="work:focusAccess"?a.label:status,a.rect,ProfileAutomationRole::button});
    return out;
}
}
#endif
