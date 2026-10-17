#include "tools/settings_automation.hpp"
#ifdef _WIN32
#include <algorithm>
#include <cmath>
#include <utility>

namespace endfield::tools {
namespace {
using Node = native::SettingsAutomationNode;
using Role = Node::Role;
using Space = Node::Space;
constexpr std::string_view safetyPrefix = "settings:safety:";
core::Rect intersection(core::Rect a, core::Rect b) {
    const double x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    const double right = std::min(a.x + a.width, b.x + b.width), bottom = std::min(a.y + a.height, b.y + b.height);
    return {x, y, std::max(0., right - x), std::max(0., bottom - y)};
}
core::Rect join(core::Rect a, core::Rect b) {
    const double x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {x, y, std::max(a.x + a.width, b.x + b.width) - x, std::max(a.y + a.height, b.y + b.height) - y};
}
// The glyph a row shows instead of a value (it opens a page or acts).
bool glyph(std::string_view value) { return value.empty() || value == "›" || value == "↺" || value == "↗"; }
const modules::SettingsAction* action(const modules::SettingsPageView& view, std::string_view id) {
    for (const auto& a : view.actions) if (a.id == id) return &a;
    return nullptr;
}
std::optional<int> safetyButton(std::string_view id) {
    if (!id.starts_with(safetyPrefix)) return std::nullopt;
    id.remove_prefix(safetyPrefix.size());
    if (id == "cancel") return 0;
    if (id == "confirm") return 1;
    return std::nullopt;
}
Node node(std::string id, std::string name, Role role, core::Rect rect, std::size_t parent = Node::topLevel) {
    Node n;
    n.id = std::move(id); n.name = std::move(name); n.role = role; n.rect = rect; n.parent = parent;
    return n;
}
// HUDScaleSafetyView: the card, its title and message, Revert and Keep.
void card(const SettingsPreview& preview, double now, std::vector<Node>& out) {
    const auto viewport = preview.safetyViewport();
    if (!(viewport.width > 0) || !(viewport.height > 0)) return;
    const auto& safety = preview.safety();
    const auto geometry = modules::SettingsSafety::geometry(viewport);
    const auto& strings = safety.strings();
    const double width = geometry.card.width;
    const bool enabled = !safety.pose(now).dismissing;
    auto dialog = node("settings:safety", strings.title, Role::dialog, {0, 0, width, 190});
    dialog.help = strings.message;
    out.push_back(std::move(dialog));
    out.push_back(node("settings:safety:title", strings.title, Role::text, {23, 42, width - 46, 28}, 0));
    out.push_back(node("settings:safety:message", strings.message, Role::text, {23, 78, width - 46, 42}, 0));
    for (int n = 0; n < 2; ++n) {
        auto button = node(n ? "settings:safety:confirm" : "settings:safety:cancel", n ? strings.confirm : strings.cancel, Role::button, n ? geometry.confirm : geometry.cancel, 0);
        button.enabled = enabled; button.focusable = true; button.focused = safety.focused() == n;
        out.push_back(std::move(button));
    }
    for (auto& n : out) n.space = Space::card;
}
} // namespace

double settingsSliderStep(std::string_view id, double minimum, double maximum) noexcept {
    if (id == "duration") return 1;
    if (id == "uiScale") return .05;
    if (id.starts_with("position")) return .01;
    return (maximum - minimum) * .02;
}

std::vector<Node> settingsAutomationNodes(const SettingsPreview& preview, double now) {
    std::vector<Node> out;
    if (preview.safety().modal()) { card(preview, now, out); return out; }
    const auto module = preview.inputModule();
    const auto* page = module ? preview.view(*module) : nullptr;
    if (!page) return out;
    const auto& view = page->view();
    const auto& status = view.accessibilityStatus;
    out.reserve(view.rows.size() + view.actions.size() + 6);
    // An action-backed node: the action's rect and enabled state, the page status as help.
    const auto acting = [&](const modules::SettingsAction& a, std::string name, Role role, std::size_t parent = Node::topLevel) {
        auto n = node(a.id, std::move(name), role, a.rect, parent);
        n.enabled = a.enabled; n.help = status;
        return n;
    };
    auto heading = node("settings:heading", view.heading, Role::heading, {12, 1, 306, 23});
    heading.level = 1;
    out.push_back(std::move(heading));
    if (const auto* back = action(view, "back")) out.push_back(acting(*back, back->label, Role::button));
    if (view.restore) {
        out.push_back(node("settings:restore:title", view.restoreTitle, Role::text, {35, 114, 330, 25}));
        out.push_back(node("settings:restore:message", view.restoreMessage, Role::text, {35, 151, 330, 34}));
        for (const auto id : {"restore:cancel", "restore:confirm"}) if (const auto* a = action(view, id)) out.push_back(acting(*a, a->label, Role::button));
    } else {
        // Choice page rows, the app icons and the theme colours are the items
        // of one list (its bounds: the union of its items).
        std::optional<std::size_t> list;
        const auto open = [&](std::string id, std::string name) {
            if (!list) { list = out.size(); out.push_back(node(std::move(id), std::move(name), Role::list, {})); }
            return *list;
        };
        const auto close = [&] {
            if (!list) return;
            std::optional<core::Rect> bounds;
            for (std::size_t n = *list + 1; n < out.size(); ++n) if (out[n].parent == *list) bounds = bounds ? join(*bounds, out[n].rect) : out[n].rect;
            if (bounds) out[*list].rect = *bounds;
            list.reset();
        };
        const auto viewport = modules::SettingsPresentation::viewport();
        double y = viewport.y - view.scroll;
        for (const auto& r : view.rows) {
            const core::Rect rect{16, y, 366, r.height - 2};
            y += r.height;
            const auto visible = intersection(rect, viewport);
            const bool shown = visible.width > 0 && visible.height >= std::min(25., rect.height);
            if (!shown) continue;
            using Kind = modules::SettingsRowKind;
            if (r.kind == Kind::selection) {
                const auto* a = action(view, r.id);
                if (!a) continue;
                auto item = acting(*a, r.detail.empty() ? r.title : r.title + ", " + r.detail, Role::item, open("settings:list", view.heading));
                item.on = r.selected;
                out.push_back(std::move(item));
                continue;
            }
            if (r.kind == Kind::icons) {
                for (const auto& icon : r.icons) if (const auto* a = action(view, "appIcon:" + icon.id)) {
                    auto item = acting(*a, a->label, Role::item, open("settings:icons", view.heading));
                    item.on = r.value == icon.id;
                    out.push_back(std::move(item));
                }
                continue;
            }
            close();
            switch (r.kind) {
            case Kind::toggle: if (const auto* a = action(view, r.id)) { auto n = acting(*a, r.title, Role::toggle); n.on = r.selected; out.push_back(std::move(n)); } break;
            case Kind::choice:
                if (const auto* a = action(view, r.id)) {
                    auto n = acting(*a, r.title, Role::button);
                    n.valued = !glyph(r.value); if (n.valued) n.value = r.value;
                    // The recorder holds the keyboard while it records (the HUD routes every key to it).
                    if (r.id == "capture") n.focusable = n.focused = view.capturing;
                    out.push_back(std::move(n));
                }
                break;
            case Kind::slider:
                for (const auto& s : view.sliders) if (s.id == r.id) {
                    auto n = node(s.id, s.label, Role::slider, s.rect);
                    n.value = s.formatted; n.number = s.value; n.minimum = s.minimum; n.maximum = s.maximum; n.step = settingsSliderStep(s.id, s.minimum, s.maximum);
                    out.push_back(std::move(n));
                    break;
                }
                break;
            case Kind::palette: {
                // The swatches the page shows (all five once the row is fully in view).
                for (const auto& a : view.actions) if (a.id.starts_with("accent:")) {
                    auto item = acting(a, a.label, Role::item, open("settings:palette", r.title));
                    item.on = std::string_view(a.id).substr(7) == r.value;
                    out.push_back(std::move(item));
                }
                close();
                if (const auto* custom = action(view, "customColor")) out.push_back(acting(*custom, custom->label, Role::button));
                break;
            }
            case Kind::link:
                if (const auto* a = action(view, r.id)) {
                    auto n = acting(*a, glyph(r.value) ? r.title : r.title + ", " + r.value, Role::link);
                    n.valued = !r.url.empty(); n.value = r.url;
                    out.push_back(std::move(n));
                } else out.push_back(node(r.id, glyph(r.value) ? r.title : r.title + ", " + r.value, Role::text, visible));
                break;
            case Kind::info: out.push_back(node(r.id, r.value.empty() ? r.title : r.title + ", " + r.value, Role::text, visible)); break;
            case Kind::heading: { auto n = node(r.id, r.title, Role::heading, visible); n.level = 2; out.push_back(std::move(n)); break; }
            case Kind::selection: case Kind::icons: break;
            }
        }
        close();
    }
    if (!view.status.empty()) out.push_back(node("settings:status", view.status, Role::status, {14, 299, view.capturing ? 276. : 372., 31}));
    if (const auto* cancel = action(view, "capture:cancel")) out.push_back(acting(*cancel, cancel->label, Role::button));
    return out;
}

SettingsAutomation::SettingsAutomation(SettingsPreview& preview, native::ScreenReaderPeerHooks hooks) : preview_(preview), hooks_(std::move(hooks)) {}
SettingsAutomation::~SettingsAutomation() { provider_.reset(); }
bool SettingsAutomation::refresh(IRawElementProviderFragmentRoot* group) {
    if (!hooks_.now) return false; // no clock: nothing can press like a click
    bool replaced = false;
    if (!provider_ || group != group_) {
        replaced = provider_ && provider_->size() > 0;
        provider_.reset();
        group_ = group;
        native::SettingsAutomationHost host;
        host.root = group;
        host.nodes = [this] { return settingsAutomationNodes(preview_, hooks_.now()); };
        host.toScreen = [this](core::Rect rect, Space space) -> std::optional<core::Rect> {
            const auto client = space == Space::card ? preview_.safetyClientRect(rect) : preview_.clientRect(rect);
            return native::screenReaderClientToScreen(hooks_.window, preview_.clientMetrics().scale, client);
        };
        host.invoke = [this](std::string_view id) {
            if (const auto button = safetyButton(id)) return preview_.performSafety(*button, hooks_.now());
            return preview_.perform(id, hooks_.now());
        };
        host.setValue = [this](std::string_view id, double value) { return preview_.setSlider(id, value, hooks_.now()); };
        host.focus = [this](std::string_view id) { const auto button = safetyButton(id); return button && preview_.focusSafety(*button, hooks_.now()); };
        host.changed = hooks_.changed;
        provider_ = std::make_unique<native::NativeSettingsAutomation>(std::move(host));
    }
    return provider_->refresh() || replaced;
}
std::size_t SettingsAutomation::count() const noexcept { return provider_ ? provider_->size() : 0; }
long SettingsAutomation::child(std::size_t index, IRawElementProviderFragment** out) const {
    if (!provider_) { if (out) *out = nullptr; return E_INVALIDARG; }
    return provider_->child(index, out);
}
long SettingsAutomation::fromPoint(double x, double y, IRawElementProviderFragment** out) const {
    if (!provider_) { if (out) *out = nullptr; return S_OK; }
    return provider_->fromPoint(x, y, out);
}
void SettingsAutomation::disconnect() noexcept {
    if (provider_) provider_->disconnect();
}
} // namespace endfield::tools
#endif
