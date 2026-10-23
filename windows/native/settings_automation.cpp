#include "native/settings_automation.hpp"
#ifdef _WIN32
#include "native/hud_design_automation.hpp"

// The element code lives in native/hud_design_automation.cpp (AutomationTree,
// LIBRARY-DESIGN.md Q2); Settings keeps its node vocabulary through this
// adapter until Phase B1 moves it onto the design library.
namespace endfield::native {
namespace {
using Node = SettingsAutomationNode;
AutomationNode::Role role(Node::Role r) noexcept {
    switch (r) {
    case Node::Role::heading: return AutomationNode::Role::heading;
    case Node::Role::text: return AutomationNode::Role::text;
    case Node::Role::status: return AutomationNode::Role::status;
    case Node::Role::button: return AutomationNode::Role::button;
    case Node::Role::link: return AutomationNode::Role::link;
    case Node::Role::toggle: return AutomationNode::Role::toggle;
    case Node::Role::slider: return AutomationNode::Role::slider;
    case Node::Role::list: return AutomationNode::Role::list;
    case Node::Role::item: return AutomationNode::Role::item;
    case Node::Role::dialog: return AutomationNode::Role::dialog;
    }
    return AutomationNode::Role::text;
}
AutomationNode node(Node&& n) {
    AutomationNode a;
    a.id = std::move(n.id); a.name = std::move(n.name); a.help = std::move(n.help); a.value = std::move(n.value);
    a.role = role(n.role); a.space = static_cast<std::uint8_t>(n.space); a.rect = n.rect; a.parent = n.parent;
    a.enabled = n.enabled; a.on = n.on; a.focused = n.focused; a.focusable = n.focusable; a.valued = n.valued; a.level = n.level;
    a.number = n.number; a.minimum = n.minimum; a.maximum = n.maximum; a.step = n.step;
    return a;
}
}
struct NativeSettingsAutomation::State {
    std::unique_ptr<AutomationTree> tree;
};
NativeSettingsAutomation::NativeSettingsAutomation(SettingsAutomationHost host) : state_(std::make_shared<State>()) {
    AutomationTreeHost h;
    h.root = host.root;
    h.nodes = [nodes = std::move(host.nodes)] {
        std::vector<AutomationNode> out;
        if (!nodes) return out;
        auto list = nodes();
        out.reserve(list.size());
        for (auto& n : list) out.push_back(node(std::move(n)));
        return out;
    };
    h.toScreen = [toScreen = std::move(host.toScreen)](core::Rect r, std::uint8_t space) -> std::optional<core::Rect> {
        return toScreen ? toScreen(r, static_cast<Node::Space>(space)) : std::nullopt;
    };
    h.invoke = std::move(host.invoke);
    h.setValue = std::move(host.setValue);
    h.focus = std::move(host.focus);
    h.changed = std::move(host.changed);
    state_->tree = std::make_unique<AutomationTree>(std::move(h));
}
NativeSettingsAutomation::~NativeSettingsAutomation() { state_->tree.reset(); }
bool NativeSettingsAutomation::refresh() { return state_->tree->refresh(); }
std::size_t NativeSettingsAutomation::size() const noexcept { return state_->tree->size(); }
std::size_t NativeSettingsAutomation::nodes() const noexcept { return state_->tree->nodes(); }
HRESULT NativeSettingsAutomation::child(std::size_t index, IRawElementProviderFragment** out) const { return state_->tree->child(index, out); }
HRESULT NativeSettingsAutomation::focused(IRawElementProviderFragment** out) const { return state_->tree->focused(out); }
HRESULT NativeSettingsAutomation::fromPoint(double x, double y, IRawElementProviderFragment** out) const { return state_->tree->fromPoint(x, y, out); }
void NativeSettingsAutomation::disconnect() noexcept { state_->tree->disconnect(); }
SettingsAutomationStats NativeSettingsAutomation::stats() const noexcept {
    const auto s = state_->tree->stats();
    return {s.refreshes, s.structureChanges, s.created, s.disconnected, s.invokes, s.toggles, s.selections, s.values, s.focusMoves, s.focusRequests, s.announcements};
}
} // namespace endfield::native
#endif
