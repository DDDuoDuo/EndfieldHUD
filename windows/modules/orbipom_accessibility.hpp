#pragma once
#include "modules/orbipom_presentation.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace endfield::modules {
// Source HUDOrbiPomInteraction accessibility contract for a native UI
// Automation provider (OrbiPomAXButton per canvas action, OrbiPomRulesMenu).
// Rects are 440x440 canvas points (the rules Close item is menu-local; the
// menu sits at NativeOrbiPomScene::rulesBounds()); the host projects them like
// pointer hit tests and invokes OrbiPomPreview::perform(action). Content-event
// only (after an action, state or language change), never per frame.
struct OrbiPomAccessibleButton {
    std::string id,label;core::Rect rect;bool enabled{};OrbiPomAction action{};
    bool operator==(const OrbiPomAccessibleButton&)const=default;
};
struct OrbiPomAccessibility {
    // Hidden unless the module is active, and while the rules menu is open.
    std::vector<OrbiPomAccessibleButton>buttons;
    // Rules menu element: label "Rules", value = the nine paragraphs joined by
    // a newline, and its Close item (Notes retained-menu "Close" label).
    bool rules{};std::string rulesLabel,rulesValue,rulesCloseLabel;core::Rect rulesClose{291,8,23,23};
    bool operator==(const OrbiPomAccessibility&)const=default;
};
std::string_view orbiPomActionID(OrbiPomAction)noexcept; // source action.id
OrbiPomAccessibility orbiPomAccessibility(const OrbiPomSession&,const OrbiPomState&,core::Language);
}
