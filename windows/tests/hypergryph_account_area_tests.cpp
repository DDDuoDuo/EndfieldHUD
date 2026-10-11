// Account Linking area: the hidden-coverage providers behind the production
// signed client and login policy. The coverage fixture must be the generator's
// output for the current Mac oracle fixture; the in-process community host
// answers only the fixed endpoints and never completes inside start(); the
// fake official-site host follows the WebView2 presenter's completion rules;
// the UI Automation peers behave like the Mac's transparent AX buttons, and
// the HUD window's UIA root (app/hud_automation.cpp, compiled into this test)
// lists them through the ModuleOwner hooks exactly as the area implements
// them. No network, browser, credential store or real account is used.
#ifdef _WIN32
#include "app/areas/account_coverage.hpp"
#include "app/hud_automation.hpp"
#include "core/data/file_io.hpp"
#include "modules/hypergryph_account_crypto.hpp"
#include "native/account_automation.hpp"
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace h = endfield::modules::hypergryph;
namespace areas = endfield::app::areas;
using ehud::data::Json;
unsigned checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }

void fixture(const std::filesystem::path& coverage, const std::filesystem::path& source) {
    const auto sourceBytes = ehud::data::detail::readFile(source, 8 * 1024 * 1024);
    const auto coverageBytes = ehud::data::detail::readFile(coverage, 256 * 1024);
    check(sourceBytes && coverageBytes, "Both fixtures are readable");
    const auto digest = h::sha256(std::string_view(*sourceBytes));
    check(Json::parse(*coverageBytes)["provenance"]["sourceSHA256"].string() == h::lowercaseHex(digest),
          "The coverage fixture was generated from the current Mac oracle fixture (rerun tools/hypergryph_account_coverage_fixture.py)");
    const auto f = areas::loadAccountCoverageFixture(coverage);
    check(f.region == h::Region::mainland && f.site == h::login_policy::siteURL(f.region) && !f.navigation.empty() && !f.popups.empty(),
          "The fake site host covers the official mainland site, navigation and popups");
    check(f.messages.size() >= 2 && f.messages.back().session && !f.messages.front().session, "Rejected bridge messages precede the accepted session");
    check(f.expired.status == 401 && f.expected["roles"].array().size() == 2, "Expired envelope and Mac role expectations are present");
}

void communityHost(const std::filesystem::path& coverage) {
    const auto f = areas::loadAccountCoverageFixture(coverage);
    areas::SyntheticCommunityHost host(f.responses);
    h::HypergryphAccountClient client(host, [&f] { return f.now; });
    const h::Credentials credentials{"C1", "T1", std::string("D1")};
    std::optional<h::Outcome<std::vector<h::Role>>> roles;
    auto request = client.bindings(credentials, h::Region::mainland, [&](h::Outcome<std::vector<h::Role>> value) { roles = std::move(value); });
    check(request && !roles && host.queued() == 1 && host.activeRequests() == 1, "Completions never run inside start()");
    check(host.drain() == 1 && roles && roles->ok() && roles->value().size() == 2 && host.activeRequests() == 0, "The bindings envelope decodes on the owner drain");
    const auto sent = host.requests().front();
    check(sent.host == "zonai.skland.com" && sent.path == "/api/v1/game/player/binding" && sent.header("sign") && sent.header("sign")->size() == 32 &&
          sent.header("cred") == std::optional<std::string>("C1") && sent.header("dId") == std::optional<std::string>("D1"), "The production client signs the request it sends");
    check(roles->value()[0].id() == f.expected["roles"].array()[0]["id"].string(), "The decoded role matches the Mac decode");
    // Expired session through a scripted one-shot override.
    host.script("/api/v1/game/endfield/card/detail", f.expired);
    std::optional<h::Outcome<h::Snapshot>> card;
    (void)client.profile(roles->value()[0], credentials, [&](h::Outcome<h::Snapshot> value) { card = std::move(value); });
    host.drain();
    check(card && !card->ok() && card->error().kind == h::ErrorKind::authenticationExpired, "HTTP 401 code 10002 maps to authenticationExpired");
    card.reset();
    (void)client.profile(roles->value()[0], credentials, [&](h::Outcome<h::Snapshot> value) { card = std::move(value); });
    host.drain();
    check(card && card->ok() && card->value().stamina && card->value().stamina->maximum == 360, "The override is consumed once; the fixture envelope returns");
    // Anything but the fixed endpoints is refused, never answered.
    h::HttpRequest other;
    other.host = "example.invalid";
    other.path = "/api/v1/game/player/binding";
    std::optional<h::HttpOutcome> outcome;
    (void)host.start(other, [&](h::HttpOutcome value) { outcome = std::move(value); });
    host.drain();
    check(host.rejected() == 1 && outcome && outcome->failure && outcome->failure->kind == h::ErrorKind::transport, "Unknown hosts are refused");
    // A cancelled request delivers nothing and releases its slot.
    bool delivered{};
    auto cancelled = host.start(sent, [&](h::HttpOutcome) { delivered = true; });
    cancelled->cancel();
    host.drain();
    check(!delivered && host.activeRequests() == 0, "Cancellation drops the completion");
}

