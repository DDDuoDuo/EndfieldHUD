#include "native/work_mode_automation.hpp"
#ifdef _WIN32
#include "core/localization.hpp"

namespace endfield::native {
std::vector<ProfileAutomationControl>workModeAutomationControls(const modules::WorkModePresentation&state,core::Language language){
    return workModeAutomationControls(state,language,std::nullopt);
}
std::vector<ProfileAutomationControl>workModeAutomationControls(const modules::WorkModePresentation&state,core::Language language,const std::optional<std::string>&editingText){
    const auto actions=state.actions();
    std::vector<ProfileAutomationControl>out;out.reserve(actions.size()+1);
    const auto status=state.accessibilityStatus()+core::localized(". Space starts, pauses or resumes.","。空格键开始、暂停或继续。",language);
    for(const auto&a:actions)out.push_back({a.id,a.label,a.id=="work:focusAccess"?a.label:status,a.rect,ProfileAutomationRole::button});
    // HUDWorkModeInteraction.beginEditing: field.setAccessibilityLabel/Help.
    if(editingText)out.push_back({std::string(workModeDurationEditorID),
        core::localized("Custom countdown: minutes and seconds","自定义倒计时：分和秒",language),
        core::localized("Enter minutes or min:sec, between 0:01 and 1440:00. Return saves; Escape cancels.","输入分钟数或分:秒，范围为 0:01 至 1440:00。回车保存，Esc 取消。",language),
        modules::WorkModePresentation::durationEditorRect(),ProfileAutomationRole::edit,0,0,1,1,*editingText});
    return out;
}
}
#endif