void loginHost(const std::filesystem::path& coverage) {
    const auto f = areas::loadAccountCoverageFixture(coverage);
    areas::SyntheticCommunityHost queue(f.responses);
    areas::SyntheticLoginHost login(queue, f.nonce);
    std::vector<std::pair<bool, h::LoginFailure>> results;
    const auto record = [&](std::optional<h::LoginResult> value, h::LoginFailure failure) { results.emplace_back(value.has_value(), failure); };
    login.present(h::Region::mainland, record);
    check(login.isPresenting() && login.session() && login.session()->nonce() == f.nonce && login.presentations() == 1, "Connect opens one session");
    login.present(h::Region::mainland, record);
    check(results.size() == 1 && results[0] == std::pair{false, h::LoginFailure::alreadyPresenting}, "A second presentation fails synchronously");
    auto* session = login.session();
    check(session->consent(1) && session->allowNavigation(f.site, true, false), "Consent opens the official site");
    const auto& accepted = f.messages.back();
    auto result = session->credentialMessage(Json::parse(accepted.body), accepted.mainFrame, accepted.frame, accepted.page);
    check(result && login.finish(std::move(result), h::LoginFailure::cancelled) && !login.isPresenting() && results.size() == 1,
          "Finishing disposes the session; the completion waits for the owner drain");
    queue.drain();
    check(results.size() == 2 && results[1].first, "The accepted session reaches the controller on the drain");
    login.present(h::Region::mainland, record);
    login.cancel();
    check(results.size() == 3 && results[2] == std::pair{false, h::LoginFailure::cancelled} && !login.isPresenting(), "Cancel completes synchronously");
    const auto& tags = login.diagnostics();
    check(std::find(tags.begin(), tags.end(), "cancelled") != tags.end(), "The cancelled session records its outcome tag");
    check(!login.finish(std::nullopt, h::LoginFailure::cancelled), "Nothing is finished twice");
}
// The Mac's transparent AX buttons as UIA fragments over a fake owner view.
void automation() {
    namespace n = endfield::native;
    using Microsoft::WRL::ComPtr;
    std::vector<n::AccountAutomationControl> controls{{"account:connect", "Connect", {10, 10, 100, 20}, true},
                                                     {"account:refresh", "Refresh", {120, 10, 20, 20}, false},
                                                     {"wallet:toggle", "Arknights: Endfield · Sanity 343 / 360", {300, 0, 168, 42}, true}};
    std::vector<std::string> pressed;
    n::AccountAutomationHost host;
    host.controls = [&] { return controls; };
    host.toScreen = [](endfield::core::Rect r) -> std::optional<endfield::core::Rect> { return endfield::core::Rect{100 + r.x * 2, 50 + r.y * 2, r.width * 2, r.height * 2}; };
    host.invoke = [&](std::string_view id) { pressed.emplace_back(id); return true; };
    n::NativeAccountAutomation a(host);
    check(a.refresh() && a.size() == 3 && !a.refresh(), "Peers are listed once; an unchanged refresh keeps the structure");
    ComPtr<IRawElementProviderFragment> connect, wallet, none;
    check(SUCCEEDED(a.fromPoint(100 + 60 * 2, 50 + 20 * 2, &connect)) && connect && SUCCEEDED(a.fromPoint(10, 10, &none)) && !none,
          "Hit testing maps physical screen pixels to the projected peer");
    ComPtr<IRawElementProviderSimple> simple;
    VARIANT value;
    VariantInit(&value);
    check(SUCCEEDED(connect.As(&simple)) && SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId, &value)) && value.vt == VT_I4 &&
          value.lVal == UIA_ButtonControlTypeId, "Peers are buttons");
    VariantClear(&value);
    check(SUCCEEDED(simple->GetPropertyValue(UIA_NamePropertyId, &value)) && value.vt == VT_BSTR && std::wstring_view(value.bstrVal) == L"Connect",
          "The accessibility label is the UIA name");
    VariantClear(&value);
    ComPtr<IInvokeProvider> invoke;
    check(SUCCEEDED(connect.As(&invoke)) && SUCCEEDED(invoke->Invoke()) && pressed == std::vector<std::string>{"account:connect"}, "Invoke presses the control");
    ComPtr<IRawElementProviderFragment> refresh;
    ComPtr<IInvokeProvider> disabled;
    check(SUCCEEDED(a.child(1, &refresh)) && SUCCEEDED(refresh.As(&disabled)) && disabled->Invoke() == UIA_E_ELEMENTNOTENABLED && pressed.size() == 1,
          "A disabled peer refuses Invoke");
    UiaRect bounds{};
    check(SUCCEEDED(a.child(2, &wallet)) && SUCCEEDED(wallet->get_BoundingRectangle(&bounds)) && bounds.left == 700 && bounds.top == 50 && bounds.width == 336,
          "Bounds are the projected screen rect");
    // The wallet label changes in place; a vanished control disconnects.
    controls[2].label = "Arknights: Endfield · Sanity 344 / 360";
    controls.erase(controls.begin() + 1);
    check(a.refresh() && a.size() == 2 && a.stats().created == 3 && a.stats().disconnected == 1, "Identity is kept per id; removed peers disconnect");
    check(refresh.As(&simple) == S_OK && simple->GetPropertyValue(UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE, "A disconnected peer is unavailable");
    ComPtr<IRawElementProviderFragment> walletAgain;
    check(SUCCEEDED(a.child(1, &walletAgain)) && walletAgain.Get() == wallet.Get(), "The wallet keeps its COM identity across refreshes");
    a.disconnect();
    check(a.size() == 0 && invoke->Invoke() == UIA_E_ELEMENTNOTAVAILABLE, "Disconnecting the tree ends every peer");
}
bool flagProperty(IUnknown* element, PROPERTYID id) {
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
    VARIANT value;
    VariantInit(&value);
    const bool on = element && SUCCEEDED(element->QueryInterface(IID_PPV_ARGS(&simple))) && SUCCEEDED(simple->GetPropertyValue(id, &value)) &&
                    value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE;
    VariantClear(&value);
    return on;
}
template <class Pattern> Microsoft::WRL::ComPtr<Pattern> pattern(IUnknown* element, PATTERNID id) {
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
    Microsoft::WRL::ComPtr<IUnknown> provider;
    Microsoft::WRL::ComPtr<Pattern> out;
    if (element && SUCCEEDED(element->QueryInterface(IID_PPV_ARGS(&simple))) && SUCCEEDED(simple->GetPatternProvider(id, &provider)) && provider) (void)provider.As(&out);
    return out;
}
// Windows additions over the Mac buttons: the sync toggles offer Toggle with
// their check state, and an open menu's keyboard selection is the UIA focus.
void focusAndToggle() {
    namespace n = endfield::native;
    using Microsoft::WRL::ComPtr;
    n::AccountAutomationControl sync{"account:syncProfile", "Sync personal profile", {360, 203, 28, 28}, true};
    sync.toggled = false;
    const std::vector<n::AccountAutomationControl> face{{"account:connect", "Connect", {276, 7, 112, 28}, true}, sync};
    std::vector<n::AccountAutomationControl> controls = face;
    std::vector<std::string> pressed;
    n::AccountAutomationHost host;
    host.controls = [&] { return controls; };
    host.toScreen = [](endfield::core::Rect r) -> std::optional<endfield::core::Rect> { return endfield::core::Rect{100 + r.x * 2, 50 + r.y * 2, r.width * 2, r.height * 2}; };
    host.invoke = [&](std::string_view id) {
        pressed.emplace_back(id);
        if (id == "account:syncProfile") controls[1].toggled = !*controls[1].toggled; // the canvas emits the opposite state
        return true;
    };
    n::NativeAccountAutomation a(host);
    check(a.refresh() && a.size() == 2 && a.stats().focusMoves == 0, "Without an open menu no peer holds the keyboard focus");
    ComPtr<IRawElementProviderFragment> connect, toggle;
    check(SUCCEEDED(a.child(0, &connect)) && SUCCEEDED(a.child(1, &toggle)), "Face peers are listed");
    check(!flagProperty(connect.Get(), UIA_IsKeyboardFocusablePropertyId) && !flagProperty(connect.Get(), UIA_HasKeyboardFocusPropertyId),
          "Face buttons are not keyboard targets (the HUD routes its own keys)");
    const auto toggler = pattern<IToggleProvider>(toggle.Get(), UIA_TogglePatternId);
    check(toggler && !pattern<IInvokeProvider>(toggle.Get(), UIA_InvokePatternId), "A sync toggle offers Toggle instead of Invoke");
    check(pattern<IInvokeProvider>(connect.Get(), UIA_InvokePatternId) && !pattern<IToggleProvider>(connect.Get(), UIA_TogglePatternId), "Other buttons offer Invoke only");
    ToggleState state{ToggleState_Indeterminate};
    check(SUCCEEDED(toggler->get_ToggleState(&state)) && state == ToggleState_Off, "An unchecked toggle reports Off");
    check(SUCCEEDED(toggler->Toggle()) && pressed == std::vector<std::string>{"account:syncProfile"} && a.stats().toggles == 1 && a.stats().invokes == 0,
          "Toggle performs the toggle's press");
    check(!a.refresh() && SUCCEEDED(toggler->get_ToggleState(&state)) && state == ToggleState_On, "The refreshed toggle reports On (same element)");
    ComPtr<IInvokeProvider> invoker;
    check(SUCCEEDED(toggle.As(&invoker)) && invoker->Invoke() == UIA_E_INVALIDOPERATION && pressed.size() == 1, "A toggle is not pressed through Invoke");
    controls[1].enabled = false;
    (void)a.refresh();
    check(toggler->Toggle() == UIA_E_ELEMENTNOTENABLED && pressed.size() == 1, "A disabled toggle refuses Toggle");
    // The painted status texts ride on Connect as its help; a new status
    // message is announced once (raised only while a client listens).
    const auto help = [&] {
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
        VARIANT value;
        VariantInit(&value);
        std::wstring out;
        if (SUCCEEDED(connect.As(&simple)) && SUCCEEDED(simple->GetPropertyValue(UIA_HelpTextPropertyId, &value)) && value.vt == VT_BSTR && value.bstrVal) out = value.bstrVal;
        VariantClear(&value);
        return out;
    };
    check(help().empty(), "No help without status text");
    controls[0].help = "Connected";
    (void)a.refresh();
    check(help() == L"Connected" && a.stats().announcements == 0, "Connect carries the status line as its help text");
    const std::string runtime = "Sign-in needs the Microsoft Edge WebView2 Runtime.";
    controls[0].help = "Connected\n" + runtime;
    controls[0].notice = runtime;
    (void)a.refresh();
    check(help() == L"Connected\nSign-in needs the Microsoft Edge WebView2 Runtime." && a.stats().announcements == 1, "A new status message is announced");
    (void)a.refresh();
    check(a.stats().announcements == 1, "An unchanged message is not announced again");
    controls[0].notice.clear();
    (void)a.refresh();
    check(a.stats().announcements == 1, "Clearing the message announces nothing");
    // A menu opens on its current row: the rows are keyboard targets and the
    // selected one has the focus; the arrows move it.
    controls = {{"account:role:a", "Endfield · Fixture", {120, 160, 260, 26}, true}, {"account:role:b", "Arknights · Fixture", {120, 191, 260, 26}, true}};
    for (auto& row : controls) row.focusable = true;
    controls[1].focused = true;
    check(a.refresh() && a.size() == 2 && a.stats().focusMoves == 1, "Opening a menu moves the keyboard focus to its selected row");
    ComPtr<IRawElementProviderFragment> first, second;
    check(SUCCEEDED(a.child(0, &first)) && SUCCEEDED(a.child(1, &second)) && flagProperty(first.Get(), UIA_IsKeyboardFocusablePropertyId) &&
          !flagProperty(first.Get(), UIA_HasKeyboardFocusPropertyId) && flagProperty(second.Get(), UIA_HasKeyboardFocusPropertyId),
          "Menu rows are focusable; only the selected one has the focus");
    controls[0].focused = true;
    controls[1].focused = false;
    check(!a.refresh() && a.stats().focusMoves == 2 && flagProperty(first.Get(), UIA_HasKeyboardFocusPropertyId) && !flagProperty(second.Get(), UIA_HasKeyboardFocusPropertyId),
          "An arrow moves the focus between the same elements");
    check(!a.refresh() && a.stats().focusMoves == 2, "An unchanged refresh announces nothing");
    controls[0].focused = false;
    check(!a.refresh() && a.stats().focusMoves == 2 && !flagProperty(first.Get(), UIA_HasKeyboardFocusPropertyId), "A row scrolled away leaves no focus");
    controls[0].focused = true;
    check(!a.refresh() && a.stats().focusMoves == 3, "Its return is announced again");
    controls = face;
    check(a.refresh() && a.stats().focusMoves == 3 && !flagProperty(first.Get(), UIA_HasKeyboardFocusPropertyId), "Closing the menu removes its rows and the focus");
    // Mac accessibilityValue: the wallet toggle's countdowns while its popover is open.
    controls = {{"wallet:toggle", "Arknights: Endfield · Sanity 343 / 360", {300, 0, 168, 42}, true}};
    ComPtr<IRawElementProviderFragment> wallet;
    check(a.refresh() && SUCCEEDED(a.child(0, &wallet)) && !pattern<IValueProvider>(wallet.Get(), UIA_ValuePatternId) &&
          pattern<IInvokeProvider>(wallet.Get(), UIA_InvokePatternId), "A closed wallet offers Invoke and no value");
    controls[0].value = "Next recovery 05:12; Full recovery 23:41:07";
    (void)a.refresh();
    const auto value = pattern<IValueProvider>(wallet.Get(), UIA_ValuePatternId);
    BSTR text{};
    BOOL readOnly{};
    check(value && SUCCEEDED(value->get_Value(&text)) && text && std::wstring_view(text) == L"Next recovery 05:12; Full recovery 23:41:07" &&
          SUCCEEDED(value->get_IsReadOnly(&readOnly)) && readOnly && value->SetValue(L"x") == UIA_E_NOTSUPPORTED, "The open wallet reports its countdowns as a read-only value");
    SysFreeString(text);
    a.disconnect();
}

// The area's ModuleOwner UIA hooks (app/areas/account_module.cpp) over a fake
// owner view, so the host root can be driven without the whole application.
class PeerOwner final : public endfield::app::ModuleOwner {
public:
    PeerOwner(std::vector<endfield::native::AccountAutomationControl>& controls, std::vector<std::string>& pressed) : controls_(controls), pressed_(pressed) {}
    ~PeerOwner() override { automation_.reset(); }
    std::string_view name() const noexcept override { return "account"; }
    bool presents(endfield::core::Module m) const noexcept override { return m == endfield::core::Module::account; }
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override {
        if (!automation_ || root_ != root) {
            automation_.reset();
            endfield::native::AccountAutomationHost host;
            host.root = root;
            host.controls = [this] { return controls_; };
            host.toScreen = [](endfield::core::Rect r) -> std::optional<endfield::core::Rect> { return endfield::core::Rect{100 + r.x * 2, 50 + r.y * 2, r.width * 2, r.height * 2}; };
            host.invoke = [this](std::string_view id) { pressed_.emplace_back(id); return true; };
            automation_ = std::make_unique<endfield::native::NativeAccountAutomation>(std::move(host));
            root_ = root;
            ++created;
        }
        return automation_->refresh();
    }
    std::size_t automationCount() const override { return automation_ ? automation_->size() : 0; }
    long automationChild(std::size_t index, IRawElementProviderFragment** out) const override {
        if (!automation_) { if (out) *out = nullptr; return E_INVALIDARG; }
        return automation_->child(index, out);
    }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override {
        if (!automation_) { if (out) *out = nullptr; return S_OK; }
        return automation_->fromPoint(x, y, out);
    }
    void automationDisconnect() noexcept override { if (automation_) automation_->disconnect(); }
    unsigned created{};

private:
    std::vector<endfield::native::AccountAutomationControl>& controls_;
    std::vector<std::string>& pressed_;
    std::unique_ptr<endfield::native::NativeAccountAutomation> automation_;
    IRawElementProviderFragmentRoot* root_{};
};
class PlainOwner final : public endfield::app::ModuleOwner {
public:
    std::string_view name() const noexcept override { return "notes"; }
};
std::wstring property(IUnknown* element, PROPERTYID id) {
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
    VARIANT value;
    VariantInit(&value);
    std::wstring out;
    if (element && SUCCEEDED(element->QueryInterface(IID_PPV_ARGS(&simple))) && SUCCEEDED(simple->GetPropertyValue(id, &value)) && value.vt == VT_BSTR && value.bstrVal)
        out = value.bstrVal;
    VariantClear(&value);
    return out;
}
bool same(IUnknown* a, IUnknown* b) {
    Microsoft::WRL::ComPtr<IUnknown> x, y;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) && SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) && x.Get() == y.Get();
}
void hostRoot() {
    namespace n = endfield::native;
    using Microsoft::WRL::ComPtr;
    WNDCLASSW type{};
    type.lpfnWndProc = DefWindowProcW;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"EndfieldAccountAutomationRootFixture";
    const ATOM atom = RegisterClassW(&type);
    check(atom != 0, "Register the hidden HUD stand-in");
    const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"HUD fixture", WS_POPUP, 0, 0, 800, 600, nullptr, nullptr, type.hInstance, nullptr);
    check(window && !IsWindowVisible(window), "The HUD stand-in stays hidden");
    std::vector<n::AccountAutomationControl> controls{{"account:connect", "Connect", {10, 10, 100, 20}, true},
                                                     {"account:refresh", "Refresh", {120, 10, 20, 20}, false},
                                                     {"wallet:toggle", "Arknights: Endfield · Sanity 343 / 360", {300, 0, 168, 42}, true}};
    std::vector<std::string> pressed;
    PlainOwner plain;
    PeerOwner account(controls, pressed);
    std::vector<endfield::app::ModuleOwner*> owners{&plain, &account};
    {
        endfield::app::HudAutomationSource source;
        source.owners = [&](std::vector<endfield::app::ModuleOwner*>& out) { out.insert(out.end(), owners.begin(), owners.end()); };
        source.refresh = [](endfield::app::ModuleOwner& o, IRawElementProviderFragmentRoot* root) { return o.automationRefresh(root); };
        source.name = [](const endfield::app::ModuleOwner& o) { return o.presents(endfield::core::Module::account) ? std::string("Account") : std::string(o.name()); };
        endfield::app::HudAutomation hud(window, std::move(source));
        check(hud.refresh() && hud.groups() == 1 && account.automationCount() == 3 && account.created == 1, "The root lists one group: the owner with fragments");
        ComPtr<IRawElementProviderFragment> root, group, first, second, last, parent, hit;
        check(SUCCEEDED(hud.root()->QueryInterface(IID_PPV_ARGS(&root))) && SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, &group)) && group,
              "The HUD root's first child is the account group");
        check(property(group.Get(), UIA_NamePropertyId) == L"Account" && property(group.Get(), UIA_AutomationIdPropertyId) == L"module.account",
              "The group is named after the module it presents");
        check(SUCCEEDED(group->Navigate(NavigateDirection_FirstChild, &first)) && property(first.Get(), UIA_AutomationIdPropertyId) == L"account:connect" &&
              SUCCEEDED(first->Navigate(NavigateDirection_Parent, &parent)) && same(parent.Get(), group.Get()), "Account peers are the group's children and navigate back to it");
        check(SUCCEEDED(first->Navigate(NavigateDirection_NextSibling, &second)) && property(second.Get(), UIA_AutomationIdPropertyId) == L"account:refresh" &&
              SUCCEEDED(group->Navigate(NavigateDirection_LastChild, &last)) && property(last.Get(), UIA_AutomationIdPropertyId) == L"wallet:toggle",
              "Siblings and the last child follow the owner's order");
        UiaRect bounds{};
        check(SUCCEEDED(group->get_BoundingRectangle(&bounds)) && bounds.left == 120 && bounds.top == 50 && bounds.width == 1036 - 120 && bounds.height == 84,
              "The group spans its peers' projected screen rects");
        check(SUCCEEDED(hud.root()->ElementProviderFromPoint(100 + 384 * 2, 50 + 21 * 2, &hit)) && hit.Get() == last.Get(), "The root's hit test reaches the wallet peer");
        ComPtr<IInvokeProvider> invoke;
        check(SUCCEEDED(hit.As(&invoke)) && SUCCEEDED(invoke->Invoke()) && pressed == std::vector<std::string>{"wallet:toggle"}, "Invoke through the root performs the press");
        // Keyboard focus (an open menu's selected row): the host's root and
        // the account group answer GetFocus with that peer.
        ComPtr<IRawElementProviderFragment> focused, groupFocus, nothing;
        ComPtr<IRawElementProviderFragmentRoot> groupRoot;
        check(SUCCEEDED(hud.root()->GetFocus(&nothing)) && !nothing, "Nothing is focused before a menu holds the keyboard");
        controls[0].focusable = controls[0].focused = true;
        (void)hud.refresh();
        check(SUCCEEDED(hud.root()->GetFocus(&focused)) && same(focused.Get(), first.Get()), "The HUD root's GetFocus returns the account peer holding the keyboard focus");
        check(SUCCEEDED(group.As(&groupRoot)) && SUCCEEDED(groupRoot->GetFocus(&groupFocus)) && same(groupFocus.Get(), first.Get()), "So does the account group's GetFocus");
        controls[0].focusable = controls[0].focused = false;
        (void)hud.refresh();
        nothing.Reset();
        check(SUCCEEDED(hud.root()->GetFocus(&nothing)) && !nothing, "The focus leaves with the menu");
        // A content event: the popover adds its Refresh, the refresh control goes.
        controls.erase(controls.begin() + 1);
        controls.push_back({"wallet:refresh", "Refresh", {320, 50, 60, 20}, true});
        check(hud.refresh() && account.automationCount() == 3 && hud.stats().structureChanges >= 2, "The host refresh reports the owner's restructured tree");
        check(property(second.Get(), UIA_NamePropertyId).empty(), "The removed peer is disconnected");
        // HUD concealed: every fragment disconnects, the group stays.
        hud.disconnectOwners();
        check(account.automationCount() == 0 && hud.groups() == 0 && property(last.Get(), UIA_NamePropertyId).empty(), "Concealing disconnects every account peer");
        check(hud.refresh() && hud.groups() == 1 && account.automationCount() == 3 && account.created == 1, "Showing again lists fresh peers under the same group");
        ComPtr<IRawElementProviderFragment> again;
        check(SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, &again)) && same(again.Get(), group.Get()), "The group keeps its identity");
        // The owner leaves the registry (failure isolation or destruction).
        hud.remove(account);
        check(account.automationCount() == 0 && hud.groups() == 0 && property(group.Get(), UIA_NamePropertyId).empty(), "Removing the owner disconnects its peers and group");
        hud.shutdown();
    }
    DestroyWindow(window);
    UnregisterClassW(type.lpszClassName, type.hInstance);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(argc == 3, "Pass hypergryph-account-coverage.json and hypergryph-account-source.json");
        fixture(argv[1], argv[2]);
        communityHost(argv[1]);
        loginHost(argv[1]);
        automation();
        focusAndToggle();
        hostRoot();
        if (SUCCEEDED(com)) CoUninitialize();
        std::cout << "PASS " << checks << " account area provider checks\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(com)) CoUninitialize();
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#endif
